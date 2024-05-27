// Copyright (c) 2020 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fork6_timeblockfork.h"

namespace zen {


#define TIMEBLOCK_ACTIVATION 576

TimeBlockFork::TimeBlockFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,740600},
                  {CBaseChainParams::Network::REGTEST,210},
                  {CBaseChainParams::Network::TESTNET,651100}});
}


/**
 * @brief returns true or false if the contextualcheckblockheader uses the MAX_FUTURE_BLOCK_TIME_MTP check block time
 */
bool TimeBlockFork::isFutureTimeStampActive(int height, CBaseChainParams::Network network) const {
	int activationHeight = getHeight(network);
	if (network != CBaseChainParams::Network::REGTEST) {
		activationHeight += TIMEBLOCK_ACTIVATION;
	}
	if (height >= activationHeight) {
		return true;
	}
	return false;
}


}
