// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COINS_H
#define BITCOIN_COINS_H

#include "compressor.h"
#include "core_memusage.h"
#include "memusage.h"
#include "serialize.h"
#include "uint256.h"

#include <assert.h>
#include <stdint.h>

#include <boost/foreach.hpp>
#include <boost/unordered_map.hpp>
#include "zcash/IncrementalMerkleTree.hpp"
#include <sc/sidechain.h>

class CBlockUndo;

/**
 * Pruned version of CTransaction: only retains metadata and unspent transaction outputs
 * **/
class CCoins
{
public:
    //! whether transaction is a coinbase
    bool fCoinBase;

    //! unspent transaction outputs; spent outputs are .IsNull(); spent outputs at the end of the array are dropped
    std::vector<CTxOut> vout;

    //! at which height this transaction was included in the active block chain
    int nHeight;

    //! version of the CTransaction; accesses to this value should probably check for nHeight as well,
    //! as new tx version will probably only be introduced at certain heights
    int nVersion;

    //! if coin comes from a bwt, originScId will contain the associated ScId; otherwise it will be null
    //! originScId will be serialized only for coins from bwt, which will be stored in chainstate db under different key
    uint256 originScId;

    //! empty constructor
    CCoins();

    //! construct a CCoins from a CTransaction, at a given height
    CCoins(const CTransactionBase &tx, int nHeightIn);

    void FromTx(const CTransactionBase &tx, int nHeightIn);

    void Clear();

    //!remove spent outputs at the end of vout
    void Cleanup();

    void ClearUnspendable();

    void swap(CCoins &to);

    //! equality test
    friend bool operator==(const CCoins &a, const CCoins &b);
    friend bool operator!=(const CCoins &a, const CCoins &b);

    bool IsCoinBase() const;

    bool IsCoinFromCert() const;

    //! mark a vout spent
    bool Spend(uint32_t nPos);

    //! check whether a particular output is still available
    bool IsAvailable(unsigned int nPos) const;

    //! check whether the entire CCoins is spent
    //! note that only !IsPruned() CCoins can be serialized
    bool IsPruned() const;

    size_t DynamicMemoryUsage() const;
};

class CCoinsKeyHasher
{
private:
    uint256 salt;

public:
    CCoinsKeyHasher();

    /**
     * This *must* return size_t. With Boost 1.46 on 32-bit systems the
     * unordered_map will behave unpredictably if the custom hasher returns a
     * uint64_t, resulting in failures when syncing the chain (#4634).
     */
    size_t operator()(const uint256& key) const {
        return key.GetHash(salt);
    }
};

struct CCoinsCacheEntry
{
    CCoins coins; // The actual cached data.
    unsigned char flags;

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view.
        FRESH = (1 << 1), // The parent view does not have this entry (or it is pruned).
    };

    CCoinsCacheEntry() : coins(), flags(0) {}
};

struct CSidechainsCacheEntry
{
    CSidechain scInfo; // The actual cached data.

    enum class Flags {
        DEFAULT = 0,
        DIRTY   = (1 << 0), // This cache entry is potentially different from the version in the parent view.
        FRESH   = (1 << 1), // The parent view does not have this entry
        ERASED  = (1 << 2), // The parent view does have this entry but current one have it erased
    } flag;

    CSidechainsCacheEntry() : scInfo(), flag(Flags::DEFAULT) {}
    CSidechainsCacheEntry(const CSidechain & _scInfo, Flags _flag) : scInfo(_scInfo), flag(_flag) {}
};

struct CAnchorsCacheEntry
{
    bool entered; // This will be false if the anchor is removed from the cache
    ZCIncrementalMerkleTree tree; // The tree itself
    unsigned char flags;

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view.
    };

    CAnchorsCacheEntry() : entered(false), flags(0) {}
};

struct CNullifiersCacheEntry
{
    bool entered; // If the nullifier is spent or not
    unsigned char flags;

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view.
    };

    CNullifiersCacheEntry() : entered(false), flags(0) {}
};

typedef boost::unordered_map<uint256, CCoinsCacheEntry, CCoinsKeyHasher>      CCoinsMap;
typedef boost::unordered_map<uint256, CSidechainsCacheEntry, CCoinsKeyHasher> CSidechainsMap;
typedef boost::unordered_map<uint256, CAnchorsCacheEntry, CCoinsKeyHasher>    CAnchorsMap;
typedef boost::unordered_map<uint256, CNullifiersCacheEntry, CCoinsKeyHasher> CNullifiersMap;

struct CCoinsStats
{
    int nHeight;
    uint256 hashBlock;
    uint64_t nTransactions;
    uint64_t nTransactionOutputs;
    uint64_t nSerializedSize;
    uint256 hashSerialized;
    CAmount nTotalAmount;

    CCoinsStats() : nHeight(0), nTransactions(0), nTransactionOutputs(0), nSerializedSize(0), nTotalAmount(0) {}
};


/** Abstract view on the open txout dataset. */
class CCoinsView
{
public:
    //! Retrieve the tree at a particular anchored root in the chain
    virtual bool GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const;

    //! Determine whether a nullifier is spent or not
    virtual bool GetNullifier(const uint256 &nullifier) const;

    //! Retrieve the CCoins (unspent transaction outputs) for a given txid
    virtual bool GetCoins(const uint256 &txid, CCoins &coins) const;

    //! Just check whether we have data for a given txid.
    //! This may (but cannot always) return true for fully spent transactions
    virtual bool HaveCoins(const uint256 &txid) const;

    //! Just check whether we have data for a given sidechain id.
    virtual bool HaveSidechain(const uint256& scId) const;

    //! Retrieve the Sidechain informations for a give sidechain id.
    virtual bool GetSidechain(const uint256& scId, CSidechain& info) const;

    //! Retrieve all the known sidechain ids
    virtual void queryScIds(std::set<uint256>& scIdsList) const;

    //! just check whether we have data for a certificate in a given epoch for given sidechain
    virtual bool HaveCertForEpoch(const uint256& scId, int epochNumber) const;

    //! Retrieve the block hash whose state this CCoinsView currently represents
    virtual uint256 GetBestBlock() const;

    //! Get the current "tip" or the latest anchored tree root in the chain
    virtual uint256 GetBestAnchor() const;

    //! Do a bulk modification (multiple CCoins changes + BestBlock change).
    //! The passed mapCoins can be modified.
    virtual bool BatchWrite(CCoinsMap &mapCoins,
                            const uint256 &hashBlock,
                            const uint256 &hashAnchor,
                            CAnchorsMap &mapAnchors,
                            CNullifiersMap &mapNullifiers,
                            CSidechainsMap& mapSidechains);

    //! Calculate statistics about the unspent transaction output set
    virtual bool GetStats(CCoinsStats &stats) const;

    //! As we use CCoinsViews polymorphically, have a virtual destructor
    virtual ~CCoinsView() {}
};


/** CCoinsView backed by another CCoinsView */
class CCoinsViewBacked : public CCoinsView
{
protected:
    CCoinsView *base;

public:
    CCoinsViewBacked(CCoinsView *viewIn);
    bool GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const;
    bool GetNullifier(const uint256 &nullifier)                        const;
    bool GetCoins(const uint256 &txid, CCoins &coins)                  const;
    bool HaveCoins(const uint256 &txid)                                const;
    bool HaveSidechain(const uint256& scId)                            const override;
    bool GetSidechain(const uint256& scId, CSidechain& info)           const override;
    void queryScIds(std::set<uint256>& scIdsList)                      const override;
    bool HaveCertForEpoch(const uint256& scId, int epochNumber)        const override;
    uint256 GetBestBlock()                                             const;
    uint256 GetBestAnchor()                                            const;
    void SetBackend(CCoinsView &viewIn);
    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashAnchor,
                    CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers,
                    CSidechainsMap& mapSidechains);
    bool GetStats(CCoinsStats &stats)                                  const;
};


class CCoinsViewCache;

/** 
 * A reference to a mutable cache entry. Encapsulating it allows us to run
 *  cleanup code after the modification is finished, and keeping track of
 *  concurrent modifications. 
 */
class CCoinsModifier
{
private:
    CCoinsViewCache& cache;
    CCoinsMap::iterator it;
    size_t cachedCoinUsage; // Cached memory usage of the CCoins object before modification
    CCoinsModifier(CCoinsViewCache& cache_, CCoinsMap::iterator it_, size_t usage);

public:
    CCoins* operator->() { return &it->second.coins; }
    CCoins& operator*() { return it->second.coins; }
    ~CCoinsModifier();
    friend class CCoinsViewCache;
};

/** CCoinsView that adds a memory cache for transactions to another CCoinsView */
class CCoinsViewCache : public CCoinsViewBacked
{
protected:
    /* Whether this cache has an active modifier. */
    bool hasModifier;


    /**
     * Make mutable so that we can "fill the cache" even from Get-methods
     * declared as "const".  
     */
    mutable uint256        hashBlock;
    mutable CCoinsMap      cacheCoins;
    mutable CSidechainsMap cacheSidechains;
    mutable uint256        hashAnchor;
    mutable CAnchorsMap    cacheAnchors;
    mutable CNullifiersMap cacheNullifiers;

    /* Cached dynamic memory usage for the inner CCoins objects. */
    mutable size_t cachedCoinsUsage;

public:
    CCoinsViewCache(CCoinsView *baseIn);
    ~CCoinsViewCache();

    // Standard CCoinsView methods
    bool GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const;
    bool GetNullifier(const uint256 &nullifier) const;
    bool GetCoins(const uint256 &txid, CCoins &coins) const;
    bool HaveCoins(const uint256 &txid) const;
    uint256 GetBestBlock() const;
    uint256 GetBestAnchor() const;
    void SetBestBlock(const uint256 &hashBlock);
    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashAnchor,
                    CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers,
                    CSidechainsMap& mapSidechains);


    // Adds the tree to mapAnchors and sets the current commitment
    // root to this root.
    void PushAnchor(const ZCIncrementalMerkleTree &tree);

    // Removes the current commitment root from mapAnchors and sets
    // the new current root.
    void PopAnchor(const uint256 &rt);

    // Marks a nullifier as spent or not.
    void SetNullifier(const uint256 &nullifier, bool spent);

    /**
     * Return a pointer to CCoins in the cache, or NULL if not found. This is
     * more efficient than GetCoins. Modifications to other cache entries are
     * allowed while accessing the returned pointer.
     */
    const CCoins* AccessCoins(const uint256 &txid) const;

    /**
     * Return a modifiable reference to a CCoins. If no entry with the given
     * txid exists, a new one is created. Simultaneous modifications are not
     * allowed.
     */
    CCoinsModifier ModifyCoins(const uint256 &txid);

    /**
     * Push the modifications applied to this cache to its base.
     * Failure to call this method before destruction will cause the changes to be forgotten.
     * If false is returned, the state of this cache (and its backing view) will be undefined.
     */

    //SIDECHAIN RELATED PUBLIC MEMBERS
    bool HaveSidechain(const uint256& scId)                           const override;
    bool GetSidechain(const uint256 & scId, CSidechain& targetScInfo) const override;
    void queryScIds(std::set<uint256>& scIdsList)                     const override;
    bool HaveScRequirements(const CTransaction& tx);
    bool UpdateScInfo(const CTransaction& tx, const CBlock&, int nHeight);
    bool RevertTxOutputs(const CTransaction& tx, int nHeight);
    bool ApplyMatureBalances(int nHeight, CBlockUndo& blockundo);
    bool RestoreImmatureBalances(int nHeight, const CBlockUndo& blockundo);

    //CERTIFICATES RELATED PUBLIC MEMBERS - TO BE REFINED
    bool HaveCertForEpoch(const uint256& scId, int epochNumber) const override;
    bool IsCertApplicableToState(const CScCertificate& cert, int nHeight, CValidationState& state);
    bool isLegalEpoch(const uint256& scId, int epochNumber, const uint256& epochBlockHash);
    int getCertificateMaxIncomingHeight(const uint256& scId, int epochNumber);
    CAmount getSidechainBalance(const uint256& scId) const;
    bool UpdateScInfo(const CScCertificate& cert, CBlockUndo& bu);
    bool RevertCertOutputs(const CScCertificate& cert);

    bool Flush();

    //! Calculate the size of the cache (in number of transactions)
    unsigned int GetCacheSize() const;

    //! Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const;

    /** 
     * Amount of bitcoins coming in to a transaction
     * Note that lightweight clients may not know anything besides the hash of previous transactions,
     * so may not be able to calculate this.
     *
     * @param[in] tx    transaction for which we are checking input total
     * @return    Sum of value of all inputs (scriptSigs)
     */
    CAmount GetValueIn(const CTransactionBase& tx) const;

    //! Check whether all prevouts of the transaction are present in the UTXO set represented by this view
    bool HaveInputs(const CTransaction& tx) const;

    //! Check whether all joinsplit requirements (anchors/nullifiers) are satisfied
    bool HaveJoinSplitRequirements(const CTransaction& tx) const;

    //! Return priority of tx at height nHeight
    double GetPriority(const CTransaction &tx, int nHeight) const;

    const CTxOut &GetOutputFor(const CTxIn& input) const;

    friend class CCoinsModifier;

private:
    CCoinsMap::iterator FetchCoins(const uint256 &txid);
    CCoinsMap::const_iterator FetchCoins(const uint256 &txid) const;
    CSidechainsMap::const_iterator FetchSidechains(const uint256& scId) const;
    static int getInitScCoinsMaturity();
    int getScCoinsMaturity();

    bool DecrementImmatureAmount(const uint256& scId, CSidechain& targetScInfo, CAmount nValue, int maturityHeight);
    static void generateNewSidechainId(uint256& scId);
    void Dump_info() const;

private:
    /**
     * By making the copy constructor private, we prevent accidentally using it when one intends to create a cache on top of a base cache.
     */
    CCoinsViewCache(const CCoinsViewCache &);
};

#endif // BITCOIN_COINS_H
