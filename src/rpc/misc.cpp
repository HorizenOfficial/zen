// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "addressindex.h"
#include "base58.h"
#include "clientversion.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "netbase.h"
#include "rpc/server.h"
#include "txmempool.h"
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
            "  \"isvalid\": true|false,            (boolean) if the address is valid or not. If not, this is the only property returned\n"
            "  \"address\": \"zenaddress\",        (string) the " + CURRENCY_UNIT  + " address validated\n"
            "  \"scriptPubKey\": \"hex\",          (string) the hex encoded scriptPubKey generated by the address\n"
            "  \"ismine\": true|false,             (boolean) if the address is yours or not\n"
            "  \"iswatchonly\": true|false,        (boolean) if the address is set to watch only mode or not\n"
            "  \"isscript\": true|false,           (boolean) if the key is a script\n"
            "  \"pubkey\": \"publickeyhex\",       (string, optional) the hex value of the raw public key, only when the address is yours\n"
            "  \"iscompressed\": true|false,       (boolean, optional) if the address is compressed, only when the address is yours\n"
            "  \"account\": \"account\"            (string, optional) DEPRECATED. the account associated with the address, \"\" is the default account, only when the address is yours\n"
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

bool getAddressFromIndex(const AddressType type, const uint160 &hash, std::string &address)
{
    switch (type) {
        case AddressType::SCRIPT:
            address = CBitcoinAddress(CScriptID(hash)).ToString();
            return true;
        case AddressType::PUBKEY:
            address = CBitcoinAddress(CKeyID(hash)).ToString();
            return true;
        default:
            return false;
    }
}

std::pair<uint160,AddressType> addressHashAndTypeFromValue(const UniValue& value) {
    const std::string value_str{value.get_str()}; // Throws if not string type

    uint160 hashBytes;
    AddressType type = AddressType::UNKNOWN;
    CBitcoinAddress address(value_str);
    if (!address.GetIndexKey(hashBytes, type)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address : ") + value_str);
    }
    return {hashBytes, type};
}

std::vector<std::pair<uint160, AddressType>> addressesHashAndTypeFromValue(const UniValue& value, bool allow_empty = true) {

    // TODO See Jira's discussion ZEND-242
    if (value.isStr()) {
        return {addressHashAndTypeFromValue(value)};
    }

    if (!value.isArray() || (value.empty() && !allow_empty)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("An array of addresses is expected") + (allow_empty ? "" : " [not empty]"));
    }

    std::vector<std::pair<uint160, AddressType>> ret;
    ret.reserve(value.size());
    for (const auto& item : value.getValues()) {
        ret.push_back(addressHashAndTypeFromValue(item));
    }
    return ret;
}

bool heightComparer(const std::pair<CAddressUnspentKey, CAddressUnspentValue>& a,
                    const std::pair<CAddressUnspentKey, CAddressUnspentValue>& b)
{
    return a.second.blockHeight < b.second.blockHeight;
}

bool timestampComparer(const std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>& a,
                       const std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>& b)
{
    return a.second.time < b.second.time;
}

UniValue getaddressmempool(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getaddressmempool\n"
            "\nReturns all mempool deltas for an address (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "}\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"address\"  (string) The base58check encoded address\n"
            "    \"txid\"  (string) The related txid\n"
            "    \"index\"  (number) The related input or output index\n"
            "    \"satoshis\"  (number) The difference of satoshis\n"
            "    \"timestamp\"  (number) The time the transaction entered the mempool (seconds)\n"
            "    \"prevtxid\"  (string) The previous txid (if spending)\n"
            "    \"prevout\"  (string) The previous transaction output index (if spending)\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressmempool", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddressmempool", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
        );


    if (!fAddressIndex) {
        throw std::runtime_error("Address indexing not enabled");
    }

    const auto param_obj = params[0];

    UniValue result(UniValue::VARR);

    std::vector<std::pair<uint160, AddressType>> addresses{addressesHashAndTypeFromValue(param_obj["addresses"], /*allow_empty=*/ false)};
    std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>> indexes;
    std::ignore = mempool.getAddressIndex(addresses, indexes); // Only chance to return false is for fAddressIndex == false but is already checked
    if (indexes.empty()) {
        return result;
    }

    std::sort(indexes.begin(), indexes.end(), timestampComparer);
    result.reserve(indexes.size());

    for (const auto& [deltaKey, delta] : indexes) {
        std::string address;
        std::ignore = getAddressFromIndex(deltaKey.type, deltaKey.addressBytes, address); // We've already validated type during parsing

        UniValue result_item(UniValue::VOBJ);
        result_item.pushKV("address", address);
        result_item.pushKV("txid", deltaKey.txhash.GetHex());
        result_item.pushKV("index", static_cast<int>(deltaKey.index));
        result_item.pushKV("satoshis", delta.amount);
        result_item.pushKV("timestamp", delta.time);
        if (delta.amount < 0) {
            result_item.pushKV("prevtxid", delta.prevhash.GetHex());
            result_item.pushKV("prevout", static_cast<int>(delta.prevout));
        } else {
            result_item.pushKV("outstatus", static_cast<int>(delta.outStatus));
        }
        result.push_back(result_item);
    }

    return result;
}

UniValue getaddressutxos(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getaddressutxos\n"
            "\nReturns all unspent outputs for an address (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"          (string) The base58check encoded address\n"
            "      ,...\n"
            "    ],\n"
            "  \"chainInfo\"            (boolean, optional) Include chain info with results\n"
            "}\n"
            "\"includeImmatureBTs\"   (bool, optional, default = false) Whether to include ImmatureBTs in the utxos list\n"
            "\nResult\n"
            "[\n"
            "  {\n"
            "    \"address\"            (string) The address base58check encoded\n"
            "    \"txid\"               (string) The output txid\n"
            "    \"height\"             (number) The block height\n"
            "    \"outputIndex\"        (number) The output index\n"
            "    \"script\"             (string) The script hex encoded\n"
            "    \"satoshis\"           (number) The number of satoshis of the output\n"
            "    \"backwardTransfer\"   (bool)   True if the output is a certificate backward transfer, False otherwise\n"
            "    \"maturityHeight\"     (number) The maturity height when the utxo is spendable (0 means already spendable)\n"           
            "    \"mature\"             (bool)   False if the output is a bwt of a certificate that has not yet reached maturity, True otherwise\n"  
            "    \"blocksToMaturity\"   (number) The number of blocks to be mined for achieving maturity (0 means already spendable)\n"           
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressutxos", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddressutxos", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
            );

    // This is needed cause GetAddressUnspent returns false in two cases
    // Either the address indexing is not enabled or an exception occurred
    // during the processing of pblocktree->ReadAddressUnspentIndex
    // this is unfortunate as we cannot distinguish amongst the two conditions
    // TODO Let the exception flow up to here (if possible) and don't trap
    // as higher level code of the RPC server already handles it
    if (!fAddressIndex) {
        throw std::runtime_error("Address indexing not enabled");
    }

    const auto param_obj = params[0].get_obj(); // Throws if not an object

    bool includeChainInfo = false;
    if (const auto chainInfo_v{param_obj["chainInfo"]}; !chainInfo_v.isNull()) {
        includeChainInfo = chainInfo_v.get_bool();
    }

    bool includeImmatureBTs = false;
    if (params.size() > 1)
        includeImmatureBTs = params[1].get_bool();

    const auto input_addresses = param_obj["addresses"];
    std::vector<std::pair<uint160, AddressType>> addresses{addressesHashAndTypeFromValue(input_addresses, /*allow_empty=*/false)};
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;
    for (const auto& [addressHash, addressType] : addresses) {

        // The only chance for this to return false here is when pblocktree->ReadAddressUnspentIndex throws
        // hence an internal error not caused by input.
        if (!GetAddressUnspent(addressHash, addressType, unspentOutputs)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Internal db error");
        }
    }

    std::sort(unspentOutputs.begin(), unspentOutputs.end(), heightComparer);

    UniValue utxos(UniValue::VARR);
    utxos.reserve(unspentOutputs.size());

    int currentTipHeight = -1;
    std::string bestHashStr;
    {
        LOCK(cs_main);
        if (includeChainInfo)
            bestHashStr = chainActive.Tip()->GetBlockHash().GetHex();
        currentTipHeight = static_cast<int>(chainActive.Height());
    }

    for (const auto& [unspentK, unspentV] : unspentOutputs) {

        std::string address;
        std::ignore = getAddressFromIndex(unspentK.type, unspentK.hashBytes, address); // We've already validated type during parsing

        int bwtMatHeight  = unspentV.maturityHeight;
        bool isBwt        = (bwtMatHeight != 0);
        int deltaMaturity = bwtMatHeight - currentTipHeight;
        bool isMature     = (deltaMaturity <= 0);

        if (isBwt)
        {
            //If maturityHeight is negative it's superseded and we skip it
            if (bwtMatHeight < 0)
                continue;
    
            //If it's immature and we don't include immature BTS, skip it
            if (!isMature && !includeImmatureBTs) {
                continue;
            }
        }

        UniValue output(UniValue::VOBJ);
        output.pushKV("address", address);
        output.pushKV("txid", unspentK.txhash.GetHex());
        output.pushKV("outputIndex", static_cast<int>(unspentK.index));
        output.pushKV("script", HexStr(unspentV.script.begin(), unspentV.script.end()));
        output.pushKV("satoshis", unspentV.satoshis);
        output.pushKV("height", unspentV.blockHeight);

        output.pushKV("backwardTransfer", isBwt);

        // [AS] proposal: add these fields only if bwt
        output.pushKV("maturityHeight", bwtMatHeight);

        if (isBwt)
        {
            output.pushKV("mature", isMature);

            if (!isMature)
            {
                output.pushKV("blocksToMaturity", deltaMaturity);
            }
            else
            {
                output.pushKV("blocksToMaturity", 0);
            }
        }
        else
        {
            output.pushKV("mature", true);
            output.pushKV("blocksToMaturity", 0);
        }

        utxos.push_back(output);
    }

    if (includeChainInfo) {
        UniValue result(UniValue::VOBJ);
        result.pushKV("utxos", utxos);

        result.pushKV("hash",   bestHashStr);
        result.pushKV("height", currentTipHeight);
        return result;
    } else {
        return utxos;
    }
}

UniValue getaddressdeltas(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1 || !params[0].isObject())
        throw runtime_error(
            "getaddressdeltas\n"
            "\nReturns all changes for an address (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "  \"start\" (number) The start block height\n"
            "  \"end\" (number) The end block height\n"
            "  \"chainInfo\" (boolean) Include chain info in results, only applies if start and end specified\n"
            "}\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"satoshis\"  (number) The difference of satoshis\n"
            "    \"txid\"  (string) The related txid\n"
            "    \"index\"  (number) The related input or output index\n"
            "    \"height\"  (number) The block height\n"
            "    \"address\"  (string) The base58check encoded address\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressdeltas", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddressdeltas", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
        );


    // This is needed cause GetAddressUnspent returns false in two cases
    // Either the address indexing is not enabled or an exception occurred
    // during the processing of pblocktree->ReadAddressUnspentIndex
    // this is unfortunate as we cannot distinguish amongst the two conditions
    // TODO Let the exception flow up to here (if possible) and don't trap
    // as higher level code of the RPC server already handles it
    if (!fAddressIndex) {
        throw std::runtime_error("Address indexing not enabled");
    }

    const auto param_obj = params[0];

    int start = 0;
    int end = 0;
    bool includeChainInfo = false;

    if (const auto v{param_obj["start"]}; !v.isNull()) {
        start = v.get_int();
    }
    if (const auto v{param_obj["end"]}; !v.isNull()) {
        end = v.get_int();
    }
    if (const auto v{param_obj["chainInfo"]}; !v.isNull()) {
        includeChainInfo = v.getBool();
    }
    if (start < 0 || end < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Start and/or end are expected to be non negative integers");
    }
    if (start > end) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Start must be lower/equal than end");
    }

    std::vector<std::pair<uint160, AddressType>> addresses{addressesHashAndTypeFromValue(param_obj["addresses"], /*allow_empty=*/false)};
    std::vector<std::pair<CAddressIndexKey, CAddressIndexValue>> addressIndex;

    for (const auto& [addressHash, addressType] : addresses) {

        // The only chance for this to return false here is when pblocktree->ReadAddressIndex throws
        // hence an internal error not caused by input.
        if (!GetAddressIndex(addressHash, addressType, addressIndex, start, end)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Internal db error");
        }
    }

    UniValue deltas(UniValue::VARR);
    deltas.reserve(addressIndex.size());

    for (const auto& [indexK, indexV] : addressIndex) {
        std::string address;
        std::ignore = getAddressFromIndex(indexK.type, indexK.hashBytes, address); // We've already validated type on parsing

        UniValue delta(UniValue::VOBJ);
        delta.pushKV("satoshis", indexV.satoshis);
        delta.pushKV("txid", indexK.txhash.GetHex());
        delta.pushKV("index", static_cast<int>(indexK.index));
        delta.pushKV("blockindex", static_cast<int>(indexK.txindex));
        delta.pushKV("height", indexK.blockHeight);
        delta.pushKV("address", address);
        deltas.push_back(delta);
    }

    UniValue result(UniValue::VOBJ);

    if (includeChainInfo && start > 0 && end > 0) {
        LOCK(cs_main);

        if (start > chainActive.Height() || end > chainActive.Height()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Start or end is outside chain range");
        }

        CBlockIndex* startIndex = chainActive[start];
        CBlockIndex* endIndex = chainActive[end];

        UniValue startInfo(UniValue::VOBJ);
        UniValue endInfo(UniValue::VOBJ);

        startInfo.pushKV("hash", startIndex->GetBlockHash().GetHex());
        startInfo.pushKV("height", start);

        endInfo.pushKV("hash", endIndex->GetBlockHash().GetHex());
        endInfo.pushKV("height", end);

        result.pushKV("deltas", deltas);
        result.pushKV("start", startInfo);
        result.pushKV("end", endInfo);

        return result;
    } else {
        return deltas;
    }
}

UniValue getaddressbalance(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getaddressbalance\n"
            "\nReturns the balance for an address(es) (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"               (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "}\n"
            "\"includeImmatureBTs\"          (bool, optional, default = false) Whether to include ImmatureBTs in the balance calculation\n"
            "\nResult:\n"
            "{\n"
            "  \"balance\"                   (string) The current balance in satoshis\n"
            "  \"received\"                  (string) The total number of satoshis received (including change)\n"
            "  \"immature\"                  (string) The current immature balance in satoshis\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressbalance", "'{\"addresses\": [\"znXWB3XGptd5T3jA9VuoGEEnVTAVHejj5bB\"]}'")
            + HelpExampleRpc("getaddressbalance", "'{\"addresses\": [\"znXWB3XGptd5T3jA9VuoGEEnVTAVHejj5bB\"]}'")
        );

    // This is needed cause GetAddressUnspent returns false in two cases
    // Either the address indexing is not enabled or an exception occurred
    // during the processing of pblocktree->ReadAddressUnspentIndex
    // this is unfortunate as we cannot distinguish amongst the two conditions
    // TODO Let the exception flow up to here (if possible) and don't trap
    // as higher level code of the RPC server already handles it
    if (!fAddressIndex) {
        throw std::runtime_error("Address indexing not enabled");
    }

    std::vector<std::pair<uint160, AddressType>> addresses{addressesHashAndTypeFromValue(params[0], /*allow_empty=*/ false)};

    bool includeImmatureBTs = false;
    if (params.size() > 1)
        includeImmatureBTs = params[1].get_bool();

    std::vector<std::pair<CAddressIndexKey, CAddressIndexValue>> addressIndex;
    for (const auto& [addressHash, addressType] : addresses) {

        // The only chance for this to return false here is when pblocktree->ReadAddressIndex throws
        // hence an internal error not caused by input.
        if (!GetAddressIndex(addressHash, addressType, addressIndex)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Internal db error");
        }
    }

    CAmount balance = 0;
    CAmount received = 0;
    CAmount immature = 0;

    // TODO Isn't a lock needed here ? (see getaddressutxos)
    int currentTipHeight = chainActive.Tip()->nHeight;

    // TODO Given the input addresses are an array is it correct
    // we produce a cumulative result ?

    for (const auto& [_, indexV] : addressIndex) {
        // If maturityHeight is negative it's superseded and we skip it
        if (indexV.maturityHeight < 0)
            continue;
        // If maturityHeight > currentTipHeight it's immature and we store the immature balance
        // and the balance only if specified
        if (indexV.maturityHeight > currentTipHeight) {
            immature += indexV.satoshis;
            if (includeImmatureBTs) {
                if (indexV.satoshis > 0) {
                    received += indexV.satoshis;
                }
                balance += indexV.satoshis;
            }
        } else {
            if (indexV.satoshis > 0) {
                received += indexV.satoshis;
            }
            balance += indexV.satoshis;
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("balance", balance);
    result.pushKV("received", received);
    result.pushKV("immature", immature);

    return result;

}

UniValue getaddresstxids(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getaddresstxids\n"
            "\nReturns the txids for an address(es) (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "  \"start\" (number) The start block height\n"
            "  \"end\" (number) The end block height\n"
            "}\n"
            "\nResult:\n"
            "[\n"
            "  \"transactionid\"  (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddresstxids", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddresstxids", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
        );

    // This is needed cause GetAddressUnspent returns false in two cases
    // Either the address indexing is not enabled or an exception occurred
    // during the processing of pblocktree->ReadAddressUnspentIndex
    // this is unfortunate as we cannot distinguish amongst the two conditions
    // TODO Let the exception flow up to here (if possible) and don't trap
    // as higher level code of the RPC server already handles it
    if (!fAddressIndex) {
        throw std::runtime_error("Address indexing not enabled");
    }

    const auto param = params[0];

    int start = 0;
    int end = 0;
    if (const auto v{param["start"]}; !v.isNull()) {
        start = v.get_int();
    }
    if (const auto v{param["end"]}; !v.isNull()) {
        end = v.get_int();
    }
    if (start < 0 || end < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Start and/or end are expected to be non negative integers");
    }
    if (start > end) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Start must be lower/equal than end");
    }

    std::vector<std::pair<uint160, AddressType>> addresses{addressesHashAndTypeFromValue(param.isObject() ? param["addresses"] : param, /*allow_empty=*/ false)};
    std::vector<std::pair<CAddressIndexKey, CAddressIndexValue>> addressIndex;
    for (const auto& [addressHash, addressType] : addresses) {
        // The only chance for this to return false here is when pblocktree->ReadAddressIndex throws
        // hence an internal error not caused by input.
        if (!GetAddressIndex(addressHash, addressType, addressIndex, start, end)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Internal db error");
        }
    }

    std::set<std::pair<int, std::string>> txids;
    UniValue result(UniValue::VARR);

    for (const auto& [indexK, _] : addressIndex) {
        int height = indexK.blockHeight;
        std::string txid = indexK.txhash.GetHex();

        if (addresses.size() > 1) {
            txids.insert(std::make_pair(height, txid));
        } else {
            if (txids.insert(std::make_pair(height, txid)).second) {
                result.push_back(txid);
            }
        }
    }

    if (addresses.size() > 1) {
        for (const auto& [_, txid] : txids) {
            result.push_back(txid);
        }
    }

    return result;

}

UniValue getspentinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1 || !params[0].isObject())
        throw runtime_error(
            "getspentinfo\n"
            "\nReturns the txid and index where an output is spent (requires spentindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"txid\" (string) The hex string of the txid\n"
            "  \"index\" (number) The start block height\n"
            "}\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\"  (string) The transaction id\n"
            "  \"index\"  (number) The spending input index\n"
            "  ,...\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getspentinfo", "'{\"txid\": \"0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9\", \"index\": 0}'") + 
            HelpExampleRpc("getspentinfo", "{\"txid\": \"0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9\", \"index\": 0}"));

    if (!fSpentIndex) {
        throw std::runtime_error("spentindex not enabled");
    }

    const auto& param_obj = params[0].get_obj();
    uint256 txid = ParseHashO(param_obj, "txid");   // Throws if not string
    int outputIndex = param_obj["index"].get_int(); // Throws if not num

    // Output index cannot be negative otherwise implicit conversion
    // in CSpentIndexKey causes martian values
    if (outputIndex < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "index cannot be negative");
    }
    
    CSpentIndexKey key(txid, outputIndex);
    CSpentIndexValue value;

    if (!GetSpentIndex(key, value)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to get spent info");
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("txid", value.txid.GetHex());
    obj.pushKV("index", (int)value.inputIndex);
    obj.pushKV("height", value.blockHeight);

    return obj;
}
