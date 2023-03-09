#include "sc/sidechainrpc.h"
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
extern void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);

namespace Sidechain
{

void AddCeasedSidechainWithdrawalInputsToJSON(const CTransaction& tx, UniValue& parentObj)
{
    UniValue vcsws(UniValue::VARR);
    for (const CTxCeasedSidechainWithdrawalInput& csw: tx.GetVcswCcIn())
    {
        UniValue o(UniValue::VOBJ);
        o.pushKV("value", ValueFromAmount(csw.nValue));
        o.pushKV("scId", csw.scId.GetHex());
        o.pushKV("nullifier", csw.nullifier.GetHexRepr());

        UniValue spk(UniValue::VOBJ);
        ScriptPubKeyToJSON(csw.scriptPubKey(), spk, true);
        o.pushKV("scriptPubKey", spk);

        o.pushKV("scProof", csw.scProof.GetHexRepr());

        UniValue rs(UniValue::VOBJ);
        rs.pushKV("asm", csw.redeemScript.ToString());
        rs.pushKV("hex", HexStr(csw.redeemScript));
        o.pushKV("redeemScript", rs);
        o.pushKV("actCertDataHash", csw.actCertDataHash.GetHexRepr());
        o.pushKV("ceasingCumScTxCommTree", csw.ceasingCumScTxCommTree.GetHexRepr());

        vcsws.push_back(o);
    }

    parentObj.pushKV("vcsw_ccin", vcsws);
}

void AddSidechainOutsToJSON(const CTransaction& tx, UniValue& parentObj)
{
    UniValue vscs(UniValue::VARR);
    // global idx
    unsigned int nIdx = 0;

    for (unsigned int i = 0; i < tx.GetVscCcOut().size(); i++) {
        const CTxScCreationOut& out = tx.GetVscCcOut()[i];
        UniValue o(UniValue::VOBJ);
        o.pushKV("scid", out.GetScId().GetHex());
        o.pushKV("n", (int64_t)nIdx);
        o.pushKV("version", out.version);
        o.pushKV("withdrawalEpochLength", (int)out.withdrawalEpochLength);
        o.pushKV("value", ValueFromAmount(out.nValue));
        o.pushKV("address", out.address.GetHex());
        o.pushKV("certProvingSystem", Sidechain::ProvingSystemTypeToString(out.wCertVk.getProvingSystemType()));
        o.pushKV("wCertVk", out.wCertVk.GetHexRepr());

        UniValue arrFieldElementConfig(UniValue::VARR);
        for(const auto& cfgEntry: out.vFieldElementCertificateFieldConfig)
        {
            arrFieldElementConfig.push_back(cfgEntry.getBitSize());
        }
        o.pushKV("vFieldElementCertificateFieldConfig", arrFieldElementConfig);

        UniValue arrBitVectorConfig(UniValue::VARR);
        for(const auto& cfgEntry: out.vBitVectorCertificateFieldConfig)
        {
            UniValue singlePair(UniValue::VARR);
            singlePair.push_back(cfgEntry.getBitVectorSizeBits());
            singlePair.push_back(cfgEntry.getMaxCompressedSizeBytes());
            arrBitVectorConfig.push_back(singlePair);
        }
        o.pushKV("vBitVectorCertificateFieldConfig", arrBitVectorConfig);

        o.pushKV("customData", HexStr(out.customData));
        if(out.constant.has_value())
            o.pushKV("constant", out.constant->GetHexRepr());
        if(out.wCeasedVk.has_value())
        {
            o.pushKV("cswProvingSystem", Sidechain::ProvingSystemTypeToString(out.wCeasedVk.value().getProvingSystemType()));
            o.pushKV("wCeasedVk", out.wCeasedVk.value().GetHexRepr());
        }
        o.pushKV("ftScFee", ValueFromAmount(out.forwardTransferScFee));
        o.pushKV("mbtrScFee", ValueFromAmount(out.mainchainBackwardTransferRequestScFee));
        o.pushKV("mbtrRequestDataLength", out.mainchainBackwardTransferRequestDataLength);
        vscs.push_back(o);
        nIdx++;
    }
    parentObj.pushKV("vsc_ccout", vscs);

    UniValue vfts(UniValue::VARR);
    for (unsigned int i = 0; i < tx.GetVftCcOut().size(); i++) {
        const CTxForwardTransferOut& out = tx.GetVftCcOut()[i];
        UniValue o(UniValue::VOBJ);
        o.pushKV("scid", out.scId.GetHex());
        o.pushKV("n", (int64_t)nIdx);
        o.pushKV("value", ValueFromAmount(out.nValue));
        o.pushKV("address", out.address.GetHex());

        std::string taddrStr = "Invalid taddress";
        uint160 pkeyValue = out.mcReturnAddress;
        CKeyID keyID(pkeyValue);
        CBitcoinAddress taddr(keyID);
        if (taddr.IsValid()) {
            taddrStr = taddr.ToString();
        }
        o.pushKV("mcReturnAddress", taddrStr);

        vfts.push_back(o);
        nIdx++;
    }
    parentObj.pushKV("vft_ccout", vfts);

    UniValue vbts(UniValue::VARR);
    for (unsigned int i = 0; i < tx.GetVBwtRequestOut().size(); i++) {
        const CBwtRequestOut& out = tx.GetVBwtRequestOut()[i];
        UniValue o(UniValue::VOBJ);
        o.pushKV("scid", out.GetScId().GetHex());
        o.pushKV("n", (int64_t)nIdx);

        std::string taddrStr = "Invalid taddress";
        uint160 pkeyValue = out.mcDestinationAddress;
        CKeyID keyID(pkeyValue);
        CBitcoinAddress taddr(keyID);
        if (taddr.IsValid()) {
            taddrStr = taddr.ToString();
        }
        o.pushKV("mcDestinationAddress", taddrStr);

        o.pushKV("scFee", ValueFromAmount(out.GetScValue()));

        UniValue arrRequestData(UniValue::VARR);
        for(const auto& requestData: out.vScRequestData)
        {
            arrRequestData.push_back(requestData.GetHexRepr());
        }
        o.pushKV("vScRequestData", arrRequestData);
        vbts.push_back(o);
        nIdx++;
    }
    parentObj.pushKV("vmbtr_out", vbts);
}

bool AddCustomFieldElement(const std::string& inputString, std::vector<unsigned char>& vBytes,
    unsigned int nBytes, std::string& errString)
{
    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
    {
        errString = std::string("Invalid format: not an hex");
        return false;
    }

    unsigned int dataLen = inputString.length();

    if (dataLen%2)
    {
        errString = strprintf("Invalid length %d, must be even (byte string)", dataLen);
        return false;
    }

    unsigned int scDataLen = dataLen/2;

    if(scDataLen > nBytes)
    {
        errString = strprintf("Invalid length %d, must be %d bytes at most", scDataLen, nBytes);
        return false;
    }

    vBytes = ParseHex(inputString);
    assert(vBytes.size() == scDataLen);

    return true;
}

bool AddScData(
    const std::string& inputString, std::vector<unsigned char>& vBytes, unsigned int vSize,
    CheckSizeMode checkSizeMode, std::string& error)
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

    if (checkSizeMode == CheckSizeMode::CHECK_STRICT && (scDataLen != vSize))
    {
        error = strprintf("Invalid length %d, must be %d bytes", scDataLen, vSize);
        return false;
    }

    if (checkSizeMode == CheckSizeMode::CHECK_UPPER_LIMIT && (scDataLen > vSize))
    {
        error = strprintf("Invalid length %d, must be %d bytes at most", scDataLen, vSize);
        return false;
    }

    vBytes = ParseHex(inputString);
    assert(vBytes.size() == scDataLen);

    return true;
}

bool AddScData(const UniValue& intArray, std::vector<FieldElementCertificateFieldConfig>& vCfg)
{ 
    if (intArray.size() != 0)
    {
        for (const UniValue& o : intArray.getValues())
        {
            if (!o.isNum())
            {
                return false;
            }
            int intVal = o.get_int();
            if (intVal <= 0 || intVal > UINT8_MAX)
            {
                return false;
            }
            vCfg.push_back(static_cast<uint8_t>(intVal));
        }
    }
    return true;
}

bool AddCeasedSidechainWithdrawalInputs(UniValue &csws, CMutableTransaction &rawTx, std::string &error)
{
    rawTx.nVersion = SC_TX_VERSION;

    for (size_t i = 0; i < csws.size(); i++)
    {
        const UniValue& input = csws[i];
        const UniValue& o = input.get_obj();

        // parse amount
        const UniValue& amount_v = find_value(o, "amount");
        if (amount_v.isNull())
        {
            error = "Missing mandatory parameter \"amount\" for the ceased sidechain withdrawal input";
            return false;
        }
        CAmount amount = AmountFromValue(amount_v);
        if (amount < 0)
        {
            error = "Invalid ceased sidechain withdrawal input parameter: \"amount\" must be positive";
            return false;
        }

        // parse sender address and get public key hash
        const UniValue& sender_v = find_value(o, "senderAddress");
        if (sender_v.isNull())
        {
            error = "Missing mandatory parameter \"senderAddress\" for the ceased sidechain withdrawal input";
            return false;
        }
        CBitcoinAddress senderAddress(sender_v.get_str());
        if (!senderAddress.IsValid())
        {
            error = "Invalid ceased sidechain withdrawal input \"senderAddress\" parameter";
            return false;
        }

        CKeyID pubKeyHash;
        if(!senderAddress.GetKeyID(pubKeyHash))
        {
            error = "Invalid ceased sidechain withdrawal input \"senderAddress\": Horizen pubKey address expected.";
            return false;
        }

        // parse sidechain id
        const UniValue& scid_v = find_value(o, "scId");
        if (scid_v.isNull())
        {
            error = "Missing mandatory parameter \"scId\" for the ceased sidechain withdrawal input";
            return false;
        }
        std::string scIdString = scid_v.get_str();
        if (scIdString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid ceased sidechain withdrawal input \"scId\" format: not an hex";
            return false;
        }

        uint256 scId;
        scId.SetHex(scIdString);

        // parse nullifier
        const UniValue& nullifier_v = find_value(o, "nullifier");
        if (nullifier_v.isNull())
        {
            error = "Missing mandatory parameter \"nullifier\" for the ceased sidechain withdrawal input";
            return false;
        }

        std::string nullifierError;
        std::vector<unsigned char> nullifierVec;
        if (!AddScData(nullifier_v.get_str(), nullifierVec, CFieldElement::ByteSize(), CheckSizeMode::CHECK_STRICT, nullifierError))
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"nullifier\": " + nullifierError;
            return false;
        }

        CFieldElement nullifier {nullifierVec};
        if (!nullifier.IsValid())
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"nullifier\": invalid nullifier data";
            return false;
        }
//---------------------------------------------------------------------------------------------
        // parse active cert data: it is an optional field and can be null string: it accounts for the case of an 
        // early ceased SC without any valid ceritificate 
        std::vector<unsigned char> vActCertData;

        const UniValue& valActCertData = find_value(o, "activeCertData");
        if (!valActCertData.isNull() && !valActCertData.get_str().empty())
        {
            std::string errStr;
            if (!AddScData(valActCertData.get_str(), vActCertData, CFieldElement::ByteSize(), CheckSizeMode::CHECK_STRICT, errStr))
            {
                error = "Invalid ceased sidechain withdrawal input parameter \"activeCertData\": " + errStr;
                return false;
            }
        }

        CFieldElement actCertDataHash = vActCertData.empty() ? CFieldElement{} : CFieldElement{vActCertData};
        if (!actCertDataHash.IsValid() && !actCertDataHash.IsNull())
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"activeCertData\": invalid field element";
            return false;
        }

//---------------------------------------------------------------------------------------------
        // parse ceasingCumScTxCommTree (do not check it though)
        const UniValue& valCumTree = find_value(o, "ceasingCumScTxCommTree");
        if (valCumTree.isNull())
        {
            error = "Missing mandatory parameter \"ceasingCumScTxCommTree\" for the ceased sidechain withdrawal input";
            return false;
        }

        std::string errStr;
        std::vector<unsigned char> vCeasingCumScTxCommTree;
        if (!AddScData(valCumTree.get_str(), vCeasingCumScTxCommTree, CFieldElement::ByteSize(), CheckSizeMode::CHECK_STRICT, errStr))
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"ceasingCumScTxCommTree\": " + errStr;
            return false;
        }

        CFieldElement ceasingCumScTxCommTree {vCeasingCumScTxCommTree};
        if (!ceasingCumScTxCommTree.IsValid())
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"ceasingCumScTxCommTree\": invalid field element";
            return false;
        }


//---------------------------------------------------------------------------------------------
        // parse snark proof
        const UniValue& proof_v = find_value(o, "scProof");
        if (proof_v.isNull())
        {
            error = "Missing mandatory parameter \"scProof\" for the ceased sidechain withdrawal input";
            return false;
        }

        std::string proofError;
        std::vector<unsigned char> scProofVec;
        if (!AddScData(proof_v.get_str(), scProofVec, CScProof::MaxByteSize(), CheckSizeMode::CHECK_UPPER_LIMIT, proofError))
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"scProof\": " + proofError;
            return false;
        }

        CScProof scProof {scProofVec};
        if (!scProof.IsValid())
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"scProof\": invalid snark proof data";
            return false;
        }

        CTxCeasedSidechainWithdrawalInput csw_input(amount, scId, nullifier, pubKeyHash, scProof, actCertDataHash, ceasingCumScTxCommTree, CScript());
        rawTx.vcsw_ccin.push_back(csw_input);
    }

    return true;
}

bool AddSidechainCreationOutputs(UniValue& sc_crs, CMutableTransaction& rawTx, std::string& error)
{
    rawTx.nVersion = SC_TX_VERSION;

    for (size_t i = 0; i < sc_crs.size(); i++)
    {
        ScFixedParameters sc;

        const UniValue& input = sc_crs[i];
        const UniValue& o = input.get_obj();

        const UniValue& vv = find_value(o, "version");
        if (vv.isNull() || !vv.isNum())
        {
            error = "Invalid or missing sidechain creation output parameter \"version\"";
            return false;
        }
        sc.version = vv.get_int();

        const UniValue& elv = find_value(o, "epoch_length");
        if (elv.isNull() || !elv.isNum())
        {
            error = "Invalid parameter or missing epoch_length key";
            return false;
        }

        char errBuf[256] = {};
        int withdrawalEpochLength = elv.get_int();

        if (!CSidechain::isNonCeasingSidechain(sc.version, withdrawalEpochLength)
            && (withdrawalEpochLength < getScMinWithdrawalEpochLength()))
        {
            sprintf(errBuf, "Invalid withdrawalEpochLength: minimum value allowed=%d\n", getScMinWithdrawalEpochLength());
            error = errBuf;
            return false;
        }
        if (withdrawalEpochLength > getScMaxWithdrawalEpochLength())
        {
            sprintf(errBuf, "Invalid withdrawalEpochLength: maximum value allowed=%d\n", getScMaxWithdrawalEpochLength());
            error = errBuf;
            return false;
        }

        sc.withdrawalEpochLength = withdrawalEpochLength;

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
            if (!AddScData(inputString, wCertVkVec, CScVKey::MaxByteSize(), CheckSizeMode::CHECK_UPPER_LIMIT, error))
            {
                error = "wCertVk: " + error;
                return false;
            }
            sc.wCertVk = CScVKey(wCertVkVec);
            if (!sc.wCertVk.IsValid())
            {
                error = "invalid wCertVk";
                return false;
            }
        }
        
        const UniValue& cd = find_value(o, "customData");
        if (!cd.isNull())
        {
            const std::string& inputString = cd.get_str();
            if (!AddScData(inputString, sc.customData, MAX_SC_CUSTOM_DATA_LEN, CheckSizeMode::CHECK_UPPER_LIMIT, error))
            {
                error = "customData: " + error;
                return false;
            }
        }

        const UniValue& constant = find_value(o, "constant");
        if (!constant.isNull())
        {
            const std::string& inputString = constant.get_str();
            std::vector<unsigned char> scConstantByteArray {};
            if (!AddScData(inputString, scConstantByteArray, CFieldElement::ByteSize(), CheckSizeMode::CHECK_UPPER_LIMIT, error))
            {
                error = "constant: " + error;
                return false;
            }

            sc.constant = CFieldElement{scConstantByteArray};
            if (!sc.constant->IsValid())
            {
                error = "invalid constant";
                return false;
            }
        }
        
        const UniValue& wCeasedVk = find_value(o, "wCeasedVk");
        if (!wCeasedVk.isNull())
        {
            const std::string& inputString = wCeasedVk.get_str();

            if (!inputString.empty())
            {
                std::vector<unsigned char> wCeasedVkVec;
                if (!AddScData(inputString, wCeasedVkVec, CScVKey::MaxByteSize(), CheckSizeMode::CHECK_UPPER_LIMIT, error))
                {
                    error = "wCeasedVk: " + error;
                    return false;
                }

                sc.wCeasedVk = CScVKey(wCeasedVkVec);
                if (!sc.wCeasedVk.value().IsValid())
                {
                    error = "invalid wCeasedVk";
                    return false;
                }
            }
        }

        const UniValue& FeCfg = find_value(o, "vFieldElementCertificateFieldConfig");
        if (!FeCfg.isNull())
        {
            UniValue intArray = FeCfg.get_array();
            if (!Sidechain::AddScData(intArray, sc.vFieldElementCertificateFieldConfig))
            {
                error = "invalid vFieldElementCertificateFieldConfig";
                return false;
            }
        }

        const UniValue& CmtCfg = find_value(o, "vBitVectorCertificateFieldConfig");
        if (!CmtCfg.isNull())
        {
            UniValue BitVectorSizesPairArray = CmtCfg.get_array();
            for(auto& pairEntry: BitVectorSizesPairArray.getValues())
            {
                if (pairEntry.size() != 2) {
                    error = "invalid vBitVectorCertificateFieldConfig";
                    return false;
                }
                if (!pairEntry[0].isNum() || !pairEntry[1].isNum())
                {
                    error = "invalid vBitVectorCertificateFieldConfig";
                    return false;
                }

                sc.vBitVectorCertificateFieldConfig.push_back(BitVectorCertificateFieldConfig{pairEntry[0].get_int(), pairEntry[1].get_int()});
            }
        }

        CAmount ftScFee(0);
        const UniValue& uniFtScFee = find_value(o, "forwardTransferScFee");
        if (!uniFtScFee.isNull())
        {
            ftScFee = AmountFromValue(uniFtScFee);

            if (!MoneyRange(ftScFee))
            {
                error = strprintf("Invalid forwardTransferScFee: out of range [%d, %d]", 0, MAX_MONEY);
                return false;
            }
        }

        CAmount mbtrScFee(0);
        const UniValue& uniMbtrScFee = find_value(o, "mainchainBackwardTransferScFee");
        if (!uniMbtrScFee.isNull())
        {
            mbtrScFee = AmountFromValue(uniMbtrScFee);

            if (!MoneyRange(mbtrScFee))
            {
                error = strprintf("Invalid mainchainBackwardTransferScFee: out of range [%d, %d]", 0, MAX_MONEY);
                return false;
            }
        }

        int32_t mbtrDataLength = 0;
        const UniValue& uniMbtrDataLength = find_value(o, "mainchainBackwardTransferRequestDataLength");
        if (!uniMbtrDataLength.isNull())
        {
            if (!uniMbtrDataLength.isNum())
            {
                error = "Invalid mainchainBackwardTransferRequestDataLength: numeric value expected";
                return false;
            }
            
            mbtrDataLength = uniMbtrDataLength.get_int();

            if (mbtrDataLength < 0 || mbtrDataLength > MAX_SC_MBTR_DATA_LEN)
            {
                error = strprintf("Invalid mainchainBackwardTransferRequestDataLength: out of range [%d, %d]", 0, MAX_SC_MBTR_DATA_LEN);
                return false;
            }
        }
        sc.mainchainBackwardTransferRequestDataLength = mbtrDataLength;

        CTxScCreationOut txccout(nAmount, address, ftScFee, mbtrScFee, sc);

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

        const UniValue& mcReturnAddressVal = find_value(o, "mcReturnAddress");
        if (mcReturnAddressVal.isNull())
        {
            error = "Missing mandatory parameter mcReturnAddress";
            return false;
        }

        inputString = mcReturnAddressVal.get_str();

        CBitcoinAddress mcReturnAddrSource(inputString);
        if (!mcReturnAddrSource.IsValid() || !mcReturnAddrSource.IsPubKey())
        {
            error = "Invalid \"mcReturnAddress\" parameter: Horizen address expected";
            return false;
        }

        CKeyID keyId;
        if(!mcReturnAddrSource.GetKeyID(keyId))
        {
            error = "Invalid \"mcReturnAddress\" parameter: can not extract pub key hash";
            return false;
        }
        uint160 mcReturnAddress = keyId;

        CTxForwardTransferOut txccout(scId, nAmount, address, mcReturnAddress);
        rawTx.vft_ccout.push_back(txccout);
    }

    return true;
}

bool AddSidechainBwtRequestOutputs(UniValue& bwtreq, CMutableTransaction& rawTx, std::string& error)
{
    rawTx.nVersion = SC_TX_VERSION;

    for (size_t j = 0; j < bwtreq.size(); j++)
    {
        ScBwtRequestParameters bwtData;

        const UniValue& input = bwtreq[j];
        const UniValue& o = input.get_obj();

        //---------------------------------------------------------------------
        const UniValue& scidVal = find_value(o, "scid");
        if (scidVal.isNull())
        {
            error = "Missing mandatory parameter scid";
            return false;
        }
        std::string inputString = scidVal.get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid scid format: not an hex";
            return false;
        }

        uint256 scId;
        scId.SetHex(inputString);

        //---------------------------------------------------------------------
        const UniValue& mcDestinationAddressVal = find_value(o, "mcDestinationAddress");
        if (mcDestinationAddressVal.isNull())
        {
            error = "Missing mandatory parameter mcDestinationAddress";
            return false;
        }

        inputString = mcDestinationAddressVal.get_str();

        CBitcoinAddress address(inputString);
        if (!address.IsValid() || !address.IsPubKey())
        {
            error = "Invalid \"mcDestinationAddress\" parameter: Horizen address expected";
            return false;
        }

        CKeyID keyId;
        if(!address.GetKeyID(keyId))
        {
            error = "Invalid \"mcDestinationAddress\" parameter: can not extract pub key hash";
            return false;
        }
        uint160 mcDestinationAddress = keyId;

        //---------------------------------------------------------------------
        const UniValue& scFeeVal = find_value(o, "scFee");
        CAmount scFee = AmountFromValue( scFeeVal );
        if (scFee < 0)
        {
            error = "Invalid parameter, amount must be positive";
            return false;
        }
        bwtData.scFee = scFee;

        //---------------------------------------------------------------------
        const UniValue& vScRequestDataVal = find_value(o, "vScRequestData");
        if (vScRequestDataVal.isNull())
        {
            error = "Missing mandatory parameter vScRequestData";
            return false;
        }


        for (UniValue inputElement : vScRequestDataVal.get_array().getValues())
        {
            std::vector<unsigned char> requestDataByteArray {};

            if (!Sidechain::AddScData(inputElement.get_str(), requestDataByteArray, CFieldElement::ByteSize(), CheckSizeMode::CHECK_STRICT, error))
            {
                throw JSONRPCError(RPC_TYPE_ERROR, std::string("requestDataByte: ") + error);
            }

            bwtData.vScRequestData.push_back(CFieldElement{requestDataByteArray});
        }


        CBwtRequestOut txccout(scId, mcDestinationAddress, bwtData);
        rawTx.vmbtr_out.push_back(txccout);
    }

    return true;
}

void fundCcRecipients(const CTransaction& tx,
    std::vector<CRecipientScCreation >& vecScSend, std::vector<CRecipientForwardTransfer >& vecFtSend,
    std::vector<CRecipientBwtRequest>& vecBwtRequest)
{
    BOOST_FOREACH(const auto& entry, tx.GetVscCcOut())
    {
        CRecipientScCreation sc;
        sc.nValue = entry.nValue;
        sc.address = entry.address;
        sc.fixedParams.withdrawalEpochLength               = entry.withdrawalEpochLength;
        sc.fixedParams.wCertVk                             = entry.wCertVk;
        sc.fixedParams.wCeasedVk                           = entry.wCeasedVk;
        sc.fixedParams.vFieldElementCertificateFieldConfig = entry.vFieldElementCertificateFieldConfig;
        sc.fixedParams.vBitVectorCertificateFieldConfig    = entry.vBitVectorCertificateFieldConfig;
        sc.fixedParams.customData                          = entry.customData;
        sc.fixedParams.constant                            = entry.constant;

        vecScSend.push_back(sc);
    }

    BOOST_FOREACH(const auto& entry, tx.GetVftCcOut())
    {
        CRecipientForwardTransfer ft;
        ft.scId = entry.scId;
        ft.address = entry.address;
        ft.nValue = entry.nValue;
        ft.mcReturnAddress = entry.mcReturnAddress;

        vecFtSend.push_back(ft);
    }

    BOOST_FOREACH(const auto& entry, tx.GetVBwtRequestOut())
    {
        CRecipientBwtRequest bt;
        bt.scId = entry.scId;
        bt.mcDestinationAddress = entry.mcDestinationAddress;
        bt.bwtRequestData.scFee = entry.scFee;
        bt.bwtRequestData.vScRequestData = entry.vScRequestData;

        vecBwtRequest.push_back(bt);
    }
}

//--------------------------------------------------------------------------------------------
// Cross chain outputs

ScRpcCmd::ScRpcCmd(
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee): 
        _fromMcAddress(fromaddress), _changeMcAddress(changeaddress), _minConf(minConf), _fee(nFee), _feeNeeded(-1),
        _automaticFee(false), _totalInputAmount(0), _totalOutputAmount(0)
{
    _hasFromAddress   = !(_fromMcAddress   == CBitcoinAddress());
    _hasChangeAddress = !(_changeMcAddress == CBitcoinAddress());

    if (_fee == SC_RPC_OPERATION_AUTO_MINERS_FEE)
    {
        // fee must start from 0 when automatically calculated, and then its updated
        _automaticFee = true;
        _fee = CAmount(0);
    }

    // Get dust threshold
    CKey secret;
    secret.MakeNewKey(true);
    CScript scriptPubKey = GetScriptForDestination(secret.GetPubKey().GetID());
    CTxOut out(CAmount(1), scriptPubKey);
    _dustThreshold = out.GetDustThreshold(minRelayTxFee);

}

void ScRpcCmd::init()
{
    _totalInputAmount = 0;
}

void ScRpcCmd::addInputs()
{
    std::vector<COutput> vAvailableCoins;
    std::vector<SelectedUTXO> vInputUtxo;

    static const bool fOnlyConfirmed = false;
    static const bool fIncludeZeroValue = false;
    static bool fMustShieldCoinBase = ForkManager::getInstance().mustCoinBaseBeShielded(chainActive.Height());
    static bool fMustShieldCommunityFund = false;
    // CF exemption allowed only after hfCommunityFundHeight hardfork
    if (!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(chainActive.Height()))
        fMustShieldCommunityFund = fMustShieldCoinBase;

    pwalletMain->AvailableCoins(vAvailableCoins, fOnlyConfirmed, NULL, fIncludeZeroValue, !fMustShieldCoinBase, !fMustShieldCommunityFund);

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
        addInput(in);
    }
}

void ScRpcCmd::addChange()
{
    // fee must start from 0 when automatically calculated, and then its updated. It might also be set explicitly to 0
    CAmount change = _totalInputAmount - ( _totalOutputAmount + _fee);

    if (change > 0)
    {
        // handle the address for the change
        // ---
        // If an addresss for the change has been set by the caller we use it; else we use the fromAddress if specified.
        // In case non of them is set, we use a new address
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

            // bitcoin code has also KeepKey() in the CommitTransaction() for preventing the key reuse,
            // but zcash does not do that.
            if (!keyChange.GetReservedKey(vchPubKey))
                throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Could not generate a taddr to use as a change address"); // should never fail, as we just unlocked

            scriptPubKey = GetScriptForDestination(vchPubKey.GetID());
        }

        // Never create dust outputs; if we would, just add the dust to the fee.
        CTxOut newTxOut(change, scriptPubKey);
        if (newTxOut.IsDust(::minRelayTxFee))
        {
            LogPrint("sc", "%s():%d - adding dust change=%lld to fee\n", __func__, __LINE__, change);
            _fee += change;
        }
        else
        {
            addOutput(CTxOut(change, scriptPubKey));
        }
    }
}

ScRpcCmdCert::ScRpcCmdCert(
        CMutableScCertificate& cert, const std::vector<sBwdParams>& bwdParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress, int minConf, const CAmount& nFee,
        const std::vector<FieldElementCertificateField>& vCfe, const std::vector<BitVectorCertificateField>& vCmt,
        const CAmount& ftScFee, const CAmount& mbtrScFee):
        ScRpcCmd(fromaddress, changeaddress, minConf, nFee),
        _cert(cert),_bwdParams(bwdParams), _vCfe(vCfe), _vCmt(vCmt),
        _ftScFee(ftScFee), _mbtrScFee(mbtrScFee)
{
}

void ScRpcCmdCert::_execute()
{
    init();
    addInputs();
    addChange();
    addBackwardTransfers();
    addCustomFields();
    addScFees();
    sign();
}

void ScRpcCmdCert::sign()
{
    std::string rawcert;
    try
    {
        CScCertificate toEncode(_cert);
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
    _cert = certStreamed;
}

bool ScRpcCmd::checkFeeRate()
{
    // if the _fee is intentionally set to 0, go on and skip the check
    if (_fee == 0 && !_automaticFee)
    {
        LogPrint("sc", "%s():%d - Null fee explicitly set, returning true\n", __func__, __LINE__);
        return true;
    }

    unsigned int nSize = getSignedObjSize();

    // there are 3 main user options handling the fee (plus an estimation algorithm currently broken):
    // -------------------------------------------------------------------
    // minRelayTxFee: set via zen option "-minrelaytxfee" defaults to 100 sat per K
    //                Nodes, and expecially miners, consider txes under this thresholds the same as "free" transactions.
    // payTxFee     : set via zen option "-paytxfee" defaults to 0 sat per K
    //                This is the fee rate a user wants to use for paying fee when sending a transaction.
    // minTxFee     : set via zen option "-mintxfee" defaults to 1000 sat per K
    //                This is the fee rate used for automatically computing the fee a user will pay when sending
    //                a transaction when the paytxfee has not been set.
    // -------------------------------------------------------------------
    // The function below checks all the various fee rate thresholds and returns the minimum needed.
    // This value is anyway not lower than minRelayFee.

    // Therefore, using default values, the fee needed is the one corresponding to the minTxFee rate of 1000 Zat / Kbyte
    _feeNeeded = CWallet::GetMinimumFee(nSize, nTxConfirmTarget, mempool);

    if (_fee < _feeNeeded)
    {
        if (!_automaticFee)
        {
            // the user explicitly set a non-zero fee
            if (_fee < ::minRelayTxFee.GetFee(nSize))
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf(
                    "tx with size %d has too low a fee: %lld < minrelaytxfee %lld, the miner might not include it in a block",
                    nSize, _fee, ::minRelayTxFee.GetFee(nSize)));
            }
            else
            {
                LogPrintf("%s():%d - Warning: using a fee(%lld) < minimum(%lld) (tx size = %d)\n",
                    __func__, __LINE__, _fee, _feeNeeded, nSize);
            }
        }
        else
        {
            LogPrint("sc", "%s():%d - Updating fee: %lld --> %lld (size=%d)\n",
                __func__, __LINE__, _fee, _feeNeeded, nSize);
            // we have to retry with this value
            _fee = _feeNeeded;
            return false;
        }
    }
    return true;
}

void ScRpcCmdCert::init()
{
    ScRpcCmd::init();

    _cert.vin.clear();
    _cert.resizeOut(0);
    _cert.resizeBwt(0);

    _cert.forwardTransferScFee = CScCertificate::INT_NULL;
    _cert.mainchainBackwardTransferRequestScFee = CScCertificate::INT_NULL;
    _cert.vFieldElementCertificateField.clear();
    _cert.vBitVectorCertificateField.clear();
}

bool ScRpcCmd::send()
{
    unsigned int nSize = getSignedObjSize();

    // check we do not exceed max obj size
    if (nSize > getMaxObjSize())
    {
        LogPrintf("%s():%d - tx/cert size[%d] > max size(%d)\n", __func__, __LINE__, nSize, getMaxObjSize());
        throw JSONRPCError(RPC_VERIFY_ERROR, strprintf(
            "tx/cert size %d > max size(%d)", nSize, getMaxObjSize()));
    }

    if (!checkFeeRate())
    {
        // try again with an updated fee
        return false;
    }

    UniValue val = UniValue(UniValue::VARR);
    val.push_back(_signedObjHex);

    UniValue hash = sendrawtransaction(val, false);
    if (hash.isNull())
    {
        // should never happen, since the above command returns a valid hash or throws an exception itself
        throw JSONRPCError(RPC_WALLET_ERROR, "sendrawtransaction has failed");
    }
    LogPrint("sc", "tx/cert sent[%s]\n", hash.get_str());
    return true;
}

void ScRpcCmdCert::addBackwardTransfers()
{
    for (const auto& entry : _bwdParams)
    {
        CTxOut txout(entry._nAmount, entry._scriptPubKey);
        if (txout.IsDust(::minRelayTxFee))
            throw JSONRPCError(RPC_WALLET_ERROR, "backward transfer amount too small");
        _cert.addBwt(txout);
    }
}

void ScRpcCmdCert::addCustomFields()
{
    if (!_vCfe.empty())
        _cert.vFieldElementCertificateField = _vCfe;
    if (!_vCmt.empty())
        _cert.vBitVectorCertificateField = _vCmt;
}

void ScRpcCmdCert::addScFees()
{
    _cert.forwardTransferScFee = _ftScFee;
    _cert.mainchainBackwardTransferRequestScFee = _mbtrScFee;
}

void ScRpcCmdTx::init()
{
    ScRpcCmd::init();

    _tx.vin.clear();
    _tx.resizeOut(0);
    _tx.vsc_ccout.clear();
    _tx.vft_ccout.clear();
    _tx.vmbtr_out.clear();
}

ScRpcCmdTx::ScRpcCmdTx(
        CMutableTransaction& tx,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee):
        ScRpcCmd(fromaddress, changeaddress, minConf, nFee), _tx(tx)
{
}

void ScRpcCmdTx::sign()
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

void ScRpcCmdTx::_execute()
{
    init();
    addInputs();
    addChange();
    addCcOutputs();
    sign();
}

void ScRpcCmd::execute()
{
    // we need a safety counter for the case when we have a large number of very small inputs that gets added to
    // the tx increasing its size and the fee needed
    // An alternative might as well be letting it fail when we do not have utxo's anymore.
    static const int MAX_LOOP = 100;

    int safeCount = MAX_LOOP;

    while (true)
    {
        _execute();

        LogPrint("sc", "%s():%d - cnt=%d, fee=%lld, feeNeeded=%lld\n", __func__, __LINE__,
            (MAX_LOOP - safeCount + 1), _fee, _feeNeeded);

        if (send())
        {
            // we made it
            break;
        }
        if (safeCount-- <= 0)
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Could not set minimum fee");
        }
    }
}

ScRpcCreationCmdTx::ScRpcCreationCmdTx(
        CMutableTransaction& tx, const std::vector<sCrOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee, const CAmount& ftScFee, const CAmount& mbtrScFee,
        const ScFixedParameters& cd):
        ScRpcCmdTx(tx, fromaddress, changeaddress, minConf, nFee), _fixedParams(cd), _outParams(outParams), _ftScFee(ftScFee), _mbtrScFee(mbtrScFee)
{
    for (const auto& entry : _outParams)
    {
        _totalOutputAmount += entry._nAmount;
    }
} 

void ScRpcCreationCmdTx::addCcOutputs()
{
    if (_outParams.size() != 1)
    {
        // creation has just one output param
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("invalid number of output: %d!", _outParams.size()));
    }

    CTxScCreationOut txccout(_outParams[0]._nAmount, _outParams[0]._toScAddress, _ftScFee, _mbtrScFee, _fixedParams);
    _tx.add(txccout);
}

ScRpcSendCmdTx::ScRpcSendCmdTx(
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


void ScRpcSendCmdTx::addCcOutputs()
{
    if (_outParams.size() == 0)
    {
        // send cmd can not have empty output vector
        throw JSONRPCError(RPC_WALLET_ERROR, "null number of output!");
    }

    for (const auto& entry : _outParams)
    {
        CTxForwardTransferOut txccout(entry._scid, entry._nAmount, entry._toScAddress, entry._mcReturnAddress);
        _tx.add(txccout);
    }
}

ScRpcRetrieveCmdTx::ScRpcRetrieveCmdTx(
        CMutableTransaction& tx, const std::vector<sBtOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee):
        ScRpcCmdTx(tx, fromaddress, changeaddress, minConf, nFee), _outParams(outParams)
{
    for (const auto& entry : _outParams)
    {
        _totalOutputAmount += entry._params.scFee;
    }
} 


void ScRpcRetrieveCmdTx::addCcOutputs()
{
    if (_outParams.size() == 0)
    {
        // send cmd can not have empty output vector
        throw JSONRPCError(RPC_WALLET_ERROR, "null number of output!");
    }

    for (const auto& entry : _outParams)
    {
        CBwtRequestOut txccout(entry._scid, entry._pkh, entry._params);
        _tx.add(txccout);
    }
}

}  // end of namespace
