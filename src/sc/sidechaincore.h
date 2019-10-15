#ifndef _SIDECHAIN_CORE_H
#define _SIDECHAIN_CORE_H

#include <vector>

#include "amount.h"
#include "chain.h"
#include "hash.h"
#include <boost/unordered_map.hpp>
#include "leveldbwrapper.h"
#include "sync.h"

#include "sc/sidechaintypes.h"

//------------------------------------------------------------------------------------
class CTxMemPool;
class CTxUndo;
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
    }
};


typedef boost::unordered_map<uint256, ScInfo, ObjectHasher> ScInfoMap;

class ScCoinsViewCache
{
    ScInfoMap mUpdate;
    std::set<uint256> sErase;

    bool createSidechain(const CTransaction& tx, const CBlock& block, int nHeight);
    bool deleteSidechain(const uint256& scId);
    bool addSidechain(const uint256& scId, const ScInfo& info);
    void removeSidechain(const uint256& scId);

    bool updateSidechainBalance(const uint256& scId, const CAmount& amount);

public:
    bool UpdateScCoins(const CTransaction& tx, const CBlock&, int nHeight);
    bool UpdateScCoins(const CTxUndo& undo);

    bool Flush();

    ScCoinsViewCache() {};
    ScCoinsViewCache(const ScCoinsViewCache&) = delete;
    ScCoinsViewCache& operator=(const ScCoinsViewCache &) = delete;
    ScCoinsViewCache(ScCoinsViewCache&) = delete;
    ScCoinsViewCache& operator=(ScCoinsViewCache &) = delete;
};

class ScMgr
{
  private:
    // Disallow instantiation outside of the class.
    ScMgr(): db(NULL) {}
    ~ScMgr() { delete db; }

    CCriticalSection sc_lock;
    ScInfoMap mScInfo;
    CLevelDBWrapper* db;

    // low level api for DB
    friend class ScCoinsViewCache;
    bool writeToDb(const uint256& scId, const ScInfo& info);
    void eraseFromDb(const uint256& scId);

    bool checkSidechainCreation(const CTransaction& tx, CValidationState& state);
    bool hasSCCreationConflictsInMempool(const CTxMemPool& pool, const CTransaction& tx);
    bool checkCertificateInMemPool(CTxMemPool& pool, const CTransaction& tx);

    // return true if the tx contains a fwd tr for the given scid
    bool anyForwardTransaction(const CTransaction& tx, const uint256& scId);

    // return true if the tx is creating the scid
    bool hasSidechainCreationOutput(const CTransaction& tx, const uint256& scId);

    CAmount getSidechainBalance(const uint256& scId);

  public:

    ScMgr(const ScMgr&) = delete;
    ScMgr& operator=(const ScMgr &) = delete;
    ScMgr(ScMgr &&) = delete;
    ScMgr & operator=(ScMgr &&) = delete;

    static ScMgr& instance();

    bool initialUpdateFromDb(size_t cacheSize, bool fWipe);

    bool sidechainExists(const uint256& scId) const;
    bool getScInfo(const uint256& scId, ScInfo& info) const;

    bool IsTxAllowedInMempool(const CTxMemPool& pool, const CTransaction& tx, CValidationState& state);
    bool checkTransaction(const CTransaction& tx, CValidationState& state);
    bool checkSidechainState(const CTransaction& tx);
    bool checkSidechainOutputs(const CTransaction& tx, CValidationState& state);

    // used when funding a raw tx 
    static void fundCcRecipients(const CTransaction& tx, std::vector<CcRecipientVariant>& vecCcSend);

    void fillJSON(UniValue& result);
    static bool fillJSON(const uint256& scId, UniValue& sc);
    static void fillJSON(const uint256& scId, const ScInfo& info, UniValue& sc);

    // print functions
    bool dump_info(const uint256& scId);
    void dump_info();
}; 

}; // end of namespace

#endif // _SIDECHAIN_CORE_H
