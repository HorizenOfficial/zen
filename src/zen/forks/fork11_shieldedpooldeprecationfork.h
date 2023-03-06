#ifndef _SHIELDEDPOOLDEPRECATIONFORK_H
#define _SHIELDEDPOOLDEPRECATIONFORK_H

#include "fork10_nonceasingsidechainfork.h"

namespace zen {

class ShieldedPoolDeprecationFork : public NonCeasingSidechainFork
{
public:
    ShieldedPoolDeprecationFork();

    /**
     * @brief returns true if the coin base transactions must be shielded (i.e. sent to a z-address)
     */
    inline virtual bool mustCoinBaseBeShielded(CBaseChainParams::Network network) const { return false; }
};

}
#endif // _SHIELDEDPOOLDEPRECATIONFORK_H
