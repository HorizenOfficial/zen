// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/standard.h"

#include "pubkey.h"
#include "script/script.h"
#include "util.h"
#include "utilstrencodings.h"

#include <boost/foreach.hpp>

// ZEN_MOD_START
#include "main.h"
#include "versionbits.h"
// ZEN_MOD_END

using namespace std;

typedef vector<unsigned char> valtype;

unsigned nMaxDatacarrierBytes = MAX_OP_RETURN_RELAY;

CScriptID::CScriptID(const CScript& in) : uint160(Hash160(in.begin(), in.end())) {}

const char* GetTxnOutputType(txnouttype t)
{
    switch (t)
    {
    case TX_NONSTANDARD: return "nonstandard";
    case TX_PUBKEY: return "pubkey";
    case TX_PUBKEYHASH: return "pubkeyhash";
    case TX_SCRIPTHASH: return "scripthash";
// ZEN_MOD_START
    case TX_SCRIPTHASH_REPLAY: return "scripthashreplay";
// ZEN_MOD_END
    case TX_MULTISIG: return "multisig";
    case TX_NULL_DATA: return "nulldata";
// ZEN_MOD_START
    case TX_PUBKEY_REPLAY: return "pubkeyreplay";
    case TX_PUBKEYHASH_REPLAY: return "pubkeyhashreplay";
    case TX_MULTISIG_REPLAY: return "multisigreplay";
    case TX_NULL_DATA_REPLAY: return "nulldatareplay";
// ZEN_MOD_END
    }
    return NULL;
}

/**
 * Return public keys or hashes from scriptPubKey, for 'standard' transaction types.
 */
bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, vector<vector<unsigned char> >& vSolutionsRet)
{
    // Templates
    static multimap<txnouttype, CScript> mTemplates;
    if (mTemplates.empty())
    {
        // Standard tx, sender provides pubkey, receiver adds signature
        mTemplates.insert(make_pair(TX_PUBKEY, CScript() << OP_PUBKEY << OP_CHECKSIG));
// ZEN_MOD_START
        mTemplates.insert(make_pair(TX_PUBKEY_REPLAY, CScript() << OP_PUBKEY << OP_CHECKSIG << OP_SMALLDATA << OP_SMALLDATA << OP_CHECKBLOCKATHEIGHT));
// ZEN_MOD_END

        // Bitcoin address tx, sender provides hash of pubkey, receiver provides signature and pubkey
        mTemplates.insert(make_pair(TX_PUBKEYHASH, CScript() << OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG));
// ZEN_MOD_START
        mTemplates.insert(make_pair(TX_PUBKEYHASH_REPLAY, CScript() << OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG << OP_SMALLDATA << OP_SMALLDATA << OP_CHECKBLOCKATHEIGHT));
// ZEN_MOD_END

        // Sender provides N pubkeys, receivers provides M signatures
        mTemplates.insert(make_pair(TX_MULTISIG, CScript() << OP_SMALLINTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_CHECKMULTISIG));
// ZEN_MOD_START
        mTemplates.insert(make_pair(TX_MULTISIG_REPLAY, CScript() << OP_SMALLINTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_CHECKMULTISIG << OP_SMALLDATA << OP_SMALLDATA << OP_CHECKBLOCKATHEIGHT));

        // P2SH, sender provides script hash
        mTemplates.insert(make_pair(TX_SCRIPTHASH, CScript() << OP_HASH160 << OP_PUBKEYHASH << OP_EQUAL));
        mTemplates.insert(make_pair(TX_SCRIPTHASH_REPLAY, CScript() << OP_HASH160 << OP_PUBKEYHASH << OP_EQUAL << OP_SMALLDATA << OP_SMALLDATA << OP_CHECKBLOCKATHEIGHT));
// ZEN_MOD_END

        // Empty, provably prunable, data-carrying output
        if (GetBoolArg("-datacarrier", true))
        {
            mTemplates.insert(make_pair(TX_NULL_DATA, CScript() << OP_RETURN << OP_SMALLDATA));
// ZEN_MOD_START
            mTemplates.insert(make_pair(TX_NULL_DATA_REPLAY, CScript() << OP_RETURN << OP_SMALLDATA << OP_SMALLDATA << OP_SMALLDATA << OP_CHECKBLOCKATHEIGHT));
// ZEN_MOD_END
        }
        mTemplates.insert(make_pair(TX_NULL_DATA, CScript() << OP_RETURN));
    }

// ZEN_MOD_START
    // OP_CHECKBLOCKATHEIGHT parameters
    vector<unsigned char> vchBlockHash, vchBlockHeight;
// ZEN_MOD_END

    // Scan templates
    const CScript& script1 = scriptPubKey;
    BOOST_FOREACH(const PAIRTYPE(txnouttype, CScript)& tplate, mTemplates)
    {
        const CScript& script2 = tplate.second;
        vSolutionsRet.clear();

        opcodetype opcode1, opcode2;
        vector<unsigned char> vch1, vch2;

        // Compare
        CScript::const_iterator pc1 = script1.begin();
        CScript::const_iterator pc2 = script2.begin();
        while (true)
        {
            if (pc1 == script1.end() && pc2 == script2.end())
            {
                // Found a match
                typeRet = tplate.first;
// ZEN_MOD_START
                if (typeRet == TX_MULTISIG || typeRet == TX_MULTISIG_REPLAY)
// ZEN_MOD_END
                {
                    // Additional checks for TX_MULTISIG:
                    unsigned char m = vSolutionsRet.front()[0];
                    unsigned char n = vSolutionsRet.back()[0];
                    if (m < 1 || n < 1 || m > n || vSolutionsRet.size()-2 != n)
                        return false;
                }

// ZEN_MOD_START
                if (typeRet == TX_SCRIPTHASH || typeRet == TX_SCRIPTHASH_REPLAY)
                {
                    vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
                    vSolutionsRet.push_back(hashBytes);
                }
// ZEN_MOD_END

                return true;
            }
            if (!script1.GetOp(pc1, opcode1, vch1))
                break;
            if (!script2.GetOp(pc2, opcode2, vch2))
                break;

            // Template matching opcodes:
            if (opcode2 == OP_PUBKEYS)
            {
                while (vch1.size() >= 33 && vch1.size() <= 65)
                {
                    vSolutionsRet.push_back(vch1);
                    if (!script1.GetOp(pc1, opcode1, vch1))
                        break;
                }
                if (!script2.GetOp(pc2, opcode2, vch2))
                    break;
                // Normal situation is to fall through
                // to other if/else statements
            }

            if (opcode2 == OP_PUBKEY)
            {
                if (vch1.size() < 33 || vch1.size() > 65)
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_PUBKEYHASH)
            {
                if (vch1.size() != sizeof(uint160))
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_SMALLINTEGER)
            {   // Single-byte small integer pushed onto vSolutions
                if (opcode1 == OP_0 ||
                    (opcode1 >= OP_1 && opcode1 <= OP_16))
                {
                    char n = (char)CScript::DecodeOP_N(opcode1);
                    vSolutionsRet.push_back(valtype(1, n));
                }
                else
                    break;
            }
            else if (opcode2 == OP_SMALLDATA)
            {
// ZEN_MOD_START
            	// Possible values of OP_CHECKBLOCKATHEIGHT parameters
            	if (vch1.size() <= sizeof(int32_t))
					vchBlockHeight = vch1;
				else
					vchBlockHash = vch1;
// ZEN_MOD_END

                // small pushdata, <= nMaxDatacarrierBytes
                if (vch1.size() > nMaxDatacarrierBytes)
                    break;
            }
// ZEN_MOD_START
            else if (opcode2 == OP_CHECKBLOCKATHEIGHT)
            {
            	// Full-fledged implementation of the OP_CHECKBLOCKATHEIGHT opcode for verification of vout's

#if !defined(BITCOIN_TX) // TODO: This is an workaround. zen-tx does not have access to chain state so no replay protection is possible

                if (vchBlockHash.size() == 0 || vchBlockHash.size() > 32)
                {
                    LogPrintf("%s: %s: OP_CHECKBLOCKATHEIGHT verification failed. Bad params.", __FILE__, __func__);
                    break;
                }

                const int32_t nHeight = vchBlockHeight.size() == 0 ? 0 : CScriptNum(vchBlockHeight, true, sizeof(int32_t)).getint();

                // According to BIP115, sufficiently old blocks are always valid, so check only blocks of depth less than 52596.
                // Skip check if referenced block is further than chainActive. It means that we are not fully synchronized.
                if (nHeight > (chainActive.Height() - 52596) && nHeight >= 0 &&
                    nHeight <= chainActive.Height())
                {
					CBlockIndex* pblockindex = chainActive[nHeight];

                    vector<unsigned char> vchCompareTo(pblockindex->GetBlockHash().begin(), pblockindex->GetBlockHash().end());
					try { vchCompareTo.erase(vchCompareTo.begin(), vchCompareTo.end() - vchBlockHash.size()); }
                    catch (...) {
                        LogPrintf("%s: %s: OP_CHECKBLOCKATHEIGHT verification failed. Bad block hash size.", __FILE__, __func__);
                        break;
                    }

					if (vchCompareTo != vchBlockHash)
                    {
                        LogPrintf("%s: %s: OP_CHECKBLOCKATHEIGHT verification failed. vout block height: %d", __FILE__, __func__, nHeight);
                        break;
                    }
                }
#endif
                if (opcode1 != opcode2 || vch1 != vch2)
                {
                    break;
                }
            }
// ZEN_MOD_END
            else if (opcode1 != opcode2 || vch1 != vch2)
            {
                // Others must match exactly
                break;
            }
        }
    }

    vSolutionsRet.clear();
    typeRet = TX_NONSTANDARD;
    return false;
}

int ScriptSigArgsExpected(txnouttype t, const std::vector<std::vector<unsigned char> >& vSolutions)
{
    switch (t)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
// ZEN_MOD_START
    case TX_NULL_DATA_REPLAY:
// ZEN_MOD_END
        return -1;
    case TX_PUBKEY:
// ZEN_MOD_START
    case TX_PUBKEY_REPLAY:
// ZEN_MOD_END
        return 1;
    case TX_PUBKEYHASH:
// ZEN_MOD_START
    case TX_PUBKEYHASH_REPLAY:
// ZEN_MOD_END
        return 2;
    case TX_MULTISIG:
// ZEN_MOD_START
    case TX_MULTISIG_REPLAY:
// ZEN_MOD_END
        if (vSolutions.size() < 1 || vSolutions[0].size() < 1)
            return -1;
        return vSolutions[0][0] + 1;
    case TX_SCRIPTHASH:
// ZEN_MOD_START
    case TX_SCRIPTHASH_REPLAY:
// ZEN_MOD_END
        return 1; // doesn't include args needed by the script
    }
    return -1;
}

bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType)
{
    vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

// ZEN_MOD_START
    if (whichType == TX_MULTISIG || whichType == TX_MULTISIG_REPLAY)
// ZEN_MOD_END
    {
        unsigned char m = vSolutions.front()[0];
        unsigned char n = vSolutions.back()[0];
        // Support up to x-of-3 multisig txns as standard
        if (n < 1 || n > 3)
            return false;
        if (m < 1 || m > n)
            return false;
    }

    return whichType != TX_NONSTANDARD;
}

bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet)
{
    vector<valtype> vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

// ZEN_MOD_START
    if (whichType == TX_PUBKEY || whichType == TX_PUBKEY_REPLAY)
// ZEN_MOD_END
    {
        CPubKey pubKey(vSolutions[0]);
        if (!pubKey.IsValid())
            return false;

        addressRet = pubKey.GetID();
        return true;
    }
// ZEN_MOD_START
    else if (whichType == TX_PUBKEYHASH || whichType == TX_PUBKEYHASH_REPLAY)
// ZEN_MOD_END
    {
        addressRet = CKeyID(uint160(vSolutions[0]));
        return true;
    }
// ZEN_MOD_START
    else if (whichType == TX_SCRIPTHASH || whichType == TX_SCRIPTHASH_REPLAY)
// ZEN_MOD_END
    {
        addressRet = CScriptID(uint160(vSolutions[0]));
        return true;
    }
    // Multisig txns have more than one address...
    return false;
}

bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, vector<CTxDestination>& addressRet, int& nRequiredRet)
{
    addressRet.clear();
    typeRet = TX_NONSTANDARD;
    vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, typeRet, vSolutions))
        return false;
// ZEN_MOD_START
    if (typeRet == TX_NULL_DATA || typeRet == TX_NULL_DATA_REPLAY){
// ZEN_MOD_END
        // This is data, not addresses
        return false;
    }

// ZEN_MOD_START
    if (typeRet == TX_MULTISIG || typeRet == TX_MULTISIG_REPLAY)
// ZEN_MOD_END
    {
        nRequiredRet = vSolutions.front()[0];
        for (unsigned int i = 1; i < vSolutions.size()-1; i++)
        {
            CPubKey pubKey(vSolutions[i]);
            if (!pubKey.IsValid())
                continue;

            CTxDestination address = pubKey.GetID();
            addressRet.push_back(address);
        }

        if (addressRet.empty())
            return false;
    }
    else
    {
        nRequiredRet = 1;
        CTxDestination address;
        if (!ExtractDestination(scriptPubKey, address))
           return false;
        addressRet.push_back(address);
    }

    return true;
}

namespace
{
class CScriptVisitor : public boost::static_visitor<bool>
{
private:
    CScript *script;
public:
    CScriptVisitor(CScript *scriptin) { script = scriptin; }

    bool operator()(const CNoDestination &dest) const {
        script->clear();
        return false;
    }

// ZEN_MOD_START
#ifdef BITCOIN_TX // zen-tx does not have access to chain state so no replay protection is possible
// ZEN_MOD_END
    bool operator()(const CKeyID &keyID) const {
        script->clear();
        *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
        return true;
    }

    bool operator()(const CScriptID &scriptID) const {
        script->clear();
        *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
        return true;
    }
// ZEN_MOD_START
#else
    bool operator()(const CKeyID &keyID) const {
        script->clear();
        CBlockIndex *currentBlock = chainActive.Tip();
        if (currentBlock == NULL) {
            *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
            return true;
        }
        int blockIndex = currentBlock->nHeight - 300;
        if (blockIndex < 0)
            blockIndex = 0;
        *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG << ToByteVector(chainActive[blockIndex]->GetBlockHash()) << chainActive[blockIndex]->nHeight << OP_CHECKBLOCKATHEIGHT;
        return true;
    }

    bool operator()(const CScriptID &scriptID) const {
        script->clear();
        CBlockIndex *currentBlock = chainActive.Tip();
        if (currentBlock == NULL) {
            *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
            return true;
        }
        int blockIndex = currentBlock->nHeight - 300;
        if (blockIndex < 0)
            blockIndex = 0;
        *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL << ToByteVector(chainActive[blockIndex]->GetBlockHash()) << chainActive[blockIndex]->nHeight << OP_CHECKBLOCKATHEIGHT;
        return true;
    }
#endif
// ZEN_MOD_END

};
}

CScript GetScriptForDestination(const CTxDestination& dest)
{
    CScript script;

    boost::apply_visitor(CScriptVisitor(&script), dest);
    return script;
}

CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys)
{
    CScript script;

    script << CScript::EncodeOP_N(nRequired);
    BOOST_FOREACH(const CPubKey& key, keys)
        script << ToByteVector(key);
    script << CScript::EncodeOP_N(keys.size()) << OP_CHECKMULTISIG;
    return script;
}
