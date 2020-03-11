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
class CTransactionBase;
class CMutableTransaction;
class CMutableTransactionBase;
class CSidechain;

namespace Sidechain
{

// utility class for handling custom data in sc
class CScCustomData : public base_blob<MAX_CUSTOM_DATA_BITS> {
public:
    CScCustomData() {}
    CScCustomData(const base_blob<MAX_CUSTOM_DATA_BITS>& b) : base_blob<MAX_CUSTOM_DATA_BITS>(b) {}
    explicit CScCustomData(const std::vector<unsigned char>& vch) : base_blob<MAX_CUSTOM_DATA_BITS>(vch) {}

    void fill(std::vector<unsigned char>& vBytes, int nBytes) const;
};

class CRecipientHandler
{
    private:
       CMutableTransactionBase* txBase;
       std::string& err;

    public:
       CRecipientHandler(CMutableTransactionBase* objIn, std::string& errIn)
           : txBase(objIn), err(errIn) {}

    bool visit(const CcRecipientVariant& rec);

    bool handle(const CRecipientScCreation& r);
    bool handle(const CRecipientCertLock& r);
    bool handle(const CRecipientForwardTransfer& r);
    bool handle(const CRecipientBackwardTransfer& r);
};

class CcRecipientVisitor : public boost::static_visitor<bool>
{
    private:
       CRecipientHandler* handler;
    public:
       explicit CcRecipientVisitor(CRecipientHandler* hIn) : handler(hIn) {}

    template <typename T>
    bool operator() (const T& r) const { return handler->handle(r); }
};

class CcRecipientAmountVisitor : public boost::static_visitor<CAmount>
{
    public:
    CAmount operator() (const CRecipientScCreation& r) const { return r.nValue; }
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

class ScRpcCreationCmd
{
  private:
    // this is a reference to the tx that gets processed
    CMutableTransaction& _tx;

    // cmd params
    uint256 _scid;
    ScCreationParameters _creationData;
    //int _withdrawalEpochLength;
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
        CMutableTransaction& tx, const uint256& scId, const ScCreationParameters& cd, const CBitcoinAddress& fromaddress,
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
