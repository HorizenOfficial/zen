// Copyright (c) 2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
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

    /**
     * @brief returns true if the shielding (t->z) transactions are forbidden
     */
    inline virtual bool isShieldingForbidden() const { return true; };
};

}
#endif // _SHIELDEDPOOLDEPRECATIONFORK_H
