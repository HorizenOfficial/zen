// Copyright (c) 2024 The Horizen Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef _SHIELDEDPOOLREMOVALFORK_H
#define _SHIELDEDPOOLREMOVALFORK_H

#include "fork11_shieldedpooldeprecationfork.h"

namespace zen {

class ShieldedPoolRemovalFork : public ShieldedPoolDeprecationFork
{
public:
    ShieldedPoolRemovalFork();

    /**
     * @brief returns true if the shielded pool has been removed (no more t->z, z->z, z->t)
     */
    inline virtual bool isShieldedPoolRemoved() const { return true; };
};

}
#endif // _SHIELDEDPOOLREMOVALFORK_H
