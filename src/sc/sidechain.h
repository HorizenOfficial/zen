#ifndef _SIDECHAIN_CORE_H
#define _SIDECHAIN_CORE_H

#include "amount.h"
#include "chain.h"
#include "hash.h"
#include <boost/unordered_map.hpp>
#include "sync.h"

#include "sc/sidechaintypes.h"

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

typedef boost::unordered_map<uint256, ScInfo, ObjectHasher> ScInfoMap;

class ScCoinsView
{
public:
    ScCoinsView() = default;
    ScCoinsView(const ScCoinsView&) = delete;
    ScCoinsView& operator=(const ScCoinsView &) = delete;
    virtual ~ScCoinsView() = default;

    static bool checkTxSemanticValidity(const CTransaction& tx, CValidationState& state);
    static bool IsTxAllowedInMempool(const CTxMemPool& pool, const CTransaction& tx, CValidationState& state);
    bool IsTxApplicableToState(const CTransaction& tx);

    virtual bool sidechainExists(const uint256& scId) const = 0;
    virtual bool getScInfo(const uint256& scId, ScInfo& info) const = 0;
    virtual std::set<uint256> getScIdSet() const = 0;

protected:
    static bool hasScCreationOutput(const CTransaction& tx, const uint256& scId); // return true if the tx is creating the scid
    static bool anyForwardTransaction(const CTransaction& tx, const uint256& scId);
};

class ScCoinsPersistedView;
class ScCoinsViewCache : public ScCoinsView
{
public:
    ScCoinsViewCache(ScCoinsPersistedView& _persistedView);

    bool sidechainExists(const uint256& scId) const;
    bool getScInfo(const uint256 & scId, ScInfo& targetScInfo) const;
    std::set<uint256> getScIdSet() const;
    bool UpdateScInfo(const CTransaction& tx, const CBlock&, int nHeight);

    bool RevertTxOutputs(const CTransaction& tx, int nHeight);
    bool ApplyMatureBalances(int nHeight, CBlockUndo& blockundo);
    bool RestoreImmatureBalances(int nHeight, const CBlockUndo& blockundo);

    bool Flush();

private:
    ScCoinsPersistedView& persistedView;
    ScInfoMap mUpdatedOrNewScInfoList;
    std::set<uint256> sDeletedScList;
};

class ScCoinsPersistedView : public ScCoinsView
{
public:
    virtual bool persist(const uint256& scId, const ScInfo& info) = 0;
    virtual bool erase(const uint256& scId) = 0;
};

class PersistenceLayer;
class ScMgr : public ScCoinsPersistedView
{
public:
    static ScMgr& instance();

    bool initPersistence(size_t cacheSize, bool fWipe);
    bool initPersistence(PersistenceLayer * pTestLayer); //utility for unit tests
    void reset(); //utility for dtor and unit tests, hence public

    bool persist(const uint256& scId, const ScInfo& info);
    bool erase(const uint256& scId);

    bool sidechainExists(const uint256& scId) const;
    bool getScInfo(const uint256& scId, ScInfo& info) const;
    std::set<uint256> getScIdSet() const;

    // print functions
    bool dump_info(const uint256& scId);
    void dump_info();

private:
    // Disallow instantiation outside of the class.
    ScMgr(): pLayer(nullptr){}
    ~ScMgr() { reset(); }

    mutable CCriticalSection sc_lock;
    ScInfoMap ManagerScInfoMap;
    PersistenceLayer * pLayer;

    bool loadInitialData();
}; 

class PersistenceLayer {
public:
    PersistenceLayer() = default;
    virtual ~PersistenceLayer() = default;
    virtual bool loadPersistedDataInto(ScInfoMap & _mapToFill) = 0;
    virtual bool persist(const uint256& scId, const ScInfo& info) = 0;
    virtual bool erase(const uint256& scId) = 0;
    virtual void dump_info() = 0;
};

class DbPersistance final : public PersistenceLayer {
public:
    DbPersistance(const boost::filesystem::path& path, size_t nCacheSize, bool fMemory, bool fWipe);
    ~DbPersistance();
    bool loadPersistedDataInto(ScInfoMap & _mapToFill);
    bool persist(const uint256& scId, const ScInfo& info);
    bool erase(const uint256& scId);
    void dump_info();
private:
    CLevelDBWrapper* _db;
};

}; // end of namespace

#endif // _SIDECHAIN_CORE_H
