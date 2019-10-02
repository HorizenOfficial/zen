#ifndef _SIDECHAINFORK_H
#define _SIDECHAINFORK_H

#include "fork5_shieldfork.h"
namespace zen {

class SidechainFork : public ShieldFork
{
public:
    SidechainFork();

    /**
	 * @brief returns sidechain tx version based on block height, if sidechains are not supported return 0
	 */
	inline virtual int getSidechainTxVersion() const { return SC_TX_VERSION; }

    /**
	 * @brief returns true sidechains are supported based on block height, false otherwise
	 */
	inline virtual bool areSidechainsSupported() const { return true; }
};

}
#endif // _SIDECHAINFORK_H
