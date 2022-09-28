#ifndef CHAINSPLITFORK_H
#define CHAINSPLITFORK_H

#include "fork0_originalfork.h"

namespace zen {

/**
 * @brief The ChainsplitFork class represents the original chainsplit fork
 */
class ChainsplitFork : public OriginalFork {
  public:
    /**
     * @brief ChainsplitFork constructor
     */
    ChainsplitFork();

    /**
     * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
     */
    virtual CAmount getCommunityFundReward(const CAmount& amount, CommunityFundType cfType) const;

    /**
     * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
     */
    virtual const std::string& getCommunityFundAddress(CBaseChainParams::Network network, int height, int maxHeight,
                                                       CommunityFundType cfType) const;

    /**
     * @brief isAfterChainsplit returns true if this height is after the original chain split, false otherwise
     */
    inline virtual bool isAfterChainsplit() const { return true; }

    /**
     * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
     */
    virtual bool isTransactionTypeAllowed(txnouttype transactionType) const;
};
}  // namespace zen

#endif  // CHAINSPLITFORK_H
