#ifndef _SIDECHAIN_CORE_H
#define _SIDECHAIN_CORE_H

#include "amount.h"
#include "chain.h"
#include "hash.h"
#include <boost/unordered_map.hpp>
#include "sync.h"

#include "sc/sidechaintypes.h"
#include <coins.h> //temp for merging sidechains with coins

//------------------------------------------------------------------------------------
class CTxMemPool;
class CBlockUndo;
class UniValue;
class CValidationState;
class CLevelDBWrapper;

namespace Sidechain
{

class ScInfo
{
public:
    ScInfo() : creationBlockHash(), creationBlockHeight(-1), creationTxHash(), balance(0) {}
    
    // reference to the block containing the tx that created the side chain 
    uint256 creationBlockHash;

    // We can not serialize a pointer value to block index, but can retrieve it from chainActive if we have height
    int creationBlockHeight;

    // hash of the tx who created it
    uint256 creationTxHash;

    // total amount given by sum(fw transfer)-sum(bkw transfer)
    CAmount balance;

    // creation data
    ScCreationParameters creationData;

    // immature amounts
    // key   = height at which amount will be considered as mature and will be part of the sc balance
    // value = the immature amount  
    std::map<int, CAmount> mImmatureAmounts;

    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(creationBlockHash);
        READWRITE(creationBlockHeight);
        READWRITE(creationTxHash);
        READWRITE(balance);
        READWRITE(creationData);
        READWRITE(mImmatureAmounts);
    }

    inline bool operator==(const ScInfo& rhs) const
    {
        return (this->creationBlockHash   == rhs.creationBlockHash)   &&
               (this->creationBlockHeight == rhs.creationBlockHeight) &&
               (this->creationTxHash      == rhs.creationTxHash)      &&
               (this->creationData        == rhs.creationData)        &&
               (this->mImmatureAmounts    == rhs.mImmatureAmounts);
    }
    inline bool operator!=(const ScInfo& rhs) const { return !(*this == rhs); }
};

struct CSidechainsCacheEntry
{
    ScInfo scInfo; // The actual cached data.

    enum class Flags {
        DEFAULT = 0,
        DIRTY   = (1 << 0), // This cache entry is potentially different from the version in the parent view.
        FRESH   = (1 << 1), // The parent view does not have this entry
        ERASED  = (1 << 2), // Flag in sidechain only, to be removed to conform what happens in CCoinsCacheEntry
    } flag;

    CSidechainsCacheEntry() : scInfo(), flag(Flags::DEFAULT) {}
    CSidechainsCacheEntry(const ScInfo & _scInfo, Flags _flag) : scInfo(_scInfo), flag(_flag) {}
};

typedef boost::unordered_map<uint256, CSidechainsCacheEntry, ObjectHasher> CSidechainsMap;

// Validation functions
bool checkTxSemanticValidity(const CTransaction& tx, CValidationState& state);
bool anyForwardTransaction(const CTransaction& tx, const uint256& scId);
bool hasScCreationOutput(const CTransaction& tx, const uint256& scId); // return true if the tx is creating the scid

bool existsInMempool(const CTxMemPool& pool, const CTransaction& tx, CValidationState& state);
// End of Validation functions

class CSidechainsView
{
public:
    CSidechainsView() = default;
    virtual ~CSidechainsView() = default;

    virtual bool HaveScInfo(const uint256& scId) const = 0;
    virtual bool GetScInfo(const uint256& scId, ScInfo& info) const = 0;
    virtual bool queryScIds(std::set<uint256>& scIdsList) const = 0;

    virtual bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap) = 0;
};

class CSidechainsViewBacked : public CSidechainsView
{
public:
    CSidechainsViewBacked(CSidechainsView &viewIn): baseView(viewIn) {}
    bool HaveScInfo(const uint256& scId)              const {return baseView.HaveScInfo(scId);}
    bool GetScInfo(const uint256& scId, ScInfo& info) const {return baseView.GetScInfo(scId,info);}
    bool queryScIds(std::set<uint256>& scIdsList)     const {return baseView.queryScIds(scIdsList);}
    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap)
                    {return baseView.BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, sidechainMap);}

protected:
    CSidechainsView &baseView;
};

class CSidechainsViewCache : public CSidechainsViewBacked
{
public:
    CSidechainsViewCache(CSidechainsView& scView);
    CSidechainsViewCache(const CSidechainsViewCache&) = delete;             //as in coins, forbid building cache on top of another
    CSidechainsViewCache& operator=(const CSidechainsViewCache &) = delete;
    bool HaveDependencies(const CTransaction& tx);

    bool HaveScInfo(const uint256& scId) const;
    bool GetScInfo(const uint256 & scId, ScInfo& targetScInfo) const;
    bool queryScIds(std::set<uint256>& scIdsList) const; //Similar to queryHashes

    bool UpdateScInfo(const CTransaction& tx, const CBlock&, int nHeight);
    bool ApplyMatureBalances(int nHeight, CBlockUndo& blockundo);

    bool RevertTxOutputs(const CTransaction& tx, int nHeight);
    bool RestoreImmatureBalances(int nHeight, const CBlockUndo& blockundo);

    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap);
    bool Flush();

private:
    mutable CSidechainsMap cacheSidechains;
    CSidechainsMap::const_iterator FetchSidechains(const uint256& scId) const;
    void Dump_info() const;
};

class CSidechainViewDB : public CSidechainsView
{
public:
    static CSidechainViewDB& instance();

    bool initPersistence(size_t cacheSize, bool fWipe);
    void reset(); //utility for dtor and unit tests, hence public

    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap);

    bool HaveScInfo(const uint256& scId) const;
    bool GetScInfo(const uint256& scId, ScInfo& info) const;
    bool queryScIds(std::set<uint256>& scIdsList) const;

protected:
    // Disallow instantiation outside of the class. Only UTs should be allowed to inherit and build a fakeDB
    CSidechainViewDB();
    ~CSidechainViewDB();

    bool Persist(const uint256& scId, const ScInfo& info) const;
    bool Erase(const uint256& scId) const;
    void Dump_info() const;

private:
    mutable CCriticalSection sc_lock;
    CLevelDBWrapper* scDb;
}; 

}; // end of namespace

#endif // _SIDECHAIN_CORE_H
