// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINPARAMS_H
#define BITCOIN_CHAINPARAMS_H

#include "chainparamsbase.h"
#include "checkpoints.h"
#include "consensus/params.h"
#include "primitives/block.h"
#include "protocol.h"

#include "zen/forkmanager.h"

#include <vector>

using namespace zen;

struct CDNSSeedData {
    std::string name, host;
    CDNSSeedData(const std::string &strName, const std::string &strHost) : name(strName), host(strHost) {}
};

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};


/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Bitcoin system. There are three: the main network on which people trade goods
 * and services, the public test network which gets reset from time to time and
 * a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams
{
public:
    enum Base58Type {
        PUBKEY_ADDRESS,
        PUBKEY_ADDRESS_OLD,
        SCRIPT_ADDRESS,
        SCRIPT_ADDRESS_OLD,
        SECRET_KEY,
        EXT_PUBLIC_KEY,
        EXT_SECRET_KEY,

        ZCPAYMENT_ADDRRESS,
        ZCSPENDING_KEY,
        ZCVIEWING_KEY,

        MAX_BASE58_TYPES
    };

    const Consensus::Params& GetConsensus() const { return consensus; }
    const CMessageHeader::MessageStartChars& MessageStart() const { return pchMessageStart; }
    int GetDefaultPort() const { return nDefaultPort; }

    const CBlock& GenesisBlock() const { return genesis; }
    /** Make miner wait to have peers to avoid wasting work */
    bool MiningRequiresPeers() const { return fMiningRequiresPeers; }
    /** Default value for -checkmempool and -checkblockindex argument */
    bool DefaultConsistencyChecks() const { return fDefaultConsistencyChecks; }
    /** Policy: Filter transactions that do not match well-defined patterns */
    bool RequireStandard() const { return fRequireStandard; }
    int CbhMinimumAge() const { return nCbhMinimumAge; }
    int CbhSafeDepth() const { return nCbhSafeDepth; }
    int ScCoinsMaturity() const { return nScCoinsMaturity; }
    int ScNumBlocksForScFeeCheck() const { return nScNumBlocksForScFeeCheck; }
    int ScMinWithdrawalEpochLength() const { return nScMinWithdrawalEpochLength; }
    int ScMaxWithdrawalEpochLength() const { return nScMaxWithdrawalEpochLength; }
    int ScMaxNumberOfCswInputsInMempool() const { return nScMaxNumberOfCswInputsInMempool; }
    int64_t MaxTipAge() const;
    int64_t PruneAfterHeight() const { return nPruneAfterHeight; }
    unsigned int EquihashN() const { return nEquihashN; }
    unsigned int EquihashK() const { return nEquihashK; }
    std::string CurrencyUnits() const { return strCurrencyUnits; }
    /** Make miner stop after a block is found. In RPC, don't return until nGenProcLimit blocks are generated */
    bool MineBlocksOnDemand() const { return fMineBlocksOnDemand; }
    /** In the future use NetworkIDString() for RPC fields */
    bool TestnetToBeDeprecatedFieldRPC() const { return fTestnetToBeDeprecatedFieldRPC; }
    /** Return the BIP70 network string (main, test or regtest) */
    std::string NetworkIDString() const { return strNetworkID; }
    const std::vector<CDNSSeedData>& DNSSeeds() const { return vSeeds; }
    const std::vector<unsigned char>& Base58Prefix(Base58Type type) const { return base58Prefixes[type]; }
    const std::vector<SeedSpec6>& FixedSeeds() const { return vFixedSeeds; }
    const Checkpoints::CCheckpointData& Checkpoints() const { return checkpointData; }
    /** Return the community fund address and script for a given block height */
    std::string GetCommunityFundAddressAtHeight(int height, Fork::CommunityFundType cfType) const;
    CScript GetCommunityFundScriptAtHeight(int height, Fork::CommunityFundType cfType) const;
    void SetSubsidyHalvingInterval(int val) { consensus.nSubsidyHalvingInterval = val;}
protected:
    CChainParams() {}

    Consensus::Params consensus;
    CMessageHeader::MessageStartChars pchMessageStart;
    int nDefaultPort = 0;
    long nMaxTipAge = 0;
    uint64_t nPruneAfterHeight = 0;
    unsigned int nEquihashN = 0;
    unsigned int nEquihashK = 0;
    std::vector<CDNSSeedData> vSeeds;
    std::vector<unsigned char> base58Prefixes[MAX_BASE58_TYPES];
    std::string strNetworkID;
    std::string strCurrencyUnits;
    CBlock genesis;
    std::vector<SeedSpec6> vFixedSeeds;
    bool fMiningRequiresPeers = false;
    bool fDefaultConsistencyChecks = false;
    bool fRequireStandard = false;
    bool fMineBlocksOnDemand = false;
    bool fTestnetToBeDeprecatedFieldRPC = false;
    int  nCbhMinimumAge = 0;
    int  nCbhSafeDepth = 0;
    int  nScCoinsMaturity = 0;
    int  nScNumBlocksForScFeeCheck = 0;
    int  nScMinWithdrawalEpochLength = 0;
    int  nScMaxWithdrawalEpochLength = 0;
    int  nScMaxNumberOfCswInputsInMempool = 0;
    Checkpoints::CCheckpointData checkpointData;
};

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CChainParams &Params();

/** Return parameters for the given network. */
CChainParams &Params(CBaseChainParams::Network network);

/** Sets the params returned by Params() to those for the given network. */
void SelectParams(CBaseChainParams::Network network);

/**
 * Looks for -regtest or -testnet and then calls SelectParams as appropriate.
 * Returns false if an invalid combination is given.
 */
bool SelectParamsFromCommandLine();

#endif // BITCOIN_CHAINPARAMS_H
