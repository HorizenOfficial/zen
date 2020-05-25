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
#include "main.h"

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
    LogPrint("cert", "%s():%d - prioIn[%22.8f] + delta[%22.8f] = prioOut[%22.8f]\n",
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

    for (unsigned int i = 0; i < tx.GetVin().size(); i++)
        mapNextTx[tx.GetVin()[i].prevout] = CInPoint(&tx, i);

    BOOST_FOREACH(const JSDescription &joinsplit, tx.GetVjoinsplit()) {
        BOOST_FOREACH(const uint256 &nf, joinsplit.nullifiers) {
            mapNullifiers[nf] = &tx;
        }
    }

    for(const auto& sc: tx.GetVscCcOut()) {
        if (mapSidechains.count(sc.scId))
            assert(mapSidechains[sc.scId].scCreationTxHash.IsNull());
        else
            LogPrint("cert", "%s():%d - adding [%s] in mapSidechain\n", __func__, __LINE__, sc.scId.ToString() );

        mapSidechains[sc.scId].scCreationTxHash = hash;
    }

    for(const auto& fwd: tx.GetVftCcOut()) {
        if (mapSidechains.count(fwd.scId) == 0)
            LogPrint("cert", "%s():%d - adding [%s] in mapSidechain\n", __func__, __LINE__, fwd.scId.ToString() );
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

    for (unsigned int i = 0; i < cert.GetVin().size(); i++)
        mapNextTx[cert.GetVin()[i].prevout] = CInPoint(&cert, i);

    if (mapSidechains.count(cert.GetScId()) )
        assert(mapSidechains[cert.GetScId()].backwardCertificate.IsNull());
    else
        LogPrint("cert", "%s():%d - adding [%s] in mapSidechain\n", __func__, __LINE__, cert.GetScId().ToString() );

    mapSidechains[cert.GetScId()].backwardCertificate = hash;
           
    nCertificatesUpdated++;
    totalCertificateSize += entry.GetCertificateSize();
    cachedInnerUsage += entry.DynamicMemoryUsage();
    // TODO cert: for the time being skip the part on policy estimator, certificates currently have maximum priority
    // minerPolicyEstimator->processTransaction(entry, fCurrentEstimate);
    LogPrint("cert", "%s():%d - cert [%s] added in mempool\n", __func__, __LINE__, hash.ToString() );
    return true;
}

void CTxMemPool::remove(const CTransactionBase& origTx, std::list<CTransaction>& removedTxs, std::list<CScCertificate>& removedCerts, bool fRecursive, bool removeDependantFwds)
{
    // Remove transaction from memory pool
    {
        LOCK(cs);
        std::deque<uint256> objToRemove;
        objToRemove.push_back(origTx.GetHash());

        if(!fRecursive)
            return removeInternal(objToRemove, removedTxs, removedCerts, fRecursive, removeDependantFwds);

        if (!mapCertificate.count(origTx.GetHash()) && !mapTx.count(origTx.GetHash())) {
            // If recursively removing but origTx isn't in the mempool
            // be sure to remove any children that are in the pool. This can
            // happen during chain re-orgs if origCert isn't re-accepted into
            // the mempool for any reason.
            for (unsigned int i = 0; i < origTx.GetVout().size(); i++) {
                std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(origTx.GetHash(), i));
                if (it == mapNextTx.end())
                    continue;
                LogPrint("sc", "%s():%d - adding tx [%s] to list for removing\n", __func__, __LINE__, it->second.ptx->GetHash().ToString());
                objToRemove.push_back(it->second.ptx->GetHash());
            }

            for(const auto& sc: origTx.GetVscCcOut()) {
                if (mapSidechains.count(sc.scId) == 0)
                    continue;
                if (removeDependantFwds) {
                    for(const auto& fwdTxHash : mapSidechains.at(sc.scId).fwdTransfersSet)
                        objToRemove.push_back(fwdTxHash);
                } else
                    objToRemove.push_back(mapSidechains.at(sc.scId).scCreationTxHash);
            }
        }
        removeInternal(objToRemove, removedTxs, removedCerts, fRecursive, removeDependantFwds);
    }
}

void::CTxMemPool::removeInternal(
    std::deque<uint256>& objToRemove,
    std::list<CTransaction>& removedTxs,
    std::list<CScCertificate>& removedCerts,
    bool fRecursive,
    bool removeDependantFwds)
{
    // called with lock taken
    AssertLockHeld(cs);

    while (!objToRemove.empty())
    {
        uint256 hash = objToRemove.front();
        objToRemove.pop_front();
        if (mapTx.count(hash))
        {
            const CTransaction& tx = mapTx[hash].GetTx();
            if (fRecursive) {
                for (unsigned int i = 0; i < tx.GetVout().size(); i++) {
                    std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(hash, i));
                    if (it == mapNextTx.end())
                        continue;
                    objToRemove.push_back(it->second.ptx->GetHash());
                }
                for(const auto& sc: tx.GetVscCcOut()) {
                    if (mapSidechains.count(sc.scId) == 0)
                        continue;

                    if (removeDependantFwds) {
                        for(const auto& ccObjHash : mapSidechains.at(sc.scId).fwdTransfersSet)
                            objToRemove.push_back(ccObjHash);
                    } else
                        objToRemove.push_back(mapSidechains.at(sc.scId).scCreationTxHash);

                    //no backward cert for unconfirmed sidechain can be in mempool
                    assert(mapSidechains.at(sc.scId).backwardCertificate.IsNull());
                }
            }

            BOOST_FOREACH(const CTxIn& txin, tx.GetVin())
                mapNextTx.erase(txin.prevout);
            BOOST_FOREACH(const JSDescription& joinsplit, tx.GetVjoinsplit()) {
                BOOST_FOREACH(const uint256& nf, joinsplit.nullifiers) {
                    mapNullifiers.erase(nf);
                }
            }

            for(const auto& fwd: tx.GetVftCcOut()) {
                if (mapSidechains.count(fwd.scId)) { //Guard against double-delete on multiple fwds toward the same sc in same tx
                    mapSidechains.at(fwd.scId).fwdTransfersSet.erase(tx.GetHash());

                    if (mapSidechains.at(fwd.scId).fwdTransfersSet.size() == 0 &&
                        mapSidechains.at(fwd.scId).scCreationTxHash.IsNull() &&
                        mapSidechains.at(fwd.scId).backwardCertificate.IsNull() )
                    {
                        LogPrint("mempool", "%s():%d - erasing [%s] from mapSidechain\n", __func__, __LINE__, fwd.scId.ToString() );
                        mapSidechains.erase(fwd.scId);
                    }
                }
            }

            for(const auto& sc: tx.GetVscCcOut()) {
                assert(mapSidechains.count(sc.scId) != 0);
                mapSidechains.at(sc.scId).scCreationTxHash.SetNull();

                if (mapSidechains.at(sc.scId).fwdTransfersSet.size() == 0)
                {
                    LogPrint("mempool", "%s():%d - erasing [%s] from mapSidechain\n", __func__, __LINE__, sc.scId.ToString() );
                    mapSidechains.erase(sc.scId);
                }
            }

            removedTxs.push_back(tx);
            totalTxSize -= mapTx[hash].GetTxSize();
            cachedInnerUsage -= mapTx[hash].DynamicMemoryUsage();

            LogPrint("mempool", "%s():%d - removing tx [%s] from mempool\n", __func__, __LINE__, hash.ToString() );
            mapTx.erase(hash);

            nTransactionsUpdated++;
            minerPolicyEstimator->removeTx(hash);
        }
        else if (mapCertificate.count(hash))
        {
            const CScCertificate& cert = mapCertificate[hash].GetCertificate();
            if (fRecursive)
            {
                for (unsigned int i = 0; i < cert.GetVout().size(); i++) {
                    std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(hash, i));
                    if (it == mapNextTx.end())
                        continue;
                    objToRemove.push_back(it->second.ptx->GetHash());
                }
            }

            BOOST_FOREACH(const CTxIn& txin, cert.GetVin())
                mapNextTx.erase(txin.prevout);

            mapSidechains.at(cert.GetScId()).backwardCertificate.SetNull();
            if (mapSidechains.at(cert.GetScId()).fwdTransfersSet.size() == 0 &&
                mapSidechains.at(cert.GetScId()).scCreationTxHash.IsNull() )
            {
                LogPrint("mempool", "%s():%d - erasing [%s] from mapSidechain\n", __func__, __LINE__, cert.GetScId().ToString() );
                mapSidechains.erase(cert.GetScId());
            }

            removedCerts.push_back(cert);
            totalTxSize -= mapCertificate[hash].GetCertificateSize();
            cachedInnerUsage -= mapCertificate[hash].DynamicMemoryUsage();
            LogPrint("mempool", "%s():%d - removing cert [%s] from mempool\n", __func__, __LINE__, hash.ToString() );
            mapCertificate.erase(hash);
            nCertificatesUpdated++;
        }
    }
}

inline bool CTxMemPool::addToListForRemovalImmatureExpenditures(
    const CTransactionBase& tx, const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight, 
    std::list<const CTransactionBase*>& transactionsToRemove)
{
    for(const CTxIn& txin: tx.GetVin()) {
        // if input is the output of a tx in mempool, skip it
        std::map<uint256, CTxMemPoolEntry>::const_iterator it2 = mapTx.find(txin.prevout.hash);
        if (it2 != mapTx.end())
            continue;
 
        // if input is the out of a cert in mempool, it must be the case when the output is the change,
        // and can happen only after a chain reorg. The tx must be removed because unconfirmed certificate change 
        // can not be spent
        std::map<uint256, CCertificateMemPoolEntry>::const_iterator it3 = mapCertificate.find(txin.prevout.hash);
        if (it3 != mapCertificate.end()) {
            LogPrint("mempool", "%s():%d - adding tx[%s] to list for removing since spends output %d of cert[%s] in mempool\n",
                __func__, __LINE__, tx.GetHash().ToString(), txin.prevout.n, txin.prevout.hash.ToString());
            // can not be anything but change
            assert(!it3->second.GetCertificate().GetVout()[txin.prevout.n].isFromBackwardTransfer);
            transactionsToRemove.push_back(&tx);
            return true;
        }
 
        // the tx input has not been found in the mempool, therefore must be in blockchain
        const CCoins *coins = pcoins->AccessCoins(txin.prevout.hash);
        if (fSanityCheck) assert(coins);
 
        if (!coins) {
            LogPrint("mempool", "%s():%d - adding tx [%s] to list for removing since can not access coins of [%s]\n",
                __func__, __LINE__, tx.GetHash().ToString(), txin.prevout.hash.ToString());
            transactionsToRemove.push_back(&tx);
            return true;
        }
 
        if (coins->IsCoinBase()) {
            if (((signed long)nMemPoolHeight) - coins->nHeight < COINBASE_MATURITY) {
                LogPrint("mempool", "%s():%d - adding tx [%s] to list for removing since it spends immature coinbase [%s]\n",
                    __func__, __LINE__, tx.GetHash().ToString(), txin.prevout.hash.ToString());
                transactionsToRemove.push_back(&tx);
                return true;
            }
        } else if (coins->IsFromCert()) {
            if (fSanityCheck) {
                assert(coins->IsAvailable(txin.prevout.n));
                assert(pcoins->HaveSidechain(coins->originScId));
            }
 
            if (pcoins->IsCertOutputMature(txin.prevout.hash, txin.prevout.n, nMemPoolHeight) !=
                        CCoinsViewCache::outputMaturity::MATURE) {
                LogPrint("mempool", "%s():%d - adding tx [%s] to list for removing since it spends immature cert output %d of [%s]\n",
                    __func__, __LINE__, tx.GetHash().ToString(), txin.prevout.n, txin.prevout.hash.ToString());
                transactionsToRemove.push_back(&tx);
                return true;
            }
        }
    }
    return false;
}

void CTxMemPool::removeImmatureExpenditures(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight)
{
    // Remove transactions spending a coinbase or a certificate output which are now immature
    LOCK(cs);
    std::list<const CTransactionBase*> transactionsToRemove;
    for (std::map<uint256, CTxMemPoolEntry>::const_iterator it = mapTx.begin(); it != mapTx.end(); it++) {
        const CTransactionBase& tx = it->second.GetTx();

        if (addToListForRemovalImmatureExpenditures(tx, pcoins, nMemPoolHeight, transactionsToRemove))
            break;          
    }

    // the same for certificates
    for (std::map<uint256, CCertificateMemPoolEntry>::const_iterator it = mapCertificate.begin(); it != mapCertificate.end(); it++) {
        const CTransactionBase& cert = it->second.GetCertificate();

        if (addToListForRemovalImmatureExpenditures(cert, pcoins, nMemPoolHeight, transactionsToRemove))
            break;          
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

    std::list<const CTransactionBase*>   txsToRemove;

    // Remove certificates referring to this block as end epoch
    for (std::map<uint256, CCertificateMemPoolEntry>::const_iterator it = mapCertificate.begin(); it != mapCertificate.end(); it++)
    {
        const CScCertificate& cert = it->second.GetCertificate();

        if (cert.endEpochBlockHash == pindexDelete->GetBlockHash() )
        {
            LogPrint("mempool", "%s():%d - adding cert [%s] to list for removing (endEpochBlockHash %s)\n",
                __func__, __LINE__, cert.GetHash().ToString(), pindexDelete->GetBlockHash().ToString());
            txsToRemove.push_back(&cert);

            // Remove also txes that depend on such certificates
            for (unsigned int i = 0; i < cert.GetVout().size(); i++) {
                std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(cert.GetHash(), i));
                if (it == mapNextTx.end())
                    continue;
                LogPrint("mempool", "%s():%d - marking tx [%s] for removal since it spends out-of-epoch cert\n", __func__, __LINE__, it->second.ptx->GetHash().ToString());
                txsToRemove.push_back(it->second.ptx);
            }
        }
    }

    std::list<CTransaction> dummyTxs;
    std::list<CScCertificate> dummyCerts;
    for(const CTransactionBase* tx: txsToRemove) {
        remove(*tx, dummyTxs, dummyCerts, true);
    }
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

    std::list<CTransaction> dummyTxs;
    std::list<CScCertificate> dummyCerts;
    BOOST_FOREACH(const CTransaction& tx, transactionsToRemove) {
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

    for(const auto& sc: tx.GetVscCcOut()) {
        if(hasSidechainCreationTx(sc.scId)) {
            const uint256& scRedeclarationHash = mapSidechains[sc.scId].scCreationTxHash;
            const CTransactionBase &scReDeclarationTx = mapTx[scRedeclarationHash].GetTx();
            std::list<CScCertificate> dummyCerts;
            remove(scReDeclarationTx, removedTxs, removedCerts, /*fRecursive*/true, /*removeDependantFwds*/false);
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

    std::list<CTransaction> dummyTxs;
    std::list<CScCertificate> dummyCerts;
    for(const CTransaction& tx: vtx)
    {
        remove(tx, dummyTxs, dummyCerts, false);
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
                remove(txConflict, removedTxs, removedCerts, true);
            }
        }
    }

    //a certificate for a sidechain has been confirmed in a block. Any unconfirmed cert in mempool is deemed conflicting and removed
    if (!mapSidechains.count(cert.GetScId()))
        return;

    if (mapSidechains.at(cert.GetScId()).backwardCertificate.IsNull())
        return;

    const CScCertificate& conflictingCert = mapCertificate.at(mapSidechains.at(cert.GetScId()).backwardCertificate).GetCertificate();
    remove(conflictingCert, removedTxs, removedCerts, true);
}

void CTxMemPool::removeForBlock(const std::vector<CScCertificate>& vcert, unsigned int nBlockHeight,
                                std::list<CTransaction>& removedTxs, std::list<CScCertificate>& removedCerts)
{
    LOCK(cs);
    for (const auto& cert : vcert)
    {
        remove(cert, removedTxs, removedCerts, false);
        removeConflicts(cert, removedTxs, removedCerts);
        ClearPrioritisation(cert.GetHash());
    }
}

void CTxMemPool::clear()
{
    LOCK(cs);
    mapTx.clear();
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

    LogPrint("mempool", "Checking mempool with %u transactions and %u inputs\n", (unsigned int)mapTx.size(), (unsigned int)mapNextTx.size());

    uint64_t checkTotal = 0;
    uint64_t innerUsage = 0;

    CCoinsViewCache mempoolDuplicate(const_cast<CCoinsViewCache*>(pcoins));

    LOCK(cs);

    std::list<const CMemPoolEntry*> waitingOnDependants;
    std::list<const CMemPoolEntry*> waitingOnDependantsCert;

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
            assert(mapSidechains.count(scCreation.scId) != 0);
            assert(mapSidechains.at(scCreation.scId).scCreationTxHash == tx.GetHash());

            //since sc creation is in mempool, there must not be in blockchain another sc re-declaring it
            assert(!pcoins->HaveSidechain(scCreation.scId));

            //there cannot be no certificates for unconfirmed sidechains
            assert(mapSidechains.at(scCreation.scId).backwardCertificate.IsNull());
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
            waitingOnDependants.push_back(&it->second);
        }
        else {
            CValidationState state;
            assert(::ContextualCheckInputs(tx, state, mempoolDuplicate, false, chainActive, 0, false, Params().GetConsensus(), NULL));
            UpdateCoins(tx, state, mempoolDuplicate, 1000000);
        }
    }

    for (auto it = mapCertificate.begin(); it != mapCertificate.end(); it++)
    {
        unsigned int i = 0;
        const auto& cert = it->second.GetCertificate();

        //certificate must be duly recorded in mapSidechain
        assert(mapSidechains.count(cert.GetScId()) != 0);
        assert(mapSidechains.at(cert.GetScId()).backwardCertificate == cert.GetHash());

        bool fDependsWait = false;
        BOOST_FOREACH(const CTxIn &txin, cert.GetVin()) {
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
            assert(it3->second.ptx == &cert);
            assert(it3->second.n == i);
            i++;
        }

        checkTotal += it->second.GetCertificateSize();
        innerUsage += it->second.DynamicMemoryUsage();
        CValidationState state;

        assert(::ContextualCheckInputs(cert, state, mempoolDuplicate, false, chainActive, 0, false, Params().GetConsensus(), NULL));

        if (fDependsWait)
        {
            waitingOnDependantsCert.push_back(&it->second);
        }
        else {
            // updating coins with cert outputs because the cache is checked below for
            // any tx inputs and maybe some tx has a cert out as its input.
            UpdateCoins(cert, state, mempoolDuplicate, 1000000);
        }
    }

    unsigned int stepsSinceLastRemove = 0;
    while (!waitingOnDependants.empty()) {
        const CMemPoolEntry* entryBase = waitingOnDependants.front();
        waitingOnDependants.pop_front();
        CValidationState state;
        const CTxMemPoolEntry* entry = (const CTxMemPoolEntry*)entryBase;
        if (!mempoolDuplicate.HaveInputs(entry->GetTx())) {
            waitingOnDependants.push_back(entry);
            stepsSinceLastRemove++;
            assert(stepsSinceLastRemove < waitingOnDependants.size());
        } else {
            assert(::ContextualCheckInputs(entry->GetTx(), state, mempoolDuplicate, false, chainActive, 0, false, Params().GetConsensus(), NULL));
            UpdateCoins(entry->GetTx(), state, mempoolDuplicate, 1000000);
            stepsSinceLastRemove = 0;
        }
    }
    unsigned int stepsSinceLastRemoveCert = 0;
    while (!waitingOnDependantsCert.empty()) {
        const CMemPoolEntry* entryBase = waitingOnDependantsCert.front();
        waitingOnDependantsCert.pop_front();
        CValidationState state;
        const CCertificateMemPoolEntry* entry = (const CCertificateMemPoolEntry*)entryBase;
        if (!mempoolDuplicate.HaveInputs(entry->GetCertificate())) {
            waitingOnDependantsCert.push_back(entry);
            stepsSinceLastRemoveCert++;
            assert(stepsSinceLastRemoveCert < waitingOnDependantsCert.size());
        } else {
            assert(::ContextualCheckInputs(entry->GetCertificate(), state, mempoolDuplicate, false, chainActive, 0, false, Params().GetConsensus(), NULL));
            UpdateCoins(entry->GetCertificate(), state, mempoolDuplicate, 1000000);
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
        LogPrint("cert", "%s():%d - making coins for tx [%s]\n", __func__, __LINE__, txid.ToString() );
        coins = CCoins(tx, MEMPOOL_HEIGHT);
        return true;
    }

    CScCertificate cert;
    if (mempool.lookup(txid, cert)) {
        LogPrint("cert", "%s():%d - making coins for cert [%s]\n", __func__, __LINE__, txid.ToString() );
        coins = CCoins(cert, MEMPOOL_HEIGHT);
        return true;
    }
    return (base->GetCoins(txid, coins) && !coins.IsPruned());
}

bool CCoinsViewMemPool::HaveCoins(const uint256 &txid) const {
    return mempool.exists(txid) || base->HaveCoins(txid);
}

bool CCoinsViewMemPool::GetSidechain(const uint256& scId, CSidechain& info) const {
    if (mempool.hasSidechainCreationTx(scId)) {
        //build sidechain from txs in mempool
        const uint256& scCreationHash = mempool.mapSidechains.at(scId).scCreationTxHash;
        const CTransaction & scCreationTx = mempool.mapTx.at(scCreationHash).GetTx();
        for (const auto& scCreation : scCreationTx.GetVscCcOut()) {
            if (scId == scCreation.scId) {
                //info.creationBlockHash doesn't exist here!
                info.creationBlockHeight = -1; //default null value for creationBlockHeight
                info.creationTxHash = scCreationHash;
                info.creationData.withdrawalEpochLength = scCreation.withdrawalEpochLength;
            }
            break;
        }

    } else if (!base->GetSidechain(scId, info))
        return false;

    //decorate sidechain with fwds and bwt in mempool
    if (mempool.mapSidechains.count(scId)) {
        for (const auto& fwdHash: mempool.mapSidechains.at(scId).fwdTransfersSet) {
            const CTransaction & fwdTx = mempool.mapTx.at(fwdHash).GetTx();
            for (const auto& fwdAmount : fwdTx.GetVftCcOut())
                if (scId == fwdAmount.scId)
                    info.mImmatureAmounts[-1] += fwdAmount.nValue;
        }

        if (!mempool.mapSidechains.at(scId).backwardCertificate.IsNull()) {
            const uint256& certHash = mempool.mapSidechains.at(scId).backwardCertificate;
            const CScCertificate & cert = mempool.mapCertificate.at(certHash).GetCertificate();
            info.balance -= cert.GetValueOfBackwardTransfers();
        }
    }

    return true;
}

bool CCoinsViewMemPool::HaveSidechain(const uint256& scId) const {
    return mempool.hasSidechainCreationTx(scId) || base->HaveSidechain(scId);
}

bool CCoinsViewMemPool::HaveCertForEpoch(const uint256& scId, int epochNumber) const {
    if ((mempool.mapSidechains.count(scId) != 0) && (!mempool.mapSidechains.at(scId).backwardCertificate.IsNull()))
        return true;

    return base->HaveCertForEpoch(scId, epochNumber);
}

size_t CTxMemPool::DynamicMemoryUsage() const {
    LOCK(cs);
    return
        ( memusage::DynamicUsage(mapTx) +
          memusage::DynamicUsage(mapNextTx) +
          memusage::DynamicUsage(mapDeltas) +
          memusage::DynamicUsage(mapCertificate) +
          cachedInnerUsage);
}

