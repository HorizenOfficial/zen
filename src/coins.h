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
 *
 * Serialized format:
 * - VARINT(nVersion)
 * - VARINT(nCode)
 * - unspentness bitvector, for vout[2] and further; least significant byte first
 * - the non-spent CTxOuts (via CTxOutCompressor)
 * - VARINT(nHeight)
 * - string(originSc);                  serialized for coins from certificates only
 * - unsigned int(changeOutputCounter); serialized for coins from certificates only
 * The nCode value consists of:
 * - bit 1: IsCoinBase()
 * - bit 2: vout[0] is not spent
 * - bit 4: vout[1] is not spent
 * - The higher bits encode N, the number of non-zero bytes in the following bitvector.
 *   - In case both bit 2 and bit 4 are unset, they encode N-1, as there must be at
 *     least one non-spent output).
 * - changeOutputCounteris introduced with certificates. It counts the number of vouts which do not
 *   originate from a backward transfer. It uniquely specifies which vouts come
 *   from backward transfers since we assume these vout are placed last, before all of other vouts.
 *   changeOutputCounter is serialized for coins coming from certificates only, along with originScid,
 *   in order to preserve backward compatibility
 *
 * Example: 0104835800816115944e077fe7c803cfa57f29b36bf87c1d358bb85e
 *          <><><--------------------------------------------><---->
 *          |  \                  |                             /
 *    version   code             vout[1]                  height
 *
 *    - version = 1
 *    - code = 4 (vout[1] is not spent, and 0 non-zero bytes of bitvector follow)
 *    - unspentness bitvector: as 0 non-zero bytes follow, it has length 0
 *    - vout[1]: 835800816115944e077fe7c803cfa57f29b36bf87c1d35
 *               * 8358: compact amount representation for 60000000000 (600 BTC)
 *               * 00: special txout type pay-to-pubkey-hash
 *               * 816115944e077fe7c803cfa57f29b36bf87c1d35: address uint160
 *    - height = 203998
 *
 *
 * Example: 0109044086ef97d5790061b01caab50f1b8e9c50a5057eb43c2d9563a4eebbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa486af3b
 *          <><><--><--------------------------------------------------><----------------------------------------------><---->
 *         /  \   \                     |                                                           |                     /
 *  version  code  unspentness       vout[4]                                                     vout[16]           height
 *
 *  - version = 1
 *  - code = 9 (coinbase, neither vout[0] or vout[1] are unspent,
 *                2 (1, +1 because both bit 2 and bit 4 are unset) non-zero bitvector bytes follow)
 *  - unspentness bitvector: bits 2 (0x04) and 14 (0x4000) are set, so vout[2+2] and vout[14+2] are unspent
 *  - vout[4]: 86ef97d5790061b01caab50f1b8e9c50a5057eb43c2d9563a4ee
 *             * 86ef97d579: compact amount representation for 234925952 (2.35 BTC)
 *             * 00: special txout type pay-to-pubkey-hash
 *             * 61b01caab50f1b8e9c50a5057eb43c2d9563a4ee: address uint160
 *  - vout[16]: bbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa4
 *              * bbd123: compact amount representation for 110397 (0.001 BTC)
 *              * 00: special txout type pay-to-pubkey-hash
 *              * 8c988f1a4a4de2161e0f50aac7f17e7f9555caa4: address uint160
 *  - height = 120891
 */
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

    std::string ToString() const;

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
    bool IsFromCert() const;

    //! mark a vout spent
    bool Spend(uint32_t nPos);

    //! check whether a particular output is still available
    bool IsAvailable(unsigned int nPos) const;

    //! check whether the entire CCoins is spent
    //! note that only !IsPruned() CCoins can be serialized
    bool IsPruned() const;

    size_t DynamicMemoryUsage() const;

    unsigned int GetSerializeSize(int nType, int nVersion) const {
        unsigned int nSize = 0;
        unsigned int nMaskSize = 0, nMaskCode = 0;
        CalcMaskSize(nMaskSize, nMaskCode);
        bool fFirst = vout.size() > 0 && !vout[0].IsNull();
        bool fSecond = vout.size() > 1 && !vout[1].IsNull();
        assert(fFirst || fSecond || nMaskCode);
        unsigned int nCode = 8*(nMaskCode - (fFirst || fSecond ? 0 : 1)) + (fCoinBase ? 1 : 0) + (fFirst ? 2 : 0) + (fSecond ? 4 : 0);
        // version
        nSize += ::GetSerializeSize(VARINT(this->nVersion), nType, nVersion);
        // size of header code
        nSize += ::GetSerializeSize(VARINT(nCode), nType, nVersion);
        // spentness bitmask
        nSize += nMaskSize;
        // txouts themself
        for (unsigned int i = 0; i < vout.size(); i++)
            if (!vout[i].IsNull())
                nSize += ::GetSerializeSize(CTxOutCompressor(REF(vout[i])), nType, nVersion);
        // height
        nSize += ::GetSerializeSize(VARINT(nHeight), nType, nVersion);

        if (this->IsFromCert()) {
            nSize += ::GetSerializeSize(originScId,      nType,nVersion);

            unsigned int changeOutputCounter = 0;
            for(const CTxOut& out: vout) {
                if (!out.isFromBackwardTransfer)
                    ++changeOutputCounter;
                else
                    break;
            }

            nSize += ::GetSerializeSize(changeOutputCounter,nType,nVersion);
        }


        return nSize;
    }

    template<typename Stream>
    void Serialize(Stream &s, int nType, int nVersion) const {
        unsigned int nMaskSize = 0, nMaskCode = 0;
        CalcMaskSize(nMaskSize, nMaskCode);
        bool fFirst = vout.size() > 0 && !vout[0].IsNull();
        bool fSecond = vout.size() > 1 && !vout[1].IsNull();
        assert(fFirst || fSecond || nMaskCode);
        unsigned int nCode = 8*(nMaskCode - (fFirst || fSecond ? 0 : 1)) + (fCoinBase ? 1 : 0) + (fFirst ? 2 : 0) + (fSecond ? 4 : 0);
        // version
        ::Serialize(s, VARINT(this->nVersion), nType, nVersion);
        // header code
        ::Serialize(s, VARINT(nCode), nType, nVersion);
        // spentness bitmask
        for (unsigned int b = 0; b<nMaskSize; b++) {
            unsigned char chAvail = 0;
            for (unsigned int i = 0; i < 8 && 2+b*8+i < vout.size(); i++)
                if (!vout[2+b*8+i].IsNull())
                    chAvail |= (1 << i);
            ::Serialize(s, chAvail, nType, nVersion);
        }
        // txouts themself
        for (unsigned int i = 0; i < vout.size(); i++) {
            if (!vout[i].IsNull())
                ::Serialize(s, CTxOutCompressor(REF(vout[i])), nType, nVersion);
        }

        // coinbase height
        ::Serialize(s, VARINT(nHeight), nType, nVersion);

        if (this->IsFromCert()) {
            ::Serialize(s,originScId,      nType,nVersion);

            unsigned int changeOutputCounter = 0;
            for(const CTxOut& out: vout) {
                if (!out.isFromBackwardTransfer)
                    ++changeOutputCounter;
                else
                    break;
            }

            ::Serialize(s,changeOutputCounter,nType,nVersion);
        }
    }

    template<typename Stream>
    void Unserialize(Stream &s, int nType, int nVersion) {
        unsigned int nCode = 0;
        // version
        ::Unserialize(s, VARINT(this->nVersion), nType, nVersion);
        // header code
        ::Unserialize(s, VARINT(nCode), nType, nVersion);
        fCoinBase = nCode & 1;
        std::vector<bool> vAvail(2, false);
        vAvail[0] = (nCode & 2) != 0;
        vAvail[1] = (nCode & 4) != 0;
        unsigned int nMaskCode = (nCode / 8) + ((nCode & 6) != 0 ? 0 : 1);
        // spentness bitmask
        while (nMaskCode > 0) {
            unsigned char chAvail = 0;
            ::Unserialize(s, chAvail, nType, nVersion);
            for (unsigned int p = 0; p < 8; p++) {
                bool f = (chAvail & (1 << p)) != 0;
                vAvail.push_back(f);
            }
            if (chAvail != 0)
                nMaskCode--;
        }
        // txouts themself
        vout.assign(vAvail.size(), CTxOut());
        for (unsigned int i = 0; i < vAvail.size(); i++) {
            if (vAvail[i])
                ::Unserialize(s, REF(CTxOutCompressor(vout[i])), nType, nVersion);
        }
        // coinbase height
        ::Unserialize(s, VARINT(nHeight), nType, nVersion);

        unsigned int changeOutputCounter = vout.size();
        if (this->IsFromCert()) {
            ::Unserialize(s, originScId,       nType,nVersion);
            ::Unserialize(s, changeOutputCounter, nType,nVersion);
        }

        for(unsigned int idx = 0; idx < vout.size(); ++idx)
            if ( idx < changeOutputCounter)
                vout[idx].isFromBackwardTransfer = false;
            else
                vout[idx].isFromBackwardTransfer = true;


        Cleanup();
    }
private:
    void CalcMaskSize(unsigned int &nBytes, unsigned int &nNonzeroBytes) const;
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

struct CCeasingScsCacheEntry
{
    CCeasingSidechains ceasingScs; // The actual cached data.

    enum class Flags {
        DEFAULT = 0,
        DIRTY   = (1 << 0), // This cache entry is potentially different from the version in the parent view.
        FRESH   = (1 << 1), // The parent view does not have this entry
        ERASED  = (1 << 2), // The parent view does have this entry but current one have it erased
    } flag;

    CCeasingScsCacheEntry() : ceasingScs(), flag(Flags::DEFAULT) {}
    CCeasingScsCacheEntry(const CCeasingSidechains & _scList, Flags _flag) : ceasingScs(_scList), flag(_flag) {}
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
typedef boost::unordered_map<int, CCeasingScsCacheEntry>                      CCeasingScsMap;
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

    //! Just check whether we have ceasing sidechains at given height
    virtual bool HaveCeasingScs(int height) const;

    //! Retrieve the scId list of sidechain ceasing at given height.
    virtual bool GetCeasingScs(int height, CCeasingSidechains& ceasingScs) const;

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
                            CSidechainsMap& mapSidechains,
                            CCeasingScsMap& mapCeasedScs);

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
    bool GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const override;
    bool GetNullifier(const uint256 &nullifier)                        const override;
    bool GetCoins(const uint256 &txid, CCoins &coins)                  const override;
    bool HaveCoins(const uint256 &txid)                                const override;
    bool HaveSidechain(const uint256& scId)                            const override;
    bool GetSidechain(const uint256& scId, CSidechain& info)           const override;
    bool HaveCeasingScs(int height)                                    const override;
    bool GetCeasingScs(int height, CCeasingSidechains& ceasingScs)     const override;
    void queryScIds(std::set<uint256>& scIdsList)                      const override;
    bool HaveCertForEpoch(const uint256& scId, int epochNumber)        const override;
    uint256 GetBestBlock()                                             const override;
    uint256 GetBestAnchor()                                            const override;
    void SetBackend(CCoinsView &viewIn);
    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashAnchor,
                    CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers,
                    CSidechainsMap& mapSidechains,
                    CCeasingScsMap& mapCeasedScs)                            override;
    bool GetStats(CCoinsStats &stats)                                  const override;
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
    mutable CCeasingScsMap cacheCeasingScs;
    mutable uint256        hashAnchor;
    mutable CAnchorsMap    cacheAnchors;
    mutable CNullifiersMap cacheNullifiers;

    /* Cached dynamic memory usage for the inner CCoins objects. */
    mutable size_t cachedCoinsUsage;

public:
    CCoinsViewCache(CCoinsView *baseIn);
    ~CCoinsViewCache();

    // Standard CCoinsView methods
    bool GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const override;
    bool GetNullifier(const uint256 &nullifier)                        const override;
    bool GetCoins(const uint256 &txid, CCoins &coins)                  const override;
    bool HaveCoins(const uint256 &txid)                                const override;
    uint256 GetBestBlock()                                             const override;
    uint256 GetBestAnchor()                                            const override;
    void SetBestBlock(const uint256 &hashBlock);
    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashAnchor,
                    CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers,
                    CSidechainsMap& mapSidechains,
                    CCeasingScsMap& mapCeasedScs)                            override;


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
    bool HaveScRequirements(const CTransaction& tx, int height);
    bool UpdateScInfo(const CTransaction& tx, const CBlock&, int nHeight);
    bool RevertTxOutputs(const CTransaction& tx, int nHeight);
    bool ApplyMatureBalances(int nHeight, CBlockUndo& blockundo);
    bool RestoreImmatureBalances(int nHeight, const CBlockUndo& blockundo);

    //CERTIFICATES RELATED PUBLIC MEMBERS
    bool HaveCertForEpoch(const uint256& scId, int epochNumber) const override;
    bool IsCertApplicableToState(const CScCertificate& cert, int nHeight, CValidationState& state);
    bool isLegalEpoch(const uint256& scId, int epochNumber, const uint256& epochBlockHash);
    bool UpdateScInfo(const CScCertificate& cert, CBlockUndo& bu);
    bool RevertCertOutputs(const CScCertificate& cert);

    //CEASING SIDECHAINS RELATED MEMBERS
    bool HaveCeasingScs(int height)                                const override;
    bool GetCeasingScs(int height, CCeasingSidechains& ceasingScs) const override;
    bool UpdateCeasingScs(const CTxScCreationOut& scCreationOut);
    bool UndoCeasingScs(const CTxScCreationOut& scCreationOut);
    bool UpdateCeasingScs(const CScCertificate& cert);
    bool UndoCeasingScs(const CScCertificate& cert);
    bool HandleCeasingScs(int height, CBlockUndo& blockUndo);
    bool RevertCeasingScs(const CTxUndo& ceasedCertUndo);

    CSidechain::state isCeasedAtHeight(const uint256& scId, int height) const;

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

    //! Verify whether given output is mature according to current view
    enum class outputMaturity {
        NOT_APPLICABLE = 0,
        MATURE,
        IMMATURE
    };
    outputMaturity IsCertOutputMature(const uint256& txHash, unsigned int pos, int spendHeight) const;

    //! Check whether all prevouts of the transaction are present in the UTXO set represented by this view
    bool HaveInputs(const CTransactionBase& txBase) const;

    //! Check whether all joinsplit requirements (anchors/nullifiers) are satisfied
    bool HaveJoinSplitRequirements(const CTransactionBase& txBase) const;

    //! Return priority of tx at height nHeight
    double GetPriority(const CTransactionBase &tx, int nHeight) const;

    const CTxOut &GetOutputFor(const CTxIn& input) const;

    friend class CCoinsModifier;

private:
    CCoinsMap::iterator            FetchCoins(const uint256 &txid);
    CCoinsMap::const_iterator      FetchCoins(const uint256 &txid)      const;
    CSidechainsMap::const_iterator FetchSidechains(const uint256& scId) const;
    CCeasingScsMap::const_iterator FetchCeasingScs(int height)          const;

    static int getInitScCoinsMaturity();
    int getScCoinsMaturity();

    bool DecrementImmatureAmount(const uint256& scId, CSidechain& targetScInfo, CAmount nValue, int maturityHeight);
    void Dump_info() const;

private:
    /**
     * By making the copy constructor private, we prevent accidentally using it when one intends to create a cache on top of a base cache.
     */
    CCoinsViewCache(const CCoinsViewCache &);
};

#endif // BITCOIN_COINS_H
