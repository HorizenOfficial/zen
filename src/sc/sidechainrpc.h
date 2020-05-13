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

    void fill(std::vector<unsigned char>& vBytes, size_t nBytes) const;
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

class ScRpcCmd
{
  protected:
    // this is a reference to the tx that gets processed
    CMutableTransactionBase& _tx;

    // cmd params
    CBitcoinAddress _fromMcAddress;
    CBitcoinAddress _changeMcAddress;
    int _minConf;
    CAmount _fee;

    // internal members
    bool _hasFromAddress;
    bool _hasChangeAddress;
    CAmount _dustThreshold;
    CAmount _totalInputAmount;
    CAmount _totalOutputAmount;

    std::string _signedObjHex;

    // Input UTXO is a tuple (triple) of txid, vout, amount)
    typedef std::tuple<uint256, int, CAmount> SelectedUTXO;

  public:
    ScRpcCmd(
        CMutableTransactionBase& tx, 
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee);

    virtual void sign() = 0;
    virtual void send() = 0;    

    void addInputs();
    void addChange();
};

class ScRpcCmdTx : public ScRpcCmd
{
  public:
    struct sOutParams
    {
        uint256 _scid;
        uint256 _toScAddress;
        CAmount _nAmount;
        sOutParams(): _scid(), _toScAddress(), _nAmount(0) {}

        sOutParams(
            const uint256& scId, const uint256& toaddress, const CAmount nAmount):
            _scid(scId), _toScAddress(toaddress), _nAmount(nAmount) {}
    };

    // cmd params
    std::vector<sOutParams> _outParams;

    ScRpcCmdTx(
        CMutableTransaction& tx, const std::vector<sOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee);

    void sign() override;
    void send() override;    

    virtual void addCcOutputs() = 0;
};

class ScRpcCmdCert : public ScRpcCmd
{
    public:
    struct sBwdParams
    {
        CScript _scriptPubKey;
        CAmount _nAmount;
        sBwdParams(): _scriptPubKey(), _nAmount(0) {}

        sBwdParams(
            const CScript& spk, const CAmount nAmount):
            _scriptPubKey(spk), _nAmount(nAmount) {}
    };

    // cmd params
    std::vector<sBwdParams> _bwdParams;

    ScRpcCmdCert(
        CMutableScCertificate& cert, const std::vector<sBwdParams>& bwdParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee);

    void sign() override;
    void send() override;    

    void addBackwardTransfers();
};

class ScRpcCreationCmd : public ScRpcCmdTx
{
  private:
    // cmd params
    ScCreationParameters _creationData;

  public:
    ScRpcCreationCmd(
        CMutableTransaction& tx, const std::vector<sOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee, const ScCreationParameters& cd);

    void addCcOutputs() override;
};

class ScRpcSendCmd : public ScRpcCmdTx
{
  public:
    ScRpcSendCmd(
        CMutableTransaction& tx, const std::vector<sOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee);

    void addCcOutputs() override;
};

bool FillCcOutput(CMutableTransaction& tx, std::vector<Sidechain::CcRecipientVariant> vecCcSend, std::string& strFailReason);

}; // end of namespace

#endif // _SIDECHAIN_RPC_H
