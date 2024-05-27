// Copyright (c) 2018-2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fork0_originalfork.h"

namespace zen {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PUBLIC MEMBERS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief OriginalFork constructor
 */
OriginalFork::OriginalFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,0},
                  {CBaseChainParams::Network::REGTEST,0},
                  {CBaseChainParams::Network::TESTNET,0}});

    setMinimumTimeMap({
                               {CBaseChainParams::Network::MAIN,0},
                               {CBaseChainParams::Network::REGTEST,0},
                               {CBaseChainParams::Network::TESTNET,0}
                           });
}

/**
 * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
 * @param reward the main reward
 * @return the community reward
 */
CAmount OriginalFork::getCommunityFundReward(const CAmount& amount, CommunityFundType cfType) const {
    return (CAmount)0L;
}

/**
 * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
 * @param height the height
 * @param maxHeight the maximum height sometimes used in the computation of the proper address
 * @return the community fund address for this height
 */
const std::string& OriginalFork::getCommunityFundAddress(CBaseChainParams::Network network, int height, int maxHeight, CommunityFundType cfType) const {
    static std::string emptyAddress = "";
    return emptyAddress;
}

/**
 * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
 * @param transactionType transaction type
 * @return true if allowed, false otherwise
 */
bool OriginalFork::isTransactionTypeAllowed(txnouttype transactionType) const {
    switch (transactionType) {
    case TX_NONSTANDARD:
    case TX_PUBKEY:
    case TX_PUBKEYHASH:
    case TX_SCRIPTHASH:
    case TX_MULTISIG:
    case TX_NULL_DATA:

    // bug: in testnet blockchain this tx type is before the chainsplit
    case TX_PUBKEYHASH_REPLAY:   
        return true;
    default:
        return false;
    }
}

bool OriginalFork::canSendCommunityFundsToTransparentAddress(CBaseChainParams::Network network) const {
    if (network == CBaseChainParams::Network::MAIN ||
        network == CBaseChainParams::Network::TESTNET ||
        mapArgs.count("-regtestprotectcoinbase"))
    {
        return false;
    }
    
    return true;
}

bool OriginalFork::mustCoinBaseBeShielded(CBaseChainParams::Network network) const {
    if (network == CBaseChainParams::Network::MAIN ||
        network == CBaseChainParams::Network::TESTNET ||
        mapArgs.count("-regtestprotectcoinbase"))
    {
        return true;
    }

    return false;
}
}
