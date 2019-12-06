#ifndef _SIDECHAIN_CORE_H
#define _SIDECHAIN_CORE_H

#include <vector>

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

class ScCoinsViewCache
{
    ScInfoMap mUpdate;
    std::set<uint256> sErase;
    std::set<uint256> sDirty;

public:
    bool UpdateScInfo(const CTransaction& tx, const CBlock&, int nHeight);
    bool RevertTxOutputs(const CTransaction& tx, int nHeight);
    bool ApplyMatureBalances(int nHeight, CBlockUndo& blockundo);
    bool RestoreImmatureBalances(int nHeight, const CBlockUndo& blockundo);

    bool sidechainExists(const uint256& scId) const;
    const ScInfoMap& getScInfoMap() const { return mUpdate; } //utility for UTs

    bool Flush();

    ScCoinsViewCache();
    ScCoinsViewCache(const ScCoinsViewCache&) = delete;
    ScCoinsViewCache& operator=(const ScCoinsViewCache &) = delete;
    ScCoinsViewCache(ScCoinsViewCache&) = delete;
    ScCoinsViewCache& operator=(ScCoinsViewCache &) = delete;
};

class ScMgr
{
public:
    enum persistencePolicy {
        mock = 0, //utility for UTs
        persist,
    };

    class persistanceLayer; //Todo: make private

private:
    // Disallow instantiation outside of the class.
    ScMgr(): pLayer(nullptr), chosenPersistencePolicy(persist) {}
    ~ScMgr() { reset(); }

    mutable CCriticalSection sc_lock;
    ScInfoMap mScInfo;

    persistanceLayer * pLayer;

    persistencePolicy chosenPersistencePolicy;

    // low level api for DB
    friend class ScCoinsViewCache;

    bool loadInitialDataFromDb(ScInfoMap & _mapToFill);
    bool writeToDb(const uint256& scId, const ScInfo& info);
    void eraseFromDb(const uint256& scId);

    bool checkScCreation(const CTransaction& tx, CValidationState& state);
    bool hasScCreationConflictsInMempool(const CTxMemPool& pool, const CTransaction& tx);
    bool checkCertificateInMemPool(CTxMemPool& pool, const CTransaction& tx);

    // return true if the tx contains a fwd tr for the given scid
    static bool anyForwardTransaction(const CTransaction& tx, const uint256& scId);

    // return true if the tx is creating the scid
    bool hasScCreationOutput(const CTransaction& tx, const uint256& scId);

  public:

    ScMgr(const ScMgr&) = delete;
    ScMgr& operator=(const ScMgr &) = delete;
    ScMgr(ScMgr &&) = delete;
    ScMgr & operator=(ScMgr &&) = delete;

    static ScMgr& instance();

    bool initPersistence(size_t cacheSize, bool fWipe, const persistencePolicy & dbPolicy = persistencePolicy::persist );
    void reset(); //utility for dtor and unit tests, hence public

    bool sidechainExists(const uint256& scId, const ScCoinsViewCache* const scView = NULL) const;
    bool getScInfo(const uint256& scId, ScInfo& info) const;

    bool IsTxAllowedInMempool(const CTxMemPool& pool, const CTransaction& tx, CValidationState& state);
    static bool checkTxSemanticValidity(const CTransaction& tx, CValidationState& state);
    bool IsTxApplicableToState(const CTransaction& tx, const ScCoinsViewCache* const scView = NULL);

    void getScIdSet(std::set<uint256>& sScIds) const;

    const ScInfoMap& getScInfoMap() const { return mScInfo; }
    CAmount getScBalance(const uint256& scId); //utility for UTs
    // print functions
    bool dump_info(const uint256& scId);
    void dump_info();
}; 

}; // end of namespace

#endif // _SIDECHAIN_CORE_H
