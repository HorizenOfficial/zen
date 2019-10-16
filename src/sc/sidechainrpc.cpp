#include "sc/sidechainrpc.h"
#include "sc/sidechain.h"
#include <univalue.h>
#include "primitives/transaction.h"
#include <boost/foreach.hpp>


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
    BOOST_FOREACH(auto& entry, tx.vsc_ccout)
    {
        CRecipientScCreation sc;
        sc.scId = entry.scId;
        // when funding a tx with sc creation, the amount is already contained in vcout to foundation
        sc.creationData.withdrawalEpochLength = entry.withdrawalEpochLength;

        vecCcSend.push_back(CcRecipientVariant(sc));
    }

    BOOST_FOREACH(auto& entry, tx.vcl_ccout)
    {
        CRecipientCertLock cl;
        cl.scId = entry.scId;
        cl.nValue = entry.nValue;
        cl.address = entry.address;
        cl.epoch = entry.activeFromWithdrawalEpoch;

        vecCcSend.push_back(CcRecipientVariant(cl));
    }

    BOOST_FOREACH(auto& entry, tx.vft_ccout)
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
    std::set<uint256> sScIds;

    ScMgr::instance().getScIdSet(sScIds);

    BOOST_FOREACH(const auto& entry, sScIds)
    {
        UniValue sc(UniValue::VOBJ);
        AddScInfoToJSON(entry, sc);
        result.push_back(sc);
    }
}


}

