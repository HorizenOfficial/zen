// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txmempool.h"

#include "clientversion.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "main.h"
#include "policy/fees.h"
#include "streams.h"
#include "util.h"
#include "utilmoneystr.h"
#include "version.h"
#include "validationinterface.h"
#include "main.h"
#include <undo.h>

CMemPoolEntry::CMemPoolEntry():
    nFee(0), nModSize(0), nUsageSize(0), nTime(0), dPriority(0.0)
{
    nHeight = MEMPOOL_HEIGHT;
}

CMemPoolEntry::CMemPoolEntry(const CAmount& _nFee, int64_t _nTime, double _dPriority, unsigned int _nHeight) :
    nFee(_nFee), nModSize(0), nUsageSize(0), nTime(_nTime), dPriority(_dPriority), nHeight(_nHeight)
{
}

CTxMemPoolEntry::CTxMemPoolEntry(): nTxSize(0), hadNoDependencies(false)
{
}

CTxMemPoolEntry::CTxMemPoolEntry(const CTransaction& _tx, const CAmount& _nFee,
                                 int64_t _nTime, double _dPriority,
                                 unsigned int _nHeight, bool poolHasNoInputsOf):
    CMemPoolEntry(_nFee, _nTime, _dPriority, _nHeight),
    tx(_tx), hadNoDependencies(poolHasNoInputsOf)
{
    nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    nModSize = tx.CalculateModifiedSize(nTxSize);
    nUsageSize = RecursiveDynamicUsage(tx);
}

CTxMemPoolEntry::CTxMemPoolEntry(const CTxMemPoolEntry& other)
{
    *this = other;
}

double CTxMemPoolEntry::GetPriority(unsigned int currentHeight) const
{
    CAmount nValueIn = tx.GetValueOut()+nFee;
    double deltaPriority = ((double)(currentHeight-nHeight)*nValueIn)/nModSize;
    double dResult = dPriority + deltaPriority;
    LogPrint("mempool", "%s():%d - prioIn[%22.8f] + delta[%22.8f] = prioOut[%22.8f]\n",
        __func__, __LINE__, dPriority, deltaPriority, dResult);
    return dResult;
}

CCertificateMemPoolEntry::CCertificateMemPoolEntry(): nCertificateSize(0){}

CCertificateMemPoolEntry::CCertificateMemPoolEntry(const CScCertificate& _cert, const CAmount& _nFee,
                                 int64_t _nTime, double _dPriority,
                                 unsigned int _nHeight):
    CMemPoolEntry(_nFee, _nTime, _dPriority, _nHeight),
    cert(_cert) 
{
    nCertificateSize = ::GetSerializeSize(cert, SER_NETWORK, PROTOCOL_VERSION);
    nModSize = cert.CalculateModifiedSize(nCertificateSize);
    nUsageSize = RecursiveDynamicUsage(cert);
}

CCertificateMemPoolEntry::CCertificateMemPoolEntry(const CCertificateMemPoolEntry& other)
{
    *this = other;
}

double
CCertificateMemPoolEntry::GetPriority(unsigned int currentHeight) const
{
#if 1
    // certificates have max priority
    return dPriority;
#else
    CAmount nValueIn = cert.GetValueOfChange()+nFee;
    double deltaPriority = ((double)(currentHeight-nHeight)*nValueIn)/nModSize;
    double dResult = dPriority + deltaPriority;
    return dResult;
#endif
}

const std::map<int64_t, uint256>::const_reverse_iterator CSidechainMemPoolEntry::GetTopQualityCert() const
{
    return mBackwardCertificates.crbegin();
}

void CSidechainMemPoolEntry::EraseCert(const uint256& hash)
{
    for(auto it = mBackwardCertificates.begin(); it != mBackwardCertificates.end(); )
    {
        if(it->second == hash)
        {
            LogPrint("mempool", "%s():%d - removing cert [%s] from mBackwardCertificates\n",
                __func__, __LINE__, hash.ToString());
            it = mBackwardCertificates.erase(it);
        }
        else
            ++it;
    }
}

const std::map<int64_t, uint256>::const_iterator CSidechainMemPoolEntry::GetCert(const uint256& hash) const
{
    return std::find_if(mBackwardCertificates.begin(), mBackwardCertificates.end(),
        [&hash](const std::map<int64_t, uint256>::value_type& item) { return hash == item.second; });
}

bool CSidechainMemPoolEntry::HasCert(const uint256& hash) const
{
    return GetCert(hash) != mBackwardCertificates.end();
}

CTxMemPool::CTxMemPool(const CFeeRate& _minRelayFee) :
    nTransactionsUpdated(0), nCertificatesUpdated(0), cachedInnerUsage(0)
{
    // Sanity checks off by default for performance, because otherwise
    // accepting transactions becomes O(N^2) where N is the number
    // of transactions in the pool
    fSanityCheck = false;

    minerPolicyEstimator = new CBlockPolicyEstimator(_minRelayFee);
}

CTxMemPool::~CTxMemPool()
{
    delete minerPolicyEstimator;
}

void CTxMemPool::pruneSpent(const uint256 &hashTx, CCoins &coins)
{
    LOCK(cs);

    std::map<COutPoint, CInPoint>::iterator it = mapNextTx.lower_bound(COutPoint(hashTx, 0));

    // iterate over all COutPoints in mapNextTx whose hash equals the provided hashTx
    while (it != mapNextTx.end() && it->first.hash == hashTx) {
        coins.Spend(it->first.n); // and remove those outputs from coins
        it++;
    }
}

unsigned int CTxMemPool::GetTransactionsUpdated() const
{
    LOCK(cs);
    return nTransactionsUpdated;
}

void CTxMemPool::AddTransactionsUpdated(unsigned int n)
{
    LOCK(cs);
    nTransactionsUpdated += n;
}


bool CTxMemPool::addUnchecked(const uint256& hash, const CTxMemPoolEntry &entry, bool fCurrentEstimate)
{
    // Add to memory pool without checking anything.
    // Used by main.cpp AcceptToMemoryPool(), which DOES do
    // all the appropriate checks.
    LOCK(cs);
    mapTx[hash] = entry;
    const CTransaction& tx = mapTx[hash].GetTx();

    mapRecentlyAddedTxBase[tx.GetHash()] = std::shared_ptr<CTransactionBase>(new CTransaction(tx));
    nRecentlyAddedSequence += 1;

    for (unsigned int i = 0; i < tx.GetVin().size(); i++)
        mapNextTx[tx.GetVin()[i].prevout] = CInPoint(&tx, i);

    BOOST_FOREACH(const JSDescription &joinsplit, tx.GetVjoinsplit()) {
        BOOST_FOREACH(const uint256 &nf, joinsplit.nullifiers) {
            mapNullifiers[nf] = &tx;
        }
    }

    for(const auto& sc: tx.GetVscCcOut()) {
        LogPrint("mempool", "%s():%d - adding [%s] in mapSidechain\n", __func__, __LINE__, sc.GetScId().ToString() );
        mapSidechains[sc.GetScId()].scCreationTxHash = hash;
    }

    for(const auto& fwd: tx.GetVftCcOut()) {
        if (mapSidechains.count(fwd.scId) == 0)
            LogPrint("mempool", "%s():%d - adding [%s] in mapSidechain\n", __func__, __LINE__, fwd.scId.ToString() );
        mapSidechains[fwd.scId].fwdTransfersSet.insert(hash);
    }

    nTransactionsUpdated++;
    totalTxSize += entry.GetTxSize();
    cachedInnerUsage += entry.DynamicMemoryUsage();
    minerPolicyEstimator->processTransaction(entry, fCurrentEstimate);
    LogPrint("sc", "%s():%d - tx [%s] added in mempool\n", __func__, __LINE__, hash.ToString() );

    return true;
}

bool CTxMemPool::addUnchecked(const uint256& hash, const CCertificateMemPoolEntry &entry, bool fCurrentEstimate)
{
    LOCK(cs);
    mapCertificate[hash] = entry;
    const CScCertificate& cert = mapCertificate[hash].GetCertificate();

    mapRecentlyAddedTxBase[cert.GetHash()] = std::shared_ptr<CTransactionBase>(new CScCertificate(cert));
    nRecentlyAddedSequence += 1;

    for (unsigned int i = 0; i < cert.GetVin().size(); i++)
        mapNextTx[cert.GetVin()[i].prevout] = CInPoint(&cert, i);

    LogPrint("mempool", "%s():%d - adding cert [%s] q=%d in mapSidechain\n", __func__, __LINE__,
        cert.GetScId().ToString(), cert.quality);

    assert(mapSidechains[cert.GetScId()].mBackwardCertificates.count(cert.quality) == 0);
    mapSidechains[cert.GetScId()].mBackwardCertificates[cert.quality] = hash;
           
    nCertificatesUpdated++;
    totalCertificateSize += entry.GetCertificateSize();
    cachedInnerUsage += entry.DynamicMemoryUsage();
    // TODO cert: for the time being skip the part on policy estimator, certificates currently have maximum priority
    // minerPolicyEstimator->processTransaction(entry, fCurrentEstimate);
    LogPrint("mempool", "%s():%d - cert [%s] added in mempool\n", __func__, __LINE__, hash.ToString() );
    return true;
}

std::set<uint256> CTxMemPool::mempoolDirectAncestorsOf(const CTransactionBase& root) const
{
    std::set<uint256> res; //set allows to avoid duplicates

    AssertLockHeld(cs);

    //collect all inputs in mempool (zero-spent ones)...
    for(const auto& input : root.GetVin()) {
        if ((mapTx.count(input.prevout.hash) != 0) || (mapCertificate.count(input.prevout.hash) != 0))
            res.insert(input.prevout.hash);
    }

    //... and scCreations of all possible fwt
    if (!root.IsCertificate() )
    {
        const CTransaction* tx = dynamic_cast<const CTransaction*>(&root);
        if (tx == nullptr) {
            LogPrintf("%s():%d - could not make a tx from obj[%s]\n", __func__, __LINE__, root.GetHash().ToString());
            assert(false);
        }

        for(const auto& fwt: tx->GetVftCcOut()) {
            if (mapSidechains.count(fwt.scId) && !mapSidechains.at(fwt.scId).scCreationTxHash.IsNull())
                res.insert(mapSidechains.at(fwt.scId).scCreationTxHash);
        }
    }

    return res;
}

std::set<uint256> CTxMemPool::mempoolFullAncestorsOf(const CTransactionBase& originTx) const
{
    AssertLockHeld(cs);
    // it's Breath-First-Search on txes/certs Direct Acyclic Graph, having originTx as root.
    std::set<uint256> res = mempoolDirectAncestorsOf(originTx);
    std::deque<uint256> toVisit(res.begin(), res.end());

    while(!toVisit.empty())
    {
        const CTransactionBase* pCurrentNode = nullptr;
        if (mapTx.count(toVisit.back()))
        {
            pCurrentNode = &mapTx.at(toVisit.back()).GetTx();
        } else if (mapCertificate.count(toVisit.back())) {
            pCurrentNode = &mapCertificate.at(toVisit.back()).GetCertificate();
        } else
            assert(pCurrentNode);

        toVisit.pop_back();
        res.insert(pCurrentNode->GetHash());

        std::set<uint256> directAncestors = mempoolDirectAncestorsOf(*pCurrentNode);
        for(const uint256& ancestor : directAncestors) {
            if (std::find(toVisit.begin(), toVisit.end(), ancestor) == toVisit.end())
                toVisit.push_front(ancestor);
        }
    }

    return res;
}

std::set<uint256> CTxMemPool::mempoolDirectDescendantsOf(const CTransactionBase& root) const
{
    std::set<uint256> res; //set allows to avoid duplicates

    AssertLockHeld(cs);

    //Direct dependencies of root are txes/certs directly spending root outputs...
    for (unsigned int i = 0; i < root.GetVout().size(); i++)
    {
        std::map<COutPoint, CInPoint>::const_iterator it = mapNextTx.find(COutPoint(root.GetHash(), i));
        if (it == mapNextTx.end())
            continue;

        res.insert(it->second.ptx->GetHash());
    }

    // ... and, should root be a scCreationTx, also all fwds in mempool directed to sc created by root
    if (!root.IsCertificate() )
    {
        const CTransaction* tx = dynamic_cast<const CTransaction*>(&root);
        if (tx == nullptr)
        {
            // should never happen
            LogPrintf("%s():%d - could not make a tx from obj[%s]\n", __func__, __LINE__, root.GetHash().ToString());
            assert(false);
        }

        for(const auto& sc: tx->GetVscCcOut())
        {
            if (mapSidechains.count(sc.GetScId()) == 0)
                continue;
            for(const auto& fwdTxHash : mapSidechains.at(sc.GetScId()).fwdTransfersSet)
                res.insert(fwdTxHash);
        }
    }
    return res;
}

std::set<uint256> CTxMemPool::mempoolFullDescendantsOf(const CTransactionBase& origTx) const
{
    // it's Depth-First-Search on txes/certs Direct Acyclic Graph, having originTx as root.

    AssertLockHeld(cs);

    std::set<uint256> res = mempoolDirectDescendantsOf(origTx);
    std::deque<uint256> toVisit(res.begin(), res.end());

    while(!toVisit.empty())
    {
        const CTransactionBase * pCurrentRoot = nullptr;
        if (mapTx.count(toVisit.front()))
        {
            pCurrentRoot = &mapTx.at(toVisit.front()).GetTx();
        } else if (mapCertificate.count(toVisit.front())) {
            pCurrentRoot = &mapCertificate.at(toVisit.front()).GetCertificate();
        } else
            assert(pCurrentRoot);

        res.insert(toVisit.front());
        toVisit.pop_front();

        std::set<uint256> directDescendants = mempoolDirectDescendantsOf(*pCurrentRoot);
        for(const uint256& dep : directDescendants)
            if (std::find(toVisit.begin(), toVisit.end(),dep) == toVisit.end())
                toVisit.push_front(dep);
    }

    return res;
}

void CTxMemPool::remove(const CTransactionBase& origTx, std::list<CTransaction>& removedTxs, std::list<CScCertificate>& removedCerts, bool fRecursive)
{
    // Remove transaction from memory pool
 
    LOCK(cs);
    std::set<uint256> objToRemove;

    if (fRecursive)
        objToRemove = mempoolFullDescendantsOf(origTx);

    objToRemove.insert(origTx.GetHash());

    for(const uint256& hash : objToRemove)
    {
        if (mapTx.count(hash))
        {
            const CTransaction& tx = mapTx[hash].GetTx();
            mapRecentlyAddedTxBase.erase(hash);

            for(const CTxIn& txin: tx.GetVin())
                mapNextTx.erase(txin.prevout);
            for(const JSDescription& joinsplit: tx.GetVjoinsplit()) {
                for(const uint256& nf: joinsplit.nullifiers) {
                    mapNullifiers.erase(nf);
                }
            }

            for(const auto& fwd: tx.GetVftCcOut()) {
                if (mapSidechains.count(fwd.scId)) { //Guard against double-delete on multiple fwds toward the same sc in same tx
                    mapSidechains.at(fwd.scId).fwdTransfersSet.erase(tx.GetHash());

                    if (mapSidechains.at(fwd.GetScId()).IsNull())
                    {
                        LogPrint("mempool", "%s():%d - erasing [%s] from mapSidechain\n", __func__, __LINE__, fwd.scId.ToString() );
                        mapSidechains.erase(fwd.scId);
                    }
                }
            }

            for(const auto& sc: tx.GetVscCcOut()) {
                assert(mapSidechains.count(sc.GetScId()) != 0);
                mapSidechains.at(sc.GetScId()).scCreationTxHash.SetNull();

                if (mapSidechains.at(sc.GetScId()).IsNull())
                {
                    LogPrint("mempool", "%s():%d - erasing [%s] from mapSidechain\n", __func__, __LINE__, sc.GetScId().ToString() );
                    mapSidechains.erase(sc.GetScId());
                }
            }

            removedTxs.push_back(tx);
            totalTxSize -= mapTx[hash].GetTxSize();
            cachedInnerUsage -= mapTx[hash].DynamicMemoryUsage();

            LogPrint("mempool", "%s():%d - removing tx [%s] from mempool\n", __func__, __LINE__, hash.ToString() );
            mapTx.erase(hash);

            nTransactionsUpdated++;
            minerPolicyEstimator->removeTx(hash);
        } else if (mapCertificate.count(hash))
        {
            const CScCertificate& cert = mapCertificate[hash].GetCertificate();
            mapRecentlyAddedTxBase.erase(hash);

            for(const CTxIn& txin: cert.GetVin())
                mapNextTx.erase(txin.prevout);

            // remove certificate hash from list
            LogPrint("mempool", "%s():%d - removing cert [%s] from mapSidechain[%s]\n",
                __func__, __LINE__, hash.ToString(), cert.GetScId().ToString());
            mapSidechains.at(cert.GetScId()).EraseCert(hash);

            if (mapSidechains.at(cert.GetScId()).IsNull())
            {
                assert(mapSidechains.at(cert.GetScId()).mBackwardCertificates.empty());
                LogPrint("mempool", "%s():%d - erasing scid [%s] from mapSidechain\n", __func__, __LINE__, cert.GetScId().ToString() );
                mapSidechains.erase(cert.GetScId());
            }

            removedCerts.push_back(cert);
            totalCertificateSize -= mapCertificate[hash].GetCertificateSize();
            cachedInnerUsage -= mapCertificate[hash].DynamicMemoryUsage();
            LogPrint("mempool", "%s():%d - removing cert [%s] from mempool\n", __func__, __LINE__, hash.ToString() );
            mapCertificate.erase(hash);
            nCertificatesUpdated++;
        }
    }
 
}

inline bool CTxMemPool::checkTxImmatureExpenditures(
    const CTransaction& tx, const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight) 
{
    for(const CTxIn& txin: tx.GetVin())
    {
        // if input is the output of a tx in mempool, skip it
        std::map<uint256, CTxMemPoolEntry>::const_iterator it2 = mapTx.find(txin.prevout.hash);
        if (it2 != mapTx.end())
            continue;
 
        // if input is the out of a cert in mempool, it must be the case when the output is the change,
        // and can happen for instance after a chain reorg.
        // This tx must be removed because unconfirmed certificate change can not be spent
        std::map<uint256, CCertificateMemPoolEntry>::const_iterator it3 = mapCertificate.find(txin.prevout.hash);
        if (it3 != mapCertificate.end()) {
            // check this is the cert change
            assert(!it3->second.GetCertificate().IsBackwardTransfer(txin.prevout.n));

                LogPrint("mempool", "%s():%d - adding tx[%s] to list for removing since spends output %d of cert[%s] in mempool\n",
                    __func__, __LINE__, tx.GetHash().ToString(), txin.prevout.n, txin.prevout.hash.ToString());
            return false;
        }
 
        // the tx input has not been found in the mempool, therefore must be in blockchain
        const CCoins *coins = pcoins->AccessCoins(txin.prevout.hash);
        if (fSanityCheck) assert(coins);
 
        if (!coins) {
            LogPrint("mempool", "%s():%d - adding tx [%s] to list for removing since can not access coins of [%s]\n",
                __func__, __LINE__, tx.GetHash().ToString(), txin.prevout.hash.ToString());
            return false;
        }
 
        if (coins->IsCoinBase() || coins->IsFromCert() )
        {
            if (!coins->isOutputMature(txin.prevout.n, nMemPoolHeight) )
            {
                LogPrintf("%s():%d - Error: tx [%s] attempts to spend immature output [%d] of tx [%s]\n",
                        __func__, __LINE__, tx.GetHash().ToString(), txin.prevout.n, txin.prevout.hash.ToString());
                LogPrintf("%s():%d - Error: Immature coin info: coin creation height [%d], output maturity height [%d], spend height [%d]\n",
                        __func__, __LINE__, coins->nHeight, coins->nBwtMaturityHeight, nMemPoolHeight);
                if (coins->IsCoinBase()) {
                    LogPrint("mempool", "%s():%d - adding tx [%s] to list for removing since it spends immature coinbase [%s]\n",
                        __func__, __LINE__, tx.GetHash().ToString(), txin.prevout.hash.ToString());
                } else {
                    LogPrint("mempool", "%s():%d - adding tx [%s] to list for removing since it spends immature cert output %d of [%s]\n",
                        __func__, __LINE__, tx.GetHash().ToString(), txin.prevout.n, txin.prevout.hash.ToString());
                }
                return false;
            }
        }       
    }
    return true;
}

inline bool CTxMemPool::checkCertImmatureExpenditures(
    const CScCertificate& cert, const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight)
{
    for(const CTxIn& txin: cert.GetVin())
    {
        // if input is the output of a tx in mempool, skip it
        std::map<uint256, CTxMemPoolEntry>::const_iterator it2 = mapTx.find(txin.prevout.hash);
        if (it2 != mapTx.end())
            continue;
 
        // if input is the output of a cert in mempool, it must be the case when the output is the change, and it is legal.
        // This can happen for instance after a chain reorg.
        std::map<uint256, CCertificateMemPoolEntry>::const_iterator it3 = mapCertificate.find(txin.prevout.hash);
        if (it3 != mapCertificate.end()) {
            // check this is the cert change
            assert(!it3->second.GetCertificate().IsBackwardTransfer(txin.prevout.n));
            continue;
        }       
 
        // the cert input has not been found in the mempool, therefore must be in blockchain
        const CCoins *coins = pcoins->AccessCoins(txin.prevout.hash);
        if (fSanityCheck) assert(coins);
 
        if (!coins) {
            LogPrint("mempool", "%s():%d - adding cert[%s] to list for removing since can not access coins of [%s]\n",
                __func__, __LINE__, cert.GetHash().ToString(), txin.prevout.hash.ToString());
            return false;
        }
 
        if (coins->IsCoinBase() || coins->IsFromCert() )
        {
            if (!coins->isOutputMature(txin.prevout.n, nMemPoolHeight) )
            {
                LogPrintf("%s():%d - Error: cert[%s] attempts to spend immature output [%d] of [%s]\n",
                        __func__, __LINE__, cert.GetHash().ToString(), txin.prevout.n, txin.prevout.hash.ToString());
                LogPrintf("%s():%d - Error: Immature coin info: coin creation height [%d], output maturity height [%d], spend height [%d]\n",
                        __func__, __LINE__, coins->nHeight, coins->nBwtMaturityHeight, nMemPoolHeight);
                if (coins->IsCoinBase()) {
                    LogPrint("mempool", "%s():%d - adding cert [%s] to list for removing since it spends immature coinbase [%s]\n",
                        __func__, __LINE__, cert.GetHash().ToString(), txin.prevout.hash.ToString());
                } else {
                    LogPrint("mempool", "%s():%d - adding cert [%s] to list for removing since it spends immature cert output %d of [%s]\n",
                        __func__, __LINE__, cert.GetHash().ToString(), txin.prevout.n, txin.prevout.hash.ToString());
                }
                return false;
            }
        }       
    }
    return true;
}

void CTxMemPool::removeImmatureExpenditures(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight)
{
    // Remove transactions spending a coinbase or a certificate output which are now immature
    LOCK(cs);
    std::list<const CTransactionBase*> transactionsToRemove;
    for (std::map<uint256, CTxMemPoolEntry>::const_iterator it = mapTx.begin(); it != mapTx.end(); it++) {
        const CTransaction& tx = it->second.GetTx();

        if (!checkTxImmatureExpenditures(tx, pcoins, nMemPoolHeight))
        {
            transactionsToRemove.push_back(&tx);
        }
    }

    // the same for certificates
    for (std::map<uint256, CCertificateMemPoolEntry>::const_iterator it = mapCertificate.begin(); it != mapCertificate.end(); it++) {
        const CScCertificate& cert = it->second.GetCertificate();

        if (!checkCertImmatureExpenditures(cert, pcoins, nMemPoolHeight))
        {
            transactionsToRemove.push_back(&cert);
        }
    }

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    for(const CTransactionBase* tx: transactionsToRemove) {
        remove(*tx, removedTxs, removedCerts, true);
    }
}

void CTxMemPool::removeOutOfEpochCertificates(const CBlockIndex* pindexDelete)
{
    LOCK(cs);
    assert(pindexDelete);

    std::set<uint256> certsToRemove;

    // Remove certificates referring to this block as end epoch
    for (std::map<uint256, CCertificateMemPoolEntry>::const_iterator itCert = mapCertificate.begin(); itCert != mapCertificate.end(); itCert++)
    {
        const CScCertificate& cert = itCert->second.GetCertificate();

        if (cert.endEpochBlockHash == pindexDelete->GetBlockHash() )
        {
            LogPrint("mempool", "%s():%d - adding cert [%s] to list for removing (endEpochBlockHash %s)\n",
                __func__, __LINE__, cert.GetHash().ToString(), pindexDelete->GetBlockHash().ToString());
            certsToRemove.insert(cert.GetHash());
        }
    }

    if (certsToRemove.empty())
        return;

    std::list<CTransaction> dummyTxs;
    std::list<CScCertificate> dummyCerts;
    for(const auto hash: certsToRemove)
    {
        // there can be dependancy also between certs, so check that a cert is still in map during the loop
        if (mapCertificate.count(hash))
        {
            const CScCertificate& cert = mapCertificate.at(hash).GetCertificate();
            remove(cert, dummyTxs, dummyCerts, true);
        }
    }
    LogPrint("mempool", "%s():%d - removed %d certs and %d txes", __func__, __LINE__, dummyCerts.size(), dummyTxs.size());
}


void CTxMemPool::removeWithAnchor(const uint256 &invalidRoot)
{
    // If a block is disconnected from the tip, and the root changed,
    // we must invalidate transactions from the mempool which spend
    // from that root -- almost as though they were spending coinbases
    // which are no longer valid to spend due to coinbase maturity.
    LOCK(cs);
    std::list<CTransaction> transactionsToRemove;

    for (std::map<uint256, CTxMemPoolEntry>::const_iterator it = mapTx.begin(); it != mapTx.end(); it++) {
        const CTransaction& tx = it->second.GetTx();
        BOOST_FOREACH(const JSDescription& joinsplit, tx.GetVjoinsplit()) {
            if (joinsplit.anchor == invalidRoot) {
                transactionsToRemove.push_back(tx);
                break;
            }
        }
    }

    BOOST_FOREACH(const CTransaction& tx, transactionsToRemove) {
        std::list<CTransaction> dummyTxs;
        std::list<CScCertificate> dummyCerts;
        remove(tx, dummyTxs, dummyCerts, true);
    }
}

void CTxMemPool::removeConflicts(const CTransaction &tx, std::list<CTransaction>& removedTxs, std::list<CScCertificate>& removedCerts)
{
    // Remove transactions which depend on inputs of tx, recursively
    // not used
    // list<CTransaction> result;
    LOCK(cs);
    BOOST_FOREACH(const CTxIn &txin, tx.GetVin()) {
        std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end()) {
            const CTransactionBase &txConflict = *it->second.ptx;
            if (txConflict != tx)
            {
                remove(txConflict, removedTxs, removedCerts, true);
            }
        }
    }

    BOOST_FOREACH(const JSDescription &joinsplit, tx.GetVjoinsplit()) {
        BOOST_FOREACH(const uint256 &nf, joinsplit.nullifiers) {
            std::map<uint256, const CTransaction*>::iterator it = mapNullifiers.find(nf);
            if (it != mapNullifiers.end()) {
                const CTransactionBase &txConflict = *it->second;
                if (txConflict != tx)
                {
                    remove(txConflict, removedTxs, removedCerts, true);
                }
            }
        }
    }
}

/**
 * Called when a block is connected. Removes from mempool and updates the miner fee estimator.
 */
void CTxMemPool::removeForBlock(const std::vector<CTransaction>& vtx, unsigned int nBlockHeight,
                                std::list<CTransaction>& conflictingTxs, std::list<CScCertificate>& conflictingCerts, bool fCurrentEstimate)
{
    LOCK(cs);
    std::vector<CTxMemPoolEntry> entries;
    for(const CTransaction& tx: vtx)
    {
        uint256 hash = tx.GetHash();
        if (mapTx.count(hash))
            entries.push_back(mapTx[hash]);
    }

    // dummy lists: dummyCerts must be empty, dummyTxs contains exactly the txes that were in the mempool
    // and now are in the block. The caller is not interested in them because they will be synced with the block
    for(const CTransaction& tx: vtx)
    {
        std::list<CTransaction> dummyTxs;
        std::list<CScCertificate> dummyCerts;
        remove(tx, dummyTxs, dummyCerts, /*fRecursive*/false);
        removeConflicts(tx, conflictingTxs, conflictingCerts);
        ClearPrioritisation(tx.GetHash());
    }
    // After the txs in the new block have been removed from the mempool, update policy estimates
    minerPolicyEstimator->processBlock(nBlockHeight, entries, fCurrentEstimate);
}

void CTxMemPool::removeConflicts(const CScCertificate &cert,std::list<CTransaction>& removedTxs, std::list<CScCertificate>& removedCerts) {
    LOCK(cs);
    BOOST_FOREACH(const CTxIn &txin, cert.GetVin()) {
        std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end()) {
            const CTransactionBase &txConflict = *it->second.ptx;
            if (txConflict.GetHash() != cert.GetHash())
            {
                LogPrint("mempool", "%s():%d - removing [%s] conflicting with cert [%s]\n",
                    __func__, __LINE__, txConflict.GetHash().ToString(), cert.GetHash().ToString());
                remove(txConflict, removedTxs, removedCerts, true);
            }
        }
    }

    if (!mapSidechains.count(cert.GetScId()) || (mapSidechains.at(cert.GetScId()).mBackwardCertificates.empty()) )
        return;

    // cert has been confirmed in a block, therefore any other cert in mempool for this scid
    // with equal or lower quality is deemed conflicting and must be removed
    std::list<CScCertificate> vLowerQualCerts;
    for (auto entry :  mapSidechains.at(cert.GetScId()).mBackwardCertificates)
    {
        const uint256& memPoolCertHash = entry.second;
        const CScCertificate& memPoolCert = mapCertificate.at(memPoolCertHash).GetCertificate();

        if (memPoolCert.epochNumber == cert.epochNumber && memPoolCert.quality <= cert.quality)
        {
            LogPrint("mempool", "%s():%d - mempool cert[%s] q=%d conflicting with cert[%s] q=%d\n",
                __func__, __LINE__, memPoolCertHash.ToString(), memPoolCert.quality, cert.GetHash().ToString(), cert.quality);
            vLowerQualCerts.push_back(memPoolCert);
        }
    }

    for (auto& conflictingCert : vLowerQualCerts)
    {
        remove(conflictingCert, removedTxs, removedCerts, true);
    }
}

void CTxMemPool::removeForBlock(const std::vector<CScCertificate>& vcert, unsigned int nBlockHeight,
                                std::list<CTransaction>& removedTxs, std::list<CScCertificate>& removedCerts)
{
    LOCK(cs);

    // dummy lists: dummyTxs must be empty, dummyCerts contains exactly the certs that were in the mempool
    // and now are in the block. The caller is not interested in them because they will be synced with the block
    std::list<CTransaction> dummyTxs;
    std::list<CScCertificate> dummyCerts;
    for (const auto& cert : vcert)
    {
        remove(cert, dummyTxs, dummyCerts, /*fRecursive*/false);
        removeConflicts(cert, removedTxs, removedCerts);
        ClearPrioritisation(cert.GetHash());
    }
}

void CTxMemPool::clear()
{
    LOCK(cs);
    mapTx.clear();
    mapCertificate.clear();
    mapDeltas.clear();
    mapNextTx.clear();
    mapSidechains.clear();
    totalTxSize = 0;
    totalCertificateSize = 0;
    cachedInnerUsage = 0;
    ++nTransactionsUpdated;
}

void CTxMemPool::check(const CCoinsViewCache *pcoins) const
{
    if (!fSanityCheck)
        return;

    LogPrint("mempool", "Checking mempool with %u transactions, %u certificates, %u sidechains, and %u inputs\n",
        (unsigned int)mapTx.size(), (unsigned int)mapCertificate.size(), (unsigned int)mapSidechains.size(), (unsigned int)mapNextTx.size());

    uint64_t checkTotal = 0;
    uint64_t innerUsage = 0;

    CCoinsViewCache mempoolDuplicateTx(const_cast<CCoinsViewCache*>(pcoins));

    LOCK(cs);

    std::list<const CTxMemPoolEntry*> waitingOnDependantsTx;

    for (std::map<uint256, CTxMemPoolEntry>::const_iterator it = mapTx.begin(); it != mapTx.end(); it++) {
        unsigned int i = 0;
        checkTotal += it->second.GetTxSize();
        innerUsage += it->second.DynamicMemoryUsage();
        const CTransaction& tx = it->second.GetTx();

        bool fDependsWait = false;
        BOOST_FOREACH(const CTxIn &txin, tx.GetVin()) {
            // Check that every mempool transaction's inputs refer to available coins, or other mempool tx's.
            std::map<uint256, CTxMemPoolEntry>::const_iterator it2 = mapTx.find(txin.prevout.hash);
            if (it2 != mapTx.end()) {
                const CTransaction& tx2 = it2->second.GetTx();
                assert(tx2.GetVout().size() > txin.prevout.n && !tx2.GetVout()[txin.prevout.n].IsNull());
                fDependsWait = true;
            } else {
                // maybe our input is a certificate?
                std::map<uint256, CCertificateMemPoolEntry>::const_iterator itCert = mapCertificate.find(txin.prevout.hash);
                if (itCert != mapCertificate.end()) {
                    const CTransactionBase& cert = itCert->second.GetCertificate();
                    LogPrintf("%s():%d - ERROR input is the output of cert[%s]\n", __func__, __LINE__, cert.GetHash().ToString());
                    assert(false);
                }
                else
                {
                    const CCoins* coins = pcoins->AccessCoins(txin.prevout.hash);
                    assert(coins && coins->IsAvailable(txin.prevout.n));
                }
            }
            // Check whether its inputs are marked in mapNextTx.
            std::map<COutPoint, CInPoint>::const_iterator it3 = mapNextTx.find(txin.prevout);
            assert(it3 != mapNextTx.end());
            assert(it3->second.ptx == &tx);
            assert(it3->second.n == i);
            i++;
        }

        for(const auto& scCreation : tx.GetVscCcOut()) {
            //sc creation must be duly recorded in mapSidechain
            assert(mapSidechains.count(scCreation.GetScId()) != 0);
            assert(mapSidechains.at(scCreation.GetScId()).scCreationTxHash == tx.GetHash());

            //since sc creation is in mempool, there must not be in blockchain another sc re-declaring it
            assert(!pcoins->HaveSidechain(scCreation.GetScId()));

            //there cannot be no certificates for unconfirmed sidechains
            assert(mapSidechains.at(scCreation.GetScId()).mBackwardCertificates.empty());
        }

        for(const auto& fwd: tx.GetVftCcOut()) {
            //fwd must be duly recorded in mapSidechain
            assert(mapSidechains.count(fwd.scId) != 0);
            const auto& fwdPos = mapSidechains.at(fwd.scId).fwdTransfersSet.find(tx.GetHash());
            assert(fwdPos != mapSidechains.at(fwd.scId).fwdTransfersSet.end());

            //there must be no dangling fwds, i.e. sc creation is either in mempool or in blockchain
            if (!mapSidechains.at(fwd.scId).scCreationTxHash.IsNull())
                assert(mapTx.count(mapSidechains.at(fwd.scId).scCreationTxHash));
            else
                assert(pcoins->HaveSidechain(fwd.scId));
        }

        boost::unordered_map<uint256, ZCIncrementalMerkleTree, CCoinsKeyHasher> intermediates;

        BOOST_FOREACH(const JSDescription &joinsplit, tx.GetVjoinsplit()) {
            BOOST_FOREACH(const uint256 &nf, joinsplit.nullifiers) {
                assert(!pcoins->GetNullifier(nf));
            }

            ZCIncrementalMerkleTree tree;
            auto it = intermediates.find(joinsplit.anchor);
            if (it != intermediates.end()) {
                tree = it->second;
            } else {
                assert(pcoins->GetAnchorAt(joinsplit.anchor, tree));
            }

            BOOST_FOREACH(const uint256& commitment, joinsplit.commitments)
            {
                tree.append(commitment);
            }

            intermediates.insert(std::make_pair(tree.root(), tree));
        }
        if (fDependsWait)
        {
            waitingOnDependantsTx.push_back(&it->second);
        }
        else {
            CValidationState state;
            assert(::ContextualCheckInputs(tx, state, mempoolDuplicateTx, false, chainActive, 0, false, Params().GetConsensus(), NULL));
            CTxUndo dummyUndo;
            UpdateCoins(tx, mempoolDuplicateTx, dummyUndo, 1000000);
        }
    }

    unsigned int stepsSinceLastRemoveTx = 0;
    while (!waitingOnDependantsTx.empty()) {
        const CTxMemPoolEntry* entry = waitingOnDependantsTx.front();
        waitingOnDependantsTx.pop_front();
        CValidationState state;
        if (!mempoolDuplicateTx.HaveInputs(entry->GetTx())) {
            waitingOnDependantsTx.push_back(entry);
            stepsSinceLastRemoveTx++;
            assert(stepsSinceLastRemoveTx < waitingOnDependantsTx.size());
        } else {
            assert(::ContextualCheckInputs(entry->GetTx(), state, mempoolDuplicateTx, false, chainActive, 0, false, Params().GetConsensus(), NULL));
            CTxUndo dummyUndo;
            UpdateCoins(entry->GetTx(), mempoolDuplicateTx, dummyUndo, 1000000);
            stepsSinceLastRemoveTx = 0;
        }
    }

    CCoinsViewCache mempoolDuplicateCert(&mempoolDuplicateTx);
    std::list<const CCertificateMemPoolEntry*> waitingOnDependantsCert;

    for (auto it = mapCertificate.begin(); it != mapCertificate.end(); it++)
    {
        unsigned int i = 0;
        const auto& cert = it->second.GetCertificate();

        //certificate must be duly recorded in mapSidechain
        assert(mapSidechains.count(cert.GetScId()) != 0);
        auto hash = cert.GetHash();
        assert(mapSidechains.at(cert.GetScId()).GetCert(hash) != mapSidechains.at(cert.GetScId()).mBackwardCertificates.end() );

        bool fDependsWait = false;
        BOOST_FOREACH(const CTxIn &txin, cert.GetVin()) {
            // Check that every mempool certificate's inputs refer to available coins (tx have been processed above), or other mempool certs's.
            std::map<uint256, CCertificateMemPoolEntry>::const_iterator itCert = mapCertificate.find(txin.prevout.hash);
            if (itCert != mapCertificate.end()) {
                // certificates can only spend change outputs of another certificate in mempool, while backward transfers must mature first
                const CTransactionBase& inputCert = itCert->second.GetCertificate();
                if (inputCert.IsBackwardTransfer(txin.prevout.n))
                {
                    LogPrintf("%s():%d - ERROR input is the output of cert[%s]\n", __func__, __LINE__, inputCert.GetHash().ToString());
                    assert(false);
                }
                assert(inputCert.GetVout().size() > txin.prevout.n && !inputCert.GetVout()[txin.prevout.n].IsNull());
                fDependsWait = true;
            }
            else
            {
                const CCoins* coins = mempoolDuplicateTx.AccessCoins(txin.prevout.hash);
                assert(coins && coins->IsAvailable(txin.prevout.n));
            }
            // Check whether its inputs are marked in mapNextTx.
            std::map<COutPoint, CInPoint>::const_iterator it3 = mapNextTx.find(txin.prevout);
            assert(it3 != mapNextTx.end());
            assert(it3->second.ptx == &cert);
            assert(it3->second.n == i);
            i++;
        }

        checkTotal += it->second.GetCertificateSize();
        innerUsage += it->second.DynamicMemoryUsage();

        if (fDependsWait)
        {
            waitingOnDependantsCert.push_back(&it->second);
        }
        else {
            CValidationState state;
            assert(::ContextualCheckInputs(cert, state, mempoolDuplicateCert, false, chainActive, 0, false, Params().GetConsensus(), NULL));
            CTxUndo dummyUndo;
            UpdateCoins(cert, mempoolDuplicateCert, dummyUndo, 1000000);
        }
    }

    unsigned int stepsSinceLastRemoveCert = 0;
    while (!waitingOnDependantsCert.empty()) {
        const CCertificateMemPoolEntry* entry = waitingOnDependantsCert.front();
        waitingOnDependantsCert.pop_front();
        CValidationState state;
        if (!mempoolDuplicateCert.HaveInputs(entry->GetCertificate())) {
            waitingOnDependantsCert.push_back(entry);
            stepsSinceLastRemoveCert++;
            assert(stepsSinceLastRemoveCert < waitingOnDependantsCert.size());
        } else {
            assert(::ContextualCheckInputs(entry->GetCertificate(), state, mempoolDuplicateCert, false, chainActive, 0, false, Params().GetConsensus(), NULL));
            CTxUndo dummyUndo;
            UpdateCoins(entry->GetCertificate(), mempoolDuplicateCert, dummyUndo, 1000000);
            stepsSinceLastRemoveCert = 0;
        }
    }
    for (std::map<COutPoint, CInPoint>::const_iterator it = mapNextTx.begin(); it != mapNextTx.end(); it++) {
        uint256 hash = it->second.ptx->GetHash();
        std::map<uint256, CTxMemPoolEntry>::const_iterator it2 = mapTx.find(hash);
        std::map<uint256, CCertificateMemPoolEntry>::const_iterator it3 = mapCertificate.find(hash);
        if (it2 != mapTx.end())
        {
            const CTransaction& tx = it2->second.GetTx();
            assert(&tx == it->second.ptx);
            assert(tx.GetVin().size() > it->second.n);
            assert(it->first == it->second.ptx->GetVin()[it->second.n].prevout);
        }
        else
        if (it3 != mapCertificate.end())
        {
            const CScCertificate& cert = it3->second.GetCertificate();
            assert(&cert == it->second.ptx);
            assert(cert.GetVin().size() > it->second.n);
            assert(it->first == it->second.ptx->GetVin()[it->second.n].prevout);
        }
        else
        {
            assert(false);
        }
    }

    for (std::map<uint256, const CTransaction*>::const_iterator it = mapNullifiers.begin(); it != mapNullifiers.end(); it++) {
        uint256 hash = it->second->GetHash();
        std::map<uint256, CTxMemPoolEntry>::const_iterator it2 = mapTx.find(hash);
        const CTransaction& tx = it2->second.GetTx();
        assert(it2 != mapTx.end());
        assert(&tx == it->second);
    }

    assert((totalTxSize+totalCertificateSize) == checkTotal);
    assert(innerUsage == cachedInnerUsage);
}

void CTxMemPool::queryHashes(std::vector<uint256>& vtxid)
{
    vtxid.clear();

    LOCK(cs);
    vtxid.reserve(mapTx.size() + mapCertificate.size());
    for (std::map<uint256, CTxMemPoolEntry>::iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi)
        vtxid.push_back((*mi).first);
    for (std::map<uint256, CCertificateMemPoolEntry>::iterator mi = mapCertificate.begin(); mi != mapCertificate.end(); ++mi)
        vtxid.push_back((*mi).first);
}

bool CTxMemPool::lookup(uint256 hash, CTransaction& result) const
{
    LOCK(cs);
    std::map<uint256, CTxMemPoolEntry>::const_iterator i = mapTx.find(hash);
    if (i == mapTx.end()) return false;
    result = i->second.GetTx();
    return true;
}

bool CTxMemPool::lookup(uint256 hash, CScCertificate& result) const
{
    LOCK(cs);
    std::map<uint256, CCertificateMemPoolEntry>::const_iterator i = mapCertificate.find(hash);
    if (i == mapCertificate.end()) return false;
    result = i->second.GetCertificate();
    return true;
}

CFeeRate CTxMemPool::estimateFee(int nBlocks) const
{
    LOCK(cs);
    return minerPolicyEstimator->estimateFee(nBlocks);
}
double CTxMemPool::estimatePriority(int nBlocks) const
{
    LOCK(cs);
    return minerPolicyEstimator->estimatePriority(nBlocks);
}

bool
CTxMemPool::WriteFeeEstimates(CAutoFile& fileout) const
{
    try {
        LOCK(cs);
        fileout << 109900; // version required to read: 0.10.99 or later
        fileout << CLIENT_VERSION; // version that wrote the file
        minerPolicyEstimator->Write(fileout);
    }
    catch (const std::exception&) {
        LogPrintf("CTxMemPool::WriteFeeEstimates(): unable to write policy estimator data (non-fatal)\n");
        return false;
    }
    return true;
}

bool
CTxMemPool::ReadFeeEstimates(CAutoFile& filein)
{
    try {
        int nVersionRequired, nVersionThatWrote;
        filein >> nVersionRequired >> nVersionThatWrote;
        if (nVersionRequired > CLIENT_VERSION)
            return error("CTxMemPool::ReadFeeEstimates(): up-version (%d) fee estimate file", nVersionRequired);

        LOCK(cs);
        minerPolicyEstimator->Read(filein);
    }
    catch (const std::exception&) {
        LogPrintf("CTxMemPool::ReadFeeEstimates(): unable to read policy estimator data (non-fatal)\n");
        return false;
    }
    return true;
}

void CTxMemPool::PrioritiseTransaction(const uint256& hash, const std::string& strHash, double dPriorityDelta, const CAmount& nFeeDelta)
{
    {
        LOCK(cs);
        std::pair<double, CAmount> &deltas = mapDeltas[hash];
        deltas.first += dPriorityDelta;
        deltas.second += nFeeDelta;
    }
    LogPrintf("PrioritiseTransaction: %s priority += %f, fee += %d\n", strHash, dPriorityDelta, FormatMoney(nFeeDelta));
}

void CTxMemPool::ApplyDeltas(const uint256& hash, double &dPriorityDelta, CAmount &nFeeDelta)
{
    LOCK(cs);
    std::map<uint256, std::pair<double, CAmount> >::iterator pos = mapDeltas.find(hash);
    if (pos == mapDeltas.end())
        return;
    const std::pair<double, CAmount> &deltas = pos->second;
    dPriorityDelta += deltas.first;
    nFeeDelta += deltas.second;
}

void CTxMemPool::ClearPrioritisation(const uint256& hash)
{
    LOCK(cs);
    mapDeltas.erase(hash);
}

bool CTxMemPool::HasNoInputsOf(const CTransaction &tx) const
{
    for (unsigned int i = 0; i < tx.GetVin().size(); i++)
        if (exists(tx.GetVin()[i].prevout.hash))
            return false;
    return true;

}

void CTxMemPool::NotifyRecentlyAdded()
{
    uint64_t recentlyAddedSequence;
    std::vector<std::shared_ptr<CTransactionBase> > vTxBase;
    {
        LOCK(cs);
        recentlyAddedSequence = nRecentlyAddedSequence;
        for (const auto& kv : mapRecentlyAddedTxBase) {
            vTxBase.push_back(kv.second);
        }
        mapRecentlyAddedTxBase.clear();
    }

    // A race condition can occur here between these SyncWithWallets calls, and
    // the ones triggered by block logic (in ConnectTip and DisconnectTip). It
    // is harmless because calling SyncWithWallets(_, NULL) does not alter the
    // wallet transaction's block information.
    for (auto txBase : vTxBase) {
        try {
            if (txBase->IsCertificate())
            {
                LogPrint("mempool", "%s():%d - sync with wallet cert[%s]\n", __func__, __LINE__, txBase->GetHash().ToString());
                SyncWithWallets( dynamic_cast<const CScCertificate&>(*txBase), nullptr);
            }
            else
            {
                LogPrint("mempool", "%s():%d - sync with wallet tx[%s]\n", __func__, __LINE__, txBase->GetHash().ToString());
                SyncWithWallets( dynamic_cast<const CTransaction&>(*txBase), nullptr);
            }
        } catch (const boost::thread_interrupted&) {
            LogPrintf("%s():%d - thread interrupted exception\n", __func__, __LINE__);
            throw;
        } catch (const std::exception& e) {
            // this also catches bad_cast
            PrintExceptionContinue(&e, "CTxMemPool::NotifyRecentlyAdded()");
        } catch (...) {
            PrintExceptionContinue(NULL, "CTxMemPool::NotifyRecentlyAdded()");
        }
    }

    // Update the notified sequence number. We only need this in regtest mode,
    // and should not lock on cs after calling SyncWithWallets otherwise.
    if (Params().NetworkIDString() == "regtest") {
        LOCK(cs);
        nNotifiedSequence = recentlyAddedSequence;
    }
}

bool CTxMemPool::IsFullyNotified() {
    assert(Params().NetworkIDString() == "regtest");
    LOCK(cs);
    return nRecentlyAddedSequence == nNotifiedSequence;
}

CCoinsViewMemPool::CCoinsViewMemPool(CCoinsView *baseIn, CTxMemPool &mempoolIn) : CCoinsViewBacked(baseIn), mempool(mempoolIn) { }

bool CCoinsViewMemPool::GetNullifier(const uint256 &nf) const {
    if (mempool.mapNullifiers.count(nf))
        return true;

    return base->GetNullifier(nf);
}

bool CCoinsViewMemPool::GetCoins(const uint256 &txid, CCoins &coins) const {
    // If an entry in the mempool exists, always return that one, as it's guaranteed to never
    // conflict with the underlying cache, and it cannot have pruned entries (as it contains full)
    // transactions. First checking the underlying cache risks returning a pruned entry instead.
    CTransaction tx;
    if (mempool.lookup(txid, tx)) {
        LogPrint("mempool", "%s():%d - making coins for tx [%s]\n", __func__, __LINE__, txid.ToString() );
        coins = CCoins(tx, MEMPOOL_HEIGHT);
        return true;
    }

    CScCertificate cert;
    if (mempool.lookup(txid, cert)) {
        LogPrint("mempool", "%s():%d - making coins for cert [%s]\n", __func__, __LINE__, txid.ToString() );
        coins = CCoins(cert, MEMPOOL_HEIGHT, MEMPOOL_HEIGHT);
        return true;
    }
    return (base->GetCoins(txid, coins) && !coins.IsPruned());
}

bool CCoinsViewMemPool::HaveCoins(const uint256 &txid) const {
    return mempool.exists(txid) || base->HaveCoins(txid);
}

bool CCoinsViewMemPool::GetSidechain(const uint256& scId, CSidechain& info) const {
    if (mempool.hasSidechainCreationTx(scId))
    {
        //build sidechain from txs in mempool
        const uint256& scCreationHash = mempool.mapSidechains.at(scId).scCreationTxHash;
        const CTransaction & scCreationTx = mempool.mapTx.at(scCreationHash).GetTx();
        for (const auto& scCreation : scCreationTx.GetVscCcOut())
        {
            if (scId == scCreation.GetScId())
            {
                //info.creationBlockHash doesn't exist here!
                info.creationBlockHeight = -1; //default null value for creationBlockHeight
                info.creationTxHash = scCreationHash;
                info.creationData.withdrawalEpochLength = scCreation.withdrawalEpochLength;
                info.creationData.customData = scCreation.customData;
                info.creationData.constant = scCreation.constant;
                info.creationData.wCertVk = scCreation.wCertVk;
                break;
            }
        }
    } else if (!base->GetSidechain(scId, info))
        return false;

    //decorate sidechain with fwds and bwt in mempool if any
    if (mempool.mapSidechains.count(scId) != 0)
    {
        for (const auto& fwdHash: mempool.mapSidechains.at(scId).fwdTransfersSet)
        {
            const CTransaction & fwdTx = mempool.mapTx.at(fwdHash).GetTx();
            for (const auto& fwdAmount : fwdTx.GetVftCcOut())
                if (scId == fwdAmount.scId)
                    info.mImmatureAmounts[-1] += fwdAmount.nValue;
        }
    }

    return true;
}

bool CCoinsViewMemPool::HaveSidechain(const uint256& scId) const {
    return mempool.hasSidechainCreationTx(scId) || base->HaveSidechain(scId);
}

size_t CTxMemPool::DynamicMemoryUsage() const {
    LOCK(cs);
    return
        ( memusage::DynamicUsage(mapTx) +
          memusage::DynamicUsage(mapNextTx) +
          memusage::DynamicUsage(mapDeltas) +
          memusage::DynamicUsage(mapCertificate) +
          memusage::DynamicUsage(mapSidechains) +
          cachedInnerUsage);
}

std::pair<uint256, CAmount> CTxMemPool::FindConflictingCert(const uint256& scId, int64_t certQuality)
{
    LOCK(cs);
    std::pair<uint256, CAmount> res = std::make_pair(uint256(),CAmount(-1));

    if (mapSidechains.count(scId) == 0)
        return res;

    for(const auto& mempoolCertEntry : mapSidechains.at(scId).mBackwardCertificates)
    {
        const CScCertificate& mempoolCert = mapCertificate.at(mempoolCertEntry.second).GetCertificate();
        if (mempoolCert.quality == certQuality) {
            res.first  = mempoolCert.GetHash();
            res.second = mapCertificate.at(mempoolCertEntry.second).GetFee();
            break;
        }
    }

    return res;
}

bool CTxMemPool::RemoveConflictingQualityCert(const uint256& certToRmHash)
{
    LOCK(cs);

    if(mapCertificate.count(certToRmHash) == 0)
        return true; //nothing to remove

    CScCertificate certToRm = mapCertificate.at(certToRmHash).GetCertificate();
    std::list<CTransaction> conflictingTxs;
    std::list<CScCertificate> conflictingCerts;
    remove(certToRm, conflictingTxs, conflictingCerts, true);

    // Tell wallet about transactions and certificates that went from mempool to conflicted:
    for(const auto &t: conflictingTxs) {
        LogPrint("mempool", "%s():%d - syncing tx %s\n", __func__, __LINE__, t.GetHash().ToString());
        SyncWithWallets(t, nullptr);
    }
    for(const auto &c: conflictingCerts) {
        LogPrint("mempool", "%s():%d - syncing cert %s\n", __func__, __LINE__, c.GetHash().ToString());
        SyncWithWallets(c, nullptr);
    }

    return true;
}

bool CTxMemPool::IsTopQualityCertInMempool(const CScCertificate& cert)
{
    const uint256& scId = cert.GetScId();
    if (mapSidechains.count(scId) != 0  &&
        !mapSidechains.at(scId).mBackwardCertificates.empty())
    {
        return cert.GetHash() == mapSidechains.at(scId).GetTopQualityCert()->second;
    }
    return false;
}

void CTxMemPool::SyncLowQualityCerts(const CScCertificate& cert)
{
    const uint256& scId = cert.GetScId();

    // look for worse certs in mempool
    if ((mapSidechains.count(scId) != 0) &&
        (!mapSidechains.at(scId).mBackwardCertificates.empty()))
    {
        for (const auto& entry : mapSidechains.at(scId).mBackwardCertificates)
        {
            const uint256& hash = entry.second;
            // if just this cert is the only one return true, otherwise skip
            if (hash == cert.GetHash())
            {
                if (mapSidechains.at(scId).mBackwardCertificates.size() == 1)
                    break;
                else
                    continue;
            }

            const CScCertificate& memPoolCert = mapCertificate.at(hash).GetCertificate();
            if (memPoolCert.quality < cert.quality)
            {
                LogPrint("cert", "%s():%d - sync voiding cert %s\n", __func__, __LINE__, hash.ToString() ); 
                SyncVoidedCert(hash, true);
            }
        }
    }

    // look for worse cert in db (will be overridden as soon as the next block will be mined)
    CCoinsViewCache &view = *pcoinsTip;
    CSidechain info;
    if (view.GetSidechain(scId, info))
    {
        if (info.topCommittedCertReferencedEpoch == cert.epochNumber &&
            info.topCommittedCertQuality < cert.quality)
        {
            LogPrint("cert", "%s():%d - sync voiding cert %s\n", __func__, __LINE__, info.topCommittedCertHash.ToString() ); 
            SyncVoidedCert(info.topCommittedCertHash, true);
        }
    }

    // finally promote it to be the best in wallet if the case
    if (IsTopQualityCertInMempool(cert))
    {
        LogPrint("cert", "%s():%d - sync refilling cert %s\n", __func__, __LINE__, cert.GetHash().ToString() ); 
        SyncVoidedCert(cert.GetHash(), false);
    }
}
