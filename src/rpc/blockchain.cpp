// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "addressindex.h"
#include "amount.h"
#include "base58.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "consensus/validation.h"
#include "main.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "rpc/server.h"
#include "streams.h"
#include "sync.h"
#include "util.h"
#include "zen/delay.h"

#include <stdint.h>

#include <univalue.h>

#include <regex>

#include "sc/asyncproofverifier.h"
#include "sc/sidechain.h"
#include "sc/sidechainrpc.h"

#include "validationinterface.h"
#include "txdb.h"
#include "maturityheightindex.h"

using namespace std;

using namespace Sidechain;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);
extern void CertToJSON(const CScCertificate& cert, const uint256 hashBlock, UniValue& entry);
void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);

double GetDifficultyINTERNAL(const CBlockIndex* blockindex, bool networkDifficulty)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (chainActive.Tip() == NULL)
            return 1.0;
        else
            blockindex = chainActive.Tip();
    }

    uint32_t bits;
    if (networkDifficulty) {
        bits = GetNextWorkRequired(blockindex, nullptr, Params().GetConsensus());
    } else {
        bits = blockindex->nBits;
    }

    uint32_t powLimit =
        UintToArith256(Params().GetConsensus().powLimit).GetCompact();
    int nShift = (bits >> 24) & 0xff;
    int nShiftAmount = (powLimit >> 24) & 0xff;

    double dDiff =
        (double)(powLimit & 0x00ffffff) /
        (double)(bits & 0x00ffffff);

    while (nShift < nShiftAmount)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > nShiftAmount)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

double GetDifficulty(const CBlockIndex* blockindex)
{
    return GetDifficultyINTERNAL(blockindex, false);
}

double GetNetworkDifficulty(const CBlockIndex* blockindex)
{
    return GetDifficultyINTERNAL(blockindex, true);
}

static UniValue ValuePoolDesc(
    const std::string &name,
    const boost::optional<CAmount> chainValue,
    const boost::optional<CAmount> valueDelta)
{
    UniValue rv(UniValue::VOBJ);
    rv.pushKV("id", name);
    rv.pushKV("monitored", (bool)chainValue);
    if (chainValue) {
        rv.pushKV("chainValue", ValueFromAmount(*chainValue));
        rv.pushKV("chainValueZat", *chainValue);
    }
    if (valueDelta) {
        rv.pushKV("valueDelta", ValueFromAmount(*valueDelta));
        rv.pushKV("valueDeltaZat", *valueDelta);
    }
    return rv;
}

UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", blockindex->GetBlockHash().GetHex());
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.pushKV("confirmations", confirmations);
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", blockindex->nVersion);
    result.pushKV("merkleroot", blockindex->hashMerkleRoot.GetHex());
    result.pushKV("time", (int64_t)blockindex->nTime);
    result.pushKV("nonce", blockindex->nNonce.GetHex());
    result.pushKV("solution", HexStr(blockindex->nSolution));
    result.pushKV("bits", strprintf("%08x", blockindex->nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());
    result.pushKV("scTxsCommitment", blockindex->hashScTxsCommitment.GetHex());
    result.pushKV("scCumTreeHash", blockindex->scCumTreeHash.GetHexRepr());

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());
    return result;
}

UniValue blockToDeltasJSON(const CBlock& block, const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", block.GetHash().GetHex());
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex)) {
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block is an orphan");
    }
    result.pushKV("confirmations", confirmations);
    result.pushKV("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION));
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", block.nVersion);
    result.pushKV("merkleroot", block.hashMerkleRoot.GetHex());

    UniValue deltas(UniValue::VARR);

    for (unsigned int i = 0; i < block.vtx.size(); i++) {
        const CTransaction &tx = block.vtx[i];
        const uint256 txhash = tx.GetHash();

        UniValue entry(UniValue::VOBJ);
        entry.pushKV("txid", txhash.GetHex());
        entry.pushKV("index", (int)i);

        UniValue inputs(UniValue::VARR);

        if (!tx.IsCoinBase()) {

            for (size_t j = 0; j < tx.GetVin().size(); j++) {
                const CTxIn input = tx.GetVin()[j];

                UniValue delta(UniValue::VOBJ);

                CSpentIndexValue spentInfo;
                CSpentIndexKey spentKey(input.prevout.hash, input.prevout.n);

                if (GetSpentIndex(spentKey, spentInfo)) {
                    if (spentInfo.addressType == AddressType::PUBKEY) {
                        delta.pushKV("address", CBitcoinAddress(CKeyID(spentInfo.addressHash)).ToString());
                    } else if (spentInfo.addressType == AddressType::SCRIPT)  {
                        delta.pushKV("address", CBitcoinAddress(CScriptID(spentInfo.addressHash)).ToString());
                    } else {
                        continue;
                    }
                    delta.pushKV("satoshis", -1 * spentInfo.satoshis);
                    delta.pushKV("index", (int)j);
                    delta.pushKV("prevtxid", input.prevout.hash.GetHex());
                    delta.pushKV("prevout", (int)input.prevout.n);

                    inputs.push_back(delta);
                } else {
                    throw JSONRPCError(RPC_INTERNAL_ERROR, "Spent information not available");
                }

            }
        }

        entry.pushKV("inputs", inputs);

        UniValue outputs(UniValue::VARR);

        for (unsigned int k = 0; k < tx.GetVout().size(); k++) {
            const CTxOut &out = tx.GetVout()[k];

            UniValue delta(UniValue::VOBJ);

            uint160 const addrHash = out.scriptPubKey.AddressHash();

            if (out.scriptPubKey.IsPayToScriptHash()) {
                delta.pushKV("address", CBitcoinAddress(CScriptID(addrHash)).ToString());

            } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                delta.pushKV("address", CBitcoinAddress(CKeyID(addrHash)).ToString());
            } else {
                continue;
            }

            delta.pushKV("satoshis", out.nValue);
            delta.pushKV("index", (int)k);

            outputs.push_back(delta);
        }

        entry.pushKV("outputs", outputs);
        deltas.push_back(entry);

    }
    result.pushKV("deltas", deltas);
    result.pushKV("time", block.GetBlockTime());
    result.pushKV("mediantime", (int64_t)blockindex->GetMedianTimePast());
    result.pushKV("nonce", block.nNonce.GetHex());
    result.pushKV("bits", strprintf("%08x", block.nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());
    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", block.GetHash().GetHex());
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;

    result.pushKV("confirmations", confirmations);
    result.pushKV("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION));
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", block.nVersion);
    result.pushKV("merkleroot", block.hashMerkleRoot.GetHex());
    result.pushKV("scTxsCommitment", block.hashScTxsCommitment.GetHex());

    UniValue txs(UniValue::VARR);
    BOOST_FOREACH(const CTransaction&tx, block.vtx)
    {
        if(txDetails)
        {
            UniValue objTx(UniValue::VOBJ);
            TxToJSON(tx, uint256(), objTx);
            txs.push_back(objTx);
        }
        else
            txs.push_back(tx.GetHash().GetHex());
    }

    result.pushKV("tx", txs);
    if (block.nVersion == BLOCK_VERSION_SC_SUPPORT)
    {
        UniValue certs(UniValue::VARR);
        BOOST_FOREACH(const CScCertificate& cert, block.vcert)
        {
            if(txDetails)
            {
                UniValue objCert(UniValue::VOBJ);
                CertToJSON(cert, uint256(), objCert);
                certs.push_back(objCert);
            }
            else
            {
                certs.push_back(cert.GetHash().GetHex());
            }
        }
        result.pushKV("cert", certs);
    }

    result.pushKV("time", block.GetBlockTime());
    result.pushKV("nonce", block.nNonce.GetHex());
    result.pushKV("solution", HexStr(block.nSolution));
    result.pushKV("bits", strprintf("%08x", block.nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());
    result.pushKV("anchor", blockindex->hashAnchorEnd.GetHex());
    result.pushKV("scCumTreeHash", blockindex->scCumTreeHash.GetHexRepr());

    UniValue valuePools(UniValue::VARR);
    valuePools.push_back(ValuePoolDesc("sprout", blockindex->nChainSproutValue, blockindex->nSproutValue));
    result.pushKV("valuePools", valuePools);

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());
    return result;
}

UniValue getblockcount(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "\nReturns the number of blocks in the longest block chain.\n"
            
            "\nResult:\n"
            "n    (numeric) the current block count\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getblockcount", "")
            + HelpExampleRpc("getblockcount", "")
        );

    LOCK(cs_main);
    return chainActive.Height();
}

UniValue getbestblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getbestblockhash\n"
            "\nReturns the hash of the best (most recent) block in the active block chain.\n"
            
            "\nResult\n"
            "\"hex\"    (string) the block hash hex encoded\n"
            
            "\nExamples\n"
            + HelpExampleCli("getbestblockhash", "")
            + HelpExampleRpc("getbestblockhash", "")
        );

    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash().GetHex();
}

UniValue getdifficulty(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "\nReturns the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            
            "\nResult:\n"
            "n.nnn       (numeric) the proof-of-work difficulty as a multiple of the minimum difficulty\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getdifficulty", "")
            + HelpExampleRpc("getdifficulty", "")
        );

    LOCK(cs_main);
    return GetNetworkDifficulty();
}

static void AddDependancy(const CTransactionBase& root, UniValue& info)
{
    std::vector<uint256> sDepHash = mempool.mempoolDirectDependenciesFrom(root);
    UniValue depends(UniValue::VARR);
    for(const uint256& hash: sDepHash)
    {
        depends.push_back(hash.ToString());
    }

    info.pushKV("depends", depends);
}

UniValue mempoolToJSON(bool fVerbose = false)
{
    if (fVerbose)
    {
        LOCK(mempool.cs);
        UniValue o(UniValue::VOBJ);
        BOOST_FOREACH(const PAIRTYPE(uint256, CTxMemPoolEntry)& entry, mempool.mapTx)
        {
            const uint256& hash = entry.first;
            const CTxMemPoolEntry& e = entry.second;
            UniValue info(UniValue::VOBJ);
            info.pushKV("size", (int)e.GetTxSize());
            info.pushKV("fee", ValueFromAmount(e.GetFee()));
            info.pushKV("time", e.GetTime());
            info.pushKV("height", (int)e.GetHeight());
            info.pushKV("startingpriority", e.GetPriority(e.GetHeight()));
            info.pushKV("currentpriority", e.GetPriority(chainActive.Height()));
            info.pushKV("isCert", false);
            const CTransaction& tx = e.GetTx();
            info.pushKV("version", tx.nVersion);
            AddDependancy(tx, info);
            o.pushKV(hash.ToString(), info);
        }
        BOOST_FOREACH(const PAIRTYPE(uint256, CCertificateMemPoolEntry)& entry, mempool.mapCertificate)
        {
            const uint256& hash = entry.first;
            const auto& e = entry.second;
            UniValue info(UniValue::VOBJ);
            info.pushKV("size", (int)e.GetCertificateSize());
            info.pushKV("fee", ValueFromAmount(e.GetFee()));
            info.pushKV("time", e.GetTime());
            info.pushKV("height", (int)e.GetHeight());
            info.pushKV("startingpriority", e.GetPriority(e.GetHeight()));
            info.pushKV("currentpriority", e.GetPriority(chainActive.Height()));
            info.pushKV("isCert", true);
            const CScCertificate& cert = e.GetCertificate();
            info.pushKV("version", cert.nVersion);
            AddDependancy(cert, info);
            o.pushKV(hash.ToString(), info);
        }
        BOOST_FOREACH(const auto& entry, mempool.mapDeltas)
        {
            const uint256& hash = entry.first;
            const auto& p = entry.second.first;
            const auto& f = entry.second.second;
            UniValue info(UniValue::VOBJ);
            info.pushKV("fee", ValueFromAmount(f));
            info.pushKV("priority", p);
            o.pushKV(hash.ToString(), info);
        }
        return o;
    }
    else
    {
        vector<uint256> vtxid;
        mempool.queryHashes(vtxid);

        UniValue a(UniValue::VARR);
        BOOST_FOREACH(const uint256& hash, vtxid)
            a.push_back(hash.ToString());

        return a;
    }
}

UniValue getrawmempool(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of string transaction ids.\n"
            
            "\nArguments:\n"
            "1. verbose                   (boolean, optional, default=false) true for a json object, false for array of transaction ids\n"
            "\nResult:                    (for verbose = false):\n"
            "[                            (json array of string)\n"
            "  \"transactionid\"          (string) the transaction id\n"
            "  ,...\n"
            "]\n"
            
            "\nResult: (for verbose = true):\n"
            "{                             (json object)\n"
            "  \"transactionid\": {        (json object)\n"
            "    \"size\": n,              (numeric) transaction size in bytes\n"
            "    \"fee\": n,               (numeric) transaction fee in " + CURRENCY_UNIT + "\n"
            "    \"time\": n,              (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT\n"
            "    \"height\": n,            (numeric) block height when transaction entered pool\n"
            "    \"startingpriority\": n,  (numeric) priority when transaction entered pool\n"
            "    \"currentpriority\": n,   (numeric) transaction priority now\n"
            "    \"depends\": [            (array) unconfirmed transactions used as inputs for this transaction\n"
            "        \"transactionid\",    (string) parent transaction id\n"
            "       ... ]\n"
            "  }, ...\n"
            "}\n"
            
            "\nExamples\n"
            + HelpExampleCli("getrawmempool", "true")
            + HelpExampleRpc("getrawmempool", "true")
        );

    LOCK(cs_main);

    bool fVerbose = false;
    if (params.size() > 0)
        fVerbose = params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

UniValue getblockdeltas(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockdeltas\n"
            "\nReturns ...  (require spentindex is enabled).\n"

            "\nArguments:\n"
            "1. \"hash\"                          (string, required) the block hash\n"

            "\nResult:\n"
            "{\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getblockdeltas", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"") +
            HelpExampleRpc("getblockdeltas", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\""));

    if(!fSpentIndex) {
        throw std::runtime_error("spentindex not enabled");
    }

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    return blockToDeltasJSON(block, pblockindex);
}

UniValue getblockhashes(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2)
        throw runtime_error(
            "getblockhashes timestamp\n"
            "\nReturns array of hashes of blocks within the timestamp range provided (requires timestampindex to be enabled).\n"
            "\nArguments:\n"
            "1. high         (numeric, required) The newer block timestamp\n"
            "2. low          (numeric, required) The older block timestamp\n"
            "3. options      (string, optional) A json object\n"
            "    {\n"
            "      \"noOrphans\":true   (boolean, required) will only include blocks on the main chain\n"
            "      \"logicalTimes\":true   (boolean, required) will include logical timestamps with hashes\n"
            "    }\n"
            "\nResult:\n"
            "[\n"
            "  \"hash\"         (string) The block hash\n"
            "]\n"
            "[\n"
            "  {\n"
            "    \"blockhash\": (string) The block hash\n"
            "    \"logicalts\": (numeric) The logical timestamp\n"
            "  }\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockhashes", "1231614698 1231024505") + 
            HelpExampleRpc("getblockhashes", "1231614698, 1231024505") + 
            HelpExampleCli("getblockhashes", "1231614698 1231024505 '{\"noOrphans\":false, \"logicalTimes\":true}'") + 
            HelpExampleRpc("getblockhashes", "1231614698, 1231024505, {\"noOrphans\":false, \"logicalTimes\":true}"));

    if (!fTimestampIndex) {
        throw std::runtime_error("timespentindex not enabled");
    }

    unsigned int high = params[0].get_int();
    unsigned int low = params[1].get_int();
    bool fActiveOnly = false;
    bool fLogicalTS = false;

    if (params.size() > 2) {
        const auto options = params[2];
        if(!options.isObject()) {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid options");
        }
        fActiveOnly = options["noOrphans"].get_bool();  // Will throw if not a valid bool
        fLogicalTS = options["logicalTimes"].get_bool();
    }

    std::vector<std::pair<uint256, unsigned int> > blockHashes;

    if (fActiveOnly)
        LOCK(cs_main);

    if (!GetTimestampIndex(high, low, fActiveOnly, blockHashes)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for block hashes");
    }

    UniValue result(UniValue::VARR);

    for (std::vector<std::pair<uint256, unsigned int> >::const_iterator it=blockHashes.begin(); it!=blockHashes.end(); it++) {
        if (fLogicalTS) {
            UniValue item(UniValue::VOBJ);
            item.pushKV("blockhash", it->first.GetHex());
            item.pushKV("logicalts", (int)it->second);
            result.push_back(item);
        } else {
            result.push_back(it->first.GetHex());
        }
    }

    return result;
}

UniValue getblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash index\n"
            "\nReturns hash of block in best-block-chain at index provided.\n"
            
            "\nArguments:\n"
            "1. index         (numeric, required) the block index\n"
            
            "\nResult:\n"
            "\"hash\"         (string) the block hash\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getblockhash", "1000")
            + HelpExampleRpc("getblockhash", "1000")
        );

    LOCK(cs_main);

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

UniValue getblockheader(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockheader \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for blockheader 'hash'.\n"
            "If verbose is true, returns an Object with information about blockheader <hash>.\n"
            
            "\nArguments:\n"
            "1. \"hash\"                          (string, required) the block hash\n"
            "2. verbose                           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\": \"hash\",                (string) the block hash (same as provided)\n"
            "  \"confirmations\": n,              (numeric) the number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"height\": n,                     (numeric) the block height or index\n"
            "  \"version\": n,                    (numeric) the block version\n"
            "  \"merkleroot\": \"xxxx\",          (string) the merkle root\n"
            "  \"time\": ttt,                     (numeric) the block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\": n,                      (numeric) the nonce\n"
            "  \"bits\": \"1d00ffff\",            (string) the bits\n"
            "  \"difficulty\": xxxx,              (numeric) the difficulty\n"
            "  \"previousblockhash\": \"hash\",   (string) the hash of the previous block\n"
            "  \"nextblockhash\": \"hash\"        (string) the hash of the next block\n"
            "}\n"
            
            "\nResult (for verbose=false):\n"
            "\"data\": \"hex\"                    (string) a string that is serialized, hex-encoded data for block 'hash'\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON(pblockindex);
}

UniValue getblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock \"hash|height\" ( verbose )\n"
            "\nIf verbosity is 0, returns a string that is serialized, hex-encoded data for the block.\n"
            "If verbosity is 1, returns an Object with information about the block.\n"
            "If verbosity is 2, returns an Object with information about the block and information about each transaction.\n"
            
            "\nArguments:\n"
            "1. \"hash|height\"                     (string, required) the block hash or height\n"
            "2. verbosity                           (numeric, optional, default=1) 0 for hex encoded data, 1 for a json object, and 2 for json object with transaction data,\n"
            "                                       also accept boolean for backward compatibility where true=1 and false=0\n"
            
            "\nResult (for verbose = 1):\n"
            "{\n"
            "  \"hash\": \"hash\",                  (string) the block hash (same as provided hash)\n"
            "  \"confirmations\": n,                (numeric) the number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\": n,                         (numeric) the block size\n"
            "  \"height\": n,                       (numeric) the block height or index (same as provided height)\n"
            "  \"version\": n,                      (numeric) the block version\n"
            "  \"merkleroot\": \"xxxx\",            (string) the merkle root\n"
            "  \"tx\": [                            (array of string) the transaction ids\n"
            "     \"transactionid\": \"hash\",      (string) the transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\": ttt,                       (numeric) the block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\": n,                        (numeric) the nonce\n"
            "  \"bits\": \"hex\",                   (string) the bits\n"
            "  \"difficulty\": xxxx,                (numeric) the difficulty\n"
            "  \"chainwork\": \"hex\",              (string) txpected number of hashes required to produce the chain up to this block (in hex)\n"
            "  \"anchor\": \"hex\",                 (string) the anchor\n"
            "  \"valuePools\": [                    (array) value pools\n"
            "      \"id\": \"sprout\"|\"sapling\",  (string) the pool id\n"
            "      \"monitored\": true|false,       (boolean) if is monitored or not\n"
            "      \"chainValue\": n.nnn,           (numeric) the chain value\n"
            "      \"chainValueZat\": n,            (numeric) the chain value zat\n"
            "      \"valueDelta\": n.nnn,           (numeric)the delta value\n"
            "      \"valueDeltaZat\": n             (numeric) the delta zat value\n"
            "  ],\n"
            "  \"previousblockhash\": \"hash\",     (string, optional) the hash of the previous block (if available)\n"
            "  \"nextblockhash\": \"hash\"          (string, optional) the hash of the next block (if available)\n"
            "}\n"
            
            "\nResult (for verbose=0):\n"
            "\"data\"                               (string) a string that is serialized, hex-encoded data for block 'hash'\n"
            
            "\nResult (for verbosity = 2):\n"
            "{\n"
            "  ...,                                 same output as verbosity = 1\n"
            "  \"tx\" : [                           (array of Objects) the transactions in the format of the getrawtransaction RPC\n"
            "         ,...\n"
            "  ],\n"
            "  ,...                     same output as verbosity = 1\n"
            "}\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getblock", "\"hash\"")
            + HelpExampleRpc("getblock", "\"hash\"")
            + HelpExampleCli("getblock", "height")
            + HelpExampleRpc("getblock", "height")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();

    // TODO - Sample clearly states that first argument can be either
    // a string hence a hash
    // a number hence a height
    // Check wheter the follwing block of code can be safely removed

    // If height is supplied, find the hash
    if (strHash.size() < (2 * sizeof(uint256))) {
        // std::stoi allows characters, whereas we want to be strict
        regex r("[[:digit:]]+");
        if (!regex_match(strHash, r)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block height parameter");
        }

        int nHeight = -1;
        try {
            nHeight = std::stoi(strHash);
        }
        catch (const std::exception &e) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block height parameter");
        }

        if (nHeight < 0 || nHeight > chainActive.Height()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
        }
        strHash = chainActive[nHeight]->GetBlockHash().GetHex();
    }

    uint256 hash(uint256S(strHash));

    int verbosity = 1;
    if (params.size() == 2) {
        verbosity = params[1].get_int(); // Throws if not NUM
        verbosity = std::min(std::max(verbosity, 0), 2); // Force in range - dont' bother to throw
    }

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    if (verbosity == 0)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex, verbosity >= 2);
}

UniValue getblockexpanded(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockexpanded \"hash|height\" ( verbose )\n"
            "\nIf verbosity is 1, returns an Object with information about the block.\n"
            "If verbosity is 2, returns an Object with information about the block and information about each transaction.\n"
            "\nIt works only with -maturityheightindex=1 and -txindex=1.\n"
            
            "\nArguments:\n"
            "1. \"hash|height\"                     (string, required) the block hash or height\n"
            "2. verbosity                           (numeric, optional, default=1) 0 for hex encoded data, 1 for a json object, and 2 for json object with transaction data,\n"
            "                                       also accept boolean for backward compatibility where true=1 and false=0\n"
            
            "\nResult (for verbose = 1):\n"
            "{\n"
            "  \"hash\": \"hash\",                  (string) the block hash (same as provided hash)\n"
            "  \"confirmations\": n,                (numeric) the number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\": n,                         (numeric) the block size\n"
            "  \"height\": n,                       (numeric) the block height or index (same as provided height)\n"
            "  \"version\": n,                      (numeric) the block version\n"
            "  \"merkleroot\": \"xxxx\",            (string) the merkle root\n"
            "  \"tx\": [                            (array of string) the transaction ids\n"
            "     \"transactionid\": \"hash\",      (string) the transaction id\n"
            "     ,...\n"
            "  ],\n"
             "  \"cert\": [                         (array of string) the certificate ids\n"
            "     \"certificateid\": \"hash\",      (string) the certificate id\n"
            "     ,...\n"
            "  ],\n"           
            "  \"time\": ttt,                       (numeric) the block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\": n,                        (numeric) the nonce\n"
            "  \"bits\": \"hex\",                   (string) the bits\n"
            "  \"difficulty\": xxxx,                (numeric) the difficulty\n"
            "  \"chainwork\": \"hex\",              (string) txpected number of hashes required to produce the chain up to this block (in hex)\n"
            "  \"anchor\": \"hex\",                 (string) the anchor\n"
            "  \"valuePools\": [                    (array) value pools\n"
            "      \"id\": \"sprout\"|\"sapling\",  (string) the pool id\n"
            "      \"monitored\": true|false,       (boolean) if is monitored or not\n"
            "      \"chainValue\": n.nnn,           (numeric) the chain value\n"
            "      \"chainValueZat\": n,            (numeric) the chain value zat\n"
            "      \"valueDelta\": n.nnn,           (numeric)the delta value\n"
            "      \"valueDeltaZat\": n             (numeric) the delta zat value\n"
            "  ],\n"
            "  \"previousblockhash\": \"hash\",     (string, optional) the hash of the previous block (if available)\n"
            "  \"nextblockhash\": \"hash\"          (string, optional) the hash of the next block (if available)\n"

            "}\n"
            "  \"matureCertificate\": [             (array of string) the certificate ids the became mature with this block\n"
            "     \"certificateid\": \"hash\",      (string) the certificate id\n"
            "     ,...\n"
            "  ],\n"  
            
            "\nResult (for verbosity = 2):\n"
            "{\n"
            "  ...,                                 same output as verbosity = 1\n"
            "  \"tx\" : [                           (array of Objects) the transactions in the format of the getrawtransaction RPC\n"
            "         ,...\n"
            "  ],\n"
            "  \"cert\" : [                         (array of Objects) the certificates in the format of the getrawtransaction RPC\n"
            "         ,...\n"
            "  ],\n"
            "  \"matureCertificate\" : [            (array of Objects) the certificates that became mature with this block in the format of the getrawtransaction RPC\n"
            "         ,...\n"
            "  ],\n"
            "  ,...                     same output as verbosity = 1\n"
            "}\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getblockexpanded", "\"hash\"")
            + HelpExampleRpc("getblockexpanded", "\"hash\"")
            + HelpExampleCli("getblockexpanded", "height")
            + HelpExampleRpc("getblockexpanded", "height")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();

    if (!fMaturityHeightIndex)
    {
        throw JSONRPCError(RPC_TYPE_ERROR, "maturityHeightIndex option not set: can not retrieve info");
    }

    // If height is supplied, find the hash
    if (strHash.size() < (2 * sizeof(uint256))) {
        // std::stoi allows characters, whereas we want to be strict
        regex r("[[:digit:]]+");
        if (!regex_match(strHash, r)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block height parameter");
        }

        int nHeight = -1;
        try {
            nHeight = std::stoi(strHash);
        }
        catch (const std::exception &e) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block height parameter");
        }

        if (nHeight < 0 || nHeight > chainActive.Height()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
        }
        strHash = chainActive[nHeight]->GetBlockHash().GetHex();
    }

    uint256 hash(uint256S(strHash));

    int verbosity = 1;
    if (params.size() > 1) {
        if(params[1].isNum()) {
            verbosity = params[1].get_int();
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Verbosity must be in range from 1 to 2");
        }
    }

    if (verbosity < 1 || verbosity > 2) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Verbosity must be in range from 1 to 2");
    }

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    UniValue blockJSON = blockToJSON(block, pblockindex, verbosity >= 2);
    
    //Add certificates that became mature with this block    
    if (block.nVersion == BLOCK_VERSION_SC_SUPPORT)
    {
        UniValue matureCertificate(UniValue::VARR);

        int height = pblockindex->nHeight;
        if (pblocktree == NULL)
        {
            throw JSONRPCError(RPC_TYPE_ERROR, "DB not initialized: can not retrieve info");
        }
        std::vector<CMaturityHeightKey> matureCertificatesKeys;
        pblocktree->ReadMaturityHeightIndex(height, matureCertificatesKeys);
        for (const CMaturityHeightKey& key : matureCertificatesKeys) 
        {
            if (verbosity == 2)
            {
                UniValue objCert(UniValue::VOBJ);
                CScCertificate certAttempt;
                uint256 hashBlock{};    
                if (GetCertificate(key.certId, certAttempt, hashBlock, false))
                {
                    CertToJSON(certAttempt, uint256(), objCert);
                    matureCertificate.push_back(objCert);
                }
                else
                {
                    throw JSONRPCError(RPC_TYPE_ERROR, "Can not retrieve info about the certificate!");
                }    
            }
            else
            {
                matureCertificate.push_back(key.certId.GetHex());
            }
        }
        blockJSON.pushKV("matureCertificate", matureCertificate);
    }
    return blockJSON;
}

UniValue gettxoutsetinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n"
            
            "\nResult:\n"
            "{\n"
            "  \"height\":n,                    (numeric) the current block height (index)\n"
            "  \"bestblock\": \"hex\",          (string) the best block hash hex\n"
            "  \"transactions\": n,             (numeric) the number of transactions\n"
            "  \"txouts\": n,                   (numeric) the number of output transactions\n"
            "  \"bytes_serialized\": n,         (numeric) the serialized size\n"
            "  \"hash_serialized\": \"hash\",   (string) the serialized hash\n"
            "  \"total_amount\": xxxx           (numeric) the total amount\n"
            "}\n"
            
            "\nExamples:\n"
            + HelpExampleCli("gettxoutsetinfo", "")
            + HelpExampleRpc("gettxoutsetinfo", "")
        );

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (pcoinsTip->GetStats(stats)) {
        ret.pushKV("height", (int64_t)stats.nHeight);
        ret.pushKV("bestblock", stats.hashBlock.GetHex());
        ret.pushKV("transactions", (int64_t)stats.nTransactions);
        ret.pushKV("txouts", (int64_t)stats.nTransactionOutputs);
        ret.pushKV("bytes_serialized", (int64_t)stats.nSerializedSize);
        ret.pushKV("hash_serialized", stats.hashSerialized.GetHex());
        ret.pushKV("total_amount", ValueFromAmount(stats.nTotalAmount));
    }
    return ret;
}

UniValue gettxout(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
            "gettxout \"txid\" n ( includemempool includeImmatureBTs)\n"
            "\nReturns details about an unspent transaction output.\n"
            
            "\nArguments:\n"
            "1. \"txid\"                    (string, required) The transaction id\n"
            "2. n                           (numeric, required) vout value\n"
            "3. includemempool              (boolean, optional, default=true) Whether to included the mem pool\n"
            "4. includeImmatureBTs          (boolean, optional, default=false) Only include mature outputs (and skip immature coinbase or cert BTs)."
            "\nResult:\n"
            "{\n"
            "  \"bestblock\": \"hash\",      (string) the block hash\n"
            "  \"confirmations\": n,         (numeric) the number of confirmations\n"
            "  \"value\": xxxx,              (numeric) the transaction value in " + CURRENCY_UNIT + "\n"
            "  \"scriptPubKey\": {           (json object)\n"
            "     \"asm\": \"code\",         (string) the asm\n"
            "     \"hex\": \"hex\",          (string) the hex\n"
            "     \"reqSigs\" : n,           (numeric) number of required signatures\n"
            "     \"type\": \"pubkeyhash\",  (string) the type, eg pubkeyhash\n"
            "     \"addresses\": [           (array of string) array of Horizen addresses\n"
            "        \"horizenaddress\"      (string) Horizen address\n"
            "        ,...\n"
            "     ]\n"
            "  },\n"
            "  \"version\": n,               (numeric) the version\n"
            "  \"coinbase\": true|false      (boolean) coinbase or not\n"
            "}\n"

            "\nExamples:\n"
            "\nGet unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nView the details\n"
            + HelpExampleCli("gettxout", "\"txid\" 1") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("gettxout", "\"txid\", 1")
        );

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = params[1].get_int();
    bool fMempool = true;
    if (params.size() > 2)
        fMempool = params[2].get_bool();

    bool fIncludeImmatureBTs = false;
    if (params.size() > 3)
        fIncludeImmatureBTs = params[3].get_bool();

    CCoins coins;
    if (fMempool) {
        LOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip, mempool);
        if (!view.GetCoins(hash, coins))
            return NullUniValue;
        mempool.pruneSpent(hash, coins); // TODO: this should be done by the CCoinsViewMemPool
    } else {
        if (!pcoinsTip->GetCoins(hash, coins))
            return NullUniValue;
    }
    if (n<0 || (unsigned int)n>=coins.vout.size() || coins.vout[n].IsNull())
        return NullUniValue;

    // Note: we may discard either immature coinbases and certificate BTs
    bool isOutputMature = coins.isOutputMature(n, pcoinsTip->GetHeight()+1);
    if(!fIncludeImmatureBTs && !isOutputMature)
        return NullUniValue;

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.pushKV("bestblock", pindex->GetBlockHash().GetHex());
    if ((unsigned int)coins.nHeight == MEMPOOL_HEIGHT)
        ret.pushKV("confirmations", 0);
    else
        ret.pushKV("confirmations", pindex->nHeight - coins.nHeight + 1);
    ret.pushKV("value", ValueFromAmount(coins.vout[n].nValue));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToJSON(coins.vout[n].scriptPubKey, o, true);

    ret.pushKV("scriptPubKey", o);
    ret.pushKV("version", coins.nVersion);
    ret.pushKV("certificate", coins.IsFromCert());
    ret.pushKV("coinbase", coins.IsCoinBase());

    bool isBackwardTransfer = coins.IsFromCert() && n >= coins.nFirstBwtPos;
    ret.pushKV("backwardtransfer", isBackwardTransfer);
    if(isBackwardTransfer) {
        ret.pushKV("mature", isOutputMature);
        bool isCoinFromMempool = coins.nBwtMaturityHeight == MEMPOOL_HEIGHT;
        ret.pushKV("maturityHeight", isCoinFromMempool ? -1 : coins.nBwtMaturityHeight);
        ret.pushKV("blocksToMaturity", isCoinFromMempool ? -1 :
                       isOutputMature ? 0 :
                           coins.nBwtMaturityHeight - (pcoinsTip->GetHeight() + 1));
    }

    return ret;
}

UniValue verifychain(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "verifychain ( checklevel numblocks )\n"
            "\nVerifies blockchain database.\n"
            
            "\nArguments:\n"
            "1. checklevel    (numeric, optional, 0-4, default=3) how thorough the block verification is\n"
            "2. numblocks     (numeric, optional, default=288, 0=all) the number of blocks to check\n"
            
            "\nResult:\n"
            "true|false       (boolean) verified or not\n"
            
            "\nExamples:\n"
            + HelpExampleCli("verifychain", "")
            + HelpExampleRpc("verifychain", "")
        );

    LOCK(cs_main);

    int nCheckLevel = GetArg("-checklevel", 3);
    int nCheckDepth = GetArg("-checkblocks", 288);
    if (params.size() > 0)
        nCheckLevel = params[0].get_int();
    if (params.size() > 1)
        nCheckDepth = params[1].get_int();

    return CVerifyDB().VerifyDB(pcoinsTip, nCheckLevel, nCheckDepth);
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int minVersion, CBlockIndex* pindex, int nRequired, const Consensus::Params& consensusParams)
{
    int nFound = 0;
    CBlockIndex* pstart = pindex;
    for (int i = 0; i < consensusParams.nMajorityWindow && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }

    UniValue rv(UniValue::VOBJ);
    rv.pushKV("status", nFound >= nRequired);
    rv.pushKV("found", nFound);
    rv.pushKV("required", nRequired);
    rv.pushKV("window", consensusParams.nMajorityWindow);
    return rv;
}

static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.pushKV("id", name);
    rv.pushKV("version", version);
    rv.pushKV("enforce", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityEnforceBlockUpgrade, consensusParams));
    rv.pushKV("reject", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityRejectBlockOutdated, consensusParams));
    return rv;
}

UniValue getblockchaininfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding block chain processing.\n"
            
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"xxxx\",             (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "  \"blocks\": xxxxxx,              (numeric) the current number of blocks processed in the server\n"
            "  \"headers\": xxxxxx,             (numeric) the current number of headers we have validated\n"
            "  \"bestblockhash\": \"...\",      (string) the hash of the currently best block\n"
            "  \"difficulty\": xxxxxx,          (numeric) the current difficulty\n"
            "  \"verificationprogress\": xxxx,  (numeric) estimate of verification progress [0..1]\n"
            "  \"chainwork\": \"xxxx\"          (string) total amount of work in active chain, in hexadecimal\n"
            "  \"commitments\": xxxxxx,         (numeric) the current number of note commitments in the commitment tree\n"
            "  \"softforks\": [                 (array) status of softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",          (string) name of softfork\n"
            "        \"version\": xx,           (numeric) block version\n"
            "        \"enforce\": {             (object) progress toward enforcing the softfork rules for new-version blocks\n"
            "           \"status\": xx,         (boolean) true if threshold reached\n"
            "           \"found\": xx,          (numeric) number of blocks with the new version found\n"
            "           \"required\": xx,       (numeric) number of blocks required to trigger\n"
            "           \"window\": xx,         (numeric) maximum size of examined window of recent blocks\n"
            "        },\n"
            "        \"reject\": { ... }        (object) progress toward rejecting pre-softfork blocks (same fields as \"enforce\")\n"
            "     }, ...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getblockchaininfo", "")
            + HelpExampleRpc("getblockchaininfo", "")
        );

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("chain",                 Params().NetworkIDString());
    obj.pushKV("blocks",                (int)chainActive.Height());
    obj.pushKV("headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1);
    obj.pushKV("bestblockhash",         chainActive.Tip()->GetBlockHash().GetHex());
    obj.pushKV("difficulty",            (double)GetNetworkDifficulty());
    obj.pushKV("verificationprogress",  Checkpoints::GuessVerificationProgress(Params().Checkpoints(), chainActive.Tip()));
    obj.pushKV("chainwork",             chainActive.Tip()->nChainWork.GetHex());
    obj.pushKV("pruned",                fPruneMode);

    ZCIncrementalMerkleTree tree;
    pcoinsTip->GetAnchorAt(pcoinsTip->GetBestAnchor(), tree);
    obj.pushKV("commitments",           tree.size());

    CBlockIndex* tip = chainActive.Tip();
    UniValue valuePools(UniValue::VARR);
    valuePools.push_back(ValuePoolDesc("sprout", tip->nChainSproutValue, boost::none));
    obj.pushKV("valuePools",            valuePools);

    const Consensus::Params& consensusParams = Params().GetConsensus();
    UniValue softforks(UniValue::VARR);
    UniValue bip9_softforks(UniValue::VOBJ);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    obj.pushKV("softforks", softforks);

    if (fPruneMode)
    {
        CBlockIndex *block = chainActive.Tip();
        while (block && block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA))
            block = block->pprev;

        if (block) obj.pushKV("pruneheight", block->nHeight);
    }
    return obj;
}

UniValue getchaintips(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getchaintips\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nArguments:\n"
            "1. \"with-penalties\" (boolean, optional) show informations related to branches penalty\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"height\": xxxx,                  (numeric) height of the chain tip\n"
            "    \"hash\": \"xxxx\"                   (string) block hash of the tip\n"
            "    \"branchlen\": 0                   (numeric) zero for main chain\n"
            "    \"status\": \"active\"               (string) \"active\" for the main chain\n"
            "    \"penalty-at-start\": \"xxxx\"       (numeric, optional) penalty of the first block in the branch\n"
            "    \"penalty-at-tip\": \"xxxx\"         (numeric, optional) penalty of the current tip of the branch\n"
            "    \"blocks-to-mainchain\": \"xxxx\"    (numeric, optional) confirmations needed for current branch to become the active chain (capped to 2000) \n"
            "  },\n"
            "  {\n"
            "    \"height\": xxxx,\n"
            "    \"hash\": \"xxxx\",\n"
            "    \"branchlen\": 1                   (numeric) length of branch connecting the tip to the main chain\n"
            "    \"status\": \"xxxx\"                 (string) status of the chain (active, valid-fork, valid-headers, headers-only, invalid)\n"
            "    \"penalty-at-start\": \"xxxx\"       (numeric, optional) penalty of the first block in the branch\n"
            "    \"penalty-at-tip\": \"xxxx\"         (numeric, optional) penalty of the current tip of the branch\n"
            "    \"blocks-to-mainchain\": \"xxxx\"    (numeric, optional) confirmations needed for current branch to become the active chain (capped to 2000) \n"
            "  }\n"
            "  ,...\n"
            "]\n"
            
            "Possible values for status:\n"
            "1.  \"invalid\"                 this branch contains at least one invalid block\n"
            "2.  \"headers-only\"            not all blocks for this branch are available, but the headers are valid\n"
            "3.  \"valid-headers\"           all blocks are available for this branch, but they were never fully validated\n"
            "4.  \"valid-fork\"              this branch is not part of the active chain, but is fully validated\n"
            "5.  \"active\"                  this is the tip of the active main chain, which is certainly valid\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getchaintips", "")
            + HelpExampleRpc("getchaintips", "")
        );

    LOCK(cs_main);

    if ((params.size() >= 1) && !params[0].isBool())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "\"with-penalties\" paramenter should be boolean");

    bool bShowPenaltyInfo = (params.size() >= 1)? params[0].getBool() : false;

    /* Build up a list of chain tips.  We start with the list of all
       known blocks, and successively remove blocks that appear as pprev
       of another block. */
    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    for(const PAIRTYPE(const uint256, CBlockIndex*)& item: mapBlockIndex)
        setTips.insert(item.second);
    for(const PAIRTYPE(const uint256, CBlockIndex*)& item: mapBlockIndex) {
        const CBlockIndex* pprev = item.second->pprev;
        if (pprev)
            setTips.erase(pprev);
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    for(const CBlockIndex* forkTip: setTips) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("height", forkTip->nHeight);
        obj.pushKV("hash", forkTip->phashBlock->GetHex());

        const int branchLen = forkTip->nHeight - chainActive.FindFork(forkTip)->nHeight;
        obj.pushKV("branchlen", branchLen);

        string status;
        if (chainActive.Contains(forkTip)) {
            // This block is part of the currently active chain.
            status = "active";
        } else if (forkTip->nStatus & BLOCK_FAILED_MASK) {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        } else if (forkTip->nChainTx == 0) {
            // This block cannot be connected because full block data for it or one of its parents is missing.
            status = "headers-only";
        } else if (forkTip->IsValid(BLOCK_VALID_SCRIPTS)) {
            // This block is fully validated, but no longer part of the active chain. It was probably the active block once, but was reorganized.
            status = "valid-fork";
        } else if (forkTip->IsValid(BLOCK_VALID_TREE)) {
            // The headers for this block are valid, but it has not been validated. It was probably never part of the most-work chain.
            status = "valid-headers";
        } else {
            // No clue.
            status = "unknown";
        }
        obj.pushKV("status", status);

        if (bShowPenaltyInfo)
        {
            const CBlockIndex* pFirstBlockInBranch = forkTip;
            for(; pFirstBlockInBranch != nullptr && pFirstBlockInBranch->pprev != nullptr
                && !chainActive.Contains(pFirstBlockInBranch->pprev);
                pFirstBlockInBranch = pFirstBlockInBranch->pprev);
            
            obj.pushKV("penalty-at-start",    pFirstBlockInBranch->nChainDelay);
            obj.pushKV("penalty-at-tip",      forkTip->nChainDelay);
            if (forkTip == chainActive.Tip())
                obj.pushKV("blocks-to-mainchain", 0);
            else
                obj.pushKV("blocks-to-mainchain", blocksToOvertakeTarget(forkTip, chainActive.Tip()));
        }

        res.push_back(obj);
    }

    return res;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("size", (int64_t) mempool.size());
    ret.pushKV("bytes", (int64_t) mempool.GetTotalSize());
    ret.pushKV("usage", (int64_t) mempool.DynamicMemoryUsage());

    if (Params().NetworkIDString() == "regtest") {
        ret.pushKV("fullyNotified", mempool.IsFullyNotified());
    }

    return ret;
}

UniValue getmempoolinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getmempoolinfo\n"
            "\nReturns details on the active state of the TX memory pool.\n"
            
            "\nResult:\n"
            "{\n"
            "  \"size\": xxxxx                (numeric) current tx count\n"
            "  \"bytes\": xxxxx               (numeric) sum of all tx sizes\n"
            "  \"usage\": xxxxx               (numeric) total memory usage for the mempool\n"
            "}\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getmempoolinfo", "")
            + HelpExampleRpc("getmempoolinfo", "")
        );

    return mempoolInfoToJSON();
}

UniValue invalidateblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "invalidateblock \"hash\"\n"
            "\nPermanently marks a block as invalid, as if it violated a consensus rule.\n"
            
            "\nArguments:\n"
            "1. hash   (string, required) the hash of the block to mark as invalid\n"
            
            "\nResult:\n"
            "Nothing\n"

            "\nExamples:\n"
            + HelpExampleCli("invalidateblock", "\"blockhash\"")
            + HelpExampleRpc("invalidateblock", "\"blockhash\"")
        );

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        InvalidateBlock(state, pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(state);
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue reconsiderblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "reconsiderblock \"hash\"\n"
            "\nRemoves invalidity status of a block and its descendants, reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            
            "\nArguments:\n"
            "1. hash   (string, required) the hash of the block to reconsider\n"
            
            "\nResult:\n"
            "Nothing\n"
            
            "\nExamples:\n"
            + HelpExampleCli("reconsiderblock", "\"blockhash\"")
            + HelpExampleRpc("reconsiderblock", "\"blockhash\"")
        );

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        ReconsiderBlock(state, pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(state);
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

static void addScUnconfCcData(const uint256& scId, UniValue& sc)
{
    if (mempool.mapSidechains.count(scId) == 0)
        return;

    UniValue ia(UniValue::VARR);
    if (mempool.hasSidechainCreationTx(scId))
    {
        const uint256& hash = mempool.mapSidechains.at(scId).scCreationTxHash;
        const CTransaction & scCrTx = mempool.mapTx.at(hash).GetTx();
        for (const auto& scCrAmount : scCrTx.GetVscCcOut())
        {
            if (scId == scCrAmount.GetScId())
            {
                 UniValue o(UniValue::VOBJ);
                 o.pushKV("unconfAmount", ValueFromAmount(scCrAmount.nValue));
                 ia.push_back(o);
             }
        }
    }

    for (const auto& fwdHash: mempool.mapSidechains.at(scId).fwdTxHashes)
    {
        const CTransaction & fwdTx = mempool.mapTx.at(fwdHash).GetTx();
        for (const auto& fwdAmount : fwdTx.GetVftCcOut())
        {
            if (scId == fwdAmount.scId)
            {
                 UniValue o(UniValue::VOBJ);
                 o.pushKV("unconfAmount", ValueFromAmount(fwdAmount.GetScValue()));
                 ia.push_back(o);
             }
        }
    }

    for (const auto& mbtrHash: mempool.mapSidechains.at(scId).mcBtrsTxHashes)
    {
        const CTransaction & mbtrTx = mempool.mapTx.at(mbtrHash).GetTx();
        for (const auto& mbtrAmount : mbtrTx.GetVBwtRequestOut())
        {
            if (scId == mbtrAmount.scId)
            {
                 UniValue o(UniValue::VOBJ);
                 o.pushKV("unconfAmount", ValueFromAmount(mbtrAmount.GetScValue()));
                 ia.push_back(o);
             }
        }
    }

    if (ia.size() > 0)
        sc.pushKV("unconfImmatureAmounts", ia);

    // there are no info about bwt requests in sc db, therefore we do not include them neither when they are in mempool
}

bool FillScRecordFromInfo(const uint256& scId, const CSidechain& info, CSidechain::State scState, const CCoinsViewCache& scView, 
    UniValue& sc, bool bOnlyAlive, bool bVerbose)
{
    if (bOnlyAlive && (scState != CSidechain::State::ALIVE))
        return false;

    sc.pushKV("scid", scId.GetHex());
    if (!info.IsNull() )
    {
        int currentEpoch = (scState == CSidechain::State::ALIVE)?
                info.EpochFor(chainActive.Height()):
                info.EpochFor(info.GetScheduledCeasingHeight());
 
        sc.pushKV("balance", ValueFromAmount(info.balance));
        sc.pushKV("epoch", currentEpoch);
        sc.pushKV("endEpochHeight", info.GetEndHeightForEpoch(currentEpoch));
        sc.pushKV("state", CSidechain::stateToString(scState));
        sc.pushKV("ceasingHeight", info.GetScheduledCeasingHeight());
 
        if (bVerbose)
        {
            sc.pushKV("creatingTxHash", info.creationTxHash.GetHex());
        }
 
        sc.pushKV("createdAtBlockHeight", info.creationBlockHeight);
        sc.pushKV("lastCertificateEpoch", info.lastTopQualityCertReferencedEpoch);
        sc.pushKV("lastCertificateHash", info.lastTopQualityCertHash.GetHex());
        sc.pushKV("lastCertificateQuality", info.lastTopQualityCertQuality);
        sc.pushKV("lastCertificateAmount", ValueFromAmount(info.lastTopQualityCertBwtAmount));

        const CScCertificateView& certView = scView.GetActiveCertView(scId);
        sc.pushKV("activeFtScFee", ValueFromAmount(certView.forwardTransferScFee));
        sc.pushKV("activeMbtrScFee", ValueFromAmount(certView.mainchainBackwardTransferRequestScFee));
 
        // creation parameters
        sc.pushKV("mbtrRequestDataLength", info.fixedParams.mainchainBackwardTransferRequestDataLength);
        sc.pushKV("withdrawalEpochLength", info.fixedParams.withdrawalEpochLength);
        sc.pushKV("version", info.fixedParams.version);
        sc.pushKV("certSubmissionWindowLength", info.GetCertSubmissionWindowLength());
 
        if (bVerbose)
        {
            sc.pushKV("certProvingSystem", Sidechain::ProvingSystemTypeToString(info.fixedParams.wCertVk.getProvingSystemType()));
            sc.pushKV("wCertVk", info.fixedParams.wCertVk.GetHexRepr());
            sc.pushKV("customData", HexStr(info.fixedParams.customData));

            if (info.fixedParams.constant.is_initialized())
                sc.pushKV("constant", info.fixedParams.constant->GetHexRepr());
            else
                sc.pushKV("constant", std::string{"NOT INITIALIZED"});

            if(info.fixedParams.wCeasedVk.is_initialized())
            {
                sc.pushKV("cswProvingSystem", Sidechain::ProvingSystemTypeToString(info.fixedParams.wCeasedVk.get().getProvingSystemType()));
                sc.pushKV("wCeasedVk", info.fixedParams.wCeasedVk.get().GetHexRepr());
            }
            else
                sc.pushKV("wCeasedVk", std::string{"NOT INITIALIZED"});

            UniValue arrFieldElementConfig(UniValue::VARR);
            for(const auto& cfgEntry: info.fixedParams.vFieldElementCertificateFieldConfig)
            {
                arrFieldElementConfig.push_back(cfgEntry.getBitSize());
            }
            sc.pushKV("vFieldElementCertificateFieldConfig", arrFieldElementConfig);

            UniValue arrBitVectorConfig(UniValue::VARR);
            for(const auto& cfgEntry: info.fixedParams.vBitVectorCertificateFieldConfig)
            {
                UniValue singlePair(UniValue::VARR);
                singlePair.push_back(cfgEntry.getBitVectorSizeBits());
                singlePair.push_back(cfgEntry.getMaxCompressedSizeBytes());
                arrBitVectorConfig.push_back(singlePair);
            }
            sc.pushKV("vBitVectorCertificateFieldConfig", arrBitVectorConfig);

            sc.pushKV("pastFtScFee", ValueFromAmount(info.pastEpochTopQualityCertView.forwardTransferScFee));
            sc.pushKV("pastMbtrScFee", ValueFromAmount(info.pastEpochTopQualityCertView.mainchainBackwardTransferRequestScFee));
            sc.pushKV("lastFtScFee", ValueFromAmount(info.lastTopQualityCertView.forwardTransferScFee));
            sc.pushKV("lastMbtrScFee", ValueFromAmount(info.lastTopQualityCertView.mainchainBackwardTransferRequestScFee));
        }
 
        UniValue ia(UniValue::VARR);
        for(const auto& entry: info.mImmatureAmounts)
        {
            UniValue o(UniValue::VOBJ);
            o.pushKV("maturityHeight", entry.first);
            o.pushKV("amount", ValueFromAmount(entry.second));
            ia.push_back(o);
        }
        sc.pushKV("immatureAmounts", ia);

        UniValue sf(UniValue::VARR);
        for(const auto& entry: info.scFees)
        {
            UniValue o(UniValue::VOBJ);
            o.pushKV("forwardTxScFee", ValueFromAmount(entry.forwardTxScFee));
            o.pushKV("mbtrTxScFee", ValueFromAmount(entry.mbtrTxScFee));
            sf.push_back(o);
        }
        sc.pushKV("scFees", sf);

        // get unconfirmed data if any
        if (mempool.hasSidechainCertificate(scId))
        {
            const uint256& topQualCertHash    = mempool.mapSidechains.at(scId).GetTopQualityCert()->second;
            const CScCertificate& topQualCert = mempool.mapCertificate.at(topQualCertHash).GetCertificate();
 
            sc.pushKV("unconfTopQualityCertificateEpoch",   topQualCert.epochNumber);
            sc.pushKV("unconfTopQualityCertificateHash",    topQualCertHash.GetHex());
            sc.pushKV("unconfTopQualityCertificateQuality", topQualCert.quality);
            sc.pushKV("unconfTopQualityCertificateAmount",  ValueFromAmount(topQualCert.GetValueOfBackwardTransfers()));
        }

        addScUnconfCcData(scId, sc);
    }
    else
    {
        if (mempool.hasSidechainCreationTx(scId))
        {
            const uint256& scCreationHash = mempool.mapSidechains.at(scId).scCreationTxHash;
            const CTransaction & scCreationTx = mempool.mapTx.at(scCreationHash).GetTx();

            CSidechain info;
            for (const auto& scCreation : scCreationTx.GetVscCcOut())
            {
                if (scId == scCreation.GetScId())
                {
                    info.creationTxHash = scCreationHash;
                    info.fixedParams.version = scCreation.version;
                    info.fixedParams.withdrawalEpochLength = scCreation.withdrawalEpochLength;
                    info.fixedParams.customData = scCreation.customData;
                    info.fixedParams.constant = scCreation.constant;
                    info.fixedParams.wCertVk = scCreation.wCertVk;
                    info.fixedParams.wCeasedVk = scCreation.wCeasedVk;
                    info.fixedParams.vFieldElementCertificateFieldConfig = scCreation.vFieldElementCertificateFieldConfig;
                    info.fixedParams.vBitVectorCertificateFieldConfig = scCreation.vBitVectorCertificateFieldConfig;
                    break;
                }
            }

            sc.pushKV("state", CSidechain::stateToString(CSidechain::State::UNCONFIRMED));
            sc.pushKV("unconfCreatingTxHash", info.creationTxHash.GetHex());
            sc.pushKV("unconfWithdrawalEpochLength", info.fixedParams.withdrawalEpochLength);
            sc.pushKV("unconfVersion", info.fixedParams.version);
            sc.pushKV("unconfCertSubmissionWindowLength", info.GetCertSubmissionWindowLength());

            if (bVerbose)
            {
                sc.pushKV("unconfCertProvingSystem", Sidechain::ProvingSystemTypeToString(info.fixedParams.wCertVk.getProvingSystemType()));
                sc.pushKV("unconfWCertVk", info.fixedParams.wCertVk.GetHexRepr());
                sc.pushKV("unconfCustomData", HexStr(info.fixedParams.customData));

                if(info.fixedParams.constant.is_initialized())
                    sc.pushKV("unconfConstant", info.fixedParams.constant->GetHexRepr());
                else
                    sc.pushKV("unconfConstant", std::string{"NOT INITIALIZED"});

                if(info.fixedParams.wCeasedVk.is_initialized())
                {
                    sc.pushKV("unconfCswProvingSystem", Sidechain::ProvingSystemTypeToString(info.fixedParams.wCeasedVk.get().getProvingSystemType()));
                    sc.pushKV("unconfWCeasedVk", info.fixedParams.wCeasedVk.get().GetHexRepr());
                }
                else
                    sc.pushKV("unconfWCeasedVk", std::string{"NOT INITIALIZED"});

                UniValue arrFieldElementConfig(UniValue::VARR);
                for(const auto& cfgEntry: info.fixedParams.vFieldElementCertificateFieldConfig)
                {
                    arrFieldElementConfig.push_back(cfgEntry.getBitSize());
                }
                sc.pushKV("unconfVFieldElementCertificateFieldConfig", arrFieldElementConfig);

                UniValue arrBitVectorConfig(UniValue::VARR);
                for(const auto& cfgEntry: info.fixedParams.vBitVectorCertificateFieldConfig)
                {
                    UniValue singlePair(UniValue::VARR);
                    singlePair.push_back(cfgEntry.getBitVectorSizeBits());
                    singlePair.push_back(cfgEntry.getMaxCompressedSizeBytes());
                    arrBitVectorConfig.push_back(singlePair);
                }
                sc.pushKV("unconfVBitVectorCertificateFieldConfig", arrBitVectorConfig);
            }

            addScUnconfCcData(scId, sc);
        }
        else
        {
            // nowhere to be found
            return false;
        }
    }

    return true;
}

bool FillScRecord(const uint256& scId, UniValue& scRecord, bool bOnlyAlive, bool bVerbose)
{
    CSidechain sidechain;
    CCoinsViewCache scView(pcoinsTip);
    if (!scView.GetSidechain(scId, sidechain)) {
        LogPrint("sc", "%s():%d - scid[%s] not yet created\n", __func__, __LINE__, scId.ToString() );
    }
    CSidechain::State scState = scView.GetSidechainState(scId);

    return FillScRecordFromInfo(scId, sidechain, scState, scView, scRecord, bOnlyAlive, bVerbose);
}

int FillScList(UniValue& scItems, bool bOnlyAlive, bool bVerbose, int from=0, int to=-1)
{
    std::set<uint256> sScIds;
    {
        LOCK(mempool.cs);
        CCoinsViewMemPool scView(pcoinsTip, mempool);

        scView.GetScIds(sScIds);
    }

    if (sScIds.size() == 0)
        return 0;

    // means upper limit max
    if (to == -1)
    {
        to = sScIds.size();
    }

    // basic check of interval parameters
    if ( from < 0 || to < 0 || from >= to)
    {
        LogPrint("sc", "invalid interval: from[%d], to[%d] (sz=%d)\n", from, to, sScIds.size());
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid interval");
    }

    UniValue totalResult(UniValue::VARR);
    std::set<uint256>::iterator it = sScIds.begin();

    while (it != sScIds.end())
    {
        UniValue scRecord(UniValue::VOBJ);
        if (FillScRecord(*it, scRecord, bOnlyAlive, bVerbose))
            totalResult.push_back(scRecord);
        ++it;
    }

    // check consistency of interval in the filtered results list
    // --
    // 'from' must be in the valid interval
    if (from > totalResult.size())
    {
        LogPrint("sc", "invalid interval: from[%d] > sz[%d]\n", from, totalResult.size());
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid interval");
    }

    // 'to' must be a formally valid upper bound interval number (positive and greater than 'from') but it is
    // topped anyway to the upper bound value 
    if (to > totalResult.size())
    {
        to = totalResult.size();
    }

    auto vec = totalResult.getValues();
    auto first = vec.begin() + from;
    auto last  = vec.begin() + to;

    while (first != last)
    {
        scItems.push_back(*first++);
    }

    return vec.size(); 
}

void FillCertDataHash(const uint256& scid, UniValue& ret)
{
    CCoinsViewCache scView(pcoinsTip);

    if (!scView.HaveSidechain(scid))
    {
        LogPrint("sc", "%s():%d - scid[%s] not yet created\n", __func__, __LINE__, scid.ToString() );
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("scid not yet created: ") + scid.ToString());
    }

    CFieldElement certDataHash = scView.GetActiveCertView(scid).certDataHash;
    if (certDataHash.IsNull() )
    {
        LogPrint("sc", "%s():%d - scid[%s] active cert data hash not in db\n", __func__, __LINE__, scid.ToString());
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("missing active cert data hash for required scid"));
    }
    ret.pushKV("certDataHash", certDataHash.GetHexRepr());
}

void FillCeasingCumScTxCommTree(const uint256& scid, UniValue& ret)
{
    CCoinsViewCache scView(pcoinsTip);

    if (!scView.HaveSidechain(scid))
    {
        LogPrint("sc", "%s():%d - scid[%s] not yet created\n", __func__, __LINE__, scid.ToString() );
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("scid not yet created: ") + scid.ToString());
    }

    CFieldElement fe = scView.GetCeasingCumTreeHash(scid);
    if (fe.IsNull() )
    {
        LogPrint("sc", "%s():%d - scid[%s] ceasing cum sc commitment tree not in db\n", __func__, __LINE__, scid.ToString());
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("missing ceasing cum sc commitment tree not for required scid"));
    }
    ret.pushKV("ceasingCumScTxCommTree", fe.GetHexRepr());
}

UniValue getscinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() == 0 || params.size() > 5)
        throw runtime_error(
            "getscinfo (\"scid\" onlyAlive)\n"
            "\nArguments:\n"
            "1. \"scid\"   (string, mandatory) Retrieve only information about specified scid, \"*\" means all \n"
            "2. onlyAlive (bool, optional, default=false) Retrieve only information for alive sidechains\n"
            "3. verbose   (bool, optional, default=true) If false include only essential info in result\n"
            "   --- meaningful if scid is not specified:\n"
            "4. from      (integer, optional, default=0) If set, limit the starting item index (0-base) in the result array to this entry (included)\n"
            "5. to        (integer, optional, default=-1) If set, limit the ending item index (0-base) in the result array to this entry (excluded) (-1 means max)\n"
            "\nReturns side chain info for the given id or for all of the existing sc if the id is not given.\n"
            "\nResult:\n"
            "{\n"
            "  \"totalItems\":            xx,      (numeric) number of items found\n"
            "  \"from\":                  xx,      (numeric) index of the starting item (included in result)\n"
            "  \"to\":                    xx,      (numeric) index of the ending item (excluded in result)\n"
            "  \"items\":[\n"
            "   {\n"
            "     \"scid\":                               xxxxx,   (string)  sidechain ID\n"
            "     \"balance\":                            xxxxx,   (numeric) available balance\n"
            "     \"epoch\":                              xxxxx,   (numeric) current epoch for this sidechain\n"
            "     \"endEpochHeight\":                     xxxxx,   (numeric) height of the last block of the current epoch\n"
            "     \"state\":                              xxxxx,   (string)  state of the sidechain at the current chain height\n"
            "     \"ceasingHeight\":                      xxxxx,   (numeric) height at which the sidechain is considered ceased if a certificate has not been received\n"
            "     \"creatingTxHash\":                     xxxxx,   (string)  txid of the creating transaction\n"
            "     \"createdAtBlockHeight\":               xxxxx,   (numeric) block height at which the sidechain was registered\n"
            "     \"lastCertificateEpoch\":               xxxxx,   (numeric) last epoch number for which a certificate has been received\n"
            "     \"lastCertificateHash\":                xxxxx,   (numeric) the hash of the last certificate that has been received\n"
            "     \"lastCertificateQuality\":             xxxxx,   (numeric) the quality of the last certificate that has been received\n"
            "     \"lastCertificateAmount\":              xxxxx,   (numeric) the amount of the last certificate that has been received\n"
            "     \"activeFtScFee\":                      xxxxx,   (numeric) The currently active fee required to create a Forward Transfer to sidechain;\n"
            "                                                              it can be either pastFtScFee or lastFtScFee value depending on the current block height, current epoch and last received top quality certificate\n"
            "     \"activeMbtrScFee\":                    xxxxx,   (numeric) The currently active fee required to create a Mainchain Backward Transfer Request to sidechain\n"
            "                                                              it can be either pastMbtrScFee or lastMbtrScFee value depending on the current block height, current epoch and last received top quality certificate\n"
            "     \"mbtrRequestDataLength\":              xxxxx,   (numeric) The size of the MBTR request data length\n"
            "     \"withdrawalEpochLength\":              xxxxx,   (numeric) length in blocks of the withdrawal epoch\n"
            "     \"version\":                            xxxxx,   (numeric) version of the sidechain\n"
            "     \"certSubmissionWindowLength\":         xxxxx,   (numeric) length in blocks of the submission window for certificates\n"
            "     \"certProvingSystem\"                   xxxxx,   (numeric) The type of proving system used for certificate verification\n"
            "     \"wCertVk\":                            xxxxx,   (string)  The verification key needed to verify a Withdrawal Certificate Proof, set at sc creation\n"
            "     \"customData\":                         xxxxx,   (string)  The arbitrary byte string of custom data set at sc creation\n"
            "     \"constant\":                           xxxxx,   (string)  The arbitrary byte string of constant set at sc creation\n"
            "     \"cswProvingSystem\"                    xxxxx,   (numeric) The type of proving system used for CSW verification\n"
            "     \"wCeasedVk\":                          xxxxx,   (string)  The verification key needed to verify a Ceased Sidechain Withdrawal input Proof, set at sc creation\n"
            "     \"vFieldElementCertificateFieldConfig\" xxxxx,   (string)  A string representation of an array whose entries are sizes (in bits). Any certificate should have as many custom FieldElements with the corresponding size.\n"
            "     \"vBitVectorCertificateFieldConfig\"    xxxxx,   (string)  A string representation of an array whose entries are bitVectorSizeBits and maxCompressedSizeBytes pairs. Any certificate should have\n"
            "                                                              as many custom vBitVectorCertificateField with the corresponding sizes\n"
            "     \"pastFtScFee\":                        xxxxx,   (numeric) The (past epoch) fee required to create a Forward Transfer to sidechain; it is the value set by the top quality certificate of the previous epoch\n"
            "     \"pastMbtrScFee\":                      xxxxx,   (numeric) The (past epoch) fee required to create a Mainchain Backward Transfer Request to sidechain; it is the value set by the top quality certificate of the previous epoch\n"
            "     \"lastFtScFee\":                        xxxxx,   (numeric) The (last epoch) fee required to create a Forward Transfer to sidechain; it refers to the most recent epoch for which a valid certificate has been received\n"
            "     \"lastMbtrScFee\":                      xxxxx,   (numeric) The (last epoch) fee required to create a Mainchain Backward Transfer Request to sidechain; it refers to the most recent epoch for which a valid certificate has been received\n"
            "     \"immatureAmounts\": [\n"              
            "       {\n"                                  
            "         \"maturityHeight\":                 xxxxx,   (numeric) height at which fund will become part of spendable balance\n"
            "         \"amount\":                         xxxxx,   (numeric) immature fund\n"
            "       },\n"
            "       ... ]\n"
            "    },\n"
            "    ...\n"
            "  ]\n"
            "}\n"

            "\nExamples\n"
            + HelpExampleCli("getscinfo", "\"1a3e7ccbfd40c4e2304c3215f76d204e4de63c578ad835510f580d529516a874\"")
            + HelpExampleCli("getscinfo", "\"*\" true false 2 10")
            + HelpExampleCli("getscinfo", "\"*\" ")
        );

    bool bRetrieveAllSc = false;
    string inputString = params[0].get_str();
    if (!inputString.compare("*"))
        bRetrieveAllSc = true;
    else
    {
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid scid format: not an hex");
    }

    bool bOnlyAlive = false;
    if (params.size() > 1)
        bOnlyAlive = params[1].get_bool();

    bool bVerbose = true;
    if (params.size() > 2)
        bVerbose = params[2].get_bool();

    UniValue ret(UniValue::VOBJ);
    UniValue scItems(UniValue::VARR);

    if (!bRetrieveAllSc)
    {
        // single search
        uint256 scId;
        scId.SetHex(inputString);
 
        UniValue scRecord(UniValue::VOBJ);
        // throws a json rpc exception if the scid is not found in the db
        if (!FillScRecord(scId, scRecord, bOnlyAlive, bVerbose) )
        {
            // after filtering no sc has been found, this can happen for instance when the sc is ceased
            // and bOnlyAlive is true
            ret.pushKV("totalItems", 0);
            ret.pushKV("from", 0);
            ret.pushKV("to", 0);
        }
        else
        {
            ret.pushKV("totalItems", 1);
            ret.pushKV("from", 0);
            ret.pushKV("to", 1);
            scItems.push_back(scRecord);
        }
    }
    else
    {
        int from = 0;
        if (params.size() > 3)
            from = params[3].get_int();

        int to = -1;
        if (params.size() > 4)
            to = params[4].get_int();

        // throws a json rpc exception if the from/to parameters are invalid or out of the range of the
        // retrieved scItems list
        int tot = FillScList(scItems, bOnlyAlive, bVerbose, from, to);

        ret.pushKV("totalItems", tot);
        ret.pushKV("from", from);
        ret.pushKV("to", from + scItems.size());
    }

    ret.pushKV("items", scItems);
    return ret;
}

UniValue getactivecertdatahash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getactivecertdatahash (\"scid\")\n"
            "\nArgument:\n"
            "   \"scid\"   (string, mandatory)  Retrive information about specified scid\n"
            "\nReturns the certificate recent data hash info for the given scid.\n"
            "\nResult:\n"
            "{\n"
            "  \"certDataHash\":              xxxxx,   (string)  A hex string representation of the field element containing the recent active certificate data hash for the specified scid.\n"
            "}\n"

            "\nExamples\n"
            + HelpExampleCli("getactivecertdatahash", "\"1a3e7ccbfd40c4e2304c3215f76d204e4de63c578ad835510f580d529516a874\"")
        );

    string scIdString = params[0].get_str();
    {
        if (scIdString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid scid format: not an hex");
    }

    UniValue ret(UniValue::VOBJ);
 
    uint256 scId;
    scId.SetHex(scIdString);

    FillCertDataHash(scId, ret);
 
    return ret;
}

UniValue getceasingcumsccommtreehash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getceasingcumsccommtreehash (\"scid\")\n"
            "\nArgument:\n"
            "   \"scid\"   (string, mandatory)  Retrive information about specified scid\n"
            "\nReturns the Cumulative SC Commitment tree hash of the ceasing block for the given scid.\n"
            "\nResult:\n"
            "{\n"
            "  \"ceasingCumScTxCommTree\":  xxxxx,   (string)  A hex string representation of the field element containing Cumulative SC Commitment tree hash of the ceasing block for the given scid.\n"
            "}\n"

            "\nExamples\n"
            + HelpExampleCli("getceasingcumsccommtreehash", "\"1a3e7ccbfd40c4e2304c3215f76d204e4de63c578ad835510f580d529516a874\"")
        );

    string scIdString = params[0].get_str();
    {
        if (scIdString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid scid format: not an hex");
    }

    UniValue ret(UniValue::VOBJ);
 
    uint256 scId;
    scId.SetHex(scIdString);

    FillCeasingCumScTxCommTree(scId, ret);
 
    return ret;
}

UniValue getscgenesisinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "getscgenesisinfo \"scid\"\n"
            "\nReturns side chain genesis info for the given id or for all of the existing sc if the id is not given.\n"
            "\n"
            "\nResult:\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data.\n"
            // TODO explain the contents

            "\nExamples\n"
            + HelpExampleCli("getscgenesisinfo", "\"1a3e7ccbfd40c4e2304c3215f76d204e4de63c578ad835510f580d529516a874\"")
        );
    }

    // side chain id
    string inputString = params[0].get_str();
    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid scid format: not an hex");

    uint256 scId;
    scId.SetHex(inputString);

    // sanity check of the side chain ID
    CCoinsViewCache scView(pcoinsTip);
    if (!scView.HaveSidechain(scId))
    {
        LogPrint("sc", "%s():%d - scid[%s] not yet created\n", __func__, __LINE__, scId.ToString() );
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("scid not yet created: ") + scId.ToString());
    }

    // find the block where it has been created
    CSidechain info;
    if (!scView.GetSidechain(scId, info))
    {
        LogPrint("sc", "cound not get info for scid[%s], probably not yet created\n", scId.ToString() );
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("scid not yet created: ") + scId.ToString());
    }

    int blockHeight = info.creationBlockHeight;

    CBlock block;
    CBlockIndex* pblockindex = chainActive[blockHeight];
    assert(pblockindex != nullptr);

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);

    // ntw type
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network >= CBaseChainParams::Network::MAX_NETWORK_TYPES)
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Illegal network type " + std::to_string(network) );
    }
    char cNetwork = (char)network;
    LogPrint("sc", "ntw type[%d]\n", cNetwork);
    ssBlock << cNetwork;

    // scid
    ssBlock << scId;

    // pow data
    const int vec_size = Params().GetConsensus().nPowAveragingWindow + CBlockIndex::nMedianTimeSpan;

    std::vector<ScPowRelatedData> vData;
    vData.reserve(vec_size);

    CBlockIndex* prev = pblockindex;

    for (int i = 0; i < vec_size; i++)
    {
        ScPowRelatedData s = {};
        prev = prev->pprev;
        if (!prev)
        {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't set block index!");
        }
        s.a = prev->nTime;
        s.b = prev->nBits;
        vData.push_back(s);
    }

    ssBlock << vData;

    // block height
    ssBlock << pblockindex->nHeight;

    // block scCommitmentTreeCumulativeHash
    ssBlock << pblockindex->scCumTreeHash;
    LogPrint("sc", "%s():%d - sc[%s], h[%d], cum[%s], bVers[0x%x]\n", __func__, __LINE__,
        scId.ToString(), pblockindex->nHeight, pblockindex->scCumTreeHash.GetHexRepr(), pblockindex->nVersion);

    // block hex data
    ssBlock << block;

    // Retrieve sidechain version for any sidechain that published a certificate in this block
    std::vector<ScVersionInfo> vSidechainVersion;

    for (const CScCertificate& cert : block.vcert)
    {
        CSidechain sc;
        if (!scView.GetSidechain(cert.GetScId(), sc))
        {
            LogPrint("sc", "cound not get info for scid[%s] while checking certificate\n", cert.GetScId().ToString() );
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("scid not found: ") + cert.GetScId().ToString());
        }

        ScVersionInfo scVersion = {};
        scVersion.sidechainId = cert.GetScId();
        scVersion.sidechainVersion = sc.fixedParams.version;

        vSidechainVersion.push_back(scVersion);
    }

    ssBlock << vSidechainVersion;

    std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
    return strHex;

}

UniValue checkcswnullifier(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "checkcswnullifier\n"
            "\nArguments:\n"
            "1. \"scid\"   (string, mandatory) scid of nullifier, \"*\" means all \n"
            "2. nullifier (string, mandatory) Retrieve only information for nullifier\n"
            "\nReturns True if nullifier exit in SC.\n"
            "\nResult:\n"
            "{\n"
            "  \"data\":            xx,      (bool) existance of nullifier\n"
            "}\n"

            "\nExamples\n"
            + HelpExampleCli("checkcswnullifier", "\"1a3e7ccbfd40c4e2304c3215f76d204e4de63c578ad835510f580d529516a874\""
                             "\"0f580d529516a8744de63c578ad83551304c3215f76d204e1a3e7ccbfd40c4e21a3e7ccbfd40c4e2304c3215f76d204e4de63c578ad835510f580d529516a8740f580d529516a8744de63c578ad83551304c3215f76d204e1a3e7ccbfd40c4e2\"" ) 
        );

    string inputString = params[0].get_str();

    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid scid format: not an hex");

    uint256 scId;
    scId.SetHex(inputString);
    
    inputString = params[1].get_str();

    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid nullifier format: not an hex");

    std::string nullifierError;
    std::vector<unsigned char> nullifierVec;
    if (!AddScData(inputString, nullifierVec, CFieldElement::ByteSize(), CheckSizeMode::CHECK_STRICT, nullifierError))
    {
        std::string error = "Invalid checkcswnullifier input parameter \"nullifier\": " + nullifierError;
        throw JSONRPCError(RPC_TYPE_ERROR, error);
    }
    CFieldElement nullifier{nullifierVec};
    if (!nullifier.IsValid())
    {
        std::string error = "Invalid checkcswnullifier input parameter \"nullifier\": invalid nullifier data";
        throw JSONRPCError(RPC_TYPE_ERROR, error);
    }

    UniValue ret(UniValue::VOBJ);
    
    if (pcoinsTip->HaveCswNullifier(scId, nullifier)) {
        ret.pushKV("data", "true");
    } else {
        ret.pushKV("data", "false");
    }

    return ret;
}

int64_t blocksToOvertakeTarget(const CBlockIndex* forkTip, const CBlockIndex* targetBlock)
{
    //this function assumes forkTip and targetBlock are non-null.
    if (!chainActive.Contains(targetBlock))
        return LLONG_MAX;

    int64_t gap = 0;
    const int targetBlockHeight = targetBlock->nHeight;
    const int selectedTipHeight = forkTip->nHeight;
    const int intersectionHeight = chainActive.FindFork(forkTip)->nHeight;

    LogPrint("forks", "%s():%d - processing tip h(%d) [%s] forkBaseHeight[%d]\n",
            __func__, __LINE__, forkTip->nHeight, forkTip->GetBlockHash().ToString(),
            intersectionHeight);

    // during a node's life, there might be many tips in the container, it is not useful
    // keeping all of them into account for calculating the finality, just consider the most recent ones.
    // Blocks are ordered by height, stop if we exceed a safe limit in depth, lets say the max age
    if ((chainActive.Height() - selectedTipHeight) >= MAX_BLOCK_AGE_FOR_FINALITY) {
        LogPrint("forks", "%s():%d - exiting loop on tips, max age reached: forkBaseHeight[%d], chain[%d]\n",
                __func__, __LINE__, intersectionHeight, chainActive.Height());
        gap = LLONG_MAX;
    } else if (intersectionHeight < targetBlockHeight) {
        // if the fork base is older than the input block, finality also depends on the current penalty
        // ongoing on the fork
        int64_t forkDelay = forkTip->nChainDelay;
        if (selectedTipHeight >= chainActive.Height()) {
            // if forkDelay is null one has to mine 1 block only
            gap = forkDelay ? forkDelay : 1;
            LogPrint("forks", "%s():%d - gap[%d], forkDelay[%d]\n", __func__,
                    __LINE__, gap, forkDelay);
        } else {
            int64_t dt = chainActive.Height() - selectedTipHeight + 1;
            dt = dt * (dt + 1) / 2;
            gap = dt + forkDelay + 1;
            LogPrint("forks", "%s():%d - gap[%d], forkDelay[%d], dt[%d]\n",
                    __func__, __LINE__, gap, forkDelay, dt);
        }
    } else {
        int64_t targetToTipDelta = chainActive.Height() - targetBlockHeight + 1;

        // this also handles the main chain tip
        if (targetToTipDelta < PENALTY_THRESHOLD + 1) {
            // an attacker can mine from previous block up to tip + 1
            gap = targetToTipDelta + 1;
            LogPrint("forks", "%s():%d - gap[%d], delta[%d]\n", __func__,
                    __LINE__, gap, targetToTipDelta);
        } else {
            // penalty applies
            gap = (targetToTipDelta * (targetToTipDelta + 1) / 2);
            LogPrint("forks", "%s():%d - gap[%d], delta[%d]\n", __func__,
                    __LINE__, gap, targetToTipDelta);
        }
    }

    return gap;
}

UniValue getblockfinalityindex(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockfinalityindex \"hash\"\n"
            "\nReturns the minimum number of consecutive blocks a miner would have to mine from now in order to revert the block of given hash\n"

            "\nArguments:\n"
            "1. hash   (string, required)  the block hash\n"

            "\nResult:\n"
            "n         (numeric) number of consecutive blocks a miner would have to mine from now in order to revert the block of given hash\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getblockfinalityindex", "\"hash\"")
            + HelpExampleRpc("getblockfinalityindex", "\"hash\"")
        );
    LOCK(cs_main);

    uint256 hash = ParseHashV(params[0], "parameter 1");

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No such block header");

    if (hash == Params().GetConsensus().hashGenesisBlock)
        throw JSONRPCError(RPC_INVALID_PARAMS, "Finality does not apply to genesis block");

    CBlockIndex* pTargetBlockIdx = mapBlockIndex[hash];

    if (fHavePruned && !(pTargetBlockIdx->nStatus & BLOCK_HAVE_DATA) && pTargetBlockIdx->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    // 0. if the input does not belong to the main chain can not tell finality
    if (!chainActive.Contains(pTargetBlockIdx))
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't tell finality of a block not on main chain");
    }

    int inputHeight = pTargetBlockIdx->nHeight;
    LogPrint("forks", "%s():%d - input h(%d) [%s]\n",
        __func__, __LINE__, pTargetBlockIdx->nHeight, pTargetBlockIdx->GetBlockHash().ToString());

    int64_t delta = chainActive.Height() - inputHeight + 1;
    if (delta >= MAX_BLOCK_AGE_FOR_FINALITY)
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Old block: older than 2000!");
    }

    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    for(auto mapPair: mGlobalForkTips)
    {
        const CBlockIndex* idx = mapPair.first;
        setTips.insert(idx);
    }
    setTips.insert(chainActive.Tip());

//    dump_global_tips();

    // For each tip find the stemming block on the main chain
    // In case of main tip such a block would be the tip itself
    //-----------------------------------------------------------------------
    int64_t minGap = LLONG_MAX;
    for(auto selectedTip: setTips)
    {
        int64_t gap = blocksToOvertakeTarget(selectedTip, pTargetBlockIdx);
        minGap = std::min(minGap, gap);
    }

    LogPrint("forks", "%s():%d - returning [%d]\n", __func__, __LINE__, minGap);
    return minGap;
}

UniValue getglobaltips(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
    {
        throw runtime_error(
            "getglobaltips\n"
            "\nReturns the list of hashes of the tips of all the existing forks\n"
            
            "\nResult:\n"
            "Global tips: n (numeric, global forks tips)\n"
            "-----------------------\n"
            "h(n (numeric, block height index)) \"hash\" (string, block hash) onFork[-] time[xxxxx (numeric, time)]\n"
            "Ordered: ---------------\n"
            "[\"hash\" (string, block hash) ]\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getglobaltips", "")
            + HelpExampleRpc("getglobaltips", "")
        );
    }
    LOCK(cs_main);
    return dbg_blk_global_tips();
}

/*
 * Can be useful when working at python scripts
 */
UniValue dbg_log(const UniValue& params, bool fHelp)
{
    if (fHelp)
    {
        throw runtime_error(
            "dbg_log\n"
            "\nPrints on debug.log any passed string."
            "\n(Valid only in regtest)\n"
            
            "\nResult:\n"
            "Nothing\n"
            
            "\nExamples:\n"
            + HelpExampleCli("dbg_log", "\"<log string>\"")
            + HelpExampleRpc("dbg_log", "\"<log string>\"")
        );
    }
    if (Params().NetworkIDString() != "regtest")
    {
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "This method can only be used on regtest");
    }

    std::string s = params[0].get_str();
    LogPrint("py", "%s() - ########## [%s] #########\n", __func__, s);
    return "Log printed";
}

UniValue dbg_do(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
    {
        throw runtime_error(
            "dbg_do\n"
            "\nDoes some hard coded helper task.\n"
            "\nExamples:\n"
            + HelpExampleCli("dbg_do", "\"todo\"")
        );
    }
    std::string ret = "TODO";

    return ret;
}

/**
 * @brief Retrieves the statistics about the sidechain proof verifier, for instance
 * the number of accepted and failed verifications, the number of pending
 * proofs, etc.
 */
UniValue getproofverifierstats(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
    {
        throw runtime_error(
            "getproofverifierstats\n"
            "\nCollects statistics about the sidechain proof verification system.\n"
            "\nExamples:\n"
            + HelpExampleCli("getproofverifierstats", "")
            + HelpExampleRpc("getproofverifierstats", "")
        );
    }

    if (Params().NetworkIDString() != "regtest")
    {
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "This method can only be used in regtest");
    }

    AsyncProofVerifierStatistics stats = TEST_FRIEND_CScAsyncProofVerifier::GetInstance().GetStatistics();
    size_t pendingCerts = TEST_FRIEND_CScAsyncProofVerifier::GetInstance().PendingAsyncCertProofs();
    size_t pendingCSWs = TEST_FRIEND_CScAsyncProofVerifier::GetInstance().PendingAsyncCswProofs();

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("pendingCerts",  pendingCerts);
    obj.pushKV("pendingCSWs",   pendingCSWs);
    obj.pushKV("failedCerts",   static_cast<uint64_t>(stats.failedCertCounter));
    obj.pushKV("failedCSWs",    static_cast<uint64_t>(stats.failedCswCounter));
    obj.pushKV("okCerts",       static_cast<uint64_t>(stats.okCertCounter));
    obj.pushKV("okCSWs",        static_cast<uint64_t>(stats.okCswCounter));

    return obj;
}


/**
 * @brief Sets the ProofVerifier guard to pause/resume low priority verification threads.
 */
UniValue setproofverifierlowpriorityguard(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "setproofverifierlowprioityguard\n"
            "\nEnable or disable the low priority threads guard to pause/resume the mempool related sc proof verifications.\n"
            "Regtest only.\n"
            "\nExamples:\n"
            + HelpExampleCli("setproofverifierlowpriorityguard", "true")
            + HelpExampleRpc("setproofverifierlowpriorityguard", "false")
        );
    }

    if (Params().NetworkIDString() != "regtest")
    {
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "This method can only be used in regtest");
    }

    bool isEnabled = params[0].getBool();

    TEST_FRIEND_CScAsyncProofVerifier::GetInstance().setProofVerifierLowPriorityGuard(isEnabled);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("enabled",  isEnabled);

    return obj;
}

UniValue getcertmaturityinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getcertmaturityinfo (\"hash\")\n"
            "\nArgument:\n"
            "   \"hash\"   (string, mandatory) certificate hash (txid)\n"
            "\nReturns the informations about certificate maturity. The cmd line option -txindex must be enabled, otherwise it works only\n"
            "for certificates in the mempool\n"
            "\nResult:\n"
            "{\n"
            "    \"maturityHeight\"     (number) The maturity height when the backwardtransfer output are spendable\n"           
            "    \"blocksToMaturity\"   (number) The number of blocks to be mined for achieving maturity (0 means already spendable)\n"           
            "    \"certificateState\"   (string) Can be one of [\"MATURE\", \"IMMATURE\", \"SUPERSEDED\", \"TOP_QUALITY_MEMPOOL\", \"LOW_QUALITY_MEMPOOL\", \"INVALID\"]\n"  
            "}\n"

            "\nExamples\n"
            + HelpExampleCli("getcertmaturityinfo", "\"1a3e7ccbfd40c4e2304c3215f76d204e4de63c578ad835510f580d529516a874\"")
        );

    UniValue ret(UniValue::VOBJ);
    uint256 hash;

    string hashString = params[0].get_str();
    {
        if (hashString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid hash format: not an hex");
    }

    hash.SetHex(hashString);

    // Search for the certificate in the mempool
    CScCertificate certOut;

    {
        LOCK(mempool.cs);
        if (mempool.lookup(hash, certOut))
        {
            ret.pushKV("maturityHeight", -1);
            ret.pushKV("blocksToMaturity", -1);
            std::string s;
            mempool.CertQualityStatusString(certOut, s);
            ret.pushKV("certificateState", s);
            return ret;
        }
    }

    if (!fTxIndex)
    {
        throw JSONRPCError(RPC_TYPE_ERROR, "txindex option not set: can not retrieve info");
    }

    if (pblocktree == NULL)
    {
        throw JSONRPCError(RPC_TYPE_ERROR, "DB not initialized: can not retrieve info");
    }

    int currentTipHeight = -1;
    CTxIndexValue txIndexValue;
 
    {
        LOCK(cs_main);
        currentTipHeight = (int)chainActive.Height();
        if (!pblocktree->ReadTxIndex(hash, txIndexValue))
        {
            throw JSONRPCError(RPC_TYPE_ERROR, "No info in Tx DB for the specified certificate");
        }
    }

    int bwtMatHeight = txIndexValue.maturityHeight;

    if (bwtMatHeight == 0)
    {
        // for instance when the hash is related to a tx
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid (null) certificate maturity height: is the input a tx hash?");
    }

    ret.pushKV("maturityHeight", bwtMatHeight);

    if (bwtMatHeight < 0)
    {
        ret.pushKV("blocksToMaturity", -1);
        if (bwtMatHeight == CTxIndexValue::INVALID_MATURITY_HEIGHT)
        {
            // this is the case when the certificate is not in the active chain
            ret.pushKV("certificateState", "INVALID");
        }
        else
        {
            ret.pushKV("certificateState", "SUPERSEDED");
        }
    }
    else
    {
        int deltaMaturity    = bwtMatHeight - currentTipHeight;
        bool isMature        = (deltaMaturity <= 0);

        if (!isMature)
        {
            ret.pushKV("blocksToMaturity", deltaMaturity);
            ret.pushKV("certificateState", "IMMATURE");
        }
        else
        {
            ret.pushKV("blocksToMaturity", 0);
            ret.pushKV("certificateState", "MATURE");
        }
    }
    
    return ret;
}

/**
 * @brief Removes any transaction from the mempool.
 */
UniValue clearmempool(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
    {
        throw runtime_error(
            "clearmempool\n"
            "\nRemoves any transaction and certificate from the mempool. Wallets are NOT synchronized.\n"
            "Regtest and Testnet only.\n"
            "\nExamples:\n"
            + HelpExampleCli("clearmempool", "")
            + HelpExampleRpc("clearmempool", "")
        );
    }

    if (Params().NetworkIDString() == "main")
    {
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "This method can not be used in main network");
    }

    LOCK(cs_main);
    mempool.clear();

    return NullUniValue;
}
