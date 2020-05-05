#include "fork6_timeblockfork.h"

namespace zen {


#define TIMEBLOCK_ACTIVATION 576

TimeBlockFork::TimeBlockFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,900000},
                  {CBaseChainParams::Network::REGTEST,250},
                  {CBaseChainParams::Network::TESTNET,500000}});
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
