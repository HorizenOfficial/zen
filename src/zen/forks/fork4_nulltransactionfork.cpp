// Copyright (c) 2018-2020 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fork4_nulltransactionfork.h"

namespace zen {

NullTransactionFork::NullTransactionFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,344700},
                  {CBaseChainParams::Network::REGTEST,105},
                  {CBaseChainParams::Network::TESTNET,260500}});
    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zszpcLB6C5B8QvfDbF2dYWXsrpac5DL9WRk"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrFzxutppvxEdjyu4QNjogBMjtC1py9Hp1S"
                                 }}}, CommunityFundType::FOUNDATION);

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zsxWnyDbU8pk2Vp98Uvkx5Nh33RFzqnCpWN"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrQG6x9j33DLbCfzAqu3qKMe7z1VDL1z2L7"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrS7QUB2eDbbKvyP43VJys3t7RpojW8GdxH"
                                 }}}, CommunityFundType::SECURENODE);

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zsnL6pKdzvZ1BPVzALUoqw2KsY966XFs5CE"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrMasbhB1yyfQ5RBUm7NPcEjGWZdRneWCEx"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrFr5HVm7woVq3oFzkMEdJdbfBchfPAPDsP"
                                 }}}, CommunityFundType::SUPERNODE);

    addressChangeIntervals = {
        {CBaseChainParams::Network::MAIN,50000},
        {CBaseChainParams::Network::REGTEST,100},
        {CBaseChainParams::Network::TESTNET,10000}
    };
}

/*
 * These are the prv keys used in REGTEST for getting the community fund p2sh addresses:
 *
 * === FOUNDATION ===
 * "privkey": "cQqMxnYBJUUS3jERyQSJWFuQV54eKTgS2v68wMNHXtNg9HzuyiAk"
 * 
 * === SECURENODE ===
 * "privkey": "cTbp5QgshYtVGRqmTw5rA3GLSfnqnSX5RsBjdY1QPaXBxU6EfKAy"
 * 
 * === SUPERNODE ===
 * "privkey": "cTjAijxL4AJxk4CFi1Sn88joturRYTaagB1NQdYVoDHsJqxqRCnM"

 * After having imported the relevant priv key:
 *     src/zen-cli --regtest importprivkey <privkey>
 * The multi sig (m=1) redeemscript can be added to the wallet via:
 *     src/zen-cli --regtest addmultisigaddress 1 "[\"<zen_addr>\"]
 */

/*
* @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
* @param reward the main reward
* @return the community reward
*/
CAmount NullTransactionFork::getCommunityFundReward(const CAmount& amount, CommunityFundType cfType) const {
    if (cfType == CommunityFundType::FOUNDATION) {
        return (CAmount)amount*100/1000;
    }
    if (cfType == CommunityFundType::SECURENODE) {
        return (CAmount)amount*100/1000;
    }
    if (cfType == CommunityFundType::SUPERNODE) {
        return (CAmount)amount*100/1000;
    }
    return (CAmount)0L;
}

/**
 * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
 * @param height the height
 * @param maxHeight the maximum height sometimes used in the computation of the proper address
 * @return the community fund address for this height
 */
const std::string& NullTransactionFork::getCommunityFundAddress(CBaseChainParams::Network network, int height, int maxHeight, CommunityFundType cfType) const {
    const std::vector<std::string>& communityFundAddresses = this->getCommunityFundAddresses(network, cfType);
    int addressChangeInterval = addressChangeIntervals.at(network);
    // change CF addresses every addressChangeInterval in a round-robin fashion
    size_t i = ((height - getHeight(network)) / addressChangeInterval) % communityFundAddresses.size();
    return communityFundAddresses[i];
}

/**
 * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
 * @param transactionType transaction type
 * @return true if allowed, false otherwise
 */
bool NullTransactionFork::isTransactionTypeAllowed(txnouttype transactionType) const {
    if (transactionType == TX_NULL_DATA_REPLAY)
        return true;
    return CommunityFundAndRPFixFork::isTransactionTypeAllowed(transactionType);
}

}
