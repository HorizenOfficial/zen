// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXMEMPOOL_H
#define BITCOIN_TXMEMPOOL_H

#include <list>

#include "amount.h"
#include "coins.h"
#include "primitives/transaction.h"
#include "primitives/certificate.h"
#include "sync.h"

class CAutoFile;

inline double AllowFreeThreshold()
{
    return COIN * 144 / 250;
}

inline bool AllowFree(double dPriority)
{
    // Large (in bytes) low-priority (new, small-coin) transactions
    // need a fee.
    return dPriority > AllowFreeThreshold();
}

/** Fake height value used in CCoins to signify they are only in the memory pool (since 0.8) */
static const unsigned int MEMPOOL_HEIGHT = 0x7FFFFFFF;

class CTxMemPool;

class CMemPoolEntry
{
protected:
    CAmount nFee; //! Cached to avoid expensive parent-transaction lookups
    size_t nModSize; //! ... and modified size for priority
    size_t nUsageSize; //! ... and total memory usage
    int64_t nTime; //! Local time when entering the mempool
    double dPriority; //! Priority when entering the mempool
    unsigned int nHeight; //! Chain height when entering the mempool
public:
    CMemPoolEntry();
    CMemPoolEntry(const CAmount& _nFee, int64_t _nTime, double _dPriority, unsigned int _nHeight);
    virtual double GetPriority(unsigned int currentHeight) const = 0;
    CAmount GetFee() const { return nFee; }
    int64_t GetTime() const { return nTime; }
    unsigned int GetHeight() const { return nHeight; }
    size_t DynamicMemoryUsage() const { return nUsageSize; }
};

/**
 * CTxMemPool stores these:
 */
class CTxMemPoolEntry : public CMemPoolEntry
{
private:
    CTransaction tx;
    size_t nTxSize; //! ... and avoid recomputing tx size
    bool hadNoDependencies; //! Not dependent on any other txs when it entered the mempool

public:
    CTxMemPoolEntry(const CTransaction& _tx, const CAmount& _nFee, int64_t _nTime, double _dPriority, unsigned int _nHeight, bool poolHasNoInputsOf = false);
    CTxMemPoolEntry();
    CTxMemPoolEntry(const CTxMemPoolEntry& other);

    const CTransaction& GetTx() const { return this->tx; }
    double GetPriority(unsigned int currentHeight) const;
    size_t GetTxSize() const { return nTxSize; }
    bool WasClearAtEntry() const { return hadNoDependencies; }
};

class CCertificateMemPoolEntry : public CMemPoolEntry
{
private:
    CScCertificate cert;
    size_t nCertificateSize; //! ... and avoid recomputing tx size

public:
    CCertificateMemPoolEntry(
        const CScCertificate& _cert, const CAmount& _nFee, int64_t _nTime, double _dPriority, unsigned int _nHeight);
    CCertificateMemPoolEntry();
    CCertificateMemPoolEntry(const CCertificateMemPoolEntry& other);

    const CScCertificate& GetCertificate() const { return this->cert; }
    double GetPriority(unsigned int currentHeight) const;
    size_t GetCertificateSize() const { return nCertificateSize; }
};

class CBlockPolicyEstimator;

/** An inpoint - a combination of a transaction and an index n into its vin */
class CInPoint
{
public:
    const CTransaction* ptx;
    uint32_t n;

    CInPoint() { SetNull(); }
    CInPoint(const CTransaction* ptxIn, uint32_t nIn) { ptx = ptxIn; n = nIn; }
    void SetNull() { ptx = NULL; n = (uint32_t) -1; }
    bool IsNull() const { return (ptx == NULL && n == (uint32_t) -1); }
    size_t DynamicMemoryUsage() const { return 0; }
};

struct CSidechainMemPoolEntry
{
    uint256 scCreationTxHash;
    std::set<uint256> CcTransfersSet;
};

/**
 * CTxMemPool stores valid-according-to-the-current-best-chain
 * transactions that may be included in the next block.
 *
 * Transactions are added when they are seen on the network
 * (or created by the local node), but not all transactions seen
 * are added to the pool: if a new transaction double-spends
 * an input of a transaction in the pool, it is dropped,
 * as are non-standard transactions.
 */
class CTxMemPool
{
private:
    bool fSanityCheck; //! Normally false, true if -checkmempool or -regtest
    unsigned int nTransactionsUpdated;
    unsigned int nCertificatesUpdated;
    CBlockPolicyEstimator* minerPolicyEstimator;

    uint64_t totalTxSize = 0; //! sum of all mempool tx' byte sizes
    uint64_t totalCertificateSize = 0; //! sum of all mempool tx' byte sizes
    uint64_t cachedInnerUsage; //! sum of dynamic memory usage of all the map elements (NOT the maps themselves)

#if 1
    void removeInternal(std::deque<uint256>& objToRemove, std::list<std::shared_ptr<CTransactionBase>>& removed, bool fRecursive, bool removeDependantFwds = true);
#endif

public:
    mutable CCriticalSection cs;
    std::map<uint256, CTxMemPoolEntry> mapTx;
    std::map<uint256, CCertificateMemPoolEntry> mapCertificate;
    std::map<COutPoint, CInPoint> mapNextTx;
    std::map<uint256, CSidechainMemPoolEntry> mapSidechains;
    std::map<uint256, const CTransaction*> mapNullifiers;
    std::map<uint256, std::pair<double, CAmount> > mapDeltas;

    CTxMemPool(const CFeeRate& _minRelayFee);
    ~CTxMemPool();

    /**
     * If sanity-checking is turned on, check makes sure the pool is
     * consistent (does not contain two transactions that spend the same inputs,
     * all inputs are in the mapNextTx array). If sanity-checking is turned off,
     * check does nothing.
     */
    void check(const CCoinsViewCache *pcoins) const;
    void setSanityCheck(bool _fSanityCheck) { fSanityCheck = _fSanityCheck; }

    bool addUnchecked(
        const CTransactionBase& tb, const CAmount& nFee, int64_t nTime, double dPriority, int nHeight,
        bool poolHasNoInputsOf, bool fCurrentEstimate);

    bool addUnchecked(const uint256& hash, const CTxMemPoolEntry &entry, bool fCurrentEstimate = true);
    bool addUnchecked(const uint256& hash, const CCertificateMemPoolEntry &entry, bool fCurrentEstimate = true);

#if 0
    void remove(const CTransaction &tx, std::list<CTransaction>& removed, bool fRecursive = false);
#else
    void remove(const CTransaction &tx, std::list<std::shared_ptr<CTransactionBase>>& removed, bool fRecursive = false, bool removeDependantFwds = true);
    void remove(const CScCertificate &origCert, std::list<std::shared_ptr<CTransactionBase>>& removed, bool fRecursive = false, bool removeDependantFwds = true);
#endif
    void removeWithAnchor(const uint256 &invalidRoot);
    void removeCoinbaseSpends(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight);
#if 1
    void removeOutOfEpochCertificates(const CBlockIndex* pindexDelete);
    void removeConflicts(const CTransaction &tx, std::list<std::shared_ptr<CTransactionBase>>& removed);
    void removeForBlock(const std::vector<CTransaction>& vtx, unsigned int nBlockHeight,
                        std::list<std::shared_ptr<CTransactionBase>>& conflicts, bool fCurrentEstimate = true);
#else
    void removeConflicts(const CTransaction &tx, std::list<CTransaction>& removed);
    void removeForBlock(const std::vector<CTransaction>& vtx, unsigned int nBlockHeight,
                        std::list<CTransaction>& conflicts, bool fCurrentEstimate = true);
#endif
    // no conflicts for certs
    void removeForBlock(const std::vector<CScCertificate>& vcert, unsigned int nBlockHeight, bool fCurrentEstimate = true);

    void clear();
    void queryHashes(std::vector<uint256>& vtxid);
    void pruneSpent(const uint256& hash, CCoins &coins);
    unsigned int GetTransactionsUpdated() const;
    void AddTransactionsUpdated(unsigned int n);
    /**
     * Check that none of this transactions inputs are in the mempool, and thus
     * the tx is not dependent on other mempool transactions to be included in a block.
     */
    bool HasNoInputsOf(const CTransactionBase& tx) const;

    /** Affect CreateNewBlock prioritisation of transactions */
    void PrioritiseTransaction(const uint256& hash, const std::string& strHash, double dPriorityDelta, const CAmount& nFeeDelta);
    void ApplyDeltas(const uint256 hash, double &dPriorityDelta, CAmount &nFeeDelta);
    void ClearPrioritisation(const uint256 hash);

    unsigned long sizeTx()
    {
        LOCK(cs);
        return mapTx.size();
    }

    unsigned long sizeCert()
    {
        LOCK(cs);
        return mapCertificate.size();
    }

    unsigned long size()
    {
        LOCK(cs);
        return sizeTx() + sizeCert();
    }

    uint64_t GetTotalCertificateSize()
    {
        LOCK(cs);
        return totalCertificateSize;
    }

    uint64_t GetTotalTxSize()
    {
        LOCK(cs);
        return totalTxSize;
    }

    uint64_t GetTotalSize()
    {
        LOCK(cs);
        return (totalTxSize + totalCertificateSize);
    }

    bool existsTx(uint256 hash) const
    {
        LOCK(cs);
        return (mapTx.count(hash) != 0);
    }

    bool existsCert(uint256 hash) const
    {
        LOCK(cs);
        return (mapCertificate.count(hash) != 0);
    }

    bool exists(uint256 hash) const
    {
        LOCK(cs);
        return (mapCertificate.count(hash) != 0 || mapTx.count(hash) != 0);
    }

    bool sidechainExists(uint256 scId) const
    {
        LOCK(cs);
        return (mapSidechains.count(scId) != 0) && (!mapSidechains.at(scId).scCreationTxHash.IsNull());
    }

    bool lookup(uint256 hash, CTransaction& result) const;
    bool lookup(uint256 hash, CScCertificate& result) const;

    /** Estimate fee rate needed to get into the next nBlocks */
    CFeeRate estimateFee(int nBlocks) const;

    /** Estimate priority needed to get into the next nBlocks */
    double estimatePriority(int nBlocks) const;
    
    /** Write/Read estimates to disk */
    bool WriteFeeEstimates(CAutoFile& fileout) const;
    bool ReadFeeEstimates(CAutoFile& filein);

    size_t DynamicMemoryUsage() const;
};

namespace Sidechain { class ScInfo; }

/** 
 * CCoinsView that brings transactions from a memorypool into view.
 * It does not check for spendings by memory pool transactions.
 */
class CCoinsViewMemPool : public CCoinsViewBacked
{
protected:
    CTxMemPool &mempool;

public:
    CCoinsViewMemPool(CCoinsView *baseIn, CTxMemPool &mempoolIn);
    bool GetNullifier(const uint256 &txid) const;
    bool GetCoins(const uint256 &txid, CCoins &coins) const;
    bool HaveCoins(const uint256 &txid) const;

#if 0
    bool GetScInfo(const uint256& scId, Sidechain::ScInfo& info) const;
    bool HaveScInfo(const uint256& scId) const;
#endif
};

#endif // BITCOIN_TXMEMPOOL_H
