#ifndef REPLAYPROTECTIONFORK_H
#define REPLAYPROTECTIONFORK_H

#include "chainsplitfork.h"

namespace zen {

/**
 * @brief The ReplayProtectionFork class represents the original replay protection fork
 */
class ReplayProtectionFork : public ChainsplitFork
{
public:
    
    /**
     * @brief ReplayProtectionFork constructor
     */
    ReplayProtectionFork();

    /**
     * @brief getReplayProtectionLevel returns the replay protection level provided by the current fork
     */
    inline ReplayProtectionLevel getReplayProtectionLevel() const { return RPLEVEL_BASIC; }
};
}
#endif // REPLAYPROTECTIONFORK_H
