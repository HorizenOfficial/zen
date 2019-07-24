#ifndef _SIDECHAIN_H
#define _SIDECHAIN_H

#include <vector>

#include "amount.h"
#include "chain.h"
#include "hash.h"
#include <boost/unordered_map.hpp>
#include "leveldbwrapper.h"
#include "sync.h"

//------------------------------------------------------------------------------------
class CTxMemPool;
class UniValue;
class CTxForwardTransferCrosschainOut;
class CValidationState;

namespace Sidechain
{

struct ScCreationParameters
{
    int startBlockHeight; 

    ScCreationParameters() :startBlockHeight(-1) {}
    
    // all creation data follows...
    // TODO
};

struct CRecipientCrossChainBase
{
    uint256 address;
    CAmount nValue;
    uint256 scId;

    virtual ~CRecipientCrossChainBase() {}
    CRecipientCrossChainBase() : nValue(0) {}
};

// probably not needed, a fee is devolved to foundation via tr address
// in that case address should be moved from base class to children, not creation
static const uint256 SC_CREATION_PAYEE_ADDRESS = uint256S("badc01dcafe");
static const CAmount SC_CREATION_FEE = 100000000; // in satoshi = 1.0 Zen

struct CRecipientScCreation : public CRecipientCrossChainBase
{
    ScCreationParameters creationData;
};

struct CRecipientCertLock : public CRecipientCrossChainBase
{
    int64_t epoch;
    CRecipientCertLock() : epoch(-1) {}
};

using ::CTxForwardTransferCrosschainOut;

class CRecipientForwardTransfer : public CRecipientCrossChainBase
{
    public:
    explicit CRecipientForwardTransfer(const CTxForwardTransferCrosschainOut&);
    CRecipientForwardTransfer() {};
};

//--------------------------------
class ScInfo
{
public:
    ScInfo() : ownerBlockHash(), creationBlockHeight(-1), creationTxIndex(-1), ownerTxHash(), balance(0) {}
    
    // reference to the block containing the tx that created the side chain 
    uint256 ownerBlockHash;

    // We can not serialize a pointer value to block index, but can retrieve it from chainActive if we have height
    int creationBlockHeight;

    // index of the creating tx in the block vect of txs
    int creationTxIndex;

    // hash of the tx who created it
    uint256 ownerTxHash;

    // total amount given by sum(fw transfer)-sum(bkw transfer)
    CAmount balance;

    // creation data
    ScCreationParameters creationData;

    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(ownerBlockHash);
        READWRITE(creationBlockHeight);
        READWRITE(creationTxIndex);
        READWRITE(ownerTxHash);
        READWRITE(balance);
    }
};

typedef boost::variant<CRecipientScCreation, CRecipientCertLock, CRecipientForwardTransfer> CcRecipientVariant;
typedef boost::unordered_map<uint256, ScInfo, ObjectHasher> ScInfoMap;

using ::CTxMemPool;
using ::UniValue;
using ::CValidationState;

class ScMgr
{
  private:
    ScMgr(); // Disallow instantiation outside of the class.

    CCriticalSection sc_lock;
    ScInfoMap mScInfo;
    CLevelDBWrapper* db;

    bool writeToDb(const uint256& scId, const ScInfo& info);
    void eraseFromDb(const uint256& scId);

    typedef boost::unordered_map<uint256, std::vector<CRecipientForwardTransfer>, ObjectHasher> ScFwdTransfers;
    ScFwdTransfers _cachedFwTransfers;

    bool addSidechain(const uint256& scId, ScInfo& info);
    void removeSidechain(const uint256& scId);

    bool checkSidechainCreation(const CTransaction& tx);
    bool checkCreationInMemPool(CTxMemPool& pool, const CTransaction& tx);

  public:
    static ScMgr& instance();

    ScMgr(const ScMgr&) = delete;
    ScMgr& operator=(const ScMgr &) = delete;
    ScMgr(ScMgr &&) = delete;
    ScMgr & operator=(ScMgr &&) = delete;

    bool initialUpdateFromDb(size_t cacheSize, bool fWipe);

    bool sidechainExists(const uint256& scId);
    bool getScInfo(const uint256& scId, ScInfo& info);

    bool onBlockConnected(const CBlock& block, int nHeight);
    bool onBlockDisconnected(const CBlock& block);

    bool updateSidechainBalance(const uint256& scId, const CAmount& amount);
    CAmount getSidechainBalance(const uint256& scId);

    bool checkTransaction(const CTransaction& tx, CValidationState& state);
    bool checkMemPool(CTxMemPool& pool, const CTransaction& tx, CValidationState& state);
    bool checkSidechainForwardTransaction(const CTransaction& tx);

    // return the index of the added vout, -1 if no output has been added 
    int evalAddCreationFeeOut(CMutableTransaction& tx);

    // used when creating a raw transaction with cc outputs
    bool fillRawCreation(UniValue& sc_crs, CMutableTransaction& rawTx, CTxMemPool& pool, std::string& error); 
    // used when funding a raw tx 
    void fillFundCcRecipients(const CTransaction& tx, std::vector<CcRecipientVariant>& vecCcSend);

    // print functions
    bool dump_info(const uint256& scId);
    void dump_info();
    bool fillJSON(const uint256& scId, UniValue& sc);
    void fillJSON(UniValue& result);
    void fillJSON(const uint256& scId, const ScInfo& info, UniValue& sc);
}; 


class CRecipientFactory;

class CcRecipientVisitor : public boost::static_visitor<bool>
{
    private:
       CRecipientFactory* fact;
    public:
       explicit CcRecipientVisitor(CRecipientFactory* factIn) : fact(factIn) {}

    bool operator() (const CRecipientScCreation& r) const;
    bool operator() (const CRecipientCertLock& r) const;
    bool operator() (const CRecipientForwardTransfer& r) const;
};

class CRecipientFactory
{
    private:
       CMutableTransaction& tx;
       std::string& err;

    public:
       CRecipientFactory(CMutableTransaction& txIn, std::string& errIn) : tx(txIn), err(errIn) {}

    bool set(const CcRecipientVariant& rec)
    {
        return boost::apply_visitor(CcRecipientVisitor(this), rec);
    };

    bool set(const CRecipientScCreation& r);
    bool set(const CRecipientCertLock& r);
    bool set(const CRecipientForwardTransfer& r);
};

class CcRecipientAmountVisitor : public boost::static_visitor<CAmount>
{
    public:
    template <typename T>
    CAmount operator() (const T& r) const { return r.nValue; }
};

}; // end of namespace

#endif // _SIDECHAIN_H
