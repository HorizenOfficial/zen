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

    // set of backward transfer tx, it is used for verifying db and wallet at startup
    // std::set<uint256> sBackwardTransfers;

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
//        READWRITE(sBackwardTransfers);
    }
};


typedef boost::unordered_map<uint256, ScInfo, ObjectHasher> ScInfoMap;

class ScMgr
{
  private:
    // Disallow instantiation outside of the class.
    ScMgr(): db(NULL), bVerifyingDb(false) {}
    ~ScMgr() { delete db; }

    CCriticalSection sc_lock;
    ScInfoMap mScInfo;
    CLevelDBWrapper* db;
    bool bVerifyingDb;

    // low level api for DB
    bool writeToDb(const uint256& scId, const ScInfo& info);
    void eraseFromDb(const uint256& scId);

    // add/remove/find obj in sc map and DB
    bool addSidechain(const uint256& scId, const ScInfo& info);
    void removeSidechain(const uint256& scId);
    bool addScBackwardTx(const uint256& scId, const uint256& hash);
    bool removeScBackwardTx(const uint256& scId, const uint256& hash);
    bool containsScBackwardTx(const uint256& scId, const uint256& txHash);

    bool checkSidechainCreation(const CTransaction& tx, CValidationState& state);
    bool checkCreationInMemPool(CTxMemPool& pool, const CTransaction& tx);
    bool checkCertificateInMemPool(CTxMemPool& pool, const CTransaction& tx);

    bool updateSidechainBalance(const uint256& scId, const CAmount& amount);
  public:

    ScMgr(const ScMgr&) = delete;
    ScMgr& operator=(const ScMgr &) = delete;
    ScMgr(ScMgr &&) = delete;
    ScMgr & operator=(ScMgr &&) = delete;

    static ScMgr& instance();

    bool verifyingDb() { return bVerifyingDb; }
    void verifyingDb(bool flag) { bVerifyingDb = flag; }
     
    bool initialUpdateFromDb(size_t cacheSize, bool fWipe);

    bool sidechainExists(const uint256& scId);
    bool getScInfo(const uint256& scId, ScInfo& info);

    bool onBlockConnected(const CBlock& block, int nHeight);
    bool onBlockDisconnected(const CBlock& block, int nHeight);

    CAmount getSidechainBalance(const uint256& scId);

    bool checkMemPool(CTxMemPool& pool, const CTransaction& tx, CValidationState& state);
    bool checkTransaction(const CTransaction& tx, CValidationState& state);
    bool checkSidechainForwardTransaction(const CTransaction& tx, CValidationState& state);
    bool checkSidechainBackwardTransaction(
        const CTransaction& tx, CValidationState& state);

    bool checkSidechainCreationFunds(const CTransaction& tx, int nHeight);

    // return true if the tx contains a fwd tr for the given scid
    bool anyForwardTransaction(const CTransaction& tx, const uint256& scId);

    // return true if the tx is creating the scid
    bool isCreating(const CTransaction& tx, const uint256& scId);

    // return the index of the added vout, -1 if no output has been added 
    int evalAddCreationFeeOut(CMutableTransaction& tx);

    // used when creating a raw transaction with cc outputs
    bool fillRawCreation(UniValue& sc_crs, CMutableTransaction& rawTx, CTxMemPool& pool, std::string& error); 
    // used when funding a raw tx 
    void fillFundCcRecipients(const CTransaction& tx, std::vector<CcRecipientVariant>& vecCcSend);

    // return true if any sc related tx has been found. In this case out parameter vTxReord has the same
    // contents as the block.vtx vector, but all certificates, if any is found, have been moved at the end,
    // and sScId contains all the concerned sc id.
    static bool hasCrosschainTransfers(const CBlock& block, std::vector<CTransaction>& vTxReord, std::set<uint256>& sScId);

    // print functions
    bool dump_info(const uint256& scId);
    void dump_info();
    bool fillJSON(const uint256& scId, UniValue& sc);
    void fillJSON(UniValue& result);
    void fillJSON(const uint256& scId, const ScInfo& info, UniValue& sc);
}; 

}; // end of namespace

#endif // _SIDECHAIN_CORE_H
