// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"
#ifdef ENABLE_MINING
#include "pow/tromp/equi_miner.h"
#endif

#include "amount.h"
#include "base58.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#ifdef ENABLE_MINING
#include "crypto/equihash.h"
#endif
#include "hash.h"
#include "main.h"
#include "metrics.h"
#include "net.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "random.h"
#include "timedata.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include "sodium.h"

#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#ifdef ENABLE_MINING
#include <functional>
#endif
#include <mutex>
#include <init.h>
#include <undo.h>

using namespace std;

#include "zen/forkmanager.h"
using namespace zen; 

//////////////////////////////////////////////////////////////////////////////
//
// BitcoinMiner
//

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockCert = 0;
uint64_t nLastBlockSize = 0;
uint64_t nLastBlockTxPartitionSize = 0;

bool TxPriorityCompare::operator()(const TxPriority& a, const TxPriority& b)
{
    // When comparing two certificates we have to order them by epoch
    // and then by quality.
    // Before the introduction of non-ceasable sidechains, we only had
    // to order by quality.
    // This criterion is a consensus rule and overrides the others two
    if (a.get<2>()->IsCertificate() && b.get<2>()->IsCertificate() )
    {
        // dynamic casting throws an exception upon failure
        try {
            const CScCertificate& aCert = dynamic_cast<const CScCertificate&>(*a.get<2>());
            const CScCertificate& bCert = dynamic_cast<const CScCertificate&>(*b.get<2>());

            if (aCert.GetScId() == bCert.GetScId() )
            {
                if (aCert.epochNumber != bCert.epochNumber )
                {
                    // First order by epoch number
                    return aCert.epochNumber > bCert.epochNumber;
                }
                else
                {
                    // Then order by quality
                    return aCert.quality > bCert.quality;
                }
            }
        } catch (...) {
            LogPrintf("%s():%d - ERROR: cast error\n", __func__, __LINE__ );
            assert("could not cast txbase obj" == 0);
        }
    }

    if (byFee)
    {
        if (a.get<1>() == b.get<1>())
            return a.get<0>() < b.get<0>();
        return a.get<1>() < b.get<1>();
    }
    else
    {
        // note: as of now all certificates have MAXIMUM_PRIORITY, therefore are sorted always by fee
        if (a.get<0>() == b.get<0>())
            return a.get<1>() < b.get<1>();
        return a.get<0>() < b.get<0>();
    }
}

void UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
   auto medianTimePast = pindexPrev->GetMedianTimePast();
   auto nTime = std::max(medianTimePast + 1, GetTime());

   if ( ForkManager::getInstance().isFutureMiningTimeStampActive(pindexPrev->nHeight))
   {
      nTime = std::min(nTime, medianTimePast + MAX_FUTURE_BLOCK_TIME_MTP);
   }
   pblock->nTime = nTime;

}

bool VerifyCertificatesDependencies(const CScCertificate& cert)
{
    // detect dependencies from the sidechain point of view
    if (mempool.mapSidechains.count(cert.GetScId()) == 0)
    {
        if (fDebug) assert("cert in mempool has not corresponding entry in mapSidechains" == 0);
        return false;
    }

    if (mempool.mapSidechains.at(cert.GetScId()).mBackwardCertificates.count(std::make_pair(cert.quality, cert.epochNumber)) == 0)
    {
        if (fDebug) assert("cert is in mempool but not duly registered  in mapSidechains." == 0);
        return false;
    }

    if (mempool.mapSidechains.at(cert.GetScId()).mBackwardCertificates.at(std::make_pair(cert.quality, cert.epochNumber)) != cert.GetHash())
    {
        if (fDebug) assert("a different cert with the same scId and quality is in mempool" == 0);
        return false;
    }

    std::vector<uint256> txesHashesSpentByCert = mempool.mempoolDependenciesFrom(cert);
    for(const uint256& dep: txesHashesSpentByCert)
    {
        if (mempool.mapCertificate.count(dep)==0)
            continue; //tx won't conflict with cert on quality

        const CScCertificate& depCert = mempool.mapCertificate.at(dep).GetCertificate();
        if (depCert.GetScId() != cert.GetScId())
            continue;
        if (depCert.quality >= cert.quality && depCert.epochNumber == cert.epochNumber)
        {
            if (fDebug) assert("cert spends outputs of an higher quality cert of same scId" == 0);
            return false;
        }
    }

    return true;
}

bool VerifySidechainTxDependencies(const CTransaction& tx, const CCoinsViewCache& view, list<COrphan>& vOrphan, map<uint256, vector<COrphan*> >& mapDependers, COrphan*& porphan)
{
    // detect dependencies from the sidechain point of view
    std::set<uint256> targetScIds;
    for (const auto& ft: tx.GetVftCcOut())
        targetScIds.insert(ft.scId);

    for (const auto& btr: tx.GetVBwtRequestOut())
        targetScIds.insert(btr.scId);

    for (const uint256& scId: targetScIds)
    {
        if (view.HaveSidechain(scId) )
            continue;
        else if (mempool.hasSidechainCreationTx(scId)) {
            const uint256& scCreationHash = mempool.mapSidechains.at(scId).scCreationTxHash;
            assert(!scCreationHash.IsNull());
            assert(mempool.exists(scCreationHash));

            // check if tx is also creating the sc
            if (scCreationHash == tx.GetHash())
                continue;

            if (!porphan) {
                vOrphan.push_back(COrphan(&tx));
                porphan = &vOrphan.back();
            }

            mapDependers[scCreationHash].push_back(porphan);
            porphan->setDependsOn.insert(scCreationHash);
            LogPrint("sc", "%s():%d - tx[%s] depends on tx[%s] for sc creation\n",
                __func__, __LINE__, tx.GetHash().ToString(), scCreationHash.ToString());
        } else {
            // This should never happen; all sc fw transactions in the memory
            // pool should connect to either sidechain in the chain or sidechain created by
            // other transactions in the memory pool.
            LogPrintf("ERROR: mempool transaction missing sidechain\n");
            if (fDebug) assert("mempool transaction missing sidechain" == 0);
            return false;
        }
    }

    return true;
}

bool GetInputsDependencies(const CTransactionBase& txBase, CAmount& nTotalIn, list<COrphan>& vOrphan,
                             map<uint256, vector<COrphan*> >& mapDependers, COrphan*& porphan)
{
    const uint256& hash = txBase.GetHash();

    // Detect orphan transaction and its dependencies
    for(const CTxIn& txin: txBase.GetVin())
    {
        if (mempool.mapCertificate.count(txin.prevout.hash))
        {
            // - tx cannot spend any output of a certificate in mempool, neither change nor backward transfer
            // - certificate can only spend change outputs of another certificate in mempool, while backward transfers must mature first
            const CScCertificate & inputCert = mempool.mapCertificate[txin.prevout.hash].GetCertificate();

            if (!txBase.IsCertificate() ||                    // this is a tx ...
                inputCert.IsBackwardTransfer(txin.prevout.n)) // ... spending a cert bwt
            {
                // This should never happen
                LogPrintf("%s():%d - ERROR: [%s] has unspendable input that is an unconfirmed certificate [%s] output %d\n",
                    __func__, __LINE__, hash.ToString(), txin.prevout.hash.ToString(), txin.prevout.n);

                if (fDebug) assert("mempool transaction unspendable input that is an unconfirmed certificate output" == 0);
                return false;
            }
            if (!porphan)
            {
                // Use list for automatic deletion
                vOrphan.push_back(COrphan(&txBase));
                porphan = &vOrphan.back();
            }
            mapDependers[txin.prevout.hash].push_back(porphan);
            porphan->setDependsOn.insert(txin.prevout.hash);
            nTotalIn += mempool.mapCertificate[txin.prevout.hash].GetCertificate().GetVout()[txin.prevout.n].nValue;
            LogPrint("sc", "%s():%d - [%s] depends on [%s] for input\n",
                __func__, __LINE__, txBase.GetHash().ToString(), txin.prevout.hash.ToString());
        }
        else if (mempool.mapTx.count(txin.prevout.hash))
        {
            if (!porphan)
            {
                // Use list for automatic deletion
                vOrphan.push_back(COrphan(&txBase));
                porphan = &vOrphan.back();
            }
            mapDependers[txin.prevout.hash].push_back(porphan);
            porphan->setDependsOn.insert(txin.prevout.hash);
            nTotalIn += mempool.mapTx[txin.prevout.hash].GetTx().GetVout()[txin.prevout.n].nValue;
            LogPrint("sc", "%s():%d - [%s] depends on [%s] for input\n",
                __func__, __LINE__, txBase.GetHash().ToString(), txin.prevout.hash.ToString());
        }
    }
    return true;
}

bool AddToPriorities(const CTransactionBase& txBase, const CCoinsViewCache& view, CAmount& nTotalIn,
                       int nHeight, const CMemPoolEntry& mpEntry, vector<TxPriority>& vecPriority, COrphan* porphan)
{
    const uint256& hash = txBase.GetHash();
    unsigned int nTxSize = txBase.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
    double dPriority = 0;
    CAmount nFee = 0;

    // Has to wait for dependencies
    if (!porphan)
    {
        dPriority = mpEntry.GetPriority(nHeight); // Csw inputs contributes to this
        nFee = mpEntry.GetFee();
        mempool.ApplyDeltas(hash, dPriority, nFee);

        CFeeRate feeRate(nFee, nTxSize);

        LogPrint("sc", "%s():%d - adding to prio vec txObj = %s, prio=%f, feeRate=%s\n",
            __func__, __LINE__, hash.ToString(), dPriority, feeRate.ToString());
 
        vecPriority.push_back(TxPriority(dPriority, feeRate, &txBase));
    }
    else
    {
        for(const CTxIn& txin: txBase.GetVin())
        {
            // Read prev transaction
            // Skip transactions in mempool
            if (mempool.mapTx.count(txin.prevout.hash))
                continue;
            else if(mempool.mapCertificate.count(txin.prevout.hash))
                continue;
            else if (!view.HaveCoins(txin.prevout.hash))
            {
                // This should never happen; all transactions in the memory
                // pool should connect to either transactions or certificates in the chain
                // or other transactions in the memory pool (not certificates in mempool, see above).
                LogPrintf("ERROR: mempool transaction missing input\n");
                if (fDebug) assert("mempool transaction missing input" == 0);
                return false;
            }
            const CCoins* coins = view.AccessCoins(txin.prevout.hash);
            assert(coins);

            CAmount nValueIn = coins->vout[txin.prevout.n].nValue;
            nTotalIn += nValueIn;

            int nConf = nHeight - coins->nHeight;

            dPriority += (double)nValueIn * nConf;
        }
        nTotalIn += txBase.GetJoinSplitValueIn() + txBase.GetCSWValueIn();

        // Csw contribute zero to initial priority

        // Priority is sum(valuein * age) / modified_txsize
        dPriority = txBase.ComputePriority(dPriority, nTxSize);
        mempool.ApplyDeltas(hash, dPriority, nTotalIn);
        //nFee = nTotalIn - tx.GetValueOut();
        nFee = txBase.GetFeeAmount(nTotalIn);

        CFeeRate feeRate(nFee, nTxSize);

        porphan->dPriority = dPriority;
        porphan->feeRate = feeRate;
    }
    return true;
}

void GetBlockCertPriorityData(const CCoinsViewCache& view, int nHeight,
                               vector<TxPriority>& vecPriority, list<COrphan>& vOrphan, map<uint256, vector<COrphan*> >& mapDependers)
{
    for (auto mi = mempool.mapCertificate.begin(); mi != mempool.mapCertificate.end(); ++mi)
    {
        const CScCertificate& cert = mi->second.GetCertificate();

        CAmount nTotalIn = 0;
        COrphan* porphan = nullptr;

        if (!GetInputsDependencies(cert, nTotalIn, vOrphan, mapDependers, porphan) )
        {
            if (porphan)
                vOrphan.pop_back();
            continue;
        }

        if (!VerifyCertificatesDependencies(cert) )
        {
            if (porphan)
                vOrphan.pop_back();
            continue;
        }

        const CMemPoolEntry& mpEntry = mi->second;
        if (!AddToPriorities(cert, view, nTotalIn, nHeight, mpEntry, vecPriority, porphan) )
        {
            if (porphan)
                vOrphan.pop_back();
            continue;
        }
    }
}

void GetBlockTxPriorityData(const CCoinsViewCache& view, int nHeight, int64_t nLockTimeCutoff,
                               vector<TxPriority>& vecPriority, list<COrphan>& vOrphan, map<uint256, vector<COrphan*> >& mapDependers)
{
    for (map<uint256, CTxMemPoolEntry>::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi)
    {
        const CTransaction& tx = mi->second.GetTx();

        if (tx.IsCoinBase() || !IsFinalTx(tx, nHeight, nLockTimeCutoff))
            continue;

        CAmount nTotalIn = 0;
        COrphan* porphan = nullptr;

        if (!GetInputsDependencies(tx, nTotalIn, vOrphan, mapDependers, porphan) )
        {
            if (porphan)
                vOrphan.pop_back();
            continue;
        }

        if (!VerifySidechainTxDependencies(tx, view, vOrphan, mapDependers, porphan) )
        {
            if (porphan)
                vOrphan.pop_back();
            continue;
        }

        const CMemPoolEntry& mpEntry = mi->second;
        if (!AddToPriorities(tx, view, nTotalIn, nHeight, mpEntry, vecPriority, porphan) )
        {
            if (porphan)
                vOrphan.pop_back();
            continue;
        }
    }
}

void GetBlockTxPriorityDataOld(const CCoinsViewCache& view, int nHeight, int64_t nLockTimeCutoff,
                               vector<TxPriority>& vecPriority, list<COrphan>& vOrphan, map<uint256, vector<COrphan*> >& mapDependers)
{
    LogPrint("cert", "%s():%d - called\n", __func__, __LINE__);

    for (map<uint256, CTxMemPoolEntry>::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi)
    {
        const CTransaction& tx = mi->second.GetTx();

        if (tx.IsCoinBase() || !IsFinalTx(tx, nHeight, nLockTimeCutoff))
            continue;

        COrphan* porphan = nullptr;
        double dPriority = 0;
        CAmount nTotalIn = 0;
        bool fMissingInputs = false;
        for(const CTxIn& txin: tx.GetVin())
        {
            // Read prev transaction
            if (!view.HaveCoins(txin.prevout.hash))
            {
                // This should never happen; all transactions in the memory
                // pool should connect to either transactions in the chain
                // or other transactions in the memory pool.
                // This also consider that the tx input can not be any output of a certificate in mempool
                if (!mempool.mapTx.count(txin.prevout.hash))
                {
                    LogPrintf("ERROR: mempool transaction missing input\n");
                    if (fDebug) assert("mempool transaction missing input" == 0);
                    fMissingInputs = true;
                    if (porphan)
                        vOrphan.pop_back();
                    break;
                }

                // Has to wait for dependencies
                if (!porphan)
                {
                    // Use list for automatic deletion
                    vOrphan.push_back(COrphan(&tx));
                    porphan = &vOrphan.back();
                }
                mapDependers[txin.prevout.hash].push_back(porphan);
                porphan->setDependsOn.insert(txin.prevout.hash);
                nTotalIn += mempool.mapTx[txin.prevout.hash].GetTx().GetVout()[txin.prevout.n].nValue;
                continue;
            }
            const CCoins* coins = view.AccessCoins(txin.prevout.hash);
            assert(coins);

            CAmount nValueIn = coins->vout[txin.prevout.n].nValue;
            nTotalIn += nValueIn;

            int nConf = nHeight - coins->nHeight;

            dPriority += (double)nValueIn * nConf;
        }
        nTotalIn += tx.GetJoinSplitValueIn() + tx.GetCSWValueIn();

        if (fMissingInputs) continue;

        if (!VerifySidechainTxDependencies(tx, view, vOrphan, mapDependers, porphan))
        {
            // should never happen because that means inconsistency in mempool, but this tx must not be
            // added to vecPriority nor in the vOrphan
            LogPrint("cert", "%s():%d - skipping tx[%s] for invalid dependencies\n",
                __func__, __LINE__, tx.GetHash().ToString() ); 
            continue;
        }

        // Priority is sum(valuein * age) / modified_txsize
        unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
        dPriority = tx.ComputePriority(dPriority, nTxSize);

        uint256 hash = tx.GetHash();
        mempool.ApplyDeltas(hash, dPriority, nTotalIn);

        CFeeRate feeRate(nTotalIn-tx.GetValueOut(), nTxSize);

        if (porphan)
        {
            porphan->dPriority = dPriority;
            porphan->feeRate = feeRate;
        }
        else
            vecPriority.push_back(TxPriority(dPriority, feeRate, &mi->second.GetTx()));
    }
}

CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn)
{
    // Block complexity is a sum of block transactions complexity. Transaction complexisty equals to number of inputs squared.
    unsigned int nBlockMaxComplexitySize = GetArg("-blockmaxcomplexity", DEFAULT_BLOCK_MAX_COMPLEXITY_SIZE);
    // Calculate current block complexity
    return  CreateNewBlock(scriptPubKeyIn,  nBlockMaxComplexitySize);
}

CMutableTransaction createCoinbase(const CScript &scriptPubKeyIn, CAmount fees, const int nHeight)
{
    const CChainParams& chainparams = Params();
    CMutableTransaction txNew;

    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = CScript() << nHeight << OP_0;

    std::vector<CTxOut> coinbaseOutputList(1);
    coinbaseOutputList.at(0).scriptPubKey = scriptPubKeyIn;
    CAmount reward = GetBlockSubsidy(nHeight, chainparams.GetConsensus());
    coinbaseOutputList.at(0).nValue = reward;

    for (Fork::CommunityFundType cfType = Fork::CommunityFundType::FOUNDATION;
            cfType < Fork::CommunityFundType::ENDTYPE; cfType = Fork::CommunityFundType(cfType + 1))
    {
        CAmount vCommunityFund = ForkManager::getInstance().getCommunityFundReward(nHeight, reward, cfType);
        if (vCommunityFund > 0)
        {
            // Take some reward away from miners
            coinbaseOutputList.at(0).nValue -= vCommunityFund;
            // And give it to the community
            coinbaseOutputList.push_back(CTxOut(vCommunityFund, chainparams.GetCommunityFundScriptAtHeight(nHeight, cfType)));
        }
    }

    coinbaseOutputList.at(0).nValue += fees;

    for(const CTxOut& coinbaseOut: coinbaseOutputList)
        txNew.addOut(coinbaseOut);

    return txNew;
}

CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn,  unsigned int nBlockMaxComplexitySize)
{
    const CChainParams& chainparams = Params();
    // Create new block
    std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    if(!pblocktemplate.get())
        return NULL;
    CBlock *pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.push_back(CTransaction());
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    int nBlockComplexity = 0;

    // Collect memory pool transactions into the block
    CAmount nFees = 0;
    {
        LOCK2(cs_main, mempool.cs);
        CBlockIndex* pindexPrev = chainActive.Tip();
        const int nHeight = pindexPrev->nHeight + 1;
        pblock->nTime = GetTime();
        const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

        pblock->nVersion = ForkManager::getInstance().getNewBlockVersion(nHeight);

        // -regtest only: allow overriding block.nVersion with
        // -blockversion=N to test forking scenarios
        if (chainparams.MineBlocksOnDemand())
            pblock->nVersion = GetArg("-blockversion", pblock->nVersion);

        // - From the sidechains fork point on, the block size has been increased 
        unsigned int block_size_limit          = MAX_BLOCK_SIZE;
        unsigned int block_priority_size_limit = DEFAULT_BLOCK_PRIORITY_SIZE;
        if (pblock->nVersion != BLOCK_VERSION_SC_SUPPORT)
        {
            block_size_limit          = MAX_BLOCK_SIZE_BEFORE_SC;
            block_priority_size_limit = DEFAULT_BLOCK_PRIORITY_SIZE_BEFORE_SC;
        }
 
        // Largest block you're willing to create:
        unsigned int nBlockMaxSize = GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
        // Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
        nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(block_size_limit-1000), nBlockMaxSize));
  
        // Minimum block size you want to create; block will be filled with free transactions
        // until there are no more or the block reaches this size:
        unsigned int nBlockMinSize = GetArg("-blockminsize", DEFAULT_BLOCK_MIN_SIZE);
        nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);

        // Largest block tx partition allowed:
        unsigned int nBlockTxPartitionMaxSize = DEFAULT_BLOCK_TX_PART_MAX_SIZE;

        // -regtest only: allow overriding 
        if (chainparams.MineBlocksOnDemand())
        {
            nBlockTxPartitionMaxSize = GetArg("-blocktxpartitionmaxsize", DEFAULT_BLOCK_TX_PART_MAX_SIZE);
        }

        // Limit to betweeen 1K and MAX-1K for sanity:
        nBlockTxPartitionMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(DEFAULT_BLOCK_TX_PART_MAX_SIZE-1000), nBlockTxPartitionMaxSize));
 
        // How much of the tx block partition should be dedicated to high-priority transactions,
        // included regardless of the fees they pay
        unsigned int nBlockPrioritySize = GetArg("-blockprioritysize", block_priority_size_limit);
        nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

        CCoinsViewCache view(pcoinsTip);

        // Priority order to process transactions
        list<COrphan> vOrphan; // list memory doesn't move
        map<uint256, vector<COrphan*> > mapDependers;
        bool fPrintPriority = GetBoolArg("-printpriority", false);

        // This vector will be sorted into a priority queue:
        vector<TxPriority> vecPriority;
        vecPriority.reserve(mempool.size()); // both tx and cert

        int64_t nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                ? nMedianTimePast
                : pblock->GetBlockTime();

        bool fDeprecatedGetBlockTemplate = GetBoolArg("-deprecatedgetblocktemplate", false);
        if (fDeprecatedGetBlockTemplate)
            GetBlockTxPriorityDataOld(view, nHeight, nLockTimeCutoff, vecPriority, vOrphan, mapDependers);
        else
            GetBlockTxPriorityData(view, nHeight, nLockTimeCutoff, vecPriority, vOrphan, mapDependers);

        GetBlockCertPriorityData(view, nHeight, vecPriority, vOrphan, mapDependers);

        // Collect transactions into block
        uint64_t nBlockSize = 1000;
        uint64_t nBlockTxPartitionSize = 0;

        uint64_t nBlockTx = 0;
        uint64_t nBlockCert = 0;
        int nBlockSigOps = 100;
        bool fSortedByFee = (nBlockPrioritySize <= 0);

        // Order transactions and certificates.
        // Note that vecPriority might not contain all the transactions/certificates in mempool as there might be input dependencies
        // in which case the depending transactions/certificates are placed in mapDependers to be sorted later according to input spending.
        TxPriorityCompare comparer(fSortedByFee);
        std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);

        // considering certs having a higher priority than any possible tx.
        // An algorithm for managing tx/cert priorities could be devised
        while (!vecPriority.empty())
        {
            // Take highest priority transaction off the priority queue:
            double dPriority = vecPriority.front().get<0>();
            CFeeRate feeRate = vecPriority.front().get<1>();
            const CTransactionBase& tx = *(vecPriority.front().get<2>());

            std::pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
            vecPriority.pop_back();

            // Size limits
            unsigned int nTxBaseSize = tx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);

            if (!tx.IsCertificate())
            {
                // only a portion of the block can have ordinary transactions, we can not exceed this size
                if (nBlockTxPartitionSize + nTxBaseSize >= nBlockTxPartitionMaxSize)
                {
                    LogPrint("sc", "%s():%d - Skipping tx[%s] because nBlockTxPartitionMaxSize %d would be exceeded (partSize=%d / txSize=%d)\n",
                        __func__, __LINE__, tx.GetHash().ToString(), nBlockTxPartitionMaxSize, nBlockTxPartitionSize, nTxBaseSize );
                    continue;
                }
            }

            if (nBlockSize + nTxBaseSize >= nBlockMaxSize)
            {
                LogPrint("sc", "%s():%d - Skipping %s[%s] because nBlockMaxSize %d would be exceeded (blSize=%d / txBaseSize=%d)\n",
                    __func__, __LINE__, tx.IsCertificate()?"cert":"tx", tx.GetHash().ToString(), nBlockMaxSize, nBlockSize, nTxBaseSize );
                continue;
            }

            // Legacy limits on sigOps:
            unsigned int nTxSigOps = GetLegacySigOpCount(tx);
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
                continue;

            const uint256& hash = tx.GetHash();

            // Skip free transactions / certificates if we're past the minimum block size:
            double dPriorityDelta = 0;
            CAmount nFeeDelta = 0;
            mempool.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
            if (fSortedByFee && (dPriorityDelta <= 0) && (nFeeDelta <= 0) && (feeRate < ::minRelayTxFee) && (nBlockSize + nTxBaseSize >= nBlockMinSize))
            {
                LogPrint("sc", "%s():%d - Skipping [%s] because it is free (feeDelta=%lld/feeRate=%s, blsz=%u/txsz=%u/blminsz=%u)\n",
                    __func__, __LINE__, tx.GetHash().ToString(), nFeeDelta, feeRate.ToString(), nBlockSize, nTxBaseSize, nBlockMinSize );
                continue;
            }

            // Prioritise by fee once past the priority size or we run out of high-priority
            // transactions:
            if (!fSortedByFee &&
                ((nBlockSize + nTxBaseSize >= nBlockPrioritySize) || !AllowFree(dPriority)))
            {
                fSortedByFee = true;
                comparer = TxPriorityCompare(fSortedByFee);
                std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
            }

            // Skip transaction if max block complexity reached.
            int nTxComplexity = tx.GetComplexity();
            if (!fDeprecatedGetBlockTemplate && nBlockMaxComplexitySize > 0 && nBlockComplexity + nTxComplexity >= nBlockMaxComplexitySize)
                continue;

            if (!view.HaveInputs(tx))
            {
                LogPrint("sc", "%s():%d - Skipping [%s] because it has no inputs\n",
                    __func__, __LINE__, tx.GetHash().ToString() );
                continue;
            }

            CAmount nTxFees = tx.GetFeeAmount(view.GetValueIn(tx));
            nTxSigOps += GetP2SHSigOpCount(tx, view);
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
            {
                LogPrint("sc", "%s():%d - Skipping [%s] because too many sigops in block\n",
                    __func__, __LINE__, tx.GetHash().ToString() );
                continue;
            }

            try {
                // Note that flags: we don't want to set mempool/IsStandard()
                // policy here, but we still have to ensure that the block we
                // create only contains transactions that are valid in new blocks.
                CValidationState dummyState;
                CTxUndo dummyUndo;

                if (tx.IsCertificate())
                {
                    const CScCertificate& castedCert = dynamic_cast<const CScCertificate&>(tx);
                    if(!ContextualCheckCertInputs(castedCert, dummyState, view, true, chainActive, MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_CHECKBLOCKATHEIGHT, true, Params().GetConsensus()))
                        continue;

                    UpdateCoins(castedCert, view, dummyUndo, nHeight, /*isBlockTopQualityCert*/true);
                    pblock->vcert.push_back(castedCert);
                    pblocktemplate.get()->vCertFees.push_back(nTxFees);
                    pblocktemplate.get()->vCertSigOps.push_back(nTxSigOps);
                    ++nBlockCert;
                } else
                {
                    const CTransaction& castedTx = dynamic_cast<const CTransaction&>(tx);
                    if (!ContextualCheckTxInputs(castedTx, dummyState, view, true, chainActive, MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_CHECKBLOCKATHEIGHT, true, Params().GetConsensus()))
                        continue;

                    UpdateCoins(castedTx, view, dummyUndo, nHeight);
                    pblock->vtx.push_back(castedTx);
                    pblocktemplate.get()->vTxFees.push_back(nTxFees);
                    pblocktemplate.get()->vTxSigOps.push_back(nTxSigOps);
                    ++nBlockTx;
                    nBlockTxPartitionSize += nTxBaseSize;
                }

                nBlockSize += nTxBaseSize;
                LogPrint("sc", "%s():%d ======> current block size                = %7d\n", __func__, __LINE__, nBlockSize);
                LogPrint("sc", "%s():%d ======> current block tx partition size   = %7d\n", __func__, __LINE__, nBlockTxPartitionSize);

                nBlockSigOps += nTxSigOps;
                nFees += nTxFees;
                nBlockComplexity += nTxComplexity;

                if (fPrintPriority)
                {
                    LogPrintf("priority %.1f fee %d feeRate %s txid %s\n",
                        dPriority, nTxFees, feeRate.ToString(), tx.GetHash().ToString());
                }

            } catch (...) {
                LogPrintf("%s():%d - ERROR: tx [%s] cast error\n",
                    __func__, __LINE__, hash.ToString());
                assert("could not cast txbase obj" == 0);
            }

            // Add transactions that depend on this one to the priority queue
            if (mapDependers.count(hash))
            {
                LogPrint("sc", "%s():%d - tx[%s] has %d orphans\n",
                    __func__, __LINE__, hash.ToString(), mapDependers[hash].size());
                for(COrphan* porphan: mapDependers[hash])
                {
                    if (!porphan->setDependsOn.empty())
                    {
                        porphan->setDependsOn.erase(hash);
                        LogPrint("sc", "%s():%d - erasing tx[%s] from orphan %p\n", __func__, __LINE__, hash.ToString(), porphan);
                        if (porphan->setDependsOn.empty())
                        {
                            LogPrint("sc", "%s():%d - tx[%s] resolved all dependencies, adding to prio vec, prio=%f, feeRate=%s\n",
                                __func__, __LINE__, porphan->ptx->GetHash().ToString(), porphan->dPriority, porphan->feeRate.ToString());
                            
                            vecPriority.push_back(TxPriority(porphan->dPriority, porphan->feeRate, porphan->ptx));
                            std::push_heap(vecPriority.begin(), vecPriority.end(), comparer);
                        }
                    }
                    else
                    {
                        LogPrint("sc", "%s():%d - tx[%s] orphan %p empty\n", __func__, __LINE__, hash.ToString(), porphan);
                    }
                }
            }
        }

        nLastBlockTx = nBlockTx;
        nLastBlockCert = nBlockCert;

        nLastBlockSize = nBlockSize;
        nLastBlockTxPartitionSize = nBlockTxPartitionSize;

        LogPrintf("%s():%d - total size %u, tx part size %u, tx[%d]/certs[%d], fee=%d\n",
            __func__, __LINE__, nBlockSize, nBlockTxPartitionSize, nBlockTx, nBlockCert, nFees);

        pblock->vtx[0] = createCoinbase(scriptPubKeyIn, nFees, nHeight);
        pblocktemplate->vTxFees[0] = -nFees;

        // Randomise nonce
        arith_uint256 nonce = UintToArith256(GetRandHash());
        // Clear the top and bottom 16 bits (for local use as thread flags and counters)
        nonce <<= 32;
        nonce >>= 16;
        pblock->nNonce = ArithToUint256(nonce);

        // Fill in header
        pblock->hashPrevBlock  = pindexPrev->GetBlockHash();

        if (pblock->nVersion == BLOCK_VERSION_SC_SUPPORT )
        {
            pblock->hashScTxsCommitment = pblock->BuildScTxsCommitment(view);
        }

        UpdateTime(pblock, Params().GetConsensus(), pindexPrev);
        pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, Params().GetConsensus());
        pblock->nSolution.clear();
        pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(pblock->vtx[0]);

        CValidationState state;
        if (!TestBlockValidity(state, *pblock, pindexPrev, flagCheckPow::OFF, flagCheckMerkleRoot::OFF, flagScRelatedChecks::OFF))
            throw std::runtime_error("CreateNewBlock(): TestBlockValidity failed");
    }

    return pblocktemplate.release();
}

#ifdef ENABLE_WALLET
boost::optional<CScript> GetMinerScriptPubKey(CReserveKey& reservekey)
#else
boost::optional<CScript> GetMinerScriptPubKey()
#endif
{
    CKeyID keyID;
    CBitcoinAddress addr;
    if (addr.SetString(GetArg("-mineraddress", ""))) {
        addr.GetKeyID(keyID);
    } else {
#ifdef ENABLE_WALLET
        CPubKey pubkey;
        if (!reservekey.GetReservedKey(pubkey)) {
            return boost::optional<CScript>();
        }
        keyID = pubkey.GetID();
#else
        return boost::optional<CScript>();
#endif
    }

    CScript scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
    return scriptPubKey;
}

#ifdef ENABLE_WALLET
CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey)
{
    boost::optional<CScript> scriptPubKey = GetMinerScriptPubKey(reservekey);
#else
CBlockTemplate* CreateNewBlockWithKey()
{
    boost::optional<CScript> scriptPubKey = GetMinerScriptPubKey();
#endif

    if (!scriptPubKey) {
        return NULL;
    }
    return CreateNewBlock(*scriptPubKey);
}

//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//

#ifdef ENABLE_MINING

void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = txCoinbase;
    pblock->hashMerkleRoot = pblock->BuildMerkleTree();
#ifdef DEBUG_SC_COMMITMENT_HASH
    std::cout << "-------------------------------------------" << std::endl;
    std::cout << "  hashScTxsCommitment: " << pblock->hashScTxsCommitment.ToString() << std::endl;
#endif
}

#ifdef ENABLE_WALLET
static bool ProcessBlockFound(CBlock* pblock, CWallet* wallet, CReserveKey& reservekey)
#else
static bool ProcessBlockFound(CBlock* pblock)
#endif // ENABLE_WALLET
{
    LogPrintf("%s\n", pblock->ToString());
    LogPrintf("generated %s\n", FormatMoney(pblock->vtx[0].GetVout()[0].nValue));

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash())
            return error("HorizenMiner: generated block is stale");
    }

#ifdef ENABLE_WALLET
    if (GetArg("-mineraddress", "").empty()) {
        // Remove key from key pool
        reservekey.KeepKey();
    }

    // Track how many getdata requests this block gets
    if (wallet)
    {
        LOCK(wallet->cs_wallet);
        wallet->mapRequestCount[pblock->GetHash()] = 0;
    }
#endif

    // Process this block the same as if we had received it from another node
    CValidationState state;
    if (!ProcessNewBlock(state, NULL, pblock, true, NULL))
        return error("HorizenMiner: ProcessNewBlock, block not accepted");

    TrackMinedBlock(pblock->GetHash());

    return true;
}

#ifdef ENABLE_WALLET
void static BitcoinMiner(CWallet *pwallet)
#else
void static BitcoinMiner()
#endif
{
    LogPrintf("HorizenMiner started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("horizen-miner");

    const CChainParams& chainparams = Params();

#ifdef ENABLE_WALLET
    // Each thread has its own key
    CReserveKey reservekey(pwallet);
#endif

    // Each thread has its own counter
    unsigned int nExtraNonce = 0;

    unsigned int n = chainparams.EquihashN();
    unsigned int k = chainparams.EquihashK();

    std::string solver = GetArg("-equihashsolver", "default");
    assert(solver == "tromp" || solver == "default");
    LogPrint("pow", "Using Equihash solver \"%s\" with n = %u, k = %u\n", solver, n, k);

    std::mutex m_cs;
    bool cancelSolver = false;
    boost::signals2::connection c = uiInterface.NotifyBlockTip.connect(
        [&m_cs, &cancelSolver](const uint256& hashNewTip) mutable {
            std::lock_guard<std::mutex> lock{m_cs};
            cancelSolver = true;
        }
    );
    miningTimer.start();

    try {
        while (true) {
            if (chainparams.MiningRequiresPeers()) {
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                miningTimer.stop();
                do {
                    bool fvNodesEmpty;
                    {
                        LOCK(cs_vNodes);
                        fvNodesEmpty = vNodes.empty();
                    }
                    if (!fvNodesEmpty && !IsInitialBlockDownload())
                        break;
                    MilliSleep(1000);
                } while (true);
                miningTimer.start();
            }

            //
            // Create new block
            //
            unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            CBlockIndex* pindexPrev = chainActive.Tip();

#ifdef ENABLE_WALLET
            unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlockWithKey(reservekey));
#else
            unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlockWithKey());
#endif
            if (!pblocktemplate.get())
            {
                if (GetArg("-mineraddress", "").empty()) {
                    LogPrintf("Error in HorizenMiner: Keypool ran out, please call keypoolrefill before restarting the mining thread\n");
                } else {
                    // Should never reach here, because -mineraddress validity is checked in init.cpp
                    LogPrintf("Error in HorizenMiner: Invalid -mineraddress\n");
                }
                return;
            }
            CBlock *pblock = &pblocktemplate->block;
            IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);
            LogPrintf("Running HorizenMiner with %u transactions in block (%u bytes)\n", pblock->vtx.size(),
                ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));

            //
            // Search
            //
            int64_t nStart = GetTime();
            arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

            while (true) {
                // Hash state
                crypto_generichash_blake2b_state state;
                EhInitialiseState(n, k, state);

                // I = the block header minus nonce and solution.
                CEquihashInput I{*pblock};
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss << I;

                // H(I||...
                crypto_generichash_blake2b_update(&state, (unsigned char*)&ss[0], ss.size());

                // H(I||V||...
                crypto_generichash_blake2b_state curr_state;
                curr_state = state;
                crypto_generichash_blake2b_update(&curr_state,
                                                  pblock->nNonce.begin(),
                                                  pblock->nNonce.size());

                // (x_1, x_2, ...) = A(I, V, n, k)
                LogPrint("pow", "Running Equihash solver \"%s\" with nNonce = %s\n",
                         solver, pblock->nNonce.ToString());

                std::function<bool(std::vector<unsigned char>)> validBlock =
#ifdef ENABLE_WALLET
                        [&pblock, &hashTarget, pwallet, &reservekey, &m_cs, &cancelSolver, &chainparams]
#else
                        [&pblock, &hashTarget, &m_cs, &cancelSolver, &chainparams]
#endif
                        (std::vector<unsigned char> soln) {
                    // Write the solution to the hash and compute the result.
                    LogPrint("pow", "- Checking solution against target\n");
                    pblock->nSolution = soln;
                    solutionTargetChecks.increment();

                    if (UintToArith256(pblock->GetHash()) > hashTarget) {
                        return false;
                    }

                    // Found a solution
                    SetThreadPriority(THREAD_PRIORITY_NORMAL);
                    LogPrintf("HorizenMiner:\n");
                    LogPrintf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", pblock->GetHash().GetHex(), hashTarget.GetHex());
#ifdef ENABLE_WALLET
                    if (ProcessBlockFound(pblock, pwallet, reservekey)) {
#else
                    if (ProcessBlockFound(pblock)) {
#endif
                        // Ignore chain updates caused by us
                        std::lock_guard<std::mutex> lock{m_cs};
                        cancelSolver = false;
                    }
                    SetThreadPriority(THREAD_PRIORITY_LOWEST);

                    // In regression test mode, stop mining after a block is found.
                    if (chainparams.MineBlocksOnDemand()) {
                        // Increment here because throwing skips the call below
                        ehSolverRuns.increment();
                        throw boost::thread_interrupted();
                    }

                    return true;
                };
                std::function<bool(EhSolverCancelCheck)> cancelled = [&m_cs, &cancelSolver](EhSolverCancelCheck pos) {
                    std::lock_guard<std::mutex> lock{m_cs};
                    return cancelSolver;
                };

                // TODO: factor this out into a function with the same API for each solver.
                if (solver == "tromp") {
                    // Create solver and initialize it.
                    equi eq(1);
                    eq.setstate(&curr_state);

                    // Intialization done, start algo driver.
                    eq.digit0(0);
                    eq.xfull = eq.bfull = eq.hfull = 0;
                    eq.showbsizes(0);
                    for (u32 r = 1; r < WK; r++) {
                        (r&1) ? eq.digitodd(r, 0) : eq.digiteven(r, 0);
                        eq.xfull = eq.bfull = eq.hfull = 0;
                        eq.showbsizes(r);
                    }
                    eq.digitK(0);
                    ehSolverRuns.increment();

                    // Convert solution indices to byte array (decompress) and pass it to validBlock method.
                    for (size_t s = 0; s < eq.nsols; s++) {
                        LogPrint("pow", "Checking solution %d\n", s+1);
                        std::vector<eh_index> index_vector(PROOFSIZE);
                        for (size_t i = 0; i < PROOFSIZE; i++) {
                            index_vector[i] = eq.sols[s][i];
                        }
                        std::vector<unsigned char> sol_char = GetMinimalFromIndices(index_vector, DIGITBITS);

                        if (validBlock(sol_char)) {
                            // If we find a POW solution, do not try other solutions
                            // because they become invalid as we created a new block in blockchain.
                            break;
                        }
                    }
                } else {
                    try {
                        // If we find a valid block, we rebuild
                        bool found = EhOptimisedSolve(n, k, curr_state, validBlock, cancelled);
                        ehSolverRuns.increment();
                        if (found) {
                            break;
                        }
                    } catch (EhSolverCancelledException&) {
                        LogPrint("pow", "Equihash solver cancelled\n");
                        std::lock_guard<std::mutex> lock{m_cs};
                        cancelSolver = false;
                    }
                }

                // Check for stop or if block needs to be rebuilt
                boost::this_thread::interruption_point();
                // Regtest mode doesn't require peers
                if (vNodes.empty() && chainparams.MiningRequiresPeers())
                    break;
                if ((UintToArith256(pblock->nNonce) & 0xffff) == 0xffff)
                    break;
                if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
                    break;
                if (pindexPrev != chainActive.Tip())
                    break;

                // Update nNonce and nTime
                pblock->nNonce = ArithToUint256(UintToArith256(pblock->nNonce) + 1);
                UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
            }
        }
    }
    catch (const boost::thread_interrupted&)
    {
        miningTimer.stop();
        c.disconnect();
        LogPrintf("HorizenMiner terminated\n");
        throw;
    }
    catch (const std::runtime_error &e)
    {
        miningTimer.stop();
        c.disconnect();
        LogPrintf("HorizenMiner runtime error: %s\n", e.what());
        return;
    }
    miningTimer.stop();
    c.disconnect();
}

#ifdef ENABLE_WALLET
void GenerateBitcoins(bool fGenerate, CWallet* pwallet, int nThreads)
#else
void GenerateBitcoins(bool fGenerate, int nThreads)
#endif
{
    static boost::thread_group* minerThreads = NULL;

    if (nThreads < 0)
        nThreads = GetNumCores();

    if (minerThreads != NULL)
    {
        minerThreads->interrupt_all();
        minerThreads->join_all();
        delete minerThreads;
        minerThreads = NULL;
    }

    if (nThreads == 0 || !fGenerate)
        return;

    minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++) {
#ifdef ENABLE_WALLET
        minerThreads->create_thread(boost::bind(&BitcoinMiner, pwallet));
#else
        minerThreads->create_thread(&BitcoinMiner);
#endif
    }
}

#endif // ENABLE_MINING
