#ifndef _UNSHIELDINGTOSCRIPTONLYFORK_H
#define _UNSHIELDINGTOSCRIPTONLYFORK_H

#include "fork11_shieldedpooldeprecationfork.h"

namespace zen {

class UnshieldingToScriptOnlyFork : public ShieldedPoolDeprecationFork
{
public:
    UnshieldingToScriptOnlyFork();

    /**
     * @brief returns true if the unshielding (z->t) transactions must be performed towards script addresses
     */
    inline virtual bool mustUnshieldToScript() const { return true; };
};

}
#endif // _UNSHIELDINGTOSCRIPTONLYFORK_H
