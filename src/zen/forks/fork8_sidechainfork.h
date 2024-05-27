// Copyright (c) 2020 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef _SIDECHAINFORK_H
#define _SIDECHAINFORK_H

#include "fork7_replayprotectionfixfork.h"
#include "primitives/block.h"

namespace zen {

class SidechainFork : public ReplayProtectionFixFork
{
public:
    SidechainFork();

    /**
	 * @brief returns sidechain tx version based on block height, if sidechains are not supported return 0
	 */
	inline virtual int getSidechainTxVersion() const { return SC_TX_VERSION; }

    /**
	 * @brief returns sidechain cert version based on block height, if sidechains are not supported return 0
	 */
	inline virtual int getCertificateVersion() const { return SC_CERT_VERSION; }

    /**
	 * @brief returns true if sidechains are supported
	 */
	inline virtual bool areSidechainsSupported() const { return true; }

    /**
	 * @brief returns new block version
	 */
	inline virtual int getNewBlockVersion() const { return BLOCK_VERSION_SC_SUPPORT; }

    /**
	 * @brief returns true if the block version is valid at this fork
	 */
    inline virtual bool isValidBlockVersion(int nVersion) const { return (nVersion == BLOCK_VERSION_SC_SUPPORT); }
};

}
#endif // _SIDECHAINFORK_H
