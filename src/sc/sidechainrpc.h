#ifndef _SIDECHAIN_RPC_H
#define _SIDECHAIN_RPC_H

#include "amount.h"

#include "sc/sidechaintypes.h"
#include "base58.h"

//------------------------------------------------------------------------------------
static const CAmount SC_RPC_OPERATION_AUTO_MINERS_FEE(-1);
static const int SC_RPC_OPERATION_DEFAULT_EPOCH_LENGTH(100);

class UniValue;
class CTransaction;
class CTransactionBase;
class CMutableTransaction;
class CMutableTransactionBase;
class CSidechain;

namespace Sidechain
{

// used in get tx family of rpc commands
void AddCeasedSidechainWithdrawalInputsToJSON(const CTransaction& tx, UniValue& parentObj);
void AddSidechainOutsToJSON(const CTransaction& tx, UniValue& parentObj);

enum class CheckSizeMode {CHECK_OFF, CHECK_STRICT, CHECK_UPPER_LIMIT};
// Parses an hex inputString and writes it into a vector vBytes of required size vSize. 
// If enforceStrictvSize is set to true, it will be checked that inputString.size()/2 == vSize,
// otherwise the check is relaxed to inputString.size()/2 <= vSize
bool AddScData(
    const std::string& inputString, std::vector<unsigned char>& vBytes,
    unsigned int vSize, CheckSizeMode checkSizeMode, std::string& error);

bool AddCustomFieldElement(const std::string& inputString, std::vector<unsigned char>& vBytes,
    unsigned int vSize, std::string& errString);

bool AddScData(const UniValue& intArray, std::vector<FieldElementCertificateFieldConfig>& vCfg);

// used when creating a raw transaction with cc outputs
bool AddCeasedSidechainWithdrawalInputs(UniValue& csws, CMutableTransaction& rawTx, std::string& error);
bool AddSidechainCreationOutputs(UniValue& sc_crs, CMutableTransaction& rawTx, std::string& error);
bool AddSidechainForwardOutputs(UniValue& fwdtr, CMutableTransaction& rawTx, std::string& error);
bool AddSidechainBwtRequestOutputs(UniValue& bwtreq, CMutableTransaction& rawTx, std::string& error);

// used when funding a raw tx
void fundCcRecipients(const CTransaction& tx,
    std::vector<CRecipientScCreation >& vecScSend, std::vector<CRecipientForwardTransfer >& vecFtSend,
    std::vector<CRecipientBwtRequest>& vecBwtRequest);

class ScRpcCmd
{
  protected:
    // cmd params
    CBitcoinAddress _fromMcAddress;
    CBitcoinAddress _changeMcAddress;
    int _minConf;
    CAmount _fee;
    CAmount _feeNeeded;
    bool _automaticFee;

    // internal members
    bool _hasFromAddress;
    bool _hasChangeAddress;
    CAmount _dustThreshold;
    CAmount _totalInputAmount;
    CAmount _totalOutputAmount;

    std::string _signedObjHex;

    // Input UTXO is a tuple (triple) of txid, vout, amount)
    typedef std::tuple<uint256, int, CAmount> SelectedUTXO;

    // set null all data members that are filled during tx/cert construction  
    virtual void init();

    size_t addInputs(size_t availableBytes);
    size_t addChange(bool onlyComputeDummyChangeSize = false);
    virtual void addOutput(const CTxOut& out) = 0;
    virtual void addInput(const CTxIn& out) = 0;
    virtual void sign() = 0;

    // gathers all steps for building a tx/cert
    virtual void _execute() = 0;

    bool checkFeeRate();
    bool send();    

  public:
    virtual ~ScRpcCmd() {};

    ScRpcCmd(
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee);

    void execute();

    unsigned int getSignedObjSize() const { return _signedObjHex.size()/2; }
    virtual unsigned int getMaxObjSize() const = 0;
};

class ScRpcCmdTx : public ScRpcCmd
{
  protected:
    // this is a reference to the tx that gets processed
    CMutableTransaction& _tx;

    void init() override;
    void addOutput(const CTxOut& out) override {_tx.addOut(out); }
    void addInput(const CTxIn& in) override    {_tx.vin.push_back(in); }

    virtual size_t addCcOutputs() = 0;

    void sign() override;
    void _execute() override;


  public:
    ScRpcCmdTx(
        CMutableTransaction& tx,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee);

    unsigned int getMaxObjSize() const override { return MAX_TX_SIZE; }
};

class ScRpcCmdCert : public ScRpcCmd
{
  protected:
    // this is a reference to the certificate that gets processed
    CMutableScCertificate& _cert;

    void init() override;
    void addOutput(const CTxOut& out) override {_cert.addOut(out); }
    void addInput(const CTxIn& in) override    {_cert.vin.push_back(in); }
  
    void sign() override;

    void _execute() override;

  private:
    size_t addBackwardTransfers();
    void addCustomFields();
    void addScFees();

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
    std::vector<FieldElementCertificateField> _vCfe;
    std::vector<BitVectorCertificateField> _vCmt;
    CAmount _ftScFee;
    CAmount _mbtrScFee;

    ScRpcCmdCert(
        CMutableScCertificate& cert, const std::vector<sBwdParams>& bwdParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress, int minConf, const CAmount& nFee,
        const std::vector<FieldElementCertificateField>& vCfe, const std::vector<BitVectorCertificateField>& vCmt,
        const CAmount& ftScFee, const CAmount& mbtrScFee);

    unsigned int getMaxObjSize() const override { return MAX_CERT_SIZE; }
};


class ScRpcCreationCmdTx : public ScRpcCmdTx
{
  protected:
    size_t addCcOutputs() override;

  public:
    struct sCrOutParams
    {
        uint256 _toScAddress;
        CAmount _nAmount;

        sCrOutParams():
            _toScAddress(), _nAmount(0)
        {}

        sCrOutParams(const uint256& toaddress, const CAmount nAmount):
            _toScAddress(toaddress), _nAmount(nAmount)
        {}
    };

    // cmd params
    std::vector<sCrOutParams> _outParams;
    ScFixedParameters _fixedParams;
    CAmount _ftScFee;     /**< Forward Transfer sidechain fee */
    CAmount _mbtrScFee;   /**< Mainchain Backward Transfer Request sidechain fee */

    ScRpcCreationCmdTx(
        CMutableTransaction& tx, const std::vector<sCrOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee, const CAmount& ftScFee, const CAmount& mbtrScFee,
        const ScFixedParameters& cd);
};

class ScRpcSendCmdTx : public ScRpcCmdTx
{
  protected:
    size_t addCcOutputs() override;

  public:
    struct sFtOutParams
    {
        uint256 _scid;
        uint256 _toScAddress;
        CAmount _nAmount;
        uint160 _mcReturnAddress;
        sFtOutParams(): _scid(), _toScAddress(), _nAmount(0), _mcReturnAddress() {}

        sFtOutParams(
            const uint256& scId, const uint256& toaddress, const CAmount nAmount, const uint160& mcReturnAddress):
            _scid(scId), _toScAddress(toaddress), _nAmount(nAmount), _mcReturnAddress(mcReturnAddress) {}
    };

    // cmd params
    std::vector<sFtOutParams> _outParams;

    ScRpcSendCmdTx(
        CMutableTransaction& tx, const std::vector<sFtOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee);
};

class ScRpcRetrieveCmdTx : public ScRpcCmdTx
{
  protected:
    size_t addCcOutputs() override;

  public:
    struct sBtOutParams
    {
        uint256 _scid;
        uint160 _pkh;
        ScBwtRequestParameters _params;
        sBtOutParams(): _scid(), _pkh(), _params() {}

        sBtOutParams(
            const uint256& scId, const uint160& pkh, const ScBwtRequestParameters& params):
            _scid(scId), _pkh(pkh), _params(params) {}
    };

    // cmd params
    std::vector<sBtOutParams> _outParams;

    ScRpcRetrieveCmdTx(
        CMutableTransaction& tx, const std::vector<sBtOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee);
};

}; // end of namespace

#endif // _SIDECHAIN_RPC_H
