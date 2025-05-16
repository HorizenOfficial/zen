// Copyright (c) 2024 The Horizen Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef _STOPTRANSACTONSFORK_H
#define _STOPTRANSACTONSFORK_H

#include "fork12_shieldedpoolremovalfork.h"

namespace zen
{

class StopTransactionsFork : public ShieldedPoolRemovalFork
{
public:
    StopTransactionsFork();

    /**
     * @brief returns true if the creation of new sidechains or forward transfers to existing sidechains has been stopped
     */
    inline virtual bool areTransactionsStopped() const { return true; };
};

} // namespace zen
#endif // _STOPTRANSACTONSFORK_H