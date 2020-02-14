#include "sc/sidechainrpc.h"
#include "sc/sidechain.h"
#include <univalue.h>
#include "primitives/transaction.h"
#include <boost/foreach.hpp>
#include <rpc/protocol.h>
#include "utilmoneystr.h"

#include <wallet/wallet.h>

#include <core_io.h>
#include <rpc/server.h>
#include <main.h>
#include <init.h>

extern UniValue ValueFromAmount(const CAmount& amount);
extern CAmount AmountFromValue(const UniValue& value);

namespace Sidechain
{

void AddSidechainOutsToJSON (const CTransaction& tx, UniValue& parentObj)
{
    UniValue vscs(UniValue::VARR);
    // global idx
    unsigned int nIdx = 0; 

    for (unsigned int i = 0; i < tx.vsc_ccout.size(); i++) {
        const CTxScCreationOut& out = tx.vsc_ccout[i];
        UniValue o(UniValue::VOBJ);
        o.push_back(Pair("scid", out.scId.GetHex()));
        o.push_back(Pair("n", (int64_t)nIdx));
        o.push_back(Pair("withdrawal epoch length", (int)out.withdrawalEpochLength));
        vscs.push_back(o);
        nIdx++;
    }
    parentObj.push_back(Pair("vsc_ccout", vscs));

    UniValue vcls(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vcl_ccout.size(); i++) {
        const CTxCertifierLockOut& out = tx.vcl_ccout[i];
        UniValue o(UniValue::VOBJ);
        o.push_back(Pair("scid", out.scId.GetHex()));
        o.push_back(Pair("value", ValueFromAmount(out.nValue)));
        o.push_back(Pair("n", (int64_t)nIdx));
        o.push_back(Pair("address", out.address.GetHex()));
        o.push_back(Pair("active from withdrawal epoch", out.activeFromWithdrawalEpoch));
        vcls.push_back(o);
        nIdx++;
    }
    parentObj.push_back(Pair("vcl_ccout", vcls));

    UniValue vfts(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vft_ccout.size(); i++) {
        const CTxForwardTransferOut& out = tx.vft_ccout[i];
        UniValue o(UniValue::VOBJ);
        o.push_back(Pair("scid", out.scId.GetHex()));
        o.push_back(Pair("value", ValueFromAmount(out.nValue)));
        o.push_back(Pair("n", (int64_t)nIdx));
        o.push_back(Pair("address", out.address.GetHex()));
        vfts.push_back(o);
        nIdx++;
    }
    parentObj.push_back(Pair("vft_ccout", vfts));
}

bool AddSidechainCreationOutputs(UniValue& sc_crs, CMutableTransaction& rawTx, std::string& error)
{
    rawTx.nVersion = SC_TX_VERSION;

    for (size_t i = 0; i < sc_crs.size(); i++)
    {
        const UniValue& input = sc_crs[i];
        const UniValue& o = input.get_obj();
 
        std::string inputString = find_value(o, "scid").get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid scid format: not an hex";
            return false;
        }
    
        uint256 scId;
        scId.SetHex(inputString);
 
        const UniValue& sh_v = find_value(o, "epoch_length");
        if (!sh_v.isNum())
        {
            error = "Invalid parameter, missing epoch_length key";
            return false;
        }
        int nHeight = sh_v.get_int();
        if (nHeight < 0)
        {
            error = "Invalid parameter, epoch_length must be positive";
            return false;
        }
 
        CTxScCreationOut txccout(scId, nHeight);
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

        CTxForwardTransferOut txccout(nAmount, address, scId);
        rawTx.vft_ccout.push_back(txccout);
    }

    return true;
}

void fundCcRecipients(const CTransaction& tx, std::vector<CcRecipientVariant >& vecCcSend)
{
    BOOST_FOREACH(const auto& entry, tx.vsc_ccout)
    {
        CRecipientScCreation sc;
        sc.scId = entry.scId;
        // when funding a tx with sc creation, the amount is already contained in vcout to foundation
        sc.creationData.withdrawalEpochLength = entry.withdrawalEpochLength;

        vecCcSend.push_back(CcRecipientVariant(sc));
    }

    BOOST_FOREACH(const auto& entry, tx.vcl_ccout)
    {
        CRecipientCertLock cl;
        cl.scId = entry.scId;
        cl.nValue = entry.nValue;
        cl.address = entry.address;
        cl.epoch = entry.activeFromWithdrawalEpoch;

        vecCcSend.push_back(CcRecipientVariant(cl));
    }

    BOOST_FOREACH(const auto& entry, tx.vft_ccout)
    {
        CRecipientForwardTransfer ft;
        ft.scId = entry.scId;
        ft.address = entry.address;
        ft.nValue = entry.nValue;

        vecCcSend.push_back(CcRecipientVariant(ft));
    }
}

void AddScInfoToJSON(const uint256& scId, const ScInfo& info, UniValue& sc)
{
    sc.push_back(Pair("scid", scId.GetHex()));
    sc.push_back(Pair("balance", ValueFromAmount(info.balance)));
    sc.push_back(Pair("creating tx hash", info.creationTxHash.GetHex()));
    sc.push_back(Pair("created in block", info.creationBlockHash.ToString()));
    sc.push_back(Pair("created at block height", info.creationBlockHeight));
    // creation parameters
    sc.push_back(Pair("withdrawalEpochLength", info.creationData.withdrawalEpochLength));

    UniValue ia(UniValue::VARR);
    BOOST_FOREACH(const auto& entry, info.mImmatureAmounts)
    {
        UniValue o(UniValue::VOBJ);
        o.push_back(Pair("maturityHeight", entry.first));
        o.push_back(Pair("amount", ValueFromAmount(entry.second)));
        ia.push_back(o);
    }
    sc.push_back(Pair("immature amounts", ia));
}

bool AddScInfoToJSON(const uint256& scId, UniValue& sc)
{
    ScInfo info;
    if (!ScMgr::instance().getScInfo(scId, info) )
    {
        LogPrint("sc", "scid[%s] not yet created\n", scId.ToString() );
        return false; 
    }
 
    AddScInfoToJSON(scId, info, sc);
    return true;
}

void AddScInfoToJSON(UniValue& result)
{
    std::set<uint256> sScIds = ScMgr::instance().getScIdSet();

    BOOST_FOREACH(const auto& entry, sScIds)
    {
        UniValue sc(UniValue::VOBJ);
        AddScInfoToJSON(entry, sc);
        result.push_back(sc);
    }
}

ScRpcCreationCmd::ScRpcCreationCmd(
        CMutableTransaction& tx, const uint256& scid, int withdrawalEpochLength, const CBitcoinAddress& fromaddress,
        const CBitcoinAddress& chaddr, const uint256& toaddress, const CAmount nAmount, int nMinConf, const CAmount& nFee):
        _tx(tx), _scid(scid), _withdrawalEpochLength(withdrawalEpochLength), _fromMcAddress(fromaddress),
        _changeMcAddress(chaddr), _toScAddress(toaddress), _nAmount(nAmount), _minConf(nMinConf), _fee(nFee)
{
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

void ScRpcCreationCmd::addInputs()
{
    std::vector<COutput> vAvailableCoins;
    std::vector<SelectedUTXO> vInputUtxo;

    static const bool fOnlyConfirmed = false;
    static const bool fIncludeZeroValue = false;
    static const bool fIncludeCoinBase = true;
    static const bool fIncludeCommunityFund = true;

    pwalletMain->AvailableCoins(vAvailableCoins, fOnlyConfirmed, NULL, fIncludeZeroValue, fIncludeCoinBase, fIncludeCommunityFund);

    for (const auto& out: vAvailableCoins)
    {
        LogPrint("sc", "utxo %s depth: %d, val: %s, spendable: %s\n",
            out.tx->GetHash().ToString(), out.nDepth, FormatMoney(out.tx->vout[out.i].nValue), out.fSpendable?"Y":"N");

        if (!out.fSpendable || out.nDepth < _minConf) {
            continue;
        }

        if (_hasFromAddress)
        {
            CTxDestination dest;
            if (!ExtractDestination(out.tx->vout[out.i].scriptPubKey, dest)) {
                continue;
            }

            if (!(CBitcoinAddress(dest) == _fromMcAddress)) {
                continue;
            }
        }

        CAmount nValue = out.tx->vout[out.i].nValue;

        SelectedUTXO utxo(out.tx->GetHash(), out.i, nValue);
        vInputUtxo.push_back(utxo);
    }

    // sort in ascending order, so smaller utxos appear first
    std::sort(vInputUtxo.begin(), vInputUtxo.end(), [](SelectedUTXO i, SelectedUTXO j) -> bool {
        return ( std::get<2>(i) < std::get<2>(j));
    });

    CAmount targetAmount = _nAmount + _fee;

    CAmount dustChange = -1;

    std::vector<SelectedUTXO> vSelectedInputUTXO;

    for (const SelectedUTXO & t : vInputUtxo)
    {
        _totalInputAmount += std::get<2>(t);
        vSelectedInputUTXO.push_back(t);

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

void ScRpcCreationCmd::addChange()
{
    CAmount change = _totalInputAmount - ( _nAmount + _fee);

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
        CTxOut out(change, scriptPubKey);
        _tx.vout.push_back(out);
    }
}

void ScRpcCreationCmd::addCcOutputs()
{
    std::vector<CcRecipientVariant> vecCcSend;

    // creation output
    CRecipientScCreation sc;
    sc.scId = _scid;
    sc.creationData.withdrawalEpochLength = _withdrawalEpochLength;

    vecCcSend.push_back(CcRecipientVariant(sc));

    // fwd output
    CRecipientForwardTransfer ft;
    ft.scId = _scid;
    ft.address = _toScAddress;
    ft.nValue = _nAmount;

    vecCcSend.push_back(CcRecipientVariant(ft));

    std::string strFailReason;
    if (!FillCcOutput(_tx, vecCcSend, strFailReason))
    {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Could not build cc output! %s", strFailReason.c_str()));
    }
}

void ScRpcCreationCmd::signTx()
{
    std::string rawtxn;
    try
    {
        rawtxn = EncodeHexTx(_tx);
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
    _signedTxHex = hexValue.get_str();

    CTransaction txStreamed;
    try
    {
        // Keep the signed transaction so we can hash to the same txid
        CDataStream stream(ParseHex(_signedTxHex), SER_NETWORK, PROTOCOL_VERSION);
        stream >> txStreamed;
    }
    catch(...)
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to parse transaction");
    }
    _tx = txStreamed;
}

void ScRpcCreationCmd::sendTx()
{
    UniValue val = UniValue(UniValue::VARR);
    val.push_back(_signedTxHex);

    UniValue sendResultValue = sendrawtransaction(val, false);
    if (sendResultValue.isNull())
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Send raw transaction did not return an error or a txid.");
    }

}

//--------------------------------------------------------------------------------------------
// Cross chain outputs

template <typename T>
bool CcRecipientVisitor::operator() (const T& r) const { return fact->set(r); }

bool CRecipientFactory::set(const CRecipientScCreation& r)
{
    CTxScCreationOut txccout(r.scId, r.creationData.withdrawalEpochLength);
    // no dust can be found in sc creation
    tx->vsc_ccout.push_back(txccout);
    return true;
}

bool CRecipientFactory::set(const CRecipientCertLock& r)
{
    CTxCertifierLockOut txccout(r.nValue, r.address, r.scId, r.epoch);
    if (txccout.IsDust(::minRelayTxFee))
    {
        err = _("Transaction amount too small");
        return false;
    }
    tx->vcl_ccout.push_back(txccout);
    return true;
}

bool CRecipientFactory::set(const CRecipientForwardTransfer& r)
{
    CTxForwardTransferOut txccout(r.nValue, r.address, r.scId);
    if (txccout.IsDust(::minRelayTxFee))
    {
        err = _("Transaction amount too small");
        return false;
    }
    tx->vft_ccout.push_back(txccout);
    return true;
}

bool CRecipientFactory::set(const CRecipientBackwardTransfer& r)
{
    // fill vout here but later their amount will be reduced carving out the fee by the caller
    CTxOut txout(r.nValue, r.scriptPubKey);

    tx->vout.push_back(txout);
    return true;
}

bool FillCcOutput(CMutableTransaction& tx, std::vector<Sidechain::CcRecipientVariant> vecCcSend, std::string& strFailReason)
{
    for (const auto& ccRecipient: vecCcSend)
    {
        CRecipientFactory fac(&tx, strFailReason);
        if (!fac.set(ccRecipient) )
        {
            return false;
        }
    }
    return true;
}

} // namespace Sidechain

