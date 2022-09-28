#include "fork5_shieldfork.h"

namespace zen {

ShieldFork::ShieldFork() {
    setHeightMap({{CBaseChainParams::Network::MAIN, 455555},
                  {CBaseChainParams::Network::REGTEST, 200},
                  {CBaseChainParams::Network::TESTNET, 369900}});
}

/*
 * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
 * @param reward the main reward
 * @return the community reward
 */
CAmount ShieldFork::getCommunityFundReward(const CAmount& amount, CommunityFundType cfType) const {
    if (cfType == CommunityFundType::FOUNDATION) {
        return (CAmount)amount * 200 / 1000;
    }
    if (cfType == CommunityFundType::SECURENODE) {
        return (CAmount)amount * 100 / 1000;
    }
    if (cfType == CommunityFundType::SUPERNODE) {
        return (CAmount)amount * 100 / 1000;
    }
    return (CAmount)0L;
}

}  // namespace zen
