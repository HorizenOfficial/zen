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

#include "main.h"
#include "zen/forkmanager.h"
using namespace zen;

using namespace std;

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
    case TX_SCRIPTHASH_REPLAY: return "scripthashreplay";
    case TX_MULTISIG: return "multisig";
    case TX_NULL_DATA: return "nulldata";
    case TX_PUBKEY_REPLAY: return "pubkeyreplay";
    case TX_PUBKEYHASH_REPLAY: return "pubkeyhashreplay";
    case TX_MULTISIG_REPLAY: return "multisigreplay";
    case TX_NULL_DATA_REPLAY: return "nulldatareplay";
    }
    return NULL;
}


/**
 * Return public keys or hashes from scriptPubKey, for 'standard' transaction types.
 */
bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, vector<vector<unsigned char> >& vSolutionsRet, ReplayProtectionAttributes& rpAttributes)
{
    // Templates
    static multimap<txnouttype, CScript> mTemplates;
    if (mTemplates.empty())
    {
        // Standard tx, sender provides pubkey, receiver adds signature
        mTemplates.insert(make_pair(TX_PUBKEY, CScript() << OP_PUBKEY << OP_CHECKSIG));
        mTemplates.insert(make_pair(TX_PUBKEY_REPLAY, CScript() << OP_PUBKEY << OP_CHECKSIG << OP_SMALLDATA << OP_SMALLDATA << OP_CHECKBLOCKATHEIGHT));

        // Bitcoin address tx, sender provides hash of pubkey, receiver provides signature and pubkey
        mTemplates.insert(make_pair(TX_PUBKEYHASH, CScript() << OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG));
        mTemplates.insert(make_pair(TX_PUBKEYHASH_REPLAY, CScript() << OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG << OP_SMALLDATA << OP_SMALLDATA << OP_CHECKBLOCKATHEIGHT));

        // Sender provides N pubkeys, receivers provides M signatures
        mTemplates.insert(make_pair(TX_MULTISIG, CScript() << OP_SMALLINTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_CHECKMULTISIG));
        mTemplates.insert(make_pair(TX_MULTISIG_REPLAY, CScript() << OP_SMALLINTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_CHECKMULTISIG << OP_SMALLDATA << OP_SMALLDATA << OP_CHECKBLOCKATHEIGHT));

        // P2SH, sender provides script hash
        mTemplates.insert(make_pair(TX_SCRIPTHASH, CScript() << OP_HASH160 << OP_PUBKEYHASH << OP_EQUAL));
        mTemplates.insert(make_pair(TX_SCRIPTHASH_REPLAY, CScript() << OP_HASH160 << OP_PUBKEYHASH << OP_EQUAL << OP_SMALLDATA << OP_SMALLDATA << OP_CHECKBLOCKATHEIGHT));

        // Empty, provably prunable, data-carrying output
        if (GetBoolArg("-datacarrier", true))
        {
            mTemplates.insert(make_pair(TX_NULL_DATA, CScript() << OP_RETURN << OP_SMALLDATA));
            mTemplates.insert(make_pair(TX_NULL_DATA_REPLAY, CScript() << OP_RETURN << OP_SMALLDATA << OP_SMALLDATA << OP_SMALLDATA << OP_CHECKBLOCKATHEIGHT));
        }
        mTemplates.insert(make_pair(TX_NULL_DATA, CScript() << OP_RETURN));
        mTemplates.insert(make_pair(TX_NULL_DATA_REPLAY, CScript() << OP_RETURN << OP_SMALLDATA << OP_SMALLDATA << OP_CHECKBLOCKATHEIGHT));
    }

#if !defined(BITCOIN_TX)
    const int32_t nChActHeight = chainActive.Height();
#else
    const int32_t nChActHeight = 0;
#endif // BITCOIN_TX

    // patch level of the replay protection forks
    ReplayProtectionLevel rpLevel = ForkManager::getInstance().getReplayProtectionLevel(nChActHeight);
    rpAttributes.SetNull();

    // Scan templates
    const CScript& script1 = scriptPubKey;
    BOOST_FOREACH(const PAIRTYPE(txnouttype, CScript)& tplate, mTemplates)
    {
        const CScript& script2 = tplate.second;
        vSolutionsRet.clear();

        opcodetype opcode1, opcode2;
        vector<unsigned char> vch1, vch2;

        // OP_CHECKBLOCKATHEIGHT parameters
        // --
        // used before rp-level-2 fix fork
        vector<unsigned char> vchBlockHash, vchBlockHeight;
        // used after rp-level-2 fix fork, in order to check the order of processing of hash and height in a rp script
        std::vector< std::pair<std::vector<unsigned char>, opcodetype>> vchCbhParams;

        // Compare
        CScript::const_iterator pc1 = script1.begin();
        CScript::const_iterator pc2 = script2.begin();
        while (true)
        {
            if (pc1 == script1.end() && pc2 == script2.end())
            {
                // Found a match
                typeRet = tplate.first;
                if (typeRet == TX_MULTISIG || typeRet == TX_MULTISIG_REPLAY)
                {
                    // Additional checks for TX_MULTISIG:
                    unsigned char m = vSolutionsRet.front()[0];
                    unsigned char n = vSolutionsRet.back()[0];
                    if (m < 1 || n < 1 || m > n || vSolutionsRet.size()-2 != n)
                        return false;
                }

                if (typeRet == TX_SCRIPTHASH || typeRet == TX_SCRIPTHASH_REPLAY)
                {
                    vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
                    vSolutionsRet.push_back(hashBytes);
                }

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
                if (rpLevel < RPLEVEL_FIXED_2)
                {
                    // Possible values of OP_CHECKBLOCKATHEIGHT parameters
                    if (vch1.size() <= sizeof(int32_t))
                    {
                        if (vch1.size() == 0 && (opcode1 >= OP_1 && opcode1 <= OP_16) )
                        {
                            // small size int (1..16) are not in vch1
                            // they are represented in the opcode itself
                            // (see CScript::push_int64() method)

                            // leave vch1 alone and use a copy, just to be in the safest side
                            vector<unsigned char> vTemp;
                            vTemp.push_back((unsigned char)(opcode1 - OP_1 + 1));
                            vchBlockHeight = vTemp;
                        }
                        else
                        {
                            vchBlockHeight = vch1;
                        }
                    }
                    else
                    {
                        vchBlockHash = vch1;
                    }
                }
                else
                {
                    std::vector<unsigned char> vchCbhData;
                    // Possible values of OP_CHECKBLOCKATHEIGHT parameters
                    // they are pushed into a stack for preventing the inversion of height/hash

                    if (vch1.size() == 0)
                    {
                        if ((opcode1 >= OP_1 && opcode1 <= OP_16) || opcode1 == OP_1NEGATE)
                        {
                            // small size int (1..16) are not in vch1
                            // they are represented in the opcode itself
                            // (see CScript::push_int64() method)
                            // the same holds for -1, which we choose to handle here too
        
                            CScriptNum bn((int)opcode1 - (int)(OP_1 - 1));
                            vchCbhData = std::move(bn.getvch());
                        }
                        else if (opcode1 == OP_0)
                        {
                            CScriptNum bn((int)opcode1);
                            // an empty vector
                            vchCbhData = std::move(bn.getvch());
                        }
                        else
                        {
                            // opcode other that the ones specified above are not legal
                            LogPrintf("%s: %s:%d - OP_CHECKBLOCKATHEIGHT verification failed. Bad height param (opcode=0x%X not legal in setting height).\n",
                                __FILE__, __func__, __LINE__, opcode1);
                            break;
                        }
                    }
                    else
                    {
                        vchCbhData = vch1;
                    }

                    vchCbhParams.push_back(std::make_pair(vchCbhData, opcode1));
                }

                // small pushdata, <= nMaxDatacarrierBytes
                if (vch1.size() > nMaxDatacarrierBytes)
                {
                    LogPrintf("%s: %s():%d - data size %d bigger than max allowed %d\n",
                        __FILE__, __func__, __LINE__, vch1.size(), nMaxDatacarrierBytes );
                    break;
                }
            }
            else if (opcode2 == OP_CHECKBLOCKATHEIGHT)
            {
                rpAttributes.foundOpCode = true;

#if !defined(BITCOIN_TX) // zen-tx does not have access to chain state so no replay protection is possible
                if (rpLevel < RPLEVEL_FIXED_2)
                {
                    // Full-fledged implementation of the OP_CHECKBLOCKATHEIGHT opcode for verification of vout's

                    if (vchBlockHash.size() != 32)
                    {
                        LogPrintf("%s: %s: OP_CHECKBLOCKATHEIGHT verification failed. Bad params.\n", __FILE__, __func__);
                        break;
                    }

                    const int32_t nHeight = CScriptNum(vchBlockHeight, false, sizeof(int32_t)).getint();

                    if ((nHeight < 0 || nHeight > nChActHeight ) && rpLevel == RPLEVEL_FIXED_1)
                    {
                        LogPrint("cbh", "%s: %s():%d - OP_CHECKBLOCKATHEIGHT nHeight not legal[%d], chainActive height: %d\n",
                            __FILE__, __func__, __LINE__, nHeight, nChActHeight);
                        break;
                    }

                    // According to BIP115, sufficiently old blocks are always valid, so reject only blocks of depth less than 52596.
                    // Skip check if referenced block is further than chainActive. It means that we are not fully synchronized.
                    if (nHeight > (nChActHeight - getCheckBlockAtHeightSafeDepth() ) && nHeight >= 0 &&
                        nHeight <= nChActHeight)
                    {
                        CBlockIndex* pblockindex = chainActive[nHeight];

                        if (pblockindex->GetBlockHash() != uint256(vchBlockHash))
                        {
                            LogPrintf("%s: %s: OP_CHECKBLOCKATHEIGHT verification failed: script block height: %d\n", __FILE__, __func__, nHeight);
                            break;
                        }
                    }

                    // interested caller will use this for enforcing that referenced block is valid and not too recent
                    rpAttributes.referencedHeight = nHeight;
                    rpAttributes.referencedHash   = vchBlockHash;
                }
                else
                {
                    size_t len = vchCbhParams.size();
                    if (len < 2)
                    {
                        LogPrintf("%s: %s():%d - OP_CHECKBLOCKATHEIGHT verification failed. Bad params size = %d\n",
                            __FILE__, __func__, __LINE__, len);
                        break;
                    }
 
                    // they must have been parsed in this order, the following check protects against their swapping
                    vchBlockHash   = vchCbhParams.at(len-2).first;

                    vchBlockHeight = vchCbhParams.at(len-1).first;
                    opcodetype hopcode  = vchCbhParams.at(len-1).second;
 
                    // vchBlockHeight can be empty when height is represented as 0
                    if ((vchBlockHeight.size() > sizeof(int)) || (vchBlockHash.size() != 32))
                    {
                        LogPrintf("%s: %s():%d - OP_CHECKBLOCKATHEIGHT verification failed. Bad params: vh size = %d, vhash size = %d\n",
                            __FILE__, __func__, __LINE__, vchBlockHeight.size(), vchBlockHash.size());
                        break;
                    }
 
                    // Check that the number is encoded with the minimum possible number of bytes. This is also different 
                    // before the fork but this way is consistent with interpreter
                    static const bool REQ_MINIMAL = true;
                    int32_t nHeight = -1;
                    try
                    {
                        nHeight = CScriptNum(vchBlockHeight, REQ_MINIMAL, sizeof(int32_t)).getint();
                    }
                    catch(const scriptnum_error& e)
                    {
                        LogPrintf("%s: %s():%d - OP_CHECKBLOCKATHEIGHT nHeight 0x%s not minimally encoded (err=%s)\n",
                            __FILE__, __func__, __LINE__, HexStr(vchBlockHeight.begin(), vchBlockHeight.end()), e.what());
                        break;
                    }
                    catch(...)
                    {
                        LogPrintf("%s: %s():%d - unexpected exception\n", __FILE__, __func__, __LINE__);
                        break;
                    }

                    if (!CheckMinimalPush(vchBlockHeight, hopcode))
                    {
                        LogPrintf("%s: %s():%d - OP_CHECKBLOCKATHEIGHT value 0x%s not minimally pushed\n",
                            __FILE__, __func__, __LINE__, HexStr(vchBlockHeight.begin(), vchBlockHeight.end()) );
                        break;
                    }

                    // height outside the chain range are legal only in old rp implementations, here we are in rp fix fork
                    if ( nHeight < 0 || nHeight> nChActHeight) 
                    {
                        // can happen also when aligning the blockchain
                        LogPrint("cbh", "%s: %s():%d - OP_CHECKBLOCKATHEIGHT nHeight not legal[%d], chainActive height: %d\n",
                            __FILE__, __func__, __LINE__, nHeight, nChActHeight);
                        break;
                    }

                    // the logic for skipping the check for sufficently old blocks is in the checker obj method, similarly
                    // to what EvalScript() parser does. 
                    if (!CheckReplayProtectionData(&chainActive, nHeight, vchBlockHash) )
                    {
                        LogPrintf("%s: %s():%d OP_CHECKBLOCKATHEIGHT verification failed. Referenced height %d invalid or not corresponding to hash %s\n",
                            __FILE__, __func__, __LINE__, nHeight, uint256(vchBlockHash).ToString());
                        break;
                    }

                    // interested caller will use this for enforcing that referenced block is valid and not too recent
                    rpAttributes.referencedHeight = nHeight;
                    rpAttributes.referencedHash   = vchBlockHash;
                }
#endif // BITCOIN_TX

                if (opcode1 != opcode2 || vch1 != vch2)
                {
                    break;
                }
            }
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

bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, vector<vector<unsigned char> >& vSolutionsRet) {
    ReplayProtectionAttributes rpAttributes;
    return Solver(scriptPubKey, typeRet, vSolutionsRet, rpAttributes);
}


int ScriptSigArgsExpected(txnouttype t, const std::vector<std::vector<unsigned char> >& vSolutions)
{
    switch (t)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
    case TX_NULL_DATA_REPLAY:
        return -1;
    case TX_PUBKEY:
    case TX_PUBKEY_REPLAY:
        return 1;
    case TX_PUBKEYHASH:
    case TX_PUBKEYHASH_REPLAY:
        return 2;
    case TX_MULTISIG:
    case TX_MULTISIG_REPLAY:
        if (vSolutions.size() < 1 || vSolutions[0].size() < 1)
            return -1;
        return vSolutions[0][0] + 1;
    case TX_SCRIPTHASH:
    case TX_SCRIPTHASH_REPLAY:
        return 1; // doesn't include args needed by the script
    }
    return -1;
}

bool CheckReplayProtectionAttributes(const CScript& scriptPubKey)
{
#if !defined(BITCOIN_TX)
    ReplayProtectionAttributes rpAttributes;
    vector<valtype> vSolutions;
    txnouttype whichType;

    bool solverResult = Solver(scriptPubKey, whichType, vSolutions, rpAttributes);
    if(!rpAttributes.foundOpCode)
    {
        if(!solverResult)
        {
            LogPrint("cbh", "%s: %s():%d solver failed but no rp attributes found for script %s\n",
                __FILE__, __func__, __LINE__, scriptPubKey.ToString());
        }
        // we are checking only the rp attributes and do not care of other cases
        return true;
    }
    return solverResult;
    
#else
    // zen-tx does not have access to chain state so replay protection check is not applicable 
    return true;
#endif // BITCOIN_TX
}

void GetReplayProtectionAttributes(const CScript& scriptPubKey, ReplayProtectionAttributes& rpAttributes)
{
    vector<valtype> vSolutions;
    txnouttype whichType;
    Solver(scriptPubKey, whichType, vSolutions, rpAttributes);
}

bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType)
{
    ReplayProtectionAttributes rpAttributes;
    return IsStandard(scriptPubKey, whichType, rpAttributes);
}

bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType, ReplayProtectionAttributes& rpAttributes)
{
    vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, whichType, vSolutions, rpAttributes))
        return false;

    if (whichType == TX_MULTISIG || whichType == TX_MULTISIG_REPLAY)
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

    if (whichType == TX_PUBKEY || whichType == TX_PUBKEY_REPLAY)
    {
        CPubKey pubKey(vSolutions[0]);
        if (!pubKey.IsValid())
            return false;

        addressRet = pubKey.GetID();
        return true;
    }
    else if (whichType == TX_PUBKEYHASH || whichType == TX_PUBKEYHASH_REPLAY)
    {
        addressRet = CKeyID(uint160(vSolutions[0]));
        return true;
    }
    else if (whichType == TX_SCRIPTHASH || whichType == TX_SCRIPTHASH_REPLAY)
    {
        addressRet = CScriptID(uint160(vSolutions[0]));
        return true;
    }
    else if (whichType == TX_NULL_DATA || whichType == TX_NULL_DATA_REPLAY)
    {
        // no address is stored
        return false;
    }
    else if (whichType == TX_MULTISIG || whichType == TX_MULTISIG_REPLAY)
    {
        // Multisig txns have more than one address...
        return false;
    }
    LogPrintf("%s():%d - Unknown transaction type found %d\n", __func__, __LINE__, whichType);
    return false;
}

bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, vector<CTxDestination>& addressRet, int& nRequiredRet)
{
    addressRet.clear();
    typeRet = TX_NONSTANDARD;
    vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, typeRet, vSolutions))
        return false;
    if (typeRet == TX_NULL_DATA || typeRet == TX_NULL_DATA_REPLAY){
        // This is data, not addresses
        return false;
    }

    if (typeRet == TX_MULTISIG || typeRet == TX_MULTISIG_REPLAY)
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
    bool withCheckBlockAtHeight;
public:
    CScriptVisitor(CScript *scriptin, bool withCheckBlockAtHeightIn) {
        script = scriptin;
        withCheckBlockAtHeight = withCheckBlockAtHeightIn;
    }

    bool operator()(const CNoDestination &dest) const {
        script->clear();
        return false;
    }

#ifdef BITCOIN_TX // zen-tx does not have access to chain state so no replay protection is possible
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
#else
    bool operator()(const CKeyID &keyID) const {
        script->clear();
        CBlockIndex *currentBlock = chainActive.Tip();
        if (currentBlock == NULL || !withCheckBlockAtHeight) {
            *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
            return true;
        }
        int blockIndex = currentBlock->nHeight - CBH_DELTA_HEIGHT;
        if (blockIndex < 0)
            blockIndex = 0;
        *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG << ToByteVector(chainActive[blockIndex]->GetBlockHash()) << chainActive[blockIndex]->nHeight << OP_CHECKBLOCKATHEIGHT;
        return true;
    }

    bool operator()(const CScriptID &scriptID) const {
        script->clear();
        CBlockIndex *currentBlock = chainActive.Tip();
        if (currentBlock == NULL || !withCheckBlockAtHeight) {
            *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
            return true;
        }
        int blockIndex = currentBlock->nHeight - CBH_DELTA_HEIGHT;
        if (blockIndex < 0)
            blockIndex = 0;
        *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL << ToByteVector(chainActive[blockIndex]->GetBlockHash()) << chainActive[blockIndex]->nHeight << OP_CHECKBLOCKATHEIGHT;
        return true;
    }
#endif // BITCOIN_TX

};
}

CScript GetScriptForDestination(const CTxDestination& dest, bool withCheckBlockAtHeight)
{
    CScript script;

    boost::apply_visitor(CScriptVisitor(&script, withCheckBlockAtHeight), dest);
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

ReplayProtectionAttributes::ReplayProtectionAttributes()
    :referencedHeight(UNDEF), referencedHash(), foundOpCode(false)
{}

void ReplayProtectionAttributes::SetNull() 
{
    referencedHeight = UNDEF;
    referencedHash.clear();
    foundOpCode = false;
}

bool ReplayProtectionAttributes::GotValues() const
{
    return ( referencedHeight != UNDEF && !referencedHash.empty() );
}

