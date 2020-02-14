#ifndef _SIDECHAIN_RPC_H
#define _SIDECHAIN_RPC_H

#include "amount.h"

#include "sc/sidechaintypes.h"
#include "base58.h"

//------------------------------------------------------------------------------------
static const CAmount SC_RPC_OPERATION_DEFAULT_MINERS_FEE(10000);
static const int SC_RPC_OPERATION_DEFAULT_EPOCH_LENGTH(100);

class UniValue;
class CTransaction;
class CMutableTransaction;

namespace Sidechain
{

class ScInfo;
class CRecipientFactory;

class CcRecipientVisitor : public boost::static_visitor<bool>
{
    private:
       CRecipientFactory* fact;
    public:
       explicit CcRecipientVisitor(CRecipientFactory* factIn) : fact(factIn) {}

    template <typename T>
    bool operator() (const T& r) const; //{ return fact->set(r); }
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
        // fwd contributions are in apposite obj below
        return 0;
    }

    CAmount operator() (const CRecipientCertLock& r) const { return r.nValue; }
    CAmount operator() (const CRecipientForwardTransfer& r) const { return r.nValue; }
    CAmount operator() (const CRecipientBackwardTransfer& r) const { return r.nValue; }
};

// used in get tx family of rpc commands
void AddSidechainOutsToJSON (const CTransaction& tx, UniValue& parentObj);

// used when creating a raw transaction with cc outputs
bool AddSidechainCreationOutputs(UniValue& sc_crs, CMutableTransaction& rawTx, std::string& error);
bool AddSidechainForwardOutputs(UniValue& fwdtr, CMutableTransaction& rawTx, std::string& error);

// used when funding a raw tx 
void fundCcRecipients(const CTransaction& tx, std::vector<CcRecipientVariant>& vecCcSend);

// used in getscinfo rpc cmd
void AddScInfoToJSON(UniValue& result);
bool AddScInfoToJSON(const uint256& scId, UniValue& sc);
void AddScInfoToJSON(const uint256& scId, const ScInfo& info, UniValue& sc);

class ScRpcCreationCmd
{
  private:
    // this is a reference to the tx that gets processed
    CMutableTransaction& _tx;

    // cmd params
    uint256 _scid;
    int _withdrawalEpochLength;
    CBitcoinAddress _fromMcAddress;
    CBitcoinAddress _changeMcAddress;
    uint256 _toScAddress;
    CAmount _nAmount;
    int _minConf;
    CAmount _fee;

    // internal members
    bool _hasFromAddress;
    bool _hasChangeAddress;
    CAmount _dustThreshold;
    CAmount _totalInputAmount;
    std::string _signedTxHex;

    // Input UTXO is a tuple (triple) of txid, vout, amount)
    typedef std::tuple<uint256, int, CAmount> SelectedUTXO;

  public:
    ScRpcCreationCmd(
        CMutableTransaction& tx, const uint256& scId, int withdrawalEpochLength, const CBitcoinAddress& fromaddress,
        const CBitcoinAddress& changeaddress, const uint256& toaddress, const CAmount nAmount, int nMinConf, const CAmount& nFee);

    void addInputs();
    void addCcOutputs();
    void addChange();
    void signTx();
    void sendTx();    
};

bool FillCcOutput(CMutableTransaction& tx, std::vector<Sidechain::CcRecipientVariant> vecCcSend, std::string& strFailReason);

}; // end of namespace

#endif // _SIDECHAIN_RPC_H
