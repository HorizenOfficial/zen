// Copyright (c) 2020 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef _REPLAY_PROT_FIX_FORK_H
#define _REPLAY_PROT_FIX_FORK_H

#include "fork6_timeblockfork.h"
#include "primitives/block.h"

namespace zen {

class ReplayProtectionFixFork : public TimeBlockFork
{
public:
    ReplayProtectionFixFork();

    /**
     * @brief getReplayProtectionLevel returns the replay protection level provided by the current fork
     */
    inline virtual ReplayProtectionLevel getReplayProtectionLevel() const { return RPLEVEL_FIXED_2; }

};

}
#endif // _REPLAY_PROT_FIX_FORK_H
