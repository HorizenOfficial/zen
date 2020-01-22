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

using namespace std;

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

double
CTxMemPoolEntry::GetPriority(unsigned int currentHeight) const
{
    CAmount nValueIn = tx.GetValueOut()+nFee;
    double deltaPriority = ((double)(currentHeight-nHeight)*nValueIn)/nModSize;
    double dResult = dPriority + deltaPriority;
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
    CAmount nValueIn = cert.GetValueOut()+nFee;
    double deltaPriority = ((double)(currentHeight-nHeight)*nValueIn)/nModSize;
    double dResult = dPriority + deltaPriority;
    return dResult;
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
        for (unsigned int i = 0; i < tx.vin.size(); i++)
            mapNextTx[tx.vin[i].prevout] = CInPoint(&tx, i);
    BOOST_FOREACH(const JSDescription &joinsplit, tx.vjoinsplit) {
        BOOST_FOREACH(const uint256 &nf, joinsplit.nullifiers) {
            mapNullifiers[nf] = &tx;
        }
    }
    nTransactionsUpdated++;
    totalTxSize += entry.GetTxSize();
    cachedInnerUsage += entry.DynamicMemoryUsage();
    minerPolicyEstimator->processTransaction(entry, fCurrentEstimate);
    LogPrint("cert", "%s():%d - tx [%s] added in mempool\n", __func__, __LINE__, hash.ToString() );

    return true;
}

bool CTxMemPool::addUnchecked(const uint256& hash, const CCertificateMemPoolEntry &entry, bool fCurrentEstimate)
{
    LOCK(cs);
    mapCertificate[hash] = entry;
    // no mapNextTx or mapNullifiers handling is necessary for certs since they have no inputs or joinsplits 
    nCertificatesUpdated++;
    totalCertificateSize += entry.GetCertificateSize();
    cachedInnerUsage += entry.DynamicMemoryUsage();
// TODO cert: for the time being skip the part on policy estimator, certificates currently have maximum priority
// minerPolicyEstimator->processTransaction(entry, fCurrentEstimate);
    LogPrint("cert", "%s():%d - cert [%s] added in mempool\n", __func__, __LINE__, hash.ToString() );
    return true;
}

bool CTxMemPool::addUnchecked(
    const CTransactionBase& tx, const CAmount& nFee, int64_t nTime, double dPriority, int nHeight,
    bool poolHasNoInputsOf, bool fCurrentEstimate)
{
    return tx.AddUncheckedToMemPool(this, nFee, nTime, dPriority, nHeight, poolHasNoInputsOf, fCurrentEstimate);
}

void CTxMemPool::remove(const CTransaction &origTx, std::list<CTransaction>& removed, bool fRecursive)
{
    // Remove transaction from memory pool
    {
        LOCK(cs);
        std::deque<uint256> txToRemove;
        txToRemove.push_back(origTx.GetHash());
        if (fRecursive && !mapTx.count(origTx.GetHash())) {
            // If recursively removing but origTx isn't in the mempool
            // be sure to remove any children that are in the pool. This can
            // happen during chain re-orgs if origTx isn't re-accepted into
            // the mempool for any reason.
            for (unsigned int i = 0; i < origTx.vout.size(); i++) {
                std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(origTx.GetHash(), i));
                if (it == mapNextTx.end())
                    continue;
                txToRemove.push_back(it->second.ptx->GetHash());
            }
        }
        while (!txToRemove.empty())
        {
            uint256 hash = txToRemove.front();
            txToRemove.pop_front();
            if (!mapTx.count(hash))
                continue;
            const CTransaction& tx = mapTx[hash].GetTx();
            if (fRecursive) {
                for (unsigned int i = 0; i < tx.vout.size(); i++) {
                    std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(hash, i));
                    if (it == mapNextTx.end())
                        continue;
                    txToRemove.push_back(it->second.ptx->GetHash());
                }
            }
            BOOST_FOREACH(const CTxIn& txin, tx.vin)
                mapNextTx.erase(txin.prevout);
            BOOST_FOREACH(const JSDescription& joinsplit, tx.vjoinsplit) {
                BOOST_FOREACH(const uint256& nf, joinsplit.nullifiers) {
                    mapNullifiers.erase(nf);
                }
            }

            removed.push_back(tx);
            totalTxSize -= mapTx[hash].GetTxSize();
            cachedInnerUsage -= mapTx[hash].DynamicMemoryUsage();
            LogPrint("cert", "%s():%d - removing tx [%s] from mempool\n", __func__, __LINE__, hash.ToString() );
            mapTx.erase(hash);
            nTransactionsUpdated++;
            minerPolicyEstimator->removeTx(hash);
        }
    }
}

void CTxMemPool::remove(const CScCertificate &origCert, bool fRecursive)
{
    // Remove certificate from memory pool
    {
        LOCK(cs);
        std::deque<uint256> objToRemove;
        objToRemove.push_back(origCert.GetHash());
        if (fRecursive && !mapCertificate.count(origCert.GetHash())) {
            // If recursively removing but origCert isn't in the mempool
            // be sure to remove any children that are in the pool. This can
            // happen during chain re-orgs if origCert isn't re-accepted into
            // the mempool for any reason.
            for (unsigned int i = 0; i < origCert.vout.size(); i++) {
                std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(origCert.GetHash(), i));
                if (it == mapNextTx.end())
                    continue;
                objToRemove.push_back(it->second.ptx->GetHash());
            }
        }
        while (!objToRemove.empty())
        {
            uint256 hash = objToRemove.front();
            objToRemove.pop_front();
            if (mapTx.count(hash))
            {
                const CTransaction& tx = mapTx[hash].GetTx();
                if (fRecursive) {
                    for (unsigned int i = 0; i < tx.vout.size(); i++) {
                        std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(hash, i));
                        if (it == mapNextTx.end())
                            continue;
                        objToRemove.push_back(it->second.ptx->GetHash());
                    }
                }
                BOOST_FOREACH(const CTxIn& txin, tx.vin)
                    mapNextTx.erase(txin.prevout);
                BOOST_FOREACH(const JSDescription& joinsplit, tx.vjoinsplit) {
                    BOOST_FOREACH(const uint256& nf, joinsplit.nullifiers) {
                        mapNullifiers.erase(nf);
                    }
                }
 
                totalTxSize -= mapTx[hash].GetTxSize();
                cachedInnerUsage -= mapTx[hash].DynamicMemoryUsage();
                mapTx.erase(hash);
                LogPrint("cert", "%s():%d - removing tx [%s] from mempool\n", __func__, __LINE__, hash.ToString() );
                nTransactionsUpdated++;
                minerPolicyEstimator->removeTx(hash);
            }
            else
            if (mapCertificate.count(hash))
            {
                const CScCertificate& cert = mapCertificate[hash].GetCertificate();
                if (fRecursive)
                {
                    for (unsigned int i = 0; i < cert.vout.size(); i++) {
                        std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(hash, i));
                        if (it == mapNextTx.end())
                            continue;
                        objToRemove.push_back(it->second.ptx->GetHash());
                    }
                }
                totalTxSize -= mapCertificate[hash].GetCertificateSize();
                cachedInnerUsage -= mapCertificate[hash].DynamicMemoryUsage();
                LogPrint("cert", "%s():%d - removing cert [%s] from mempool\n", __func__, __LINE__, hash.ToString() );
                mapCertificate.erase(hash);
                nCertificatesUpdated++;
            }
        }
// TODO cert: miner policy not handled for certificates
//        minerPolicyEstimator->removeTx(hash);
    }
}

void CTxMemPool::removeCoinbaseSpends(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight)
{
    // Remove transactions spending a coinbase which are now immature
    LOCK(cs);
    list<CTransaction> transactionsToRemove;
    for (std::map<uint256, CTxMemPoolEntry>::const_iterator it = mapTx.begin(); it != mapTx.end(); it++) {
        const CTransaction& tx = it->second.GetTx();
        BOOST_FOREACH(const CTxIn& txin, tx.vin) {
            std::map<uint256, CTxMemPoolEntry>::const_iterator it2 = mapTx.find(txin.prevout.hash);
            if (it2 != mapTx.end())
                continue;
#if 1
            // if input is a certificate skip as well, even if it can not be coinbase anyway
            std::map<uint256, CCertificateMemPoolEntry>::const_iterator it3 = mapCertificate.find(txin.prevout.hash);
            if (it3 != mapCertificate.end())
            {
                LogPrint("cert", "%s():%d - tx input is cert [%s] which is still in mempool\n", __func__, __LINE__, it3->first.ToString() );
                continue;
            }
#endif
            const CCoins *coins = pcoins->AccessCoins(txin.prevout.hash);
            if (fSanityCheck) assert(coins);
            if (!coins || (coins->IsCoinBase() && ((signed long)nMemPoolHeight) - coins->nHeight < COINBASE_MATURITY)) {
                transactionsToRemove.push_back(tx);
                break;
            }
        }
    }
    BOOST_FOREACH(const CTransaction& tx, transactionsToRemove) {
        list<CTransaction> removed;
        remove(tx, removed, true);
    }
}


void CTxMemPool::removeWithAnchor(const uint256 &invalidRoot)
{
    // If a block is disconnected from the tip, and the root changed,
    // we must invalidate transactions from the mempool which spend
    // from that root -- almost as though they were spending coinbases
    // which are no longer valid to spend due to coinbase maturity.
    LOCK(cs);
    list<CTransaction> transactionsToRemove;

    for (std::map<uint256, CTxMemPoolEntry>::const_iterator it = mapTx.begin(); it != mapTx.end(); it++) {
        const CTransaction& tx = it->second.GetTx();
        BOOST_FOREACH(const JSDescription& joinsplit, tx.vjoinsplit) {
            if (joinsplit.anchor == invalidRoot) {
                transactionsToRemove.push_back(tx);
                break;
            }
        }
    }

    BOOST_FOREACH(const CTransaction& tx, transactionsToRemove) {
        list<CTransaction> removed;
        remove(tx, removed, true);
    }
}

void CTxMemPool::removeConflicts(const CTransaction &tx, std::list<CTransaction>& removed)
{
    // Remove transactions which depend on inputs of tx, recursively
    list<CTransaction> result;
    LOCK(cs);
    BOOST_FOREACH(const CTxIn &txin, tx.vin) {
        std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end()) {
            const CTransaction &txConflict = *it->second.ptx;
            if (txConflict != tx)
            {
                remove(txConflict, removed, true);
            }
        }
    }

    BOOST_FOREACH(const JSDescription &joinsplit, tx.vjoinsplit) {
        BOOST_FOREACH(const uint256 &nf, joinsplit.nullifiers) {
            std::map<uint256, const CTransaction*>::iterator it = mapNullifiers.find(nf);
            if (it != mapNullifiers.end()) {
                const CTransaction &txConflict = *it->second;
                if (txConflict != tx)
                {
                    remove(txConflict, removed, true);
                }
            }
        }
    }
}

/**
 * Called when a block is connected. Removes from mempool and updates the miner fee estimator.
 */
void CTxMemPool::removeForBlock(const std::vector<CTransaction>& vtx, unsigned int nBlockHeight,
                                std::list<CTransaction>& conflicts, bool fCurrentEstimate)
{
    LOCK(cs);
    std::vector<CTxMemPoolEntry> entries;
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        uint256 hash = tx.GetHash();
        if (mapTx.count(hash))
            entries.push_back(mapTx[hash]);
    }
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        std::list<CTransaction> dummy;
        remove(tx, dummy, false);
        removeConflicts(tx, conflicts);
        ClearPrioritisation(tx.GetHash());
    }
    // After the txs in the new block have been removed from the mempool, update policy estimates
    minerPolicyEstimator->processBlock(nBlockHeight, entries, fCurrentEstimate);
}

#if 1
// a certificate has no conflicting txes since it has no inputs
void CTxMemPool::removeForBlock(const std::vector<CScCertificate>& vcert, unsigned int nBlockHeight, bool fCurrentEstimate)
{
    LOCK(cs);
    std::vector<CCertificateMemPoolEntry> entries;
    for (const auto& obj : vcert)
    {
        uint256 hash = obj.GetHash();
        if (mapCertificate.count(hash))
            entries.push_back(mapCertificate[hash]);
    }
    for (const auto& obj : vcert)
    {
        remove(obj, false);
        ClearPrioritisation(obj.GetHash());
    }
}
#endif

void CTxMemPool::clear()
{
    LOCK(cs);
    mapTx.clear();
    mapNextTx.clear();
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
    list<const CTxMemPoolEntry*> waitingOnDependants;
    for (std::map<uint256, CTxMemPoolEntry>::const_iterator it = mapTx.begin(); it != mapTx.end(); it++) {
        unsigned int i = 0;
        checkTotal += it->second.GetTxSize();
        innerUsage += it->second.DynamicMemoryUsage();
        const CTransaction& tx = it->second.GetTx();
#if 1
        // TODO cert: TEST TEST
        CValidationState state;
        if (!Sidechain::ScCoinsView::IsTxAllowedInMempool(*this, tx, state))
        {
            LogPrint("sc", "%s():%d - tx [%s] has conflicts in mempool\n", __func__, __LINE__, tx.GetHash().ToString());
            assert(false);
        }
#endif

        bool fDependsWait = false;
        BOOST_FOREACH(const CTxIn &txin, tx.vin) {
            // Check that every mempool transaction's inputs refer to available coins, or other mempool tx's.
            std::map<uint256, CTxMemPoolEntry>::const_iterator it2 = mapTx.find(txin.prevout.hash);
            if (it2 != mapTx.end()) {
                const CTransaction& tx2 = it2->second.GetTx();
                assert(tx2.vout.size() > txin.prevout.n && !tx2.vout[txin.prevout.n].IsNull());
                fDependsWait = true;
            } else {
#if 1
                // maybe our input is a certificate?
                std::map<uint256, CCertificateMemPoolEntry>::const_iterator itCert = mapCertificate.find(txin.prevout.hash);
                if (itCert != mapCertificate.end()) {
                    const CTransactionBase& cert = itCert->second.GetCertificate();
                    assert(cert.vout.size() > txin.prevout.n && !cert.vout[txin.prevout.n].IsNull());
                    fDependsWait = true;
                }
                else
                {
                    const CCoins* coins = pcoins->AccessCoins(txin.prevout.hash);
                    assert(coins && coins->IsAvailable(txin.prevout.n));
                }
#else
                const CCoins* coins = pcoins->AccessCoins(txin.prevout.hash);
                assert(coins && coins->IsAvailable(txin.prevout.n));
#endif
            }
            // Check whether its inputs are marked in mapNextTx.
            std::map<COutPoint, CInPoint>::const_iterator it3 = mapNextTx.find(txin.prevout);
            assert(it3 != mapNextTx.end());
            assert(it3->second.ptx == &tx);
            assert(it3->second.n == i);
            i++;
        }

        boost::unordered_map<uint256, ZCIncrementalMerkleTree, CCoinsKeyHasher> intermediates;

        BOOST_FOREACH(const JSDescription &joinsplit, tx.vjoinsplit) {
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
            waitingOnDependants.push_back(&it->second);
        else {
            CValidationState state;
            assert(ContextualCheckInputs(tx, state, mempoolDuplicate, false, chainActive, 0, false, Params().GetConsensus(), NULL));
            UpdateCoins(tx, state, mempoolDuplicate, 1000000);
        }
    }

#if 1
    for (auto it = mapCertificate.begin(); it != mapCertificate.end(); it++)
    {
        checkTotal += it->second.GetCertificateSize();
        innerUsage += it->second.DynamicMemoryUsage();
        const auto& cert = it->second.GetCertificate();
        CValidationState state;
#if 1
        // TODO cert: TEST TEST
        if (!Sidechain::ScCoinsView::IsCertAllowedInMempool(*this, cert, state))
        {
            LogPrint("sc", "%s():%d - cert [%s] has conflicts in mempool\n", __func__, __LINE__, cert.GetHash().ToString());
            assert(false);
        }
#endif
        assert(cert.ContextualCheckInputs(state, mempoolDuplicate, false, chainActive, 0, false, Params().GetConsensus(), NULL));
        // updating coins with cert outputs because the cache is checked below for
        // any tx inputs and maybe some tx has a cert out as its input.
        cert.UpdateCoins(state, mempoolDuplicate, 1000000);
    }
#endif

    unsigned int stepsSinceLastRemove = 0;
    while (!waitingOnDependants.empty()) {
        const CTxMemPoolEntry* entry = waitingOnDependants.front();
        waitingOnDependants.pop_front();
        CValidationState state;
        if (!mempoolDuplicate.HaveInputs(entry->GetTx())) {
            waitingOnDependants.push_back(entry);
            stepsSinceLastRemove++;
            assert(stepsSinceLastRemove < waitingOnDependants.size());
        } else {
            assert(ContextualCheckInputs(entry->GetTx(), state, mempoolDuplicate, false, chainActive, 0, false, Params().GetConsensus(), NULL));
            UpdateCoins(entry->GetTx(), state, mempoolDuplicate, 1000000);
            stepsSinceLastRemove = 0;
        }
    }
    for (std::map<COutPoint, CInPoint>::const_iterator it = mapNextTx.begin(); it != mapNextTx.end(); it++) {
        uint256 hash = it->second.ptx->GetHash();
        map<uint256, CTxMemPoolEntry>::const_iterator it2 = mapTx.find(hash);
        const CTransaction& tx = it2->second.GetTx();
        assert(it2 != mapTx.end());
        assert(&tx == it->second.ptx);
        assert(tx.vin.size() > it->second.n);
        assert(it->first == it->second.ptx->vin[it->second.n].prevout);
    }

    for (std::map<uint256, const CTransaction*>::const_iterator it = mapNullifiers.begin(); it != mapNullifiers.end(); it++) {
        uint256 hash = it->second->GetHash();
        map<uint256, CTxMemPoolEntry>::const_iterator it2 = mapTx.find(hash);
        const CTransaction& tx = it2->second.GetTx();
        assert(it2 != mapTx.end());
        assert(&tx == it->second);
    }

    assert((totalTxSize+totalCertificateSize) == checkTotal);
    assert(innerUsage == cachedInnerUsage);
}

void CTxMemPool::queryHashes(vector<uint256>& vtxid)
{
    vtxid.clear();

    LOCK(cs);
    vtxid.reserve(mapTx.size() + mapCertificate.size());
    for (map<uint256, CTxMemPoolEntry>::iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi)
        vtxid.push_back((*mi).first);
    for (map<uint256, CCertificateMemPoolEntry>::iterator mi = mapCertificate.begin(); mi != mapCertificate.end(); ++mi)
        vtxid.push_back((*mi).first);
}

bool CTxMemPool::lookup(uint256 hash, CTransaction& result) const
{
    LOCK(cs);
    map<uint256, CTxMemPoolEntry>::const_iterator i = mapTx.find(hash);
    if (i == mapTx.end()) return false;
    result = i->second.GetTx();
    return true;
}

// TODO make it virtual
#if 1
bool CTxMemPool::lookup(uint256 hash, CScCertificate& result) const
{
    LOCK(cs);
    map<uint256, CCertificateMemPoolEntry>::const_iterator i = mapCertificate.find(hash);
    if (i == mapCertificate.end()) return false;
    result = i->second.GetCertificate();
    return true;
}
#endif

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

void CTxMemPool::PrioritiseTransaction(const uint256& hash, const string& strHash, double dPriorityDelta, const CAmount& nFeeDelta)
{
    {
        LOCK(cs);
        std::pair<double, CAmount> &deltas = mapDeltas[hash];
        deltas.first += dPriorityDelta;
        deltas.second += nFeeDelta;
    }
    LogPrintf("PrioritiseTransaction: %s priority += %f, fee += %d\n", strHash, dPriorityDelta, FormatMoney(nFeeDelta));
}

void CTxMemPool::ApplyDeltas(const uint256 hash, double &dPriorityDelta, CAmount &nFeeDelta)
{
    LOCK(cs);
    std::map<uint256, std::pair<double, CAmount> >::iterator pos = mapDeltas.find(hash);
    if (pos == mapDeltas.end())
        return;
    const std::pair<double, CAmount> &deltas = pos->second;
    dPriorityDelta += deltas.first;
    nFeeDelta += deltas.second;
}

void CTxMemPool::ClearPrioritisation(const uint256 hash)
{
    LOCK(cs);
    mapDeltas.erase(hash);
}

bool CTxMemPool::HasNoInputsOf(const CTransactionBase &tx) const
{
#if 0
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        if (exists(tx.vin[i].prevout.hash))
            return false;
    return true;
#else
    return tx.HasNoInputsInMempool(*this);
#endif
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

size_t CTxMemPool::DynamicMemoryUsage() const {
    LOCK(cs);
#if 0
    return memusage::DynamicUsage(mapTx) + memusage::DynamicUsage(mapNextTx) + memusage::DynamicUsage(mapDeltas) + cachedInnerUsage;
#else
    return
        ( memusage::DynamicUsage(mapTx) +
          memusage::DynamicUsage(mapNextTx) +
          memusage::DynamicUsage(mapDeltas) +
          memusage::DynamicUsage(mapCertificate) +
          cachedInnerUsage);
#endif
}

