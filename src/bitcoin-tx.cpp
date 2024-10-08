// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "clientversion.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "core_io.h"
#include "keystore.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/sign.h"
#include <univalue.h>
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "main.h"

#include <stdio.h>

#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>

using namespace std;

ScCumTreeRootMap mapCumtreeHeight;
static bool fCreateBlank;
static map<string,UniValue> registers;

static bool AppInitRawTx(int argc, char* argv[])
{
    //
    // Parameters
    //
    ParseParameters(argc, argv);

    // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
    if (!SelectParamsFromCommandLine()) {
        fprintf(stderr, "Error: Invalid combination of -regtest and -testnet.\n");
        return false;
    }

    fCreateBlank = GetBoolArg("-create", false);

    if (argc<2 || mapArgs.count("-?") || mapArgs.count("-h") || mapArgs.count("-help"))
    {
        // First part of help message is specific to this utility
        std::string strUsage = _("Horizen zen-tx utility version") + " " + FormatFullVersion() + "\n\n" +
            _("Usage:") + "\n" +
              "  zen-tx [options] <hex-tx> [commands]  " + _("Update hex-encoded zencash transaction") + "\n" +
              "  zen-tx [options] -create [commands]   " + _("Create hex-encoded zencash transaction") + "\n" +
              "\n";

        fprintf(stdout, "%s", strUsage.c_str());

        strUsage = HelpMessageGroup(_("Options:"));
        strUsage += HelpMessageOpt("-?", _("This help message"));
        strUsage += HelpMessageOpt("-create", _("Create new, empty TX."));
        strUsage += HelpMessageOpt("-json", _("Select JSON output"));
        strUsage += HelpMessageOpt("-txid", _("Output only the hex-encoded transaction id of the resultant transaction."));
        strUsage += HelpMessageOpt("-regtest", _("Enter regression test mode, which uses a special chain in which blocks can be solved instantly."));
        strUsage += HelpMessageOpt("-testnet", _("Use the test network"));

        fprintf(stdout, "%s", strUsage.c_str());

        strUsage = HelpMessageGroup(_("Commands:"));
        strUsage += HelpMessageOpt("delin=N", _("Delete input N from TX"));
        strUsage += HelpMessageOpt("delout=N", _("Delete output N from TX"));
        strUsage += HelpMessageOpt("in=TXID:VOUT", _("Add input to TX"));
        strUsage += HelpMessageOpt("locktime=N", _("Set TX lock time to N"));
        strUsage += HelpMessageOpt("nversion=N", _("Set TX version to N"));
        strUsage += HelpMessageOpt("outaddr=VALUE:ADDRESS", _("Add address-based output to TX"));
        strUsage += HelpMessageOpt("outscript=VALUE:SCRIPT", _("Add raw script output to TX"));
        strUsage += HelpMessageOpt("sign=SIGHASH-FLAGS", _("Add zero or more signatures to transaction") + ". " +
            _("This command requires JSON registers:") +
            _("prevtxs=JSON object") + ", " +
            _("privatekeys=JSON object") + ". " +
            _("See signrawtransaction docs for format of sighash flags, JSON objects."));
        fprintf(stdout, "%s", strUsage.c_str());

        strUsage = HelpMessageGroup(_("Register Commands:"));
        strUsage += HelpMessageOpt("load=NAME:FILENAME", _("Load JSON file FILENAME into register NAME"));
        strUsage += HelpMessageOpt("set=NAME:JSON-STRING", _("Set register NAME to given JSON-STRING"));
        fprintf(stdout, "%s", strUsage.c_str());

        return false;
    }
    return true;
}

static void RegisterSetJson(const string& key, const string& rawJson)
{
    UniValue val;
    if (!val.read(rawJson)) {
        string strErr = "Cannot parse JSON for key " + key;
        throw runtime_error(strErr);
    }

    registers[key] = val;
}

static void RegisterSet(const string& strInput)
{
    // separate NAME:VALUE in string
    size_t pos = strInput.find(':');
    if ((pos == string::npos) ||
        (pos == 0) ||
        (pos == (strInput.size() - 1)))
        throw runtime_error("Register input requires NAME:VALUE");

    string key = strInput.substr(0, pos);
    string valStr = strInput.substr(pos + 1, string::npos);

    RegisterSetJson(key, valStr);
}

static void RegisterLoad(const string& strInput)
{
    // separate NAME:FILENAME in string
    size_t pos = strInput.find(':');
    if ((pos == string::npos) ||
        (pos == 0) ||
        (pos == (strInput.size() - 1)))
        throw runtime_error("Register load requires NAME:FILENAME");

    string key = strInput.substr(0, pos);
    string filename = strInput.substr(pos + 1, string::npos);

    FILE *f = fopen(filename.c_str(), "r");
    if (!f) {
        string strErr = "Cannot open file " + filename;
        throw runtime_error(strErr);
    }

    // load file chunks into one big buffer
    string valStr;
    while ((!feof(f)) && (!ferror(f))) {
        char buf[4096];
        int bread = fread(buf, 1, sizeof(buf), f);
        if (bread <= 0)
            break;

        valStr.insert(valStr.size(), buf, bread);
    }

    int error = ferror(f);
    fclose(f);

    if (error) {
        string strErr = "Error reading file " + filename;
        throw runtime_error(strErr);
    }

    // evaluate as JSON buffer register
    RegisterSetJson(key, valStr);
}

static void MutateTxVersion(CMutableTransaction& tx, const string& cmdVal)
{
    int64_t newVersion = atoi64(cmdVal);
    //TODO VERIFY WHAT'S USED FOR
    if ((newVersion < CTransaction::MIN_OLD_VERSION && newVersion != GROTH_TX_VERSION)) // || newVersion > CTransaction::MAX_CURRENT_VERSION)
        throw runtime_error("Invalid TX version requested");

    tx.nVersion = (int) newVersion;
}

static void MutateTxLocktime(CMutableTransaction& tx, const string& cmdVal)
{
    int64_t newLocktime = atoi64(cmdVal);
    if (newLocktime < 0LL || newLocktime > 0xffffffffLL)
        throw runtime_error("Invalid TX locktime requested");

    tx.nLockTime = (unsigned int) newLocktime;
}

static void MutateTxAddInput(CMutableTransaction& tx, const string& strInput)
{
    // separate TXID:VOUT in string
    size_t pos = strInput.find(':');
    if ((pos == string::npos) ||
        (pos == 0) ||
        (pos == (strInput.size() - 1)))
        throw runtime_error("TX input missing separator");

    // extract and validate TXID
    string strTxid = strInput.substr(0, pos);
    if ((strTxid.size() != 64) || !IsHex(strTxid))
        throw runtime_error("invalid TX input txid");
    uint256 txid(uint256S(strTxid));

    static const unsigned int minTxOutSz = 9;
    static const unsigned int maxVout = MAX_BLOCK_SIZE / minTxOutSz;

    // extract and validate vout
    string strVout = strInput.substr(pos + 1, string::npos);
    int vout = atoi(strVout);
    if ((vout < 0) || (vout > (int)maxVout))
        throw runtime_error("invalid TX input vout");

    // append to transaction input list
    CTxIn txin(txid, vout);
    tx.vin.push_back(txin);
}

static void MutateTxAddOutAddr(CMutableTransaction& tx, const string& strInput)
{
    // separate VALUE:ADDRESS in string
    size_t pos = strInput.find(':');
    if ((pos == string::npos) ||
        (pos == 0) ||
        (pos == (strInput.size() - 1)))
        throw runtime_error("TX output missing separator");

    // extract and validate VALUE
    string strValue = strInput.substr(0, pos);
    CAmount value;
    if (!ParseMoney(strValue, value))
        throw runtime_error("invalid TX output value");

    // extract and validate ADDRESS
    string strAddr = strInput.substr(pos + 1, string::npos);
    CBitcoinAddress addr(strAddr);
    if (!addr.IsValid())
        throw runtime_error("invalid TX output address");

    // build standard output script via GetScriptForDestination()
    CScript scriptPubKey = GetScriptForDestination(addr.Get());

    // construct TxOut, append to transaction output list
    tx.addOut(CTxOut(value, scriptPubKey));
}

static void MutateTxAddOutScript(CMutableTransaction& tx, const string& strInput)
{
    // separate VALUE:SCRIPT in string
    size_t pos = strInput.find(':');
    if ((pos == string::npos) ||
        (pos == 0))
        throw runtime_error("TX output missing separator");

    // extract and validate VALUE
    string strValue = strInput.substr(0, pos);
    CAmount value;
    if (!ParseMoney(strValue, value))
        throw runtime_error("invalid TX output value");

    // extract and validate script
    string strScript = strInput.substr(pos + 1, string::npos);
    CScript scriptPubKey = ParseScript(strScript); // throws on err

    // construct TxOut, append to transaction output list
    tx.addOut(CTxOut(value, scriptPubKey));
}

static void MutateTxDelInput(CMutableTransaction& tx, const string& strInIdx)
{
    // parse requested deletion index
    int inIdx = atoi(strInIdx);
    if (inIdx < 0 || inIdx >= (int)tx.vin.size()) {
        string strErr = "Invalid TX input index '" + strInIdx + "'";
        throw runtime_error(strErr.c_str());
    }

    // delete input from transaction
    tx.vin.erase(tx.vin.begin() + inIdx);
}

static void MutateTxDelOutput(CMutableTransaction& tx, const string& strOutIdx)
{
    // parse requested deletion index
    int outIdx = atoi(strOutIdx);
    if (outIdx < 0 || outIdx >= (int)tx.getVout().size()) {
        string strErr = "Invalid TX output index '" + strOutIdx + "'";
        throw runtime_error(strErr.c_str());
    }

    // delete output from transaction
    tx.eraseAtPos(outIdx);
}

static const unsigned int N_SIGHASH_OPTS = 6;
static const struct {
    const char *flagStr;
    int flags;
} sighashOptions[N_SIGHASH_OPTS] = {
    {"ALL", SIGHASH_ALL},
    {"NONE", SIGHASH_NONE},
    {"SINGLE", SIGHASH_SINGLE},
    {"ALL|ANYONECANPAY", SIGHASH_ALL|SIGHASH_ANYONECANPAY},
    {"NONE|ANYONECANPAY", SIGHASH_NONE|SIGHASH_ANYONECANPAY},
    {"SINGLE|ANYONECANPAY", SIGHASH_SINGLE|SIGHASH_ANYONECANPAY},
};

static bool findSighashFlags(int& flags, const string& flagStr)
{
    flags = 0;

    for (unsigned int i = 0; i < N_SIGHASH_OPTS; i++) {
        if (flagStr == sighashOptions[i].flagStr) {
            flags = sighashOptions[i].flags;
            return true;
        }
    }

    return false;
}

uint256 ParseHashUO(map<string,UniValue>& o, string strKey)
{
    if (!o.count(strKey))
        return uint256();
    return ParseHashUV(o[strKey], strKey);
}

vector<unsigned char> ParseHexUO(map<string,UniValue>& o, string strKey)
{
    if (!o.count(strKey)) {
        vector<unsigned char> emptyVec;
        return emptyVec;
    }
    return ParseHexUV(o[strKey], strKey);
}

static void MutateTxSign(CMutableTransaction& tx, const string& flagStr)
{
    int nHashType = SIGHASH_ALL;

    if (flagStr.size() > 0)
        if (!findSighashFlags(nHashType, flagStr))
            throw runtime_error("unknown sighash flag/sign option");

    vector<CTransaction> txVariants;
    txVariants.push_back(tx);

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the raw tx:
    CMutableTransaction mergedTx(txVariants[0]);
    bool fComplete = true;
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);

    if (!registers.count("privatekeys"))
        throw runtime_error("privatekeys register variable must be set.");
    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    UniValue keysObj = registers["privatekeys"];
    fGivenKeys = true;

    for (size_t kidx = 0; kidx < keysObj.size(); kidx++) {
        if (!keysObj[kidx].isStr())
            throw runtime_error("privatekey not a string");
        CBitcoinSecret vchSecret;
        bool fGood = vchSecret.SetString(keysObj[kidx].getValStr());
        if (!fGood)
            throw runtime_error("privatekey not valid");

        CKey key = vchSecret.GetKey();
        tempKeystore.AddKey(key);
    }

    // Add previous txouts given in the RPC call:
    if (!registers.count("prevtxs"))
        throw runtime_error("prevtxs register variable must be set.");
    UniValue prevtxsObj = registers["prevtxs"];
    {
        for (size_t previdx = 0; previdx < prevtxsObj.size(); previdx++) {
            UniValue prevOut = prevtxsObj[previdx];
            if (!prevOut.isObject())
                throw runtime_error("expected prevtxs internal object");

            map<string,UniValue::VType> types = boost::assign::map_list_of("txid", UniValue::VSTR)("vout",UniValue::VNUM)("scriptPubKey",UniValue::VSTR);
            if (!prevOut.checkObject(types))
                throw runtime_error("prevtxs internal object typecheck fail");

            uint256 txid = ParseHashUV(prevOut["txid"], "txid");

            int nOut = atoi(prevOut["vout"].getValStr());
            if (nOut < 0)
                throw runtime_error("vout must be positive");

            vector<unsigned char> pkData(ParseHexUV(prevOut["scriptPubKey"], "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                CCoinsModifier coins = view.ModifyCoins(txid);
                if (coins->IsAvailable(nOut) && coins->vout[nOut].scriptPubKey != scriptPubKey) {
                    string err("Previous output scriptPubKey mismatch:\n");
                    err = err + coins->vout[nOut].scriptPubKey.ToString() + "\nvs:\n"+
                        scriptPubKey.ToString();
                    throw runtime_error(err);
                }
                if ((unsigned int)nOut >= coins->vout.size())
                    coins->vout.resize(nOut+1);
                coins->vout[nOut].scriptPubKey = scriptPubKey;
                coins->vout[nOut].nValue = 0; // we don't know the actual output value
            }

            // if redeemScript given and private keys given,
            // add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && scriptPubKey.IsPayToScriptHash() &&
                prevOut.exists("redeemScript")) {
                UniValue v = prevOut["redeemScript"];
                vector<unsigned char> rsData(ParseHexUV(v, "redeemScript"));
                CScript redeemScript(rsData.begin(), rsData.end());
                tempKeystore.AddCScript(redeemScript);
            }
        }
    }

    const CKeyStore& keystore = tempKeystore;

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn& txin = mergedTx.vin[i];
        const CCoins* coins = view.AccessCoins(txin.prevout.hash);
        if (!coins || !coins->IsAvailable(txin.prevout.n)) {
            fComplete = false;
            continue;
        }
        const CScript& prevPubKey = coins->vout[txin.prevout.n].scriptPubKey;

        txin.scriptSig.clear();
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.getVout().size()))
            SignSignature(keystore, prevPubKey, mergedTx, i, nHashType);

        // ... and merge in other signatures:
        BOOST_FOREACH(const CTransaction& txv, txVariants) {
            txin.scriptSig = CombineSignatures(prevPubKey, mergedTx, i, txin.scriptSig, txv.GetVin()[i].scriptSig);
        }
        if (!VerifyScript(txin.scriptSig, prevPubKey, STANDARD_NONCONTEXTUAL_SCRIPT_VERIFY_FLAGS, MutableTransactionSignatureChecker(&mergedTx, i)))
            fComplete = false;
    }

    // NOTE: Since no other sidechains features were imported in this module, it was decided to keep CSW code commented
//    if(mergedTx.IsScVersion())
//    {
//        // Try to sign CeasedSidechainWithdrawal inputs:
//        unsigned int nAllInputsIndex = mergedTx.vin.size();
//        for (unsigned int i = 0; i < mergedTx.vcsw_ccin.size(); i++, nAllInputsIndex++)
//        {
//            CTxCeasedSidechainWithdrawalInput& txCswIn = mergedTx.vcsw_ccin[i];

//            const CScript& prevPubKey = txCswIn.scriptPubKey();

//            txCswIn.redeemScript.clear();
//            // Only sign SIGHASH_SINGLE if there's a corresponding output:
//            // Note: we should consider the regular inputs as well.
//            if (!fHashSingle || (nAllInputsIndex < mergedTx.getVout().size()))
//                SignSignature(keystore, prevPubKey, mergedTx, nAllInputsIndex, nHashType);

//            // ... and merge in other signatures:
//            /* Note:
//             * For CTxCeasedSidechainWithdrawalInput currently only P2PKH is allowed.
//             * SignSignature can return true and set `txCswIn.redeemScript` value in case there is a proper private key in the keystore.
//             * It can return false and leave `txCswIn.redeemScript` empty in case of any error occurs.
//             * CombineSignatures will try to get the most recent signature:
//             * 1) if SignSignature operation was successful -> leave `txCswIn.redeemScript value as is.
//             * 2) if SignSignature operation was unsuccessful -> set `txCswIn.redeemScript value equal to the origin `txv` csw input script.
//             * Later the signature will be checked, so in case no origin signature and no new one exist -> verification will fail.
//             */
//            for(const CMutableTransaction& txv : txVariants)
//                txCswIn.redeemScript = CombineSignatures(prevPubKey, mergedTx, nAllInputsIndex, txCswIn.redeemScript, txv.vcsw_ccin[i].redeemScript);

//            ScriptError serror = SCRIPT_ERR_OK;
//            if (!VerifyScript(txCswIn.redeemScript, prevPubKey, STANDARD_NONCONTEXTUAL_SCRIPT_VERIFY_FLAGS,
//                              MutableTransactionSignatureChecker(&mergedTx, nAllInputsIndex), &serror))
//                 fComplete = false;
//        }
//    }

    if (fComplete) {
        // do nothing... for now
        // perhaps store this for later optional JSON output
    }

    tx = mergedTx;
}

class Secp256k1Init
{
    ECCVerifyHandle globalVerifyHandle;

public:
    Secp256k1Init() {
        ECC_Start();
    }
    ~Secp256k1Init() {
        ECC_Stop();
    }
};

// TODO: do we need to have prossibility to add CSW inputs and CC outputs?
static void MutateTx(CMutableTransaction& tx, const string& command,
                     const string& commandVal)
{
    boost::scoped_ptr<Secp256k1Init> ecc;

    if (command == "nversion")
        MutateTxVersion(tx, commandVal);
    else if (command == "locktime")
        MutateTxLocktime(tx, commandVal);

    else if (command == "delin")
        MutateTxDelInput(tx, commandVal);
    else if (command == "in")
        MutateTxAddInput(tx, commandVal);

    else if (command == "delout")
        MutateTxDelOutput(tx, commandVal);
    else if (command == "outaddr")
        MutateTxAddOutAddr(tx, commandVal);
    else if (command == "outscript")
        MutateTxAddOutScript(tx, commandVal);

    else if (command == "sign") {
        if (!ecc) { ecc.reset(new Secp256k1Init()); }
        MutateTxSign(tx, commandVal);
    }

    else if (command == "load")
        RegisterLoad(commandVal);

    else if (command == "set")
        RegisterSet(commandVal);

    else
        throw runtime_error("unknown command");
}

static void OutputTxJSON(const CTransaction& tx)
{
    UniValue entry(UniValue::VOBJ);
    TxToUniv(tx, uint256(), entry);

    string jsonOutput = entry.write(4);
    fprintf(stdout, "%s\n", jsonOutput.c_str());
}

static void OutputTxHash(const CTransaction& tx)
{
    string strHexHash = tx.GetHash().GetHex(); // the hex-encoded transaction hash (aka the transaction id)

    fprintf(stdout, "%s\n", strHexHash.c_str());
}

static void OutputTxHex(const CTransaction& tx)
{
    string strHex = EncodeHexTx(tx);

    fprintf(stdout, "%s\n", strHex.c_str());
}

static void OutputTx(const CTransaction& tx)
{
    if (GetBoolArg("-json", false))
        OutputTxJSON(tx);
    else if (GetBoolArg("-txid", false))
        OutputTxHash(tx);
    else
        OutputTxHex(tx);
}

static string readStdin()
{
    char buf[4096];
    string ret;

    while (!feof(stdin)) {
        size_t bread = fread(buf, 1, sizeof(buf), stdin);
        ret.append(buf, bread);
        if (bread < sizeof(buf))
            break;
    }

    if (ferror(stdin))
        throw runtime_error("error reading stdin");

    boost::algorithm::trim_right(ret);

    return ret;
}

static int CommandLineRawTx(int argc, char* argv[])
{
    string strPrint;
    int nRet = 0;
    try {
        // Skip switches; Permit common stdin convention "-"
        while (argc > 1 && IsSwitchChar(argv[1][0]) &&
               (argv[1][1] != 0)) {
            argc--;
            argv++;
        }

        CTransaction txDecodeTmp;
        int startArg;

        if (!fCreateBlank) {
            // require at least one param
            if (argc < 2)
                throw runtime_error("too few parameters");

            // param: hex-encoded bitcoin transaction
            string strHexTx(argv[1]);
            if (strHexTx == "-")                 // "-" implies standard input
                strHexTx = readStdin();

            if (!DecodeHexTx(txDecodeTmp, strHexTx))
                throw runtime_error("invalid transaction encoding");

            startArg = 2;
        } else
            startArg = 1;

        CMutableTransaction tx(txDecodeTmp);

        for (int i = startArg; i < argc; i++) {
            string arg = argv[i];
            string key, value;
            size_t eqpos = arg.find('=');
            if (eqpos == string::npos)
                key = arg;
            else {
                key = arg.substr(0, eqpos);
                value = arg.substr(eqpos + 1);
            }

            MutateTx(tx, key, value);
        }

        OutputTx(tx);
    }

    catch (const boost::thread_interrupted&) {
        throw;
    }
    catch (const std::exception& e) {
        strPrint = string("error: ") + e.what();
        nRet = EXIT_FAILURE;
    }
    catch (...) {
        PrintExceptionContinue(NULL, "CommandLineRawTx()");
        throw;
    }

    if (strPrint != "") {
        fprintf((nRet == 0 ? stdout : stderr), "%s\n", strPrint.c_str());
    }
    return nRet;
}

int main(int argc, char* argv[])
{
    SetupEnvironment();

    try {
        if(!AppInitRawTx(argc, argv))
            return EXIT_FAILURE;
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInitRawTx()");
        return EXIT_FAILURE;
    } catch (...) {
        PrintExceptionContinue(NULL, "AppInitRawTx()");
        return EXIT_FAILURE;
    }

    int ret = EXIT_FAILURE;
    try {
        ret = CommandLineRawTx(argc, argv);
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "CommandLineRawTx()");
    } catch (...) {
        PrintExceptionContinue(NULL, "CommandLineRawTx()");
    }
    return ret;
}
