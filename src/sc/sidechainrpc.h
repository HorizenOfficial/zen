#ifndef _SIDECHAIN_RPC_H
#define _SIDECHAIN_RPC_H

#include "amount.h"

#include "sc/sidechaintypes.h"
#include "base58.h"

//------------------------------------------------------------------------------------
static const CAmount SC_RPC_OPERATION_DEFAULT_MINERS_FEE(1000);
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
void AddSidechainOutsToJSON (const CTransaction& tx, UniValue& parentObj);

// Parses an hex inputString and writes it into a vector vBytes of required size vSize. 
// If enforceStrictvSize is set to true, it will be checked that inputString.size()/2 == vSize,
// otherwise the check is relaxed to inputString.size()/2 <= vSize
bool AddScData(const std::string& inputString, std::vector<unsigned char>& vBytes, unsigned int vSize, bool enforceStrictvSize, std::string& error);

// used when creating a raw transaction with cc outputs
bool AddSidechainCreationOutputs(UniValue& sc_crs, CMutableTransaction& rawTx, std::string& error);
bool AddSidechainForwardOutputs(UniValue& fwdtr, CMutableTransaction& rawTx, std::string& error);

// used when funding a raw tx
void fundCcRecipients(const CTransaction& tx,
    std::vector<CRecipientScCreation >& vecScSend, std::vector<CRecipientForwardTransfer >& vecFtSend);

class ScRpcCmd
{
  protected:
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

    void addInputs();
    void addChange();

    virtual void sign() = 0;
    virtual void send() = 0;    
    virtual void addOutput(const CTxOut& out) = 0;
    virtual void addInput(const CTxIn& out) = 0;

  public:
    virtual ~ScRpcCmd() {};

    ScRpcCmd(
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee);

    virtual void execute() = 0;

};

class ScRpcCmdTx : public ScRpcCmd
{
  protected:
    // this is a reference to the tx that gets processed
    CMutableTransaction& _tx;

    void addOutput(const CTxOut& out) override {_tx.addOut(out); }
    void addInput(const CTxIn& in) override    {_tx.vin.push_back(in); }

    virtual void addCcOutputs() = 0;

    void sign() override;
    void send() override;    

  public:
    ScRpcCmdTx(
        CMutableTransaction& tx,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee);

    void execute() override;

};

class ScRpcCmdCert : public ScRpcCmd
{
  protected:
    // this is a reference to the tx that gets processed
    CMutableScCertificate& _cert;

    void addOutput(const CTxOut& out) override {_cert.addOut(out); }
    void addInput(const CTxIn& in) override    {_cert.vin.push_back(in); }
  
    void sign() override;
    void send() override;    

  private:
    void addBackwardTransfers();

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

    void execute() override;
};


class ScRpcCreationCmdTx : public ScRpcCmdTx
{
  protected:
    void addCcOutputs() override;

  public:
    struct sCrOutParams
    {
        uint256 _toScAddress;
        CAmount _nAmount;
        sCrOutParams(): _toScAddress(), _nAmount(0) {}

        sCrOutParams(
            const uint256& toaddress, const CAmount nAmount):
            _toScAddress(toaddress), _nAmount(nAmount) {}
    };

    // cmd params
    std::vector<sCrOutParams> _outParams;
    ScCreationParameters _creationData;

    ScRpcCreationCmdTx(
        CMutableTransaction& tx, const std::vector<sCrOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee, const ScCreationParameters& cd);
};

class ScRpcSendCmdTx : public ScRpcCmdTx
{
  protected:
    void addCcOutputs() override;

  public:
    struct sFtOutParams
    {
        uint256 _scid;
        uint256 _toScAddress;
        CAmount _nAmount;
        sFtOutParams(): _scid(), _toScAddress(), _nAmount(0) {}

        sFtOutParams(
            const uint256& scId, const uint256& toaddress, const CAmount nAmount):
            _scid(scId), _toScAddress(toaddress), _nAmount(nAmount) {}
    };

    // cmd params
    std::vector<sFtOutParams> _outParams;

    ScRpcSendCmdTx(
        CMutableTransaction& tx, const std::vector<sFtOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee);
};

}; // end of namespace

#endif // _SIDECHAIN_RPC_H
