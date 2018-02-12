#ifndef CHAINSPLITFORK_H
#define CHAINSPLITFORK_H

#include "originalfork.h"

namespace zen {

/**
 * @brief The ChainsplitFork class represents the original chainsplit fork
 */
class ChainsplitFork : public OriginalFork
{
public:
    
    /**
     * @brief ChainsplitFork constructor
     */
    ChainsplitFork();
    
    /**
     * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
     */
    virtual CAmount getCommunityFundReward(const CAmount& amount) const;

    /**
     * @brief isAfterChainsplit returns true if this height is after the original chain split, false otherwise
     */
    inline virtual bool isAfterChainsplit() const { return true; }
};
}

#endif // CHAINSPLITFORK_H
