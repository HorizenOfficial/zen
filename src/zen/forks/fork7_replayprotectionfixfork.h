#ifndef _REPLAY_PROT_FIX_FORK_H
#define _REPLAY_PROT_FIX_FORK_H

#include "fork6_timeblockfork.h"
#include "primitives/block.h"

namespace zen {

class ReplayProtectionFixFork : public TimeBlockFork {
  public:
    ReplayProtectionFixFork();

    /**
     * @brief getReplayProtectionLevel returns the replay protection level provided by the current fork
     */
    inline virtual ReplayProtectionLevel getReplayProtectionLevel() const { return RPLEVEL_FIXED_2; }
};

}  // namespace zen
#endif  // _REPLAY_PROT_FIX_FORK_H
