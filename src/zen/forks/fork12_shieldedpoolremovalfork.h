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
