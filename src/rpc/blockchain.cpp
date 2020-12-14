// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "consensus/validation.h"
#include "main.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "streams.h"
#include "sync.h"
#include "util.h"
#include "zen/delay.h"

#include <stdint.h>

#include <univalue.h>

#include <regex>

#include "sc/sidechain.h"
#include "sc/sidechainrpc.h"

#include "validationinterface.h"

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
    rv.push_back(Pair("id", name));
    rv.push_back(Pair("monitored", (bool)chainValue));
    if (chainValue) {
        rv.push_back(Pair("chainValue", ValueFromAmount(*chainValue)));
        rv.push_back(Pair("chainValueZat", *chainValue));
    }
    if (valueDelta) {
        rv.push_back(Pair("valueDelta", ValueFromAmount(*valueDelta)));
        rv.push_back(Pair("valueDeltaZat", *valueDelta));
    }
    return rv;
}

UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", blockindex->nVersion));
    result.push_back(Pair("merkleroot", blockindex->hashMerkleRoot.GetHex()));
    result.push_back(Pair("time", (int64_t)blockindex->nTime));
    result.push_back(Pair("nonce", blockindex->nNonce.GetHex()));
    result.push_back(Pair("solution", HexStr(blockindex->nSolution)));
    result.push_back(Pair("bits", strprintf("%08x", blockindex->nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(Pair("scTxsCommitment", block.hashScTxsCommitment.GetHex()));
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
    result.push_back(Pair("tx", txs));
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
        result.push_back(Pair("cert", certs));
    }
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("nonce", block.nNonce.GetHex()));
    result.push_back(Pair("solution", HexStr(block.nSolution)));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));
    result.push_back(Pair("anchor", blockindex->hashAnchorEnd.GetHex()));

    UniValue valuePools(UniValue::VARR);
    valuePools.push_back(ValuePoolDesc("sprout", blockindex->nChainSproutValue, blockindex->nSproutValue));
    result.push_back(Pair("valuePools", valuePools));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue getblockcount(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "\nReturns the number of blocks in the longest block chain.\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
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
            "\nReturns the hash of the best (tip) block in the longest block chain.\n"
            "\nResult\n"
            "\"hex\"      (string) the block hash hex encoded\n"
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
            "n.nnn       (numeric) the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nExamples:\n"
            + HelpExampleCli("getdifficulty", "")
            + HelpExampleRpc("getdifficulty", "")
        );

    LOCK(cs_main);
    return GetNetworkDifficulty();
}

void AddDependancy(const CTransaction& tx, UniValue& info)
{
    set<string> setDepends;
    BOOST_FOREACH(const CTxIn& txin, tx.GetVin())
    {
        if (mempool.exists(txin.prevout.hash))
            setDepends.insert(txin.prevout.hash.ToString());
    }
    // the dependancy of a certificate from the sc creation is not considered
    for (const auto& ft: tx.GetVftCcOut())
    {
        if (mempool.hasSidechainCreationTx(ft.scId))
        {
            const uint256& scCreationHash = mempool.mapSidechains.at(ft.scId).scCreationTxHash;

            // check if tx is also creating the sc
            if (scCreationHash != tx.GetHash())
                setDepends.insert(scCreationHash.ToString());
        }
    }

    UniValue depends(UniValue::VARR);
    BOOST_FOREACH(const string& dep, setDepends)
    {
        depends.push_back(dep);
    }

    info.push_back(Pair("depends", depends));
}

void AddDependancy(const CScCertificate& cert, UniValue& info)
{
    set<string> setDepends;
    BOOST_FOREACH(const CTxIn& txin, cert.GetVin())
    {
        if (mempool.exists(txin.prevout.hash))
            setDepends.insert(txin.prevout.hash.ToString());
    }

    UniValue depends(UniValue::VARR);
    BOOST_FOREACH(const string& dep, setDepends)
    {
        depends.push_back(dep);
    }

    info.push_back(Pair("depends", depends));
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
            info.push_back(Pair("size", (int)e.GetTxSize()));
            info.push_back(Pair("fee", ValueFromAmount(e.GetFee())));
            info.push_back(Pair("time", e.GetTime()));
            info.push_back(Pair("height", (int)e.GetHeight()));
            info.push_back(Pair("startingpriority", e.GetPriority(e.GetHeight())));
            info.push_back(Pair("currentpriority", e.GetPriority(chainActive.Height())));
            const CTransaction& tx = e.GetTx();
            AddDependancy(tx, info);
            o.push_back(Pair(hash.ToString(), info));
        }
        BOOST_FOREACH(const PAIRTYPE(uint256, CCertificateMemPoolEntry)& entry, mempool.mapCertificate)
        {
            const uint256& hash = entry.first;
            const auto& e = entry.second;
            UniValue info(UniValue::VOBJ);
            info.push_back(Pair("size", (int)e.GetCertificateSize()));
            info.push_back(Pair("fee", ValueFromAmount(e.GetFee())));
            info.push_back(Pair("time", e.GetTime()));
            info.push_back(Pair("height", (int)e.GetHeight()));
            info.push_back(Pair("startingpriority", e.GetPriority(e.GetHeight())));
            info.push_back(Pair("currentpriority", e.GetPriority(chainActive.Height())));
            const CScCertificate& cert = e.GetCertificate();
            AddDependancy(cert, info);
            o.push_back(Pair(hash.ToString(), info));
        }
        BOOST_FOREACH(const auto& entry, mempool.mapDeltas)
        {
            const uint256& hash = entry.first;
            const auto& p = entry.second.first;
            const auto& f = entry.second.second;
            UniValue info(UniValue::VOBJ);
            info.push_back(Pair("fee", ValueFromAmount(f)));
            info.push_back(Pair("priority", p));
            o.push_back(Pair(hash.ToString(), info));
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
            "1. verbose           (boolean, optional, default=false) true for a json object, false for array of transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nResult: (for verbose = true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            "    \"size\" : n,             (numeric) transaction size in bytes\n"
            "    \"fee\" : n,              (numeric) transaction fee in " + CURRENCY_UNIT + "\n"
            "    \"time\" : n,             (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT\n"
            "    \"height\" : n,           (numeric) block height when transaction entered pool\n"
            "    \"startingpriority\" : n, (numeric) priority when transaction entered pool\n"
            "    \"currentpriority\" : n,  (numeric) transaction priority now\n"
            "    \"depends\" : [           (array) unconfirmed transactions used as inputs for this transaction\n"
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

UniValue getblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash index\n"
            "\nReturns hash of block in best-block-chain at index provided.\n"
            "\nArguments:\n"
            "1. index         (numeric, required) The block index\n"
            "\nResult:\n"
            "\"hash\"         (string) The block hash\n"
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
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
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
            "If verbosity is 2, returns an Object with information about the block and information about each transaction. \n"
            "\nArguments:\n"
            "1. \"hash|height\"     (string, required) The block hash or height\n"
            "2. verbosity              (numeric, optional, default=1) 0 for hex encoded data, 1 for a json object, and 2 for json object with transaction data\n"
            "\nResult (for verbosity = 0):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nResult (for verbosity = 1):\n"
            "{\n"
            "  \"hash\" : \"hash\",       (string) the block hash (same as provided hash)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index (same as provided height)\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"tx\" : [               (array of string) The transaction ids\n"
            "     \"transactionid\"     (string) The transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\",   (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbosity = 2):\n"
            "{\n"
            "  ...,                     Same output as verbosity = 1.\n"
            "  \"tx\" : [               (array of Objects) The transactions in the format of the getrawtransaction RPC.\n"
            "         ,...\n"
            "  ],\n"
            "  ,...                     Same output as verbosity = 1.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleCli("getblock", "12800")
            + HelpExampleRpc("getblock", "12800")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();

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
            verbosity = params[1].get_bool() ? 1 : 0;
        }
    }

    if (verbosity < 0 || verbosity > 2) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Verbosity must be in range from 0 to 2");
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

UniValue gettxoutsetinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n"
            "\nResult:\n"
            "{\n"
            "  \"height\":n,     (numeric) The current block height (index)\n"
            "  \"bestblock\": \"hex\",   (string) the best block hash hex\n"
            "  \"transactions\": n,      (numeric) The number of transactions\n"
            "  \"txouts\": n,            (numeric) The number of output transactions\n"
            "  \"bytes_serialized\": n,  (numeric) The serialized size\n"
            "  \"hash_serialized\": \"hash\",   (string) The serialized hash\n"
            "  \"total_amount\": x.xxx          (numeric) The total amount\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("gettxoutsetinfo", "")
            + HelpExampleRpc("gettxoutsetinfo", "")
        );

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (pcoinsTip->GetStats(stats)) {
        ret.push_back(Pair("height", (int64_t)stats.nHeight));
        ret.push_back(Pair("bestblock", stats.hashBlock.GetHex()));
        ret.push_back(Pair("transactions", (int64_t)stats.nTransactions));
        ret.push_back(Pair("txouts", (int64_t)stats.nTransactionOutputs));
        ret.push_back(Pair("bytes_serialized", (int64_t)stats.nSerializedSize));
        ret.push_back(Pair("hash_serialized", stats.hashSerialized.GetHex()));
        ret.push_back(Pair("total_amount", ValueFromAmount(stats.nTotalAmount)));
    }
    return ret;
}

UniValue gettxout(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "gettxout \"txid\" n ( includemempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id\n"
            "2. n              (numeric, required) vout value\n"
            "3. includemempool  (boolean, optional) Whether to included the mem pool\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\" : \"hash\",    (string) the block hash\n"
            "  \"confirmations\" : n,       (numeric) The number of confirmations\n"
            "  \"value\" : x.xxx,           (numeric) The transaction value in " + CURRENCY_UNIT + "\n"
            "  \"scriptPubKey\" : {         (json object)\n"
            "     \"asm\" : \"code\",       (string) \n"
            "     \"hex\" : \"hex\",        (string) \n"
            "     \"reqSigs\" : n,          (numeric) Number of required signatures\n"
            "     \"type\" : \"pubkeyhash\", (string) The type, eg pubkeyhash\n"
            "     \"addresses\" : [          (array of string) array of Horizen addresses\n"
            "        \"horizenaddress\"        (string) Horizen address\n"
            "        ,...\n"
            "     ]\n"
            "  },\n"
            "  \"version\" : n,              (numeric) The version\n"
            "  \"coinbase\" : true|false     (boolean) Coinbase or not\n"
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

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.push_back(Pair("bestblock", pindex->GetBlockHash().GetHex()));
    if ((unsigned int)coins.nHeight == MEMPOOL_HEIGHT)
        ret.push_back(Pair("confirmations", 0));
    else
        ret.push_back(Pair("confirmations", pindex->nHeight - coins.nHeight + 1));
    ret.push_back(Pair("value", ValueFromAmount(coins.vout[n].nValue)));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToJSON(coins.vout[n].scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("version", coins.nVersion));
#if 0
    ret.push_back(Pair("coinbase", coins.fCoinBase));
#else
    ret.push_back(Pair("certificate", coins.IsFromCert()));
    ret.push_back(Pair("coinbase", coins.IsCoinBase()));
#endif

    return ret;
}

UniValue verifychain(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "verifychain ( checklevel numblocks )\n"
            "\nVerifies blockchain database.\n"
            "\nArguments:\n"
            "1. checklevel   (numeric, optional, 0-4, default=3) How thorough the block verification is.\n"
            "2. numblocks    (numeric, optional, default=288, 0=all) The number of blocks to check.\n"
            "\nResult:\n"
            "true|false       (boolean) Verified or not\n"
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
    rv.push_back(Pair("status", nFound >= nRequired));
    rv.push_back(Pair("found", nFound));
    rv.push_back(Pair("required", nRequired));
    rv.push_back(Pair("window", consensusParams.nMajorityWindow));
    return rv;
}

static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    rv.push_back(Pair("version", version));
    rv.push_back(Pair("enforce", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityEnforceBlockUpgrade, consensusParams)));
    rv.push_back(Pair("reject", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityRejectBlockOutdated, consensusParams)));
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
            "  \"chain\": \"xxxx\",        (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "  \"blocks\": xxxxxx,         (numeric) the current number of blocks processed in the server\n"
            "  \"headers\": xxxxxx,        (numeric) the current number of headers we have validated\n"
            "  \"bestblockhash\": \"...\", (string) the hash of the currently best block\n"
            "  \"difficulty\": xxxxxx,     (numeric) the current difficulty\n"
            "  \"verificationprogress\": xxxx, (numeric) estimate of verification progress [0..1]\n"
            "  \"chainwork\": \"xxxx\"     (string) total amount of work in active chain, in hexadecimal\n"
            "  \"commitments\": xxxxxx,    (numeric) the current number of note commitments in the commitment tree\n"
            "  \"softforks\": [            (array) status of softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",        (string) name of softfork\n"
            "        \"version\": xx,         (numeric) block version\n"
            "        \"enforce\": {           (object) progress toward enforcing the softfork rules for new-version blocks\n"
            "           \"status\": xx,       (boolean) true if threshold reached\n"
            "           \"found\": xx,        (numeric) number of blocks with the new version found\n"
            "           \"required\": xx,     (numeric) number of blocks required to trigger\n"
            "           \"window\": xx,       (numeric) maximum size of examined window of recent blocks\n"
            "        },\n"
            "        \"reject\": { ... }      (object) progress toward rejecting pre-softfork blocks (same fields as \"enforce\")\n"
            "     }, ...\n"
            "  \n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockchaininfo", "")
            + HelpExampleRpc("getblockchaininfo", "")
        );

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chain",                 Params().NetworkIDString()));
    obj.push_back(Pair("blocks",                (int)chainActive.Height()));
    obj.push_back(Pair("headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1));
    obj.push_back(Pair("bestblockhash",         chainActive.Tip()->GetBlockHash().GetHex()));
    obj.push_back(Pair("difficulty",            (double)GetNetworkDifficulty()));
    obj.push_back(Pair("verificationprogress",  Checkpoints::GuessVerificationProgress(Params().Checkpoints(), chainActive.Tip())));
    obj.push_back(Pair("chainwork",             chainActive.Tip()->nChainWork.GetHex()));
    obj.push_back(Pair("pruned",                fPruneMode));

    ZCIncrementalMerkleTree tree;
    pcoinsTip->GetAnchorAt(pcoinsTip->GetBestAnchor(), tree);
    obj.push_back(Pair("commitments",           tree.size()));

    CBlockIndex* tip = chainActive.Tip();
    UniValue valuePools(UniValue::VARR);
    valuePools.push_back(ValuePoolDesc("sprout", tip->nChainSproutValue, boost::none));
    obj.push_back(Pair("valuePools",            valuePools));

    const Consensus::Params& consensusParams = Params().GetConsensus();
    UniValue softforks(UniValue::VARR);
    UniValue bip9_softforks(UniValue::VOBJ);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));

    obj.push_back(Pair("softforks",             softforks));

    if (fPruneMode)
    {
        CBlockIndex *block = chainActive.Tip();
        while (block && block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA))
            block = block->pprev;

        if (block)
            obj.push_back(Pair("pruneheight",        block->nHeight));
    }
    return obj;
}

UniValue getchaintips(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getchaintips\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"height\": xxxx,         (numeric) height of the chain tip\n"
            "    \"hash\": \"xxxx\",         (string) block hash of the tip\n"
            "    \"branchlen\": 0          (numeric) zero for main chain\n"
            "    \"status\": \"active\"      (string) \"active\" for the main chain\n"
            "  },\n"
            "  {\n"
            "    \"height\": xxxx,\n"
            "    \"hash\": \"xxxx\",\n"
            "    \"branchlen\": 1          (numeric) length of branch connecting the tip to the main chain\n"
            "    \"status\": \"xxxx\"        (string) status of the chain (active, valid-fork, valid-headers, headers-only, invalid)\n"
            "  }\n"
            "]\n"
            "Possible values for status:\n"
            "1.  \"invalid\"               This branch contains at least one invalid block\n"
            "2.  \"headers-only\"          Not all blocks for this branch are available, but the headers are valid\n"
            "3.  \"valid-headers\"         All blocks are available for this branch, but they were never fully validated\n"
            "4.  \"valid-fork\"            This branch is not part of the active chain, but is fully validated\n"
            "5.  \"active\"                This is the tip of the active main chain, which is certainly valid\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintips", "")
            + HelpExampleRpc("getchaintips", "")
        );

    LOCK(cs_main);

    /* Build up a list of chain tips.  We start with the list of all
       known blocks, and successively remove blocks that appear as pprev
       of another block. */
    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
        setTips.insert(item.second);
    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        const CBlockIndex* pprev = item.second->pprev;
        if (pprev)
            setTips.erase(pprev);
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    BOOST_FOREACH(const CBlockIndex* block, setTips)
    {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("height", block->nHeight));
        obj.push_back(Pair("hash", block->phashBlock->GetHex()));

        const int branchLen = block->nHeight - chainActive.FindFork(block)->nHeight;
        obj.push_back(Pair("branchlen", branchLen));

        string status;
        if (chainActive.Contains(block)) {
            // This block is part of the currently active chain.
            status = "active";
        } else if (block->nStatus & BLOCK_FAILED_MASK) {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        } else if (block->nChainTx == 0) {
            // This block cannot be connected because full block data for it or one of its parents is missing.
            status = "headers-only";
        } else if (block->IsValid(BLOCK_VALID_SCRIPTS)) {
            // This block is fully validated, but no longer part of the active chain. It was probably the active block once, but was reorganized.
            status = "valid-fork";
        } else if (block->IsValid(BLOCK_VALID_TREE)) {
            // The headers for this block are valid, but it has not been validated. It was probably never part of the most-work chain.
            status = "valid-headers";
        } else {
            // No clue.
            status = "unknown";
        }
        obj.push_back(Pair("status", status));

        res.push_back(obj);
    }

    return res;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t) mempool.size()));
    ret.push_back(Pair("bytes", (int64_t) mempool.GetTotalSize()));
    ret.push_back(Pair("usage", (int64_t) mempool.DynamicMemoryUsage()));

    if (Params().NetworkIDString() == "regtest") {
        ret.push_back(Pair("fullyNotified", mempool.IsFullyNotified()));
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
            "  \"size\": xxxxx                (numeric) Current tx count\n"
            "  \"bytes\": xxxxx               (numeric) Sum of all tx sizes\n"
            "  \"usage\": xxxxx               (numeric) Total memory usage for the mempool\n"
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

bool FillScRecordFromInfo(const uint256& scId, const CSidechain& info, CSidechain::State scState,
    UniValue& sc, bool bOnlyAlive, bool bVerbose)
{
    if (bOnlyAlive && (scState != CSidechain::State::ALIVE))
    	return false;

    sc.push_back(Pair("scid", scId.GetHex()));
    if (!info.IsNull() )
    {
        int currentEpoch = (scState == CSidechain::State::ALIVE)?
                info.EpochFor(chainActive.Height()):
                info.EpochFor(info.GetCeasingHeight());
 
        sc.push_back(Pair("balance", ValueFromAmount(info.balance)));
        sc.push_back(Pair("epoch", currentEpoch));
        sc.push_back(Pair("end epoch height", info.StartHeightForEpoch(currentEpoch +1) - 1));
        sc.push_back(Pair("state", CSidechain::stateToString(scState)));
        sc.push_back(Pair("ceasing height", info.GetCeasingHeight()));
 
        if (bVerbose)
        {
            sc.push_back(Pair("creating tx hash", info.creationTxHash.GetHex()));
            sc.push_back(Pair("created in block", info.creationBlockHash.ToString()));
        }
 
        sc.push_back(Pair("created at block height", info.creationBlockHeight));
        sc.push_back(Pair("last certificate epoch", info.topCommittedCertReferencedEpoch));
        sc.push_back(Pair("last certificate hash", info.topCommittedCertHash.GetHex()));
        sc.push_back(Pair("last certificate quality", info.topCommittedCertQuality));
        sc.push_back(Pair("last certificate amount", ValueFromAmount(info.topCommittedCertBwtAmount)));
 
        // creation parameters
        sc.push_back(Pair("withdrawalEpochLength", info.creationData.withdrawalEpochLength));
 
        if (bVerbose)
        {
            sc.push_back(Pair("wCertVk", HexStr(info.creationData.wCertVk)));
            sc.push_back(Pair("customData", HexStr(info.creationData.customData)));
            sc.push_back(Pair("constant", HexStr(info.creationData.constant)));
 
            UniValue ia(UniValue::VARR);
            for(const auto& entry: info.mImmatureAmounts)
            {
                UniValue o(UniValue::VOBJ);
                o.push_back(Pair("maturityHeight", entry.first));
                o.push_back(Pair("amount", ValueFromAmount(entry.second)));
                ia.push_back(o);
            }
            sc.push_back(Pair("immature amounts", ia));
        }

        // get fwd / bwt unconfirmed data if any
        if (mempool.mapSidechains.count(scId)!= 0)
        {
            if (!mempool.mapSidechains.at(scId).mBackwardCertificates.empty())
            {
                const uint256& topQualCertHash    = mempool.mapSidechains.at(scId).GetTopQualityCert()->second;
                const CScCertificate& topQualCert = mempool.mapCertificate.at(topQualCertHash).GetCertificate();
 
                sc.push_back(Pair("unconf top quality certificate epoch",   topQualCert.epochNumber));
                sc.push_back(Pair("unconf top quality certificate hash",    topQualCertHash.GetHex()));
                sc.push_back(Pair("unconf top quality certificate quality", topQualCert.quality));
                sc.push_back(Pair("unconf top quality certificate amount",  ValueFromAmount(topQualCert.GetValueOfBackwardTransfers())));
            }

            if (bVerbose)
            {
                UniValue ia(UniValue::VARR);
                for (const auto& fwdHash: mempool.mapSidechains.at(scId).fwdTransfersSet)
                {
                    const CTransaction & fwdTx = mempool.mapTx.at(fwdHash).GetTx();
                    for (const auto& fwdAmount : fwdTx.GetVftCcOut())
                    {
                        if (scId == fwdAmount.scId)
                        {
                             UniValue o(UniValue::VOBJ);
                             o.push_back(Pair("unconf maturityHeight", -1));
                             o.push_back(Pair("unconf amount", ValueFromAmount(fwdAmount.nValue)));
                             ia.push_back(o);
                         }
                    }
                }
            }
        }
    }
    else
    {
        // check if we have it in mempool
        CCoinsView dummy;
        CCoinsViewMemPool vm(&dummy, mempool);
        CSidechain info;
        if (vm.GetSidechain(scId, info))
        {
            sc.push_back(Pair("unconf creating tx hash", info.creationTxHash.GetHex()));
            sc.push_back(Pair("unconf withdrawalEpochLength", info.creationData.withdrawalEpochLength));

            if (bVerbose)
            {
                sc.push_back(Pair("unconf wCertVk", HexStr(info.creationData.wCertVk)));
                sc.push_back(Pair("unconf customData", HexStr(info.creationData.customData)));
                sc.push_back(Pair("unconf constant", HexStr(info.creationData.constant)));
  
                UniValue ia(UniValue::VARR);
                for(const auto& entry: info.mImmatureAmounts)
                {
                    UniValue o(UniValue::VOBJ);
                    o.push_back(Pair("unconf maturityHeight", entry.first));
                    o.push_back(Pair("unconf amount", ValueFromAmount(entry.second)));
                    ia.push_back(o);
                }
                sc.push_back(Pair("unconf immature amounts", ia));
            }
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
    CSidechain scInfo;
    CCoinsViewCache scView(pcoinsTip);
    if (!scView.GetSidechain(scId, scInfo)) {
        LogPrint("sc", "%s():%d - scid[%s] not yet created\n", __func__, __LINE__, scId.ToString() );
    }
    CSidechain::State scState = scView.isCeasedAtHeight(scId, chainActive.Height() + 1);

    return FillScRecordFromInfo(scId, scInfo, scState, scRecord, bOnlyAlive, bVerbose);
}

int FillScList(UniValue& scItems, bool bOnlyAlive, bool bVerbose, int from=0, int to=-1)
{
    CCoinsViewCache scView(pcoinsTip);
    std::set<uint256> sScIds;
    scView.GetScIds(sScIds);

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

UniValue getscinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() == 0 || params.size() > 5)
        throw runtime_error(
            "getscinfo (\"scid\" onlyAlive)\n"
			"\nArguments:\n"
			"1. \"scid\"   (string, mandatory) Retrive only information about specified scid, \"*\" means all \n"
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
            "     \"scid\":                    xxxxx,   (string)  sidechain ID\n"
            "     \"balance\":                 xxxxx,   (numeric) available balance\n"
            "     \"epoch\":                   xxxxx,   (numeric) current epoch for this sidechain\n"
            "     \"end epoch height\":        xxxxx,   (numeric) height of the last block of the current epoch\n"
            "     \"state\":                   xxxxx,   (string)  state of the sidechain at the current chain height\n"
            "     \"ceasing height\":          xxxxx,   (numeric) height at which the sidechain is considered ceased if a certificate has not been received\n"
            "     \"creating tx hash\":        xxxxx,   (string)  txid of the creating transaction\n"
            "     \"created in block\":        xxxxx,   (string)  hash of the block containing the creatimg tx\n"
            "     \"created at block height\": xxxxx,   (numeric) height of the above block\n"
            "     \"last certificate epoch\":  xxxxx,   (numeric) last epoch number for which a certificate has been received\n"
            "     \"last certificate hash\":   xxxxx,   (numeric) the hash of the last certificate that has been received\n"
            "     \"withdrawalEpochLength\":   xxxxx,   (numeric) length of the withdrawal epoch\n"
            "     \"wCertVk\":                 xxxxx,   (string)  The verification key needed to verify a Withdrawal Certificate Proof, set at sc creation\n"
            "     \"customData\":              xxxxx,   (string)  The arbitrary byte string of custom data set at sc creation\n"
            "     \"constant\":                xxxxx,   (string)  The arbitrary byte string of constant set at sc creation\n"
            "     \"immature amounts\": [\n"
            "       {\n"
            "         \"maturityHeight\":      xxxxx,   (numeric) height at which fund will become part of spendable balance\n"
            "         \"amount\":              xxxxx,   (numeric) immature fund\n"
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
            ret.push_back(Pair("totalItems", 0));
            ret.push_back(Pair("from", 0));
            ret.push_back(Pair("to", 0));
        }
        else
        {
            ret.push_back(Pair("totalItems", 1));
            ret.push_back(Pair("from", 0));
            ret.push_back(Pair("to", 1));
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

        ret.push_back(Pair("totalItems", tot));
        ret.push_back(Pair("from", from));
        ret.push_back(Pair("to", from + scItems.size()));
    }

    ret.push_back(Pair("items", scItems));
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

    const uint256& blockHash = info.creationBlockHash;

    assert(mapBlockIndex.count(blockHash) != 0);

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[blockHash];

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

    // block hex data
    ssBlock << block;

    std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
    return strHex;

}

UniValue getblockfinalityindex(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockfinalityindex \"hash\"\n"
            "\nReturns the minimum number of consecutive blocks a miner should mine from now in order to revert the block of given hash\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockfinalityindex", "\"hash\"")
        );
    LOCK(cs_main);

    uint256 hash = ParseHashV(params[0], "parameter 1");

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No such block header");

    if (hash == Params().GetConsensus().hashGenesisBlock)
        throw JSONRPCError(RPC_INVALID_PARAMS, "Finality does not apply to genesis block");

    CBlockIndex* pblkIndex = mapBlockIndex[hash];

    if (fHavePruned && !(pblkIndex->nStatus & BLOCK_HAVE_DATA) && pblkIndex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");
/*
 *  CBlock block;
 *  if(!ReadBlockFromDisk(block, pblkIndex))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk (header only)");
 */

    // 0. if the input does not belong to the main chain can not tell finality
    if (!chainActive.Contains(pblkIndex))
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't tell finality of a block not on main chain");
    }

    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    BOOST_FOREACH(auto mapPair, mGlobalForkTips)
    {
        const CBlockIndex* idx = mapPair.first;
        setTips.insert(idx);
    }
    setTips.insert(chainActive.Tip());

    int inputHeight = pblkIndex->nHeight;
    LogPrint("forks", "%s():%d - input h(%d) [%s]\n",
        __func__, __LINE__, pblkIndex->nHeight, pblkIndex->GetBlockHash().ToString());

    int64_t delta = chainActive.Height() - inputHeight + 1;
    if (delta >= MAX_BLOCK_AGE_FOR_FINALITY)
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Old block: older than 2000!");
    }

//    dump_global_tips();

    int64_t gap = 0;
    int64_t minGap = LLONG_MAX;

    // For each tip find the stemming block on the main chain
    // In case of main tip such a block would be the tip itself
    //-----------------------------------------------------------------------
    BOOST_FOREACH(auto idx, setTips)
    {
        const int forkTipHeight = idx->nHeight;
        const int forkBaseHeight = chainActive.FindFork(idx)->nHeight;

        LogPrint("forks", "%s():%d - processing tip h(%d) [%s] forkBaseHeight[%d]\n",
            __func__, __LINE__, idx->nHeight, idx->GetBlockHash().ToString(), forkBaseHeight);

        // during a node's life, there might be many tips in the container, it is not useful
        // keeping all of them into account for calculating the finality, just consider the most recent ones.
        // Blocks are ordered by heigth, stop if we exceed a safe limit in depth, lets say the max age
        if ( (chainActive.Height() - forkTipHeight) >=  MAX_BLOCK_AGE_FOR_FINALITY )
        {
            LogPrint("forks", "%s():%d - exiting loop on tips, max age reached: forkBaseHeight[%d], chain[%d]\n",
                __func__, __LINE__, forkBaseHeight, chainActive.Height());
            break;
        }

        if (forkBaseHeight < inputHeight)
        {
            // if the fork base is older than the input block, finality also depends on the current penalty
            // ongoing on the fork
            int64_t forkDelay  = idx->nChainDelay;
            if (forkTipHeight >= chainActive.Height())
            {
                // if forkDelay is null one has to mine 1 block only
                gap = forkDelay ? forkDelay : 1;
                LogPrint("forks", "%s():%d - gap[%d], forkDelay[%d]\n", __func__, __LINE__, gap, forkDelay);
            }
            else
            {
                int64_t dt = chainActive.Height() - forkTipHeight + 1;
                dt = dt * ( dt + 1) / 2;

                gap  = dt + forkDelay + 1;
                LogPrint("forks", "%s():%d - gap[%d], forkDelay[%d], dt[%d]\n", __func__, __LINE__, gap, forkDelay, dt);
            }
        }
        else
        {
            // this also handles the main chain tip
            if (delta < PENALTY_THRESHOLD + 1)
            {
                // an attacker can mine from previous block up to tip + 1
                gap = delta + 1;
                LogPrint("forks", "%s():%d - gap[%d], delta[%d]\n", __func__, __LINE__, gap, delta);
            }
            else
            {
                // penalty applies
                gap = (delta * (delta + 1) / 2);
                LogPrint("forks", "%s():%d - gap[%d], delta[%d]\n", __func__, __LINE__, gap, delta);
            }
        }
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
            "\nExamples:\n"
            + HelpExampleCli("getglobaltips", "\"hash\"")
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
            "\nExamples:\n"
            + HelpExampleCli("dbg_log", "\"<log string>\"")
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
            "dbg_do: does some hard coded helper task\n"
            "\nExamples:\n"
            + HelpExampleCli("dbg_do", "\"todo\"")
        );
    }
    std::string ret = "TODO";

    return ret;
}



