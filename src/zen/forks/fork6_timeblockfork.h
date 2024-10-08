// Copyright (c) 2020 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef TIMEBLOCKFORK_H
#define TIMEBLOCKFORK_H

#include "fork5_shieldfork.h"
namespace zen {

class TimeBlockFork : public ShieldFork
{
public:
	TimeBlockFork();

    /**
	 * @brief returns true or false if the miner has to use MAX_FUTURE_BLOCK_TIME_MTP
	 */
	inline virtual bool isFutureMiningTimeStampActive() const { return true; };

    /**
	 * @brief returns true or false if the contextualcheckblockheader uses the MAX_FUTURE_BLOCK_TIME_MTP check block time
	 */
	virtual bool isFutureTimeStampActive(int height, CBaseChainParams::Network network) const;

};

}
#endif // SHIELDFORK_H
