// Copyright (c) 2024 The Horizen Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef _STOPSCCRFWTFORK_H
#define _STOPSCCRFWTFORK_H

#include "fork12_shieldedpoolremovalfork.h"

namespace zen
{

class StopScCreationAndFwdtFork : public ShieldedPoolRemovalFork
{
public:
    StopScCreationAndFwdtFork();

    /**
     * @brief returns true if the creation of new sidechains or forward transfers to existing sidechains has been stopped
     */
    inline virtual bool isScCreationAndFwdtStopped() const { return true; };
};

} // namespace zen
#endif // _STOPSCCRFWTFORK_H