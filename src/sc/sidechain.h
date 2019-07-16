#ifndef _SIDECHAIN_H
#define _SIDECHAIN_H

#include <vector>

#include "amount.h"
#include "chain.h"
#include "hash.h"
#include <boost/unordered_map.hpp>

//------------------------------------------------------------------------------------
class CTxMemPool;

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
    ScInfo() : creationBlockIndex(NULL), creationTxIndex(-1), ownerTxHash(), balance(0) {}
    
    // reference to the block containing the tx that created the side chain 
    const CBlockIndex* creationBlockIndex;;
    // index of the creating tx in the block vect of txs
    int creationTxIndex;
    // hash of the tx who created it
    uint256 ownerTxHash;

    // total amount given by sum(fw transfer)
    CAmount balance;

    // list of certifiers
    std::vector<CRecipientCertLock> certifiers;

    // creation data
    ScCreationParameters creationData;

    std::string ToString() const;
};

typedef boost::unordered_map<uint256, ScInfo, ObjectHasher> ScInfoMap;

using ::CTxMemPool;

class ScMgr
{
  private:
    ScMgr(); // Disallow instantiation outside of the class.
    ScInfoMap mScInfo;

    typedef boost::unordered_map<uint256, std::vector<CRecipientForwardTransfer>, ObjectHasher> ScFwdTransfers;
    ScFwdTransfers _cachedFwTransfers;

  public:
    static ScMgr& instance();

    ScMgr(const ScMgr&) = delete;
    ScMgr& operator=(const ScMgr &) = delete;
    ScMgr(ScMgr &&) = delete;
    ScMgr & operator=(ScMgr &&) = delete;

    void addSidechain(const uint256& id, ScInfo& info);
    void removeSidechain(const uint256& id);

    bool sidechainExists(const uint256& id);

    bool addBlockScTransactions(const CBlock& block, const CBlockIndex* pindex);
    bool removeBlockScTransactions(const CBlock& block);

    bool updateSidechainBalance(const uint256& id, const CAmount& amount);
    CAmount getSidechainBalance(const uint256& id);

    bool getScInfo(const uint256& scId, ScInfo& info);
    bool checkSidechainTxCreation(const CTransaction& tx);
    bool checkCreationInMemPool(CTxMemPool& pool, const CTransaction& tx);

    // used at node startupwhen verifying blocks
    void addSidechainsAndCacheAmounts(const CBlock& block, const CBlockIndex* pindex);
    bool updateAmountsFromCache();

    void evalSendCreationFee(CMutableTransaction& tx);


    // dbg functions
    void dump_info();
    void dump_info(const uint256& scId);
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

typedef boost::variant<CRecipientScCreation, CRecipientCertLock, CRecipientForwardTransfer> CcRecipientVariant;

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
