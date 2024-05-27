// Copyright (c) 2017 The Zen Core developers
// Copyright (c) 2018 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "fork.h"

namespace zen {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PUBLIC MEMBERS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief ~Fork destructor
 */
Fork::~Fork() {
    heightMap.clear();
}

/**
 * @brief getMinimumTime returns the minimum time at which a block of a given height can be processed.
 * Note that this is used only for checking nodes that were before the original chainsplit and might be obsolete
 * @param network the network to check against
 * @return the minimum time at which this block can be processed
 */
int Fork::getMinimumTime(CBaseChainParams::Network network) const {
    return minimumTimeMap.at(network);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PROTECTED MEMBERS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief setHeightMap sets the fork height per network map
 * @param heightMap the height map
 */
void Fork::setHeightMap(const std::map<CBaseChainParams::Network,int>& heightMap) {
    if (heightMap.size() != CBaseChainParams::Network::MAX_NETWORK_TYPES) {
        printf("Fork attempting to set height map of the wrong size! heightMap.size()=%ld MAX_NETWORK_TYPES=%d\n",heightMap.size(),CBaseChainParams::Network::MAX_NETWORK_TYPES);
        assert(0);
    }
    this->heightMap = heightMap;
}

/**
 * @brief getHeight returns the start height of this fork based on the network
 * @param network the network
 * @return the start height of this fork
 */
int Fork::getHeight(CBaseChainParams::Network network) const {
    return heightMap.at(network);
}

/**
 * @brief setCommunityFundAddressMap sets the list of community addresses per network map
 * @param communityFundAddressMap the map to set
 */
void Fork::setCommunityFundAddressMap(const std::map<CBaseChainParams::Network,std::vector<std::string>>& communityFundAddressMap, CommunityFundType cfType) {
    if (communityFundAddressMap.size() != CBaseChainParams::Network::MAX_NETWORK_TYPES) {
        printf("Fork attempting to set communityFundAddress map of the wrong size! communityFundAddressMap.size()=%ld MAX_NETWORK_TYPES=%d\n",communityFundAddressMap.size(),CBaseChainParams::Network::MAX_NETWORK_TYPES);
        assert(0);
    }
    switch (cfType) {
    case CommunityFundType::FOUNDATION:
        this->communityFundAddressMap = communityFundAddressMap;
        break;
    case CommunityFundType::SECURENODE:
        this->secureNodeFundAddressMap = communityFundAddressMap;
        break;
    case CommunityFundType::SUPERNODE:
        this->superNodeFundAddressMap = communityFundAddressMap;
        break;
    default:
        break;
    }
}

/**
 * @brief getCommunityFundAddresses returns the community fund addresses for this fork based on the network
 * @param network the network
 * @return the community fund addresses for this fork and network
 */
const std::vector<std::string>& Fork::getCommunityFundAddresses(CBaseChainParams::Network network, CommunityFundType cfType) const {
    switch (cfType) {
    case CommunityFundType::FOUNDATION:
        return communityFundAddressMap.at(network);
        break;
    case CommunityFundType::SECURENODE:
        return secureNodeFundAddressMap.at(network);
        break;
    case CommunityFundType::SUPERNODE:
        return superNodeFundAddressMap.at(network);
        break;
    default:
        return communityFundAddressMap.at(network);
        break;
    }
}

/**
 * @brief setMinimumTimeMap sets the minimum required system time per network map
 * @param minimumTimeMap the map to set
 */
void Fork::setMinimumTimeMap(const std::map<CBaseChainParams::Network,int>& minimumTimeMap) {
    if (minimumTimeMap.size() != CBaseChainParams::Network::MAX_NETWORK_TYPES) {
        printf("Fork attempting to set splitTime map of the wrong size! minimumTimeMap.size()=%ld MAX_NETWORK_TYPES=%d\n",minimumTimeMap.size(),CBaseChainParams::Network::MAX_NETWORK_TYPES);
        assert(0);
    }
    this->minimumTimeMap = minimumTimeMap;
}

}
