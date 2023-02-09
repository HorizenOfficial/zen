// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXMEMPOOL_H
#define BITCOIN_TXMEMPOOL_H

#include <list>

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#ifdef ENABLE_ADDRESS_INDEXING
#include "addressindex.h"
#include "spentindex.h"
#endif // ENABLE_ADDRESS_INDEXING

#include "amount.h"
#include "coins.h"
#include "primitives/transaction.h"
#include "primitives/certificate.h"
#include "sync.h"

class CAutoFile;

inline double AllowFreeThreshold()
{
    // the threshold represents a one day old, 1 ZEN coin (144*4 is the expected number of blocks per day) and a transaction size of 250 bytes.
    // TODO, check this: should be:
    // return COIN * 144 * 4 / 250;
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
    virtual ~CMemPoolEntry() = default;
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

    const CTransaction& GetTx() const { return this->tx; }
    double GetPriority(unsigned int currentHeight) const override;
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

    const CScCertificate& GetCertificate() const { return this->cert; }
    double GetPriority(unsigned int currentHeight) const override;
    size_t GetCertificateSize() const { return nCertificateSize; }
};

class CBlockPolicyEstimator;

/** An inpoint - a combination of a transaction and an index n into its vin */
class CInPoint
{
public:
    const CTransactionBase* ptx;
    uint32_t n;

    CInPoint() { SetNull(); }
    CInPoint(const CTransactionBase* ptxIn, uint32_t nIn): ptx(ptxIn), n(nIn) { }
    void SetNull() { ptx = nullptr; n = (uint32_t) -1; }
    bool IsNull() const { return (ptx == nullptr && n == (uint32_t) -1); }
    size_t DynamicMemoryUsage() const { return 0; }
};

struct CSidechainMemPoolEntry
{
    uint256 scCreationTxHash;
    std::set<uint256> fwdTxHashes; 
    std::map<int64_t, uint256> mBackwardCertificates; // quality -> certHash
    std::set<uint256> mcBtrsTxHashes;
    std::map<CFieldElement, uint256> cswNullifiers; // csw nullifier -> containing Tx hash
    CAmount cswTotalAmount;

    // Note: in fwdTxHashes and mcBtrsTxHashes, a tx is registered only once,
    // even if sends multiple fwts/btrs founds to a sidechain.
    // Upon removal we will need to guard against potential double deletes.
    bool IsNull() const {
        return  scCreationTxHash.IsNull()     &&
                fwdTxHashes.empty()           &&
                mBackwardCertificates.empty() &&
                mcBtrsTxHashes.empty()        &&
                cswNullifiers.empty()         &&
                cswTotalAmount == 0;
    }

    const std::map<int64_t, uint256>::const_reverse_iterator GetTopQualityCert() const;
    const std::map<int64_t, uint256>::const_iterator GetCert(const uint256& hash) const;

    void EraseCert(const uint256& hash);
    bool HasCert(const uint256& hash) const;
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

    bool checkTxImmatureExpenditures(const CTransaction& tx, const CCoinsViewCache * const pcoins);
    bool checkCertImmatureExpenditures(const CScCertificate& cert, const CCoinsViewCache * const pcoins);

    std::map<uint256, std::shared_ptr<CTransactionBase> > mapRecentlyAddedTxBase;
    uint64_t nRecentlyAddedSequence = 0;
    uint64_t nNotifiedSequence = 0;

#ifdef ENABLE_ADDRESS_INDEXING
    typedef std::map<CMempoolAddressDeltaKey, CMempoolAddressDelta, CMempoolAddressDeltaKeyCompare> addressDeltaMap;
    addressDeltaMap mapAddress;

    typedef std::map<uint256, std::vector<CMempoolAddressDeltaKey> > addressDeltaMapInserted;
    addressDeltaMapInserted mapAddressInserted;

    typedef std::map<CSpentIndexKey, CSpentIndexValue, CSpentIndexKeyCompare> mapSpentIndex;
    mapSpentIndex mapSpent;

    typedef std::map<uint256, std::vector<CSpentIndexKey> > mapSpentIndexInserted;
    mapSpentIndexInserted mapSpentInserted;
#endif // ENABLE_ADDRESS_INDEXING

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

    CTxMemPool(const CTxMemPool& ) = delete;
    CTxMemPool(CTxMemPool& ) = delete;
    CTxMemPool& operator=(const CTxMemPool& ) = delete;

    /**
     * If sanity-checking is turned on, check makes sure the pool is
     * consistent (does not contain two transactions that spend the same inputs,
     * all inputs are in the mapNextTx array). If sanity-checking is turned off,
     * check does nothing.
     */
    void check(const CCoinsViewCache *pcoins) const;

    bool checkCswInputsPerScLimit(const CTransaction& incomingTx) const;
    bool checkIncomingTxConflicts(const CTransaction& incomingTx) const;
    bool certificateExists(const uint256& scId) const;
    bool checkIncomingCertConflicts(const CScCertificate& incomingCert) const;

    void setSanityCheck(bool _fSanityCheck) { fSanityCheck = _fSanityCheck; }

    std::pair<uint256, CAmount> FindCertWithQuality(const uint256& scId, int64_t certQuality) const;
    bool RemoveCertAndSync(const uint256& certToRmHash);

    bool addUnchecked(const uint256& hash, const CTxMemPoolEntry &entry, bool fCurrentEstimate = true);
    bool addUnchecked(const uint256& hash, const CCertificateMemPoolEntry &entry, bool fCurrentEstimate = true);

#ifdef ENABLE_ADDRESS_INDEXING
    void addAddressIndex(const CTransactionBase &txBase, int64_t nTime, const CCoinsViewCache &view);
    bool getAddressIndex(std::vector<std::pair<uint160, int> > &addresses,
                         std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta> > &results);
    bool removeAddressIndex(const uint256& txBaseHash);
    void updateTopQualCertAddressIndex(const uint256& scid);

    void addSpentIndex(const CTransactionBase& txBase, const CCoinsViewCache &view);
    bool getSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value);
    bool removeSpentIndex(const uint256& txBaseHash);
#endif // ENABLE_ADDRESS_INDEXING

    std::vector<uint256> mempoolDirectDependenciesFrom(const CTransactionBase& root) const;
    std::vector<uint256> mempoolDirectDependenciesOf(const CTransactionBase& root) const;

    std::vector<uint256> mempoolDependenciesFrom(const CTransactionBase& origTx) const;
    std::vector<uint256> mempoolDependenciesOf(const CTransactionBase& origTx) const;

    void remove(const CTransactionBase& origTx, std::list<CTransaction>& removedTxs, std::list<CScCertificate>& removedCerts, bool fRecursive = false);

    void removeWithAnchor(const uint256 &invalidRoot);

    // UNCONFIRMED TRANSACTIONS CLEANUP METHODS
    void removeForBlock(const std::vector<CTransaction>& vtx, unsigned int nBlockHeight,
                        std::list<CTransaction>& conflictingTxs, std::list<CScCertificate>& removedCerts, bool fCurrentEstimate = true);
    void removeConflicts(const CTransaction &tx,
                         std::list<CTransaction>& removedTxs, std::list<CScCertificate>& removedCerts);
    void removeOutOfScBalanceCsw(const CCoinsViewCache * const pCoinsView,
                                 std::list<CTransaction> &removedTxs, std::list<CScCertificate> &removedCerts);
    void removeStaleTransactions(const CCoinsViewCache * const pCoinsView,
                                 std::list<CTransaction>& outdatedTxs, std::list<CScCertificate>& outdatedCerts);
    // END OF UNCONFIRMED TRANSACTIONS CLEANUP METHODS

    // UNCONFIRMED CERTIFICATES CLEANUP METHODS
    void removeForBlock(const std::vector<CScCertificate>& vcert, unsigned int nBlockHeight,
                        std::list<CTransaction>& removedTxs, std::list<CScCertificate>& removedCerts);
    void removeConflicts(const CScCertificate &cert,
                         std::list<CTransaction>& removedTxs, std::list<CScCertificate>& removedCerts);
    void removeStaleCertificates(const CCoinsViewCache * const pCoinsView,
                                 std::list<CScCertificate>& outdatedCerts);
    void removeCertificatesWithoutRef(const CCoinsViewCache * const pCoinsView,
                                 std::list<CScCertificate>& outdatedCerts);
    // END OF UNCONFIRMED CERTIFICATES CLEANUP METHODS

    void clear();
    void queryHashes(std::vector<uint256>& vtxid) const;
    void pruneSpent(const uint256& hash, CCoins &coins);
    unsigned int GetTransactionsUpdated() const;
    void AddTransactionsUpdated(unsigned int n);
    /**
     * Check that none of this transactions inputs are in the mempool, and thus
     * the tx is not dependent on other mempool transactions to be included in a block.
     */
    bool HasNoInputsOf(const CTransaction& tx) const;

    /** Affect CreateNewBlock prioritisation of transactions */
    void PrioritiseTransaction(const uint256& hash, const std::string& strHash, double dPriorityDelta, const CAmount& nFeeDelta);
    void ApplyDeltas(const uint256& hash, double &dPriorityDelta, CAmount &nFeeDelta);
    void ClearPrioritisation(const uint256& hash);

    void NotifyRecentlyAdded();
    bool IsFullyNotified();

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

    bool existsTx(const uint256& hash) const
    {
        LOCK(cs);
        return (mapTx.count(hash) != 0);
    }

    bool existsCert(const uint256& hash) const
    {
        LOCK(cs);
        return (mapCertificate.count(hash) != 0);
    }
    bool exists(const uint256& hash) const
    {
        LOCK(cs);
        return (mapCertificate.count(hash) != 0 || mapTx.count(hash) != 0);
    }

    bool hasSidechainCertificate(const uint256& scId) const
    {
        LOCK(cs);
        return (mapSidechains.count(scId) != 0) && (!mapSidechains.at(scId).mBackwardCertificates.empty());
    }

    bool hasSidechainCreationTx(const uint256& scId) const
    {
        LOCK(cs);
        return (mapSidechains.count(scId) != 0) && (!mapSidechains.at(scId).scCreationTxHash.IsNull());
    }

    bool HaveCswNullifier(const uint256& scId, const CFieldElement &nullifier) const
    {
        LOCK(cs);
        return mapSidechains.count(scId) != 0 && mapSidechains.at(scId).cswNullifiers.count(nullifier) != 0;
    }

    bool hasSidechainBwtRequest(const uint256& scId) const
    {
        LOCK(cs);
        return (mapSidechains.count(scId) != 0) && (!mapSidechains.at(scId).mcBtrsTxHashes.empty());
    }

    bool hasSidechainFwt(const uint256& scId) const
    {
        LOCK(cs);
        return (mapSidechains.count(scId) != 0) && (!mapSidechains.at(scId).fwdTxHashes.empty());
    }

    int getNumOfCswInputs(const uint256& scId) const;

    bool lookup(const uint256& hash, CTransaction& result) const;
    bool lookup(const uint256& hash, CScCertificate& result) const;

    void CertQualityStatusString(const CScCertificate& cert, std::string& statusString) const;

    /** Estimate fee rate needed to get into the next nBlocks */
    CFeeRate estimateFee(int nBlocks) const;

    /** Estimate priority needed to get into the next nBlocks */
    double estimatePriority(int nBlocks) const;
    
    /** Write/Read estimates to disk */
    bool WriteFeeEstimates(CAutoFile& fileout) const;
    bool ReadFeeEstimates(CAutoFile& filein);

    size_t DynamicMemoryUsage() const;
};

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

    bool GetNullifier(const uint256 &txid)                              const override;
    bool GetCoins(const uint256 &txid, CCoins &coins)                   const override;
    bool HaveCoins(const uint256 &txid)                                 const override;
    bool GetSidechain(const uint256& scId, CSidechain& info)            const override;
    bool HaveSidechain(const uint256& scId)                             const override;
    void GetScIds(std::set<uint256>& scIdsList)                         const override;
    bool HaveCswNullifier(const uint256& scId,
                          const CFieldElement &nullifier) const override;
};

#endif // BITCOIN_TXMEMPOOL_H
