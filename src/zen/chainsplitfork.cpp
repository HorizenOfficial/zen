#include "chainsplitfork.h"

namespace zen {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PUBLIC MEMBERS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief ChainsplitFork constructor
 */
ChainsplitFork::ChainsplitFork() {
    setHeightMap({{CBaseChainParams::Network::MAIN,110001},
                  {CBaseChainParams::Network::REGTEST,0},
                  {CBaseChainParams::Network::TESTNET,70001}});
    setMinimumTimeMap({
                               {CBaseChainParams::Network::MAIN,1496187000},
                               {CBaseChainParams::Network::REGTEST,0},
                               {CBaseChainParams::Network::TESTNET,1494616813}
                           });
}

/**
 * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
 * @param reward the main reward
 * @return the community reward
 */
CAmount ChainsplitFork::getCommunityFundReward(const CAmount& amount) const {
    return (CAmount)amount*85/1000;
}

}
