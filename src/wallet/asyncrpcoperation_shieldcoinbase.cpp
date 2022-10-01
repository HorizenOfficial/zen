// Copyright (c) 2017 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "asyncrpcoperation_shieldcoinbase.h"

#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "asyncrpcqueue.h"
#include "core_io.h"
#include "init.h"
#include "main.h"
#include "miner.h"
#include "net.h"
#include "netbase.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include "script/interpreter.h"
#include "sodium.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utiltime.h"
#include "walletdb.h"
#include "zcash/IncrementalMerkleTree.hpp"

using namespace libzcash;

static int find_output(UniValue obj, int n) {
    UniValue outputMapValue = find_value(obj, "outputmap");
    if (!outputMapValue.isArray()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing outputmap for JoinSplit operation");
    }

    UniValue outputMap = outputMapValue.get_array();
    assert(outputMap.size() == ZC_NUM_JS_OUTPUTS);
    for (size_t i = 0; i < outputMap.size(); i++) {
        if (outputMap[i].get_int() == n) {
            return i;
        }
    }

    throw std::logic_error("n is not present in outputmap");
}

AsyncRPCOperation_shieldcoinbase::AsyncRPCOperation_shieldcoinbase(CMutableTransaction contextualTx,
                                                                   std::vector<ShieldCoinbaseUTXO> inputs,
                                                                   std::string toAddress, CAmount fee, UniValue contextInfo)
    : tx_(contextualTx), inputs_(inputs), fee_(fee), contextinfo_(contextInfo) {
    assert(contextualTx.nVersion >= PHGR_TX_VERSION ||
           contextualTx.nVersion == GROTH_TX_VERSION);  // transaction format version must support vjoinsplit

    if (fee < 0 || fee > MAX_MONEY) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Fee is out of range");
    }

    if (inputs.size() == 0) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Empty inputs");
    }

    //  Check the destination address is valid for this network i.e. not testnet being used on mainnet
    CZCPaymentAddress address(toAddress);
    try {
        tozaddr_ = address.Get();
    } catch (const std::runtime_error& e) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid to address");
    }

    // Log the context info
    if (LogAcceptCategory("zrpcunsafe")) {
        LogPrint("zrpcunsafe", "%s: z_shieldcoinbase initialized (context=%s)\n", getId(), contextInfo.write());
    } else {
        LogPrint("zrpc", "%s: z_shieldcoinbase initialized\n", getId());
    }

    // Lock UTXOs
    lock_utxos();
}

AsyncRPCOperation_shieldcoinbase::~AsyncRPCOperation_shieldcoinbase() {}

void AsyncRPCOperation_shieldcoinbase::main() {
    if (isCancelled()) {
        unlock_utxos();  // clean up
        return;
    }

    set_state(OperationStatus::EXECUTING);
    start_execution_clock();

    bool success = false;

#ifdef ENABLE_MINING
#ifdef ENABLE_WALLET
    GenerateBitcoins(false, NULL, 0);
#else
    GenerateBitcoins(false, 0);
#endif
#endif

    try {
        success = main_impl();
    } catch (const UniValue& objError) {
        int code = find_value(objError, "code").get_int();
        std::string message = find_value(objError, "message").get_str();
        set_error_code(code);
        set_error_message(message);
    } catch (const std::runtime_error& e) {
        set_error_code(-1);
        set_error_message("runtime error: " + std::string(e.what()));
    } catch (const std::logic_error& e) {
        set_error_code(-1);
        set_error_message("logic error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        set_error_code(-1);
        set_error_message("general exception: " + std::string(e.what()));
    } catch (...) {
        set_error_code(-2);
        set_error_message("unknown error");
    }

#ifdef ENABLE_MINING
#ifdef ENABLE_WALLET
    GenerateBitcoins(GetBoolArg("-gen", false), pwalletMain, GetArg("-genproclimit", 1));
#else
    GenerateBitcoins(GetBoolArg("-gen", false), GetArg("-genproclimit", 1));
#endif
#endif

    stop_execution_clock();

    if (success) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    std::string s = strprintf("%s: z_shieldcoinbase finished (status=%s", getId(), getStateAsString());
    if (success) {
        s += strprintf(", txid=%s)\n", tx_.GetHash().ToString());
    } else {
        s += strprintf(", error=%s)\n", getErrorMessage());
    }
    LogPrintf("%s", s);

    unlock_utxos();  // clean up
}

bool AsyncRPCOperation_shieldcoinbase::main_impl() {
    CAmount minersFee = fee_;

    size_t numInputs = inputs_.size();

    // Check mempooltxinputlimit to avoid creating a transaction which the local mempool rejects
    size_t limit = (size_t)GetArg("-mempooltxinputlimit", 0);
    if (limit > 0 && numInputs > limit) {
        throw JSONRPCError(RPC_WALLET_ERROR,
                           strprintf("Number of inputs %d is greater than mempooltxinputlimit of %d", numInputs, limit));
    }

    CAmount targetAmount = 0;
    for (ShieldCoinbaseUTXO& utxo : inputs_) {
        targetAmount += utxo.amount;
    }

    if (targetAmount <= minersFee) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strprintf("Insufficient coinbase funds, have %s and miners fee is %s",
                                                                    FormatMoney(targetAmount), FormatMoney(minersFee)));
    }

    CAmount sendAmount = targetAmount - minersFee;
    LogPrint("zrpc", "%s: spending %s to shield %s with fee %s\n", getId(), FormatMoney(targetAmount), FormatMoney(sendAmount),
             FormatMoney(minersFee));

    // update the transaction with these inputs
    CMutableTransaction rawTx(tx_);
    for (ShieldCoinbaseUTXO& t : inputs_) {
        CTxIn in(COutPoint(t.txid, t.vout));
        rawTx.vin.push_back(in);
    }
    tx_ = CTransaction(rawTx);

    // Prepare raw transaction to handle JoinSplits
    CMutableTransaction mtx(tx_);
    crypto_sign_keypair(joinSplitPubKey_.begin(), joinSplitPrivKey_);
    mtx.joinSplitPubKey = joinSplitPubKey_;
    tx_ = CTransaction(mtx);

    // Create joinsplit
    UniValue obj(UniValue::VOBJ);
    ShieldCoinbaseJSInfo info;
    info.vpub_old = sendAmount;
    info.vpub_new = 0;
    JSOutput jso = JSOutput(tozaddr_, sendAmount);
    info.vjsout.push_back(jso);
    obj = perform_joinsplit(info);

    sign_send_raw_transaction(obj);
    return true;
}

/**
 * Sign and send a raw transaction.
 * Raw transaction as hex string should be in object field "rawtxn"
 */
void AsyncRPCOperation_shieldcoinbase::sign_send_raw_transaction(UniValue obj) {
    // Sign the raw transaction
    UniValue rawtxnValue = find_value(obj, "rawtxn");
    if (rawtxnValue.isNull()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing hex data for raw transaction");
    }
    std::string rawtxn = rawtxnValue.get_str();

    UniValue params = UniValue(UniValue::VARR);
    params.push_back(rawtxn);
    UniValue signResultValue = signrawtransaction(params, false);
    UniValue signResultObject = signResultValue.get_obj();
    UniValue completeValue = find_value(signResultObject, "complete");
    bool complete = completeValue.get_bool();
    if (!complete) {
        // TODO: #1366 Maybe get "errors" and print array vErrors into a string
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to sign transaction");
    }

    UniValue hexValue = find_value(signResultObject, "hex");
    if (hexValue.isNull()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing hex data for signed transaction");
    }
    std::string signedtxn = hexValue.get_str();

    // Send the signed transaction
    if (!testmode) {
        params.clear();
        params.setArray();
        params.push_back(signedtxn);
        UniValue sendResultValue = sendrawtransaction(params, false);
        if (sendResultValue.isNull()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Send raw transaction did not return an error or a txid.");
        }

        std::string txid = sendResultValue.get_str();

        UniValue o(UniValue::VOBJ);
        o.pushKV("txid", txid);
        set_result(o);
    } else {
        // Test mode does not send the transaction to the network.

        CDataStream stream(ParseHex(signedtxn), SER_NETWORK, PROTOCOL_VERSION);
        CTransaction tx;
        stream >> tx;

        UniValue o(UniValue::VOBJ);
        o.pushKV("test", 1);
        o.pushKV("txid", tx.GetHash().ToString());
        o.pushKV("hex", signedtxn);
        set_result(o);
    }

    // Keep the signed transaction so we can hash to the same txid
    CDataStream stream(ParseHex(signedtxn), SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;
    stream >> tx;
    tx_ = tx;
}

UniValue AsyncRPCOperation_shieldcoinbase::perform_joinsplit(ShieldCoinbaseJSInfo& info) {
    uint256 anchor = pcoinsTip->GetBestAnchor();
    if (anchor.IsNull()) {
        throw std::runtime_error("anchor is null");
    }

    // Make sure there are two inputs and two outputs
    while (info.vjsin.size() < ZC_NUM_JS_INPUTS) {
        info.vjsin.push_back(JSInput());
    }

    while (info.vjsout.size() < ZC_NUM_JS_OUTPUTS) {
        info.vjsout.push_back(JSOutput());
    }

    if (info.vjsout.size() != ZC_NUM_JS_INPUTS || info.vjsin.size() != ZC_NUM_JS_OUTPUTS) {
        throw std::runtime_error("unsupported joinsplit input/output counts");
    }

    CMutableTransaction mtx(tx_);

    LogPrint("zrpcunsafe",
             "%s: creating joinsplit at index %d (vpub_old=%s, vpub_new=%s, in[0]=%s, in[1]=%s, out[0]=%s, out[1]=%s)\n",
             getId(), tx_.GetVjoinsplit().size(), FormatMoney(info.vpub_old), FormatMoney(info.vpub_new),
             FormatMoney(info.vjsin[0].note.value()), FormatMoney(info.vjsin[1].note.value()),
             FormatMoney(info.vjsout[0].value), FormatMoney(info.vjsout[1].value));

    // Generate the proof, this can take over a minute.
    std::array<libzcash::JSInput, ZC_NUM_JS_INPUTS> inputs{info.vjsin[0], info.vjsin[1]};
    std::array<libzcash::JSOutput, ZC_NUM_JS_OUTPUTS> outputs{info.vjsout[0], info.vjsout[1]};
#ifdef __APPLE__
    std::array<uint64_t, ZC_NUM_JS_INPUTS> inputMap;
    std::array<uint64_t, ZC_NUM_JS_OUTPUTS> outputMap;
#else
    std::array<size_t, ZC_NUM_JS_INPUTS> inputMap;
    std::array<size_t, ZC_NUM_JS_OUTPUTS> outputMap;
#endif

    uint256 esk;

    JSDescription jsdesc =
        JSDescription::Randomized(mtx.nVersion == GROTH_TX_VERSION, *pzcashParams, joinSplitPubKey_, anchor, inputs, outputs,
                                  inputMap, outputMap, info.vpub_old, info.vpub_new, !this->testmode,
                                  &esk);  // parameter expects pointer to esk, so pass in address
    {
        auto verifier = libzcash::ProofVerifier::Strict();
        if (!(jsdesc.Verify(*pzcashParams, verifier, joinSplitPubKey_))) {
            throw std::runtime_error("error verifying joinsplit");
        }
    }

    mtx.vjoinsplit.push_back(jsdesc);

    // Empty output script.
    CScript scriptCode;
    CTransaction signTx(mtx);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);

    // Add the signature
    if (!(crypto_sign_detached(&mtx.joinSplitSig[0], NULL, dataToBeSigned.begin(), 32, joinSplitPrivKey_) == 0)) {
        throw std::runtime_error("crypto_sign_detached failed");
    }

    // Sanity check
    if (!(crypto_sign_verify_detached(&mtx.joinSplitSig[0], dataToBeSigned.begin(), 32, mtx.joinSplitPubKey.begin()) == 0)) {
        throw std::runtime_error("crypto_sign_verify_detached failed");
    }

    CTransaction rawTx(mtx);
    tx_ = rawTx;

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;

    std::string encryptedNote1;
    std::string encryptedNote2;
    {
        CDataStream ss2(SER_NETWORK, PROTOCOL_VERSION);
        ss2 << ((unsigned char)0x00);
        ss2 << jsdesc.ephemeralKey;
        ss2 << jsdesc.ciphertexts[0];
        ss2 << jsdesc.h_sig(*pzcashParams, joinSplitPubKey_);

        encryptedNote1 = HexStr(ss2.begin(), ss2.end());
    }
    {
        CDataStream ss2(SER_NETWORK, PROTOCOL_VERSION);
        ss2 << ((unsigned char)0x01);
        ss2 << jsdesc.ephemeralKey;
        ss2 << jsdesc.ciphertexts[1];
        ss2 << jsdesc.h_sig(*pzcashParams, joinSplitPubKey_);

        encryptedNote2 = HexStr(ss2.begin(), ss2.end());
    }

    UniValue arrInputMap(UniValue::VARR);
    UniValue arrOutputMap(UniValue::VARR);
    for (size_t i = 0; i < ZC_NUM_JS_INPUTS; i++) {
        arrInputMap.push_back(inputMap[i]);
    }
    for (size_t i = 0; i < ZC_NUM_JS_OUTPUTS; i++) {
        arrOutputMap.push_back(outputMap[i]);
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("encryptednote1", encryptedNote1);
    obj.pushKV("encryptednote2", encryptedNote2);
    obj.pushKV("rawtxn", HexStr(ss.begin(), ss.end()));
    obj.pushKV("inputmap", arrInputMap);
    obj.pushKV("outputmap", arrOutputMap);
    return obj;
}

/**
 * Override getStatus() to append the operation's context object to the default status object.
 */
UniValue AsyncRPCOperation_shieldcoinbase::getStatus() const {
    UniValue v = AsyncRPCOperation::getStatus();
    if (contextinfo_.isNull()) {
        return v;
    }

    UniValue obj = v.get_obj();
    obj.pushKV("method", "z_shieldcoinbase");
    obj.pushKV("params", contextinfo_);
    return obj;
}

/**
 * Lock input utxos
 */
void AsyncRPCOperation_shieldcoinbase::lock_utxos() {
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto utxo : inputs_) {
        COutPoint outpt(utxo.txid, utxo.vout);
        pwalletMain->LockCoin(outpt);
    }
}

/**
 * Unlock input utxos
 */
void AsyncRPCOperation_shieldcoinbase::unlock_utxos() {
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto utxo : inputs_) {
        COutPoint outpt(utxo.txid, utxo.vout);
        pwalletMain->UnlockCoin(outpt);
    }
}
