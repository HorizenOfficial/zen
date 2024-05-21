// Copyright (c) 2018-2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fork3_communityfundandrpfixfork.h"

namespace zen {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PUBLIC MEMBERS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief CommunityFundAndRPFixFork constructor
 */
CommunityFundAndRPFixFork::CommunityFundAndRPFixFork(){
    setHeightMap({{CBaseChainParams::Network::MAIN,139200},
                  {CBaseChainParams::Network::REGTEST,101},
                  {CBaseChainParams::Network::TESTNET,85500}});
    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zsyF68hcYYNLPj5i4PfQJ1kUY6nsFnZkc82",
                                     "zsfULrmbX7xbhqhAFRffVqCw9RyGv2hqNNG",
                                     "zsoemTfqjicem2QVU8cgBHquKb1o9JR5p4Z",
                                     "zt339oiGL6tTgc9Q71f5g1sFTZf6QiXrRUr"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{ 
                                     "zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD" 
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1"
                                 }}}, CommunityFundType::FOUNDATION);
    addressChangeIntervals = {
        {CBaseChainParams::Network::MAIN,50000},
        {CBaseChainParams::Network::REGTEST,100},
        {CBaseChainParams::Network::TESTNET,10000}
    };
}

/*
 * This is the prv key used in REGTEST for getting the community fund p2sh address:
 *
 * "privkey": "cQqMxnYBJUUS3jERyQSJWFuQV54eKTgS2v68wMNHXtNg9HzuyiAk"
 * 
 * After having imported the relevant priv key:
 *     src/zen-cli --regtest importprivkey <privkey>
 * The multi sig (m=1) redeemscript can be added to the wallet via:
 *     src/zen-cli --regtest addmultisigaddress 1 "[\"<zen_addr>\"]
 */

/**
 * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
 * @param reward the main reward
 * @return the community reward
 */
CAmount CommunityFundAndRPFixFork::getCommunityFundReward(const CAmount& amount, CommunityFundType cfType) const {
    if (cfType != CommunityFundType::FOUNDATION) { return 0; }
    return (CAmount)amount*120/1000;
}

/**
 * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
 * @param height the height
 * @param maxHeight the maximum height sometimes used in the computation of the proper address
 * @return the community fund address for this height
 */
const std::string& CommunityFundAndRPFixFork::getCommunityFundAddress(CBaseChainParams::Network network, int height, int maxHeight, CommunityFundType cfType) const {
    if (cfType != CommunityFundType::FOUNDATION) {
        static std::string emptyAddress = "";
        return emptyAddress;
    }
    const std::vector<std::string>& communityFundAddresses = this->getCommunityFundAddresses(network, cfType);
    int addressChangeInterval = addressChangeIntervals.at(network);
    // change CF addresses every addressChangeInterval in a round-robin fashion
    size_t i = ((height - getHeight(network)) / addressChangeInterval) % communityFundAddresses.size();
    return communityFundAddresses[i];
}

/**
 * @brief canSendCommunityFundsToTransparentAddress true if community funds can be sent to a transparent address
 */
bool CommunityFundAndRPFixFork::canSendCommunityFundsToTransparentAddress(CBaseChainParams::Network network) const { return true; }

/**
 * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
 * @param transactionType transaction type
 * @return true if allowed, false otherwise
 */
bool CommunityFundAndRPFixFork::isTransactionTypeAllowed(txnouttype transactionType) const {
    if (transactionType == TX_SCRIPTHASH_REPLAY)
        return true;
    return ReplayProtectionFork::isTransactionTypeAllowed(transactionType);
}

}
