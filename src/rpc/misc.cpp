// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "clientversion.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "netbase.h"
#include "rpc/server.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include <univalue.h>

#include "zcash/Address.hpp"

using namespace std;

/**
 * @note Do not add or change anything in the information returned by this
 * method. `getinfo` exists for backwards-compatibility only. It combines
 * information from wildly different sources in the program, which is a mess,
 * and is thus planned to be deprecated eventually.
 *
 * Based on the source of the information, new information should be added to:
 * - `getblockchaininfo`,
 * - `getnetworkinfo` or
 * - `getwalletinfo`
 *
 * Or alternatively, create a specific query method for the information.
 **/
UniValue getinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getinfo\n"
            "Returns an object containing various state info.\n"
            
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,           (numeric) the server version\n"
            "  \"protocolversion\": xxxxx,   (numeric) the latest supported protocol version\n"
            "  \"walletversion\": xxxxx,     (numeric) the wallet version\n"
            "  \"balance\": xxxxxxx,         (numeric) the total balance of the wallet in " + CURRENCY_UNIT + "\n"
            "  \"blocks\": xxxxxx,           (numeric) the current number of blocks processed in the server\n"
            "  \"timeoffset\": 0,            (numeric) the time offset (deprecated; always 0)\n"
            "  \"connections\": xxxxx,       (numeric) the number of connected peers\n"
            "  \"proxy\": \"host:port\",     (string, optional) the proxy used by the server\n"
            "  \"difficulty\": xxxxxx,       (numeric) the current difficulty\n"
            "  \"testnet\": true|false,      (boolean) if the server is using testnet or not\n"
            "  \"keypoololdest\": xxxxxx,    (numeric) the timestamp in seconds of the oldest pre-generated key in the key pool\n"
            "  \"keypoolsize\": xxxx,        (numeric) how many new keys are pre-generated\n"
            "  \"unlocked_until\": ttt,      (numeric, optional) the timestamp in seconds that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": xxxxx,          (numeric) the transaction fee set in " + CURRENCY_UNIT + " /kB\n"
            "  \"relayfee\": xxxxx,          (numeric) minimum relay fee for non-free transactions in " + CURRENCY_UNIT + " /kB\n"
            "  \"errors\": \"...\"           (string) any error messages\n"
            "}\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getinfo", "")
            + HelpExampleRpc("getinfo", "")
        );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("version", CLIENT_VERSION);
    obj.pushKV("protocolversion", PROTOCOL_VERSION);
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.pushKV("walletversion", pwalletMain->GetVersion());
        obj.pushKV("balance",       ValueFromAmount(pwalletMain->GetBalance()));
    }
#endif
    obj.pushKV("blocks",        (int)chainActive.Height());
    obj.pushKV("timeoffset",    0);
    obj.pushKV("connections",   (int)vNodes.size());
    obj.pushKV("proxy",         (proxy.IsValid() ? proxy.proxy.ToStringIPPort() : string()));
    obj.pushKV("difficulty",    (double)GetDifficulty());
    obj.pushKV("testnet",       Params().TestnetToBeDeprecatedFieldRPC());
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.pushKV("keypoololdest", pwalletMain->GetOldestKeyPoolTime());
        obj.pushKV("keypoolsize",   (int)pwalletMain->GetKeyPoolSize());
    }
    if (pwalletMain && pwalletMain->IsCrypted())
        obj.pushKV("unlocked_until", nWalletUnlockTime);
    obj.pushKV("paytxfee",      ValueFromAmount(payTxFee.GetFeePerK()));
#endif
    obj.pushKV("relayfee",      ValueFromAmount(::minRelayTxFee.GetFeePerK()));
    obj.pushKV("errors",        GetWarnings("statusbar"));
    return obj;
}

#ifdef ENABLE_WALLET
class DescribeAddressVisitor : public boost::static_visitor<UniValue>
{
public:
    UniValue operator()(const CNoDestination &dest) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const CKeyID &keyID) const {
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        obj.pushKV("isscript", false);
        if (pwalletMain && pwalletMain->GetPubKey(keyID, vchPubKey)) {
            obj.pushKV("pubkey", HexStr(vchPubKey));
            obj.pushKV("iscompressed", vchPubKey.IsCompressed());
        }
        return obj;
    }

    UniValue operator()(const CScriptID &scriptID) const {
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        obj.pushKV("isscript", true);
        if (pwalletMain && pwalletMain->GetCScript(scriptID, subscript)) {
            std::vector<CTxDestination> addresses;
            txnouttype whichType;
            int nRequired;
            ExtractDestinations(subscript, whichType, addresses, nRequired);
            obj.pushKV("script", GetTxnOutputType(whichType));
            obj.pushKV("hex", HexStr(subscript.begin(), subscript.end()));
            UniValue a(UniValue::VARR);
            BOOST_FOREACH(const CTxDestination& addr, addresses)
                a.push_back(CBitcoinAddress(addr).ToString());
            obj.pushKV("addresses", a);
            if (whichType == TX_MULTISIG)
                obj.pushKV("sigsrequired", nRequired);
        }
        return obj;
    }
};
#endif

UniValue validateaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "validateaddress \"zenaddress\"\n"
            "\nReturn information about the given ZEN address.\n"

            "\nArguments:\n"
            "1. \"zenaddress\"                   (string, required) the ZEN address to validate\n"

            "\nResult:\n"
            "{\n"
            "  \"isvalid\": true|false,          (boolean) if the address is valid or not. If not, this is the only property returned\n"
            "  \"address\": \"zenaddress\",      (string) the " + CURRENCY_UNIT  + " address validated\n"
            "  \"scriptPubKey\": \"hex\",        (string) the hex encoded scriptPubKey generated by the address\n"
            "  \"ismine\": true|false,           (boolean) if the address is yours or not\n"
            "  \"iswatchonly\": true|false,      (boolean) if the address is set to watch only mode or not\n"
            "  \"isscript\": true|false,         (boolean) if the key is a script\n"
            "  \"pubkey\": \"publickeyhex\",     (string, optional) the hex value of the raw public key, only when the address is yours\n"
            "  \"iscompressed\": true|false,     (boolean, optional) if the address is compressed, only when the address is yours\n"
            "  \"account\": \"account\"          (string, optional) DEPRECATED. the account associated with the address, \"\" is the default account, only when the address is yours\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("validateaddress", "\"zenaddress\"")
            + HelpExampleRpc("validateaddress", "\"zenaddress\"")
        );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif
    
    string strAddress = params[0].get_str();
    CBitcoinAddress address(strAddress);
    bool isValid = address.IsValid();
    // do not valid addr `t` addr via rpc only
    if (isValid && strAddress[0]=='t') {
        isValid = false;
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("isvalid", isValid);
    if (isValid)
    {
        CTxDestination dest = address.Get();
        string currentAddress = address.ToString();
        ret.pushKV("address", currentAddress);

        CScript scriptPubKey = GetScriptForDestination(dest);
        ret.pushKV("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

#ifdef ENABLE_WALLET
        isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : ISMINE_NO;
        ret.pushKV("ismine", (mine & ISMINE_SPENDABLE) ? true : false);
        ret.pushKV("iswatchonly", (mine & ISMINE_WATCH_ONLY) ? true: false);
        UniValue detail = boost::apply_visitor(DescribeAddressVisitor(), dest);
        ret.pushKVs(detail);
        if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
            ret.pushKV("account", pwalletMain->mapAddressBook[dest].name);
#endif
    }
    return ret;
}


UniValue z_validateaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "z_validateaddress \"zaddr\"\n"
            "\nReturn information about the given zaddress.\n"

            "\nArguments:\n"
            "1. \"zaddr\"                       (string, required) the zaddress to validate\n"

            "\nResult:\n"
            "{\n"
            "  \"isvalid\": true|false,         (boolean) if the address is valid or not. If not, this is the only property returned\n"
            "  \"address\": \"zaddr\",          (string) the zaddress validated\n"
            "  \"payingkey\": \"hex\",          (string) the hex value of the paying key, a_pk\n"
            "  \"transmissionkey\": \"hex\",    (string) the hex value of the transmission key, pk_enc\n"
            "  \"ismine\": true|false           (boolean) if the address is yours or not\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("z_validateaddress", "\"zaddr\"")
            + HelpExampleRpc("z_validateaddress", "\"zaddr\"")
        );


#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain->cs_wallet);
#else
    LOCK(cs_main);
#endif

    bool isValid = false;
    bool isMine = false;
    std::string payingKey, transmissionKey;

    string strAddress = params[0].get_str();
    try {
        CZCPaymentAddress address(strAddress);
        libzcash::PaymentAddress addr = address.Get();

#ifdef ENABLE_WALLET
        isMine = pwalletMain->HaveSpendingKey(addr);
#endif
        payingKey = addr.a_pk.GetHex();
        transmissionKey = addr.pk_enc.GetHex();
        isValid = true;
    } catch (std::runtime_error e) {
        // address is invalid, nop here as isValid is false.
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("isvalid", isValid);
    if (isValid)
    {
        ret.pushKV("address", strAddress);
        ret.pushKV("payingkey", payingKey);
        ret.pushKV("transmissionkey", transmissionKey);
#ifdef ENABLE_WALLET
        ret.pushKV("ismine", isMine);
#endif
    }
    return ret;
}


/**
 * Used by addmultisigaddress / createmultisig:
 */
CScript _createmultisig_redeemScript(const UniValue& params)
{
    int nRequired = params[0].get_int();
    const UniValue& keys = params[1].get_array();

    // Gather public keys
    if (nRequired < 1)
        throw runtime_error("a multisignature address must require at least one key to redeem");
    if ((int)keys.size() < nRequired)
        throw runtime_error(
            strprintf("not enough keys supplied "
                      "(got %u keys, but need at least %d to redeem)", keys.size(), nRequired));
    if (keys.size() > 16)
        throw runtime_error("Number of addresses involved in the multisignature address creation > 16\nReduce the number");
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(keys.size());
    for (unsigned int i = 0; i < keys.size(); i++)
    {
        const std::string& ks = keys[i].get_str();
#ifdef ENABLE_WALLET
        // Case 1: Bitcoin address and we have full public key:
        CBitcoinAddress address(ks);
        if (pwalletMain && address.IsValid())
        {
            CKeyID keyID;
            if (!address.GetKeyID(keyID))
                throw runtime_error(
                    strprintf("%s does not refer to a key",ks));
            CPubKey vchPubKey;
            if (!pwalletMain->GetPubKey(keyID, vchPubKey))
                throw runtime_error(
                    strprintf("no full public key for address %s",ks));
            if (!vchPubKey.IsFullyValid())
                throw runtime_error(" Invalid public key: "+ks);
            pubkeys[i] = vchPubKey;
        }

        // Case 2: hex public key
        else
#endif
        if (IsHex(ks))
        {
            CPubKey vchPubKey(ParseHex(ks));
            if (!vchPubKey.IsFullyValid())
                throw runtime_error(" Invalid public key: "+ks);
            pubkeys[i] = vchPubKey;
        }
        else
        {
            throw runtime_error(" Invalid public key: "+ks);
        }
    }
    CScript result = GetScriptForMultisig(nRequired, pubkeys);

    if (result.size() > MAX_SCRIPT_ELEMENT_SIZE)
        throw runtime_error(
                strprintf("redeemScript exceeds size limit: %d > %d", result.size(), MAX_SCRIPT_ELEMENT_SIZE));

    return result;
}

UniValue createmultisig(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2)
    {
        string msg = "createmultisig nrequired [\"key\",...]\n"
            "\nCreates a multi-signature address with n signature of m keys required.\n"
            "It returns a json object with the address and redeemScript.\n"

            "\nArguments:\n"
            "1. nrequired                          (numeric, required) the number of required signatures out of the n keys or addresses\n"
            "2. \"keys\"                           (string, required) a json array of keys which are " + CURRENCY_UNIT + " addresses or hex-encoded public keys\n"
            "     [\n"
            "       \"key\"                        (string) " + CURRENCY_UNIT + " address or hex-encoded public key\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "{\n"
            "  \"address\": \"multisigaddress\",   (string) the value of the new multisig address\n"
            "  \"redeemScript\": \"hex\"           (string) the string value of the hex-encoded redemption script\n"
            "}\n"

            "\nExamples:\n"
            "\nCreate a multisig address from 2 addresses\n"
            + HelpExampleCli("createmultisig", "2 \"[\\\"addr1\\\",\\\"addr2\\\"]\"")
            + HelpExampleRpc("createmultisig", "2, \"[\\\"addr1\\\",\\\"addr2\\\"]\"")
        ;
        throw runtime_error(msg);
    }

    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(params);
    CScriptID innerID(inner);
    CBitcoinAddress address(innerID);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", address.ToString());
    result.pushKV("redeemScript", HexStr(inner.begin(), inner.end()));

    return result;
}

UniValue verifymessage(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "verifymessage \"zenaddress\" \"signature\" \"message\"\n"
            "\nVerify a signed message\n"

            "\nArguments:\n"
            "1. \"zenaddress\"      (string, required) the " + CURRENCY_UNIT + " address to use for the signature\n"
            "2. \"signature\"       (string, required) the signature provided by the signer in base 64 encoding (see signmessage)\n"
            "3. \"message\"         (string, required) the message that was signed\n"

            "\nResult:\n"
            "true|false             (boolean) if the signature is verified or not\n"

            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n"
            + HelpExampleCli("signmessage", "\"zenaddress\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessage", "\"zenaddress\" \"signature\" \"my message\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("verifymessage", "\"zenaddress\", \"signature\", \"my message\"")
        );

    LOCK(cs_main);

    string strAddress  = params[0].get_str();
    string strSign     = params[1].get_str();
    string strMessage  = params[2].get_str();

    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");

    bool fInvalid = false;
    vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == keyID);
}

UniValue setmocktime(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "setmocktime timestamp\n"
            "\nSet the local time to given timestamp (-regtest only)\n"
            
            "\nArguments:\n"
            "1. timestamp  (numeric, required) Unix seconds-since-epoch timestamp\n"
            "               pass 0 to go back to using the system time."
            
            "\nResult:\n"
            "Nothing\n"
            
            "\nExamples:\n"
            + HelpExampleCli("setmocktime", "0")
            + HelpExampleRpc("setmocktime", "0")
        );

    if (!Params().MineBlocksOnDemand())
        throw runtime_error("setmocktime for regression testing (-regtest mode) only");

    // cs_vNodes is locked and node send/receive times are updated
    // atomically with the time change to prevent peers from being
    // disconnected because we think we haven't communicated with them
    // in a long time.
    LOCK2(cs_main, cs_vNodes);

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));
    SetMockTime(params[0].get_int64());

    uint64_t t = GetTime();
    BOOST_FOREACH(CNode* pnode, vNodes) {
        pnode->nLastSend = pnode->nLastRecv = t;
    }

    return NullUniValue;
}
