#include "sc/sidechainrpc.h"
#include <univalue.h>
#include "primitives/transaction.h"


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

}

