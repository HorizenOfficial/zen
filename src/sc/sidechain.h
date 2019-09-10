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

// useful in sc rpc command for getting genesis info
struct sPowRelatedData {
    uint32_t a;
    uint32_t b;
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(a);
        READWRITE(b);
    }
};

struct ScCreationParameters
{
    int withdrawalEpochLength; 
    // all creation data follows...
    // TODO

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(withdrawalEpochLength);
    }

    ScCreationParameters() :withdrawalEpochLength(-1) {}
    
};

struct CRecipientCrossChainBase
{
    uint256 scId;

    virtual ~CRecipientCrossChainBase() {}
};

static const CAmount SC_CREATION_FEE = 100000000; // in satoshi = 1.0 Zen

struct CRecipientScCreation : public CRecipientCrossChainBase
{
    ScCreationParameters creationData;
};

struct CRecipientCertLock : public CRecipientCrossChainBase
{
    uint256 address;
    CAmount nValue;
    int64_t epoch;
    CRecipientCertLock() : nValue(0), epoch(-1) {}
};

using ::CTxForwardTransferCrosschainOut;

struct CRecipientForwardTransfer : public CRecipientCrossChainBase
{
    uint256 address;
    CAmount nValue;
    explicit CRecipientForwardTransfer(const CTxForwardTransferCrosschainOut&);
    CRecipientForwardTransfer(): nValue(0) {};
};

using ::CTxBackwardTransferCrosschainOut;

struct CRecipientBackwardTransfer 
{
    CScript scriptPubKey;
    CAmount nValue;

    explicit CRecipientBackwardTransfer(const CTxBackwardTransferCrosschainOut&);
    CRecipientBackwardTransfer(): nValue(0) {};
};

//#define SC_TIMED_BALANCE 1
//--------------------------------
#ifdef SC_TIMED_BALANCE
struct sScTimedBalance {
    int height;
    CAmount scAmount;
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(height);
        READWRITE(scAmount);
    }
};
#endif

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

#ifdef SC_TIMED_BALANCE
    // vector of timed amounts of the sc. Each entry is the amount stored at the reference height
    std::vector<sScTimedBalance> vTimedBalances;
#endif

    // vector of backward transfer tx is. Used for verifying db and wallet at startup
    std::vector<uint256> vBackwardTransfers;

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
        READWRITE(creationData);
#ifdef SC_TIMED_BALANCE
        READWRITE(vTimedBalances);
#endif
        READWRITE(vBackwardTransfers);
    }
};

typedef boost::variant<
        CRecipientScCreation,
        CRecipientCertLock,
        CRecipientForwardTransfer,
        CRecipientBackwardTransfer
    > CcRecipientVariant;

typedef boost::unordered_map<uint256, ScInfo, ObjectHasher> ScInfoMap;

using ::CTxMemPool;
using ::UniValue;
using ::CValidationState;

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

    bool writeToDb(const uint256& scId, const ScInfo& info);
    void eraseFromDb(const uint256& scId);

    typedef boost::unordered_map<uint256, std::vector<CRecipientForwardTransfer>, ObjectHasher> ScFwdTransfers;
    ScFwdTransfers _cachedFwTransfers;

    bool addSidechain(const uint256& scId, const ScInfo& info);
    void removeSidechain(const uint256& scId);

    bool addScBackwardTx(const uint256& scId, const uint256& hash);
    bool removeScBackwardTx(const uint256& scId, const uint256& hash);
    bool containsScBackwardTx(const uint256& scId, const uint256& txHash);

    bool checkSidechainCreation(const CTransaction& tx, CValidationState& state);
    bool checkCreationInMemPool(CTxMemPool& pool, const CTransaction& tx);
    bool checkCertificateInMemPool(CTxMemPool& pool, const CTransaction& tx);

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

    bool updateSidechainBalance(const uint256& scId, const CAmount& amount, int nHeight);
    CAmount getSidechainBalance(const uint256& scId);

    bool checkTransaction(const CTransaction& tx, CValidationState& state);
    bool checkMemPool(CTxMemPool& pool, const CTransaction& tx, CValidationState& state);
    bool checkSidechainForwardTransaction(const CTransaction& tx, CValidationState& state);
    bool checkSidechainBackwardTransaction(const CTransaction& tx, CValidationState& state);
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

    // print functions
    bool dump_info(const uint256& scId);
    void dump_info();
    bool fillJSON(const uint256& scId, UniValue& sc);
    void fillJSON(UniValue& result);
    void fillJSON(const uint256& scId, const ScInfo& info, UniValue& sc);
}; 

class ScVerifyDbGuard
{
  public:
    ScVerifyDbGuard()
    {
//        std::cout << "Locking sc db verify... >>>>>>>>>>>>>>>>>>" << std::endl;
        ScMgr::instance().verifyingDb(true);
    }

    ~ScVerifyDbGuard()
    {
//        std::cout << "<<<<<<<<<<<<<<<<<< Unlocking sc db verify..." << std::endl;
        ScMgr::instance().verifyingDb(false);
    }

  private:
    void *operator new(size_t size); //overloaded and private
    ScVerifyDbGuard(const ScVerifyDbGuard&) = delete;
    ScVerifyDbGuard(ScVerifyDbGuard&&) = delete;
    ScVerifyDbGuard& operator=(const ScVerifyDbGuard&) = delete;
    ScVerifyDbGuard& operator=(ScVerifyDbGuard&&) = delete;
}; 


class CRecipientFactory;

class CcRecipientVisitor : public boost::static_visitor<bool>
{
    private:
       CRecipientFactory* fact;
    public:
       explicit CcRecipientVisitor(CRecipientFactory* factIn) : fact(factIn) {}

    template <typename T>
    bool operator() (const T& r) const; //{ return fact->set(r); }
/*
    bool operator() (const CRecipientScCreation& r) const;
    bool operator() (const CRecipientCertLock& r) const;
    bool operator() (const CRecipientForwardTransfer& r) const;
    bool operator() (const CRecipientBackwardTransfer& r) const;
*/
};

class CRecipientFactory
{
    private:
       CMutableTransaction* tx;
       std::string& err;

    public:
       CRecipientFactory(CMutableTransaction* txIn, std::string& errIn)
           : tx(txIn), err(errIn) {}

    bool set(const CcRecipientVariant& rec)
    {
        return boost::apply_visitor(CcRecipientVisitor(this), rec);
    };

    bool set(const CRecipientScCreation& r);
    bool set(const CRecipientCertLock& r);
    bool set(const CRecipientForwardTransfer& r);
    bool set(const CRecipientBackwardTransfer& r);
};

class CcRecipientAmountVisitor : public boost::static_visitor<CAmount>
{
    public:
    CAmount operator() (const CRecipientScCreation& r) const
    {
        // creation fee are in standard vout of the tx, while fwd contributions are in apposite obj below
#if 1
        return SC_CREATION_FEE;
#else
        return 0;
#endif
    }

    CAmount operator() (const CRecipientCertLock& r) const { return r.nValue; }
    CAmount operator() (const CRecipientForwardTransfer& r) const { return r.nValue; }
    CAmount operator() (const CRecipientBackwardTransfer& r) const { return r.nValue; }
};

}; // end of namespace

#endif // _SIDECHAIN_H
