#include "communityfundandrpfixfork.h"

namespace zen {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PUBLIC MEMBERS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief CommunityFundAndRPFixFork constructor
 */
CommunityFundAndRPFixFork::CommunityFundAndRPFixFork(){
    setHeightMap({{CBaseChainParams::Network::MAIN,139200},
                  {CBaseChainParams::Network::REGTEST,1200},
                  {CBaseChainParams::Network::TESTNET,85500}});
    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zsyF68hcYYNLPj5i4PfQJ1kUY6nsFnZkc82",
                                     "zsfULrmbX7xbhqhAFRffVqCw9RyGv2hqNNG",
                                     "zsoemTfqjicem2QVU8cgBHquKb1o9JR5p4Z",
                                     "zt339oiGL6tTgc9Q71f5g1sFTZf6QiXrRUr"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{ 
                                     "zrKmSdqZKZjnARd5e8FfRg4v1m74X7twxGa" 
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1"
                                 }}});
    addressChangeIntervals = {
        {CBaseChainParams::Network::MAIN,50000},
        {CBaseChainParams::Network::REGTEST,100},
        {CBaseChainParams::Network::TESTNET,10000}
    };
}

/**
 * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
 * @param reward the main reward
 * @return the community reward
 */
CAmount CommunityFundAndRPFixFork::getCommunityFundReward(const CAmount& amount) const {
    return (CAmount)amount*120/1000;
}

/**
 * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
 * @param height the height
 * @param maxHeight the maximum height sometimes used in the computation of the proper address
 * @return the community fund address for this height
 */
const std::string& CommunityFundAndRPFixFork::getCommunityFundAddress(CBaseChainParams::Network network, int height, int maxHeight) const {
    const std::vector<std::string>& communityFundAddresses = this->getCommunityFundAddresses(network);
    int addressChangeInterval = addressChangeIntervals.at(network);
    // change CF addresses every addressChangeInterval in a round-robin fashion
    size_t i = ((height - getHeight(network)) / addressChangeInterval) % communityFundAddresses.size();
    return communityFundAddresses[i];
}
}
