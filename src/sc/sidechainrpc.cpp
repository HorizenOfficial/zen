#include "sc/sidechainrpc.h"
#include <univalue.h>
#include "primitives/transaction.h"
#include <boost/foreach.hpp>

#include <rpc/protocol.h>
#include "utilmoneystr.h"
#include "uint256.h"

#include <wallet/wallet.h>

#include <core_io.h>
#include <rpc/server.h>
#include <main.h>
#include <init.h>

extern UniValue ValueFromAmount(const CAmount& amount);
extern CAmount AmountFromValue(const UniValue& value);
extern CFeeRate minRelayTxFee;

namespace Sidechain
{

void AddSidechainOutsToJSON (const CTransaction& tx, UniValue& parentObj)
{
    UniValue vscs(UniValue::VARR);
    // global idx
    unsigned int nIdx = 0;

    for (unsigned int i = 0; i < tx.GetVscCcOut().size(); i++) {
        const CTxScCreationOut& out = tx.GetVscCcOut()[i];
        UniValue o(UniValue::VOBJ);
        o.pushKV("scid", out.GetScId().GetHex());
        o.pushKV("n", (int64_t)nIdx);
        o.pushKV("withdrawal epoch length", (int)out.withdrawalEpochLength);
        o.pushKV("value", ValueFromAmount(out.nValue));
        o.pushKV("address", out.address.GetHex());
        o.pushKV("wCertVk", HexStr(out.wCertVk));
        o.pushKV("customData", HexStr(out.customData));
        o.pushKV("constant", HexStr(out.constant));
        vscs.push_back(o);
        nIdx++;
    }
    parentObj.pushKV("vsc_ccout", vscs);

    UniValue vfts(UniValue::VARR);
    for (unsigned int i = 0; i < tx.GetVftCcOut().size(); i++) {
        const CTxForwardTransferOut& out = tx.GetVftCcOut()[i];
        UniValue o(UniValue::VOBJ);
        o.pushKV("scid", out.scId.GetHex());
        o.pushKV("value", ValueFromAmount(out.nValue));
        o.pushKV("n", (int64_t)nIdx);
        o.pushKV("address", out.address.GetHex());
        vfts.push_back(o);
        nIdx++;
    }
    parentObj.pushKV("vft_ccout", vfts);
}


bool AddScData(const std::string& inputString, std::vector<unsigned char>& vBytes, unsigned int vSize, bool enforceStrictSize, std::string& error)
{ 

    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
    {
        error = std::string("Invalid format: not an hex");
        return false;
    }

    unsigned int dataLen = inputString.length();

    if (dataLen%2)
    {
        error = strprintf("Invalid length %d, must be even (byte string)", dataLen);
        return false;
    }

    unsigned int scDataLen = dataLen/2;

    if(enforceStrictSize && (scDataLen != vSize))
    {
        error = strprintf("Invalid length %d, must be %d bytes", scDataLen, vSize);
        return false;
    }

    if (!enforceStrictSize && (scDataLen > vSize))
    {
        error = strprintf("Invalid length %d, must be %d bytes at most", scDataLen, vSize);
        return false;
    }

    vBytes = ParseHex(inputString);
    assert(vBytes.size() == scDataLen);

    return true;
}

bool AddSidechainCreationOutputs(UniValue& sc_crs, CMutableTransaction& rawTx, std::string& error)
{
    rawTx.nVersion = SC_TX_VERSION;

    for (size_t i = 0; i < sc_crs.size(); i++)
    {
        ScCreationParameters sc;

        const UniValue& input = sc_crs[i];
        const UniValue& o = input.get_obj();

        const UniValue& sh_v = find_value(o, "epoch_length");
        if (sh_v.isNull() || !sh_v.isNum())
        {
            error = "Invalid parameter or missing epoch_length key";
            return false;
        }
        int el = sh_v.get_int();
        if (el < 0)
        {
            error = "Invalid parameter, epoch_length must be positive";
            return false;
        }

        sc.withdrawalEpochLength = el;

        const UniValue& av = find_value(o, "amount");
        if (av.isNull())
        {
            error = "Missing mandatory parameter amount";
            return false;
        }
        CAmount nAmount = AmountFromValue( av );
        if (nAmount < 0)
        {
            error = "Invalid parameter, amount must be positive";
            return false;
        }

        const UniValue& adv = find_value(o, "address");
        if (adv.isNull())
        {
            error = "Missing mandatory parameter address";
            return false;
        }

        const std::string& inputString = adv.get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid address format: not an hex";
            return false;
        }

        uint256 address;
        address.SetHex(inputString);

        const UniValue& wCertVk = find_value(o, "wCertVk");
        if (wCertVk.isNull())
        {
            error = "Missing mandatory parameter wCertVk";
            return false;
        }
        else
        {
            const std::string& inputString = wCertVk.get_str();
            std::vector<unsigned char> wCertVkVec;
            if (!AddScData(inputString, wCertVkVec, SC_VK_SIZE, true, error))
            {
                error = "wCertVk: " + error;
                return false;
            }

            sc.wCertVk = libzendoomc::ScVk(wCertVkVec);

            if (!libzendoomc::IsValidScVk(sc.wCertVk))
            {
                error = "invalid wCertVk";
                return false;
            }
        }
        
        const UniValue& cd = find_value(o, "customData");
        if (!cd.isNull())
        {
            const std::string& inputString = cd.get_str();
            if (!AddScData(inputString, sc.customData, MAX_SC_DATA_LEN, false, error))
            {
                error = "customData: " + error;
                return false;
            }
        }

        const UniValue& constant = find_value(o, "constant");
        if (!constant.isNull())
        {
            const std::string& inputString = constant.get_str();
            if (!AddScData(inputString, sc.constant, SC_FIELD_SIZE, false, error))
            {
                error = "constant: " + error;
                return false;
            }

            if (!libzendoomc::IsValidScConstant(sc.constant))
            {
                error = "invalid constant";
                return false;
            }
        }

        CTxScCreationOut txccout(nAmount, address, sc);

        rawTx.vsc_ccout.push_back(txccout);
    }

    return true;
}

bool AddSidechainForwardOutputs(UniValue& fwdtr, CMutableTransaction& rawTx, std::string& error)
{
    rawTx.nVersion = SC_TX_VERSION;

    for (size_t j = 0; j < fwdtr.size(); j++)
    {
        const UniValue& input = fwdtr[j];
        const UniValue& o = input.get_obj();

        std::string inputString = find_value(o, "scid").get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid scid format: not an hex";
            return false;
        }

        uint256 scId;
        scId.SetHex(inputString);

        const UniValue& av = find_value(o, "amount");
        CAmount nAmount = AmountFromValue( av );
        if (nAmount < 0)
        {
            error = "Invalid parameter, amount must be positive";
            return false;
        }

        inputString = find_value(o, "address").get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid address format: not an hex";
            return false;
        }

        uint256 address;
        address.SetHex(inputString);

        CTxForwardTransferOut txccout(scId, nAmount, address);
        rawTx.vft_ccout.push_back(txccout);
    }

    return true;
}

void fundCcRecipients(const CTransaction& tx, std::vector<CcRecipientVariant >& vecCcSend)
{
    BOOST_FOREACH(const auto& entry, tx.GetVscCcOut())
    {
        CRecipientScCreation sc;
        sc.nValue = entry.nValue;
        sc.address = entry.address;
        sc.creationData.withdrawalEpochLength = entry.withdrawalEpochLength;
        sc.creationData.wCertVk = entry.wCertVk;
        sc.creationData.customData = entry.customData;
        sc.creationData.constant = entry.constant;

        vecCcSend.push_back(CcRecipientVariant(sc));
    }

    BOOST_FOREACH(const auto& entry, tx.GetVftCcOut())
    {
        CRecipientForwardTransfer ft;
        ft.scId = entry.scId;
        ft.address = entry.address;
        ft.nValue = entry.nValue;

        vecCcSend.push_back(CcRecipientVariant(ft));
    }
}

//--------------------------------------------------------------------------------------------
// Cross chain outputs

bool CRecipientHandler::visit(const CcRecipientVariant& rec)
{
    return boost::apply_visitor(CcRecipientVisitor(this), rec);
};


bool CRecipientHandler::handle(const CRecipientScCreation& r)
{
    CTxScCreationOut txccout(r.nValue, r.address, r.creationData);
    // no dust can be found in sc creation
    return txBase->add(txccout);
};

bool CRecipientHandler::handle(const CRecipientForwardTransfer& r)
{
    CTxForwardTransferOut txccout(r.scId, r.nValue, r.address);
    if (txccout.IsDust(::minRelayTxFee))
    {
        err = _("Transaction amount too small");
        return false;
    }
    return txBase->add(txccout);
};

bool CRecipientHandler::handle(const CRecipientBackwardTransfer& r)
{
    CTxOut txout(r.nValue, r.scriptPubKey);
    return txBase->addBwt(txout);
};

bool FillCcOutput(CMutableTransaction& tx, std::vector<Sidechain::CcRecipientVariant> vecCcSend, std::string& strFailReason)
{
    for (const auto& ccRecipient: vecCcSend)
    {
        CRecipientHandler handler(&tx, strFailReason);
        if (!handler.visit(ccRecipient) )
        {
            return false;
        }
    }
    return true;
}

bool FillBackwardTransfer(CMutableScCertificate& tx, std::vector<Sidechain::CcRecipientVariant> vecCcSend, std::string& strFailReason)
{
    for (const auto& ccRecipient: vecCcSend)
    {
        CRecipientHandler handler(&tx, strFailReason);
        if (!handler.visit(ccRecipient) )
        {
            return false;
        }
    }
    return true;
}


ScRpcCmd::ScRpcCmd(
        CMutableTransactionBase& tx,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee): 
        _tx(tx), _fromMcAddress(fromaddress), _changeMcAddress(changeaddress), _minConf(minConf), _fee(nFee)
{
    _totalOutputAmount = 0;

    _hasFromAddress   = !(_fromMcAddress   == CBitcoinAddress());
    _hasChangeAddress = !(_changeMcAddress == CBitcoinAddress());

    // Get dust threshold
    CKey secret;
    secret.MakeNewKey(true);
    CScript scriptPubKey = GetScriptForDestination(secret.GetPubKey().GetID());
    CTxOut out(CAmount(1), scriptPubKey);
    _dustThreshold = out.GetDustThreshold(minRelayTxFee);

    _totalInputAmount = 0;
}

void ScRpcCmd::addInputs()
{
    if (_fee == 0)
    {
        LogPrint("sc", "%s():%d - No fee therefore no inputs are added to cert, exiting\n", __func__, __LINE__);
        return;
    }

    std::vector<COutput> vAvailableCoins;
    std::vector<SelectedUTXO> vInputUtxo;

    static const bool fOnlyConfirmed = false;
    static const bool fIncludeZeroValue = false;
    bool fProtectCoinbase = !Params().GetConsensus().fCoinbaseMustBeProtected;
    static const bool fIncludeCoinBase = fProtectCoinbase;
    static const bool fIncludeCommunityFund = fProtectCoinbase;

    pwalletMain->AvailableCoins(vAvailableCoins, fOnlyConfirmed, NULL, fIncludeZeroValue, fIncludeCoinBase, fIncludeCommunityFund);

    for (const auto& out: vAvailableCoins)
    {
        LogPrint("sc", "utxo %s depth: %5d, val: %12s, spendable: %s\n",
            out.tx->getTxBase()->GetHash().ToString(), out.nDepth, FormatMoney(out.tx->getTxBase()->GetVout()[out.pos].nValue), out.fSpendable?"Y":"N");

        if (!out.fSpendable || out.nDepth < _minConf) {
            continue;
        }

        if (_hasFromAddress)
        {
            CTxDestination dest;
            if (!ExtractDestination(out.tx->getTxBase()->GetVout()[out.pos].scriptPubKey, dest)) {
                continue;
            }

            if (!(CBitcoinAddress(dest) == _fromMcAddress)) {
                continue;
            }
        }

        CAmount nValue = out.tx->getTxBase()->GetVout()[out.pos].nValue;

        SelectedUTXO utxo(out.tx->getTxBase()->GetHash(), out.pos, nValue);
        vInputUtxo.push_back(utxo);
    }

    // sort in ascending order, so smaller utxos appear first
    std::sort(vInputUtxo.begin(), vInputUtxo.end(), [](SelectedUTXO i, SelectedUTXO j) -> bool {
        return ( std::get<2>(i) < std::get<2>(j));
    });

    CAmount targetAmount = _totalOutputAmount + _fee;

    CAmount dustChange = -1;

    std::vector<SelectedUTXO> vSelectedInputUTXO;

    for (const SelectedUTXO & t : vInputUtxo)
    {
        _totalInputAmount += std::get<2>(t);
        vSelectedInputUTXO.push_back(t);

        LogPrint("sc", "---> added tx %s val: %12s, vout.n: %d\n",
            std::get<0>(t).ToString(), FormatMoney(std::get<2>(t)), std::get<1>(t));

        if (_totalInputAmount >= targetAmount)
        {
            // Select another utxo if there is change less than the dust threshold.
            dustChange = _totalInputAmount - targetAmount;
            if (dustChange == 0 || dustChange >= _dustThreshold) {
                break;
            }
        }
    }

    if (_totalInputAmount < targetAmount)
    {
        std::string addrDetails;
        if (_hasFromAddress)
            addrDetails = strprintf(" for taddr[%s]", _fromMcAddress.ToString());

        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient transparent funds %s, have %s, need %s (minconf=%d)",
            addrDetails, FormatMoney(_totalInputAmount), FormatMoney(targetAmount), _minConf));
    }

    // If there is transparent change, is it valid or is it dust?
    if (dustChange < _dustThreshold && dustChange != 0) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient transparent funds, have %s, need %s more to avoid creating invalid change output %s (dust threshold is %s)",
            FormatMoney(_totalInputAmount), FormatMoney(_dustThreshold - dustChange), FormatMoney(dustChange), FormatMoney(_dustThreshold)));
    }

    // Check mempooltxinputlimit to avoid creating a transaction which the local mempool rejects
    size_t limit = (size_t)GetArg("-mempooltxinputlimit", 0);
    if (limit > 0)
    {
        size_t n = vSelectedInputUTXO.size();
        if (n > limit) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Too many transparent inputs %zu > limit %zu", n, limit));
        }
    }

    // update the transaction with these inputs
    for (const auto& t : vSelectedInputUTXO)
    {
        uint256 txid = std::get<0>(t);
        int vout = std::get<1>(t);

        CTxIn in(COutPoint(txid, vout));
        _tx.vin.push_back(in);
    }
}

void ScRpcCmd::addChange()
{
    CAmount change = _totalInputAmount - ( _totalOutputAmount + _fee);

    if (change > 0)
    {
        // handle the address for the change
        CScript scriptPubKey;
        if (_hasChangeAddress)
        {
            scriptPubKey = GetScriptForDestination(_changeMcAddress.Get());
        }
        else
        if (_hasFromAddress)
        {
            scriptPubKey = GetScriptForDestination(_fromMcAddress.Get());
        }
        else
        {
            CReserveKey keyChange(pwalletMain);
            CPubKey vchPubKey;

            if (!keyChange.GetReservedKey(vchPubKey))
                throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Could not generate a taddr to use as a change address"); // should never fail, as we just unlocked

            scriptPubKey = GetScriptForDestination(vchPubKey.GetID());
        }

        _tx.addOut(CTxOut(change, scriptPubKey));
    }
}

ScRpcCmdTx::ScRpcCmdTx(
        CMutableTransaction& tx,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee):
        ScRpcCmd(tx, fromaddress, changeaddress, minConf, nFee)
{
}

ScRpcCmdCert::ScRpcCmdCert(
        CMutableScCertificate& cert, const std::vector<sBwdParams>& bwdParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee):
        ScRpcCmd(cert, fromaddress, changeaddress, minConf, nFee),
        _bwdParams(bwdParams)
{
}

void ScRpcCmdCert::sign()
{
    std::string rawcert;
    try
    {
        CScCertificate toEncode((CMutableScCertificate&)_tx);
        rawcert = EncodeHexCert(toEncode);
        LogPrint("sc", "      toEncode[%s]\n", toEncode.GetHash().ToString());
        LogPrint("sc", "      toEncode: %s\n", toEncode.ToString());
    }
    catch(...)
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to encode certificate");
    }

    UniValue val = UniValue(UniValue::VARR);
    val.push_back(rawcert);

    UniValue signResultValue = signrawcertificate(val, false);

    UniValue signResultObject = signResultValue.get_obj();

    UniValue completeValue = find_value(signResultObject, "complete");
    if (!completeValue.get_bool())
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to sign transaction");
    }

    UniValue hexValue = find_value(signResultObject, "hex");
    if (hexValue.isNull())
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing hex data for signed transaction");
    }
    _signedObjHex = hexValue.get_str();

    CMutableScCertificate certStreamed;
    try
    {
        // Keep the signed certificate so we can hash to the same certid
        CDataStream stream(ParseHex(_signedObjHex), SER_NETWORK, PROTOCOL_VERSION);
        stream >> certStreamed;
    }
    catch(...)
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to parse certificate");
    }
    LogPrint("sc", "      pre _tx[%s]\n", _tx.GetHash().ToString());
    LogPrint("sc", "      pre _tx: %s\n", ((CMutableScCertificate&)_tx).ToString());
    LogPrint("sc", "cert streamed[%s]\n", certStreamed.GetHash().ToString());
    LogPrint("sc", "cert streamed: %s\n", certStreamed.ToString());
    _tx = certStreamed;
}

void ScRpcCmdCert::send()
{
    UniValue val = UniValue(UniValue::VARR);
    val.push_back(_signedObjHex);

    UniValue sendResultValue = sendrawcertificate(val, false);
    if (sendResultValue.isNull())
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Send raw transaction did not return an error or a txid.");
    }
    LogPrint("sc", "cert sent[%s]\n", sendResultValue.get_str());
}

void ScRpcCmdCert::addBackwardTransfers()
{
    std::vector<CcRecipientVariant> vecCcSend;

    for (const auto& entry : _bwdParams)
    {
        CRecipientBackwardTransfer bt;
        bt.scriptPubKey = entry._scriptPubKey;
        bt.nValue = entry._nAmount;

        vecCcSend.push_back(CcRecipientVariant(bt));
    }

    std::string strFailReason;
    if (!FillBackwardTransfer((CMutableScCertificate&)_tx, vecCcSend, strFailReason))
    {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Could not build backward transfers! %s", strFailReason.c_str()));
    }
}

void ScRpcCmdTx::sign()
{
    std::string rawtxn;
    try
    {
        rawtxn = EncodeHexTx((CMutableTransaction&)_tx);
    }
    catch(...)
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to encode transaction");
    }

    UniValue val = UniValue(UniValue::VARR);
    val.push_back(rawtxn);

    UniValue signResultValue = signrawtransaction(val, false);

    UniValue signResultObject = signResultValue.get_obj();

    UniValue completeValue = find_value(signResultObject, "complete");
    if (!completeValue.get_bool())
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to sign transaction");
    }

    UniValue hexValue = find_value(signResultObject, "hex");
    if (hexValue.isNull())
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing hex data for signed transaction");
    }
    _signedObjHex = hexValue.get_str();

    CMutableTransaction txStreamed;
    try
    {
        // Keep the signed transaction so we can hash to the same txid
        CDataStream stream(ParseHex(_signedObjHex), SER_NETWORK, PROTOCOL_VERSION);
        stream >> txStreamed;
    }
    catch(...)
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to parse transaction");
    }
    _tx = txStreamed;
}

void ScRpcCmdTx::send()
{
    UniValue val = UniValue(UniValue::VARR);
    val.push_back(_signedObjHex);

    UniValue sendResultValue = sendrawtransaction(val, false);
    if (sendResultValue.isNull())
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Send raw transaction did not return an error or a txid.");
    }
}

ScRpcCreationCmd::ScRpcCreationCmd(
        CMutableTransaction& tx, const std::vector<sCrOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee, const ScCreationParameters& cd):
        ScRpcCmdTx(tx, fromaddress, changeaddress, minConf, nFee), _creationData(cd), _outParams(outParams)
{
    for (const auto& entry : _outParams)
    {
        _totalOutputAmount += entry._nAmount;
    }
} 

void ScRpcCreationCmd::addCcOutputs()
{
    if (_outParams.size() != 1)
    {
        // creation has just one output param
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("invalid number of output: %d!", _outParams.size()));
    }

    std::vector<CcRecipientVariant> vecCcSend;

    // creation output
    CRecipientScCreation sc;
    sc.address = _outParams[0]._toScAddress;
    sc.nValue = _outParams[0]._nAmount;
    sc.creationData = _creationData;

    vecCcSend.push_back(CcRecipientVariant(sc));

    std::string strFailReason;
    if (!FillCcOutput((CMutableTransaction&)_tx, vecCcSend, strFailReason))
    {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Could not build cc output! %s", strFailReason.c_str()));
    }
}

ScRpcSendCmd::ScRpcSendCmd(
        CMutableTransaction& tx, const std::vector<sFtOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee):
        ScRpcCmdTx(tx, fromaddress, changeaddress, minConf, nFee), _outParams(outParams)
{
    for (const auto& entry : _outParams)
    {
        _totalOutputAmount += entry._nAmount;
    }
} 


void ScRpcSendCmd::addCcOutputs()
{
    if (_outParams.size() == 0)
    {
        // send cmd can not have empty output vector
        throw JSONRPCError(RPC_WALLET_ERROR, "null number of output!");
    }

    std::vector<CcRecipientVariant> vecCcSend;

    for (const auto& entry : _outParams)
    {
        CRecipientForwardTransfer ft;
        ft.address = entry._toScAddress;
        ft.nValue = entry._nAmount;
        ft.scId = entry._scid;

        vecCcSend.push_back(CcRecipientVariant(ft));
    }

    std::string strFailReason;
    if (!FillCcOutput((CMutableTransaction&)_tx, vecCcSend, strFailReason))
    {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Could not build cc output! %s", strFailReason.c_str()));
    }
}
}  // end of namespace
