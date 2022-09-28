#ifndef REPLAYPROTECTIONFORK_H
#define REPLAYPROTECTIONFORK_H

#include "fork1_chainsplitfork.h"

namespace zen {

/**
 * @brief The ReplayProtectionFork class represents the original replay protection fork
 */
class ReplayProtectionFork : public ChainsplitFork {
  public:
    /**
     * @brief ReplayProtectionFork constructor
     */
    ReplayProtectionFork();

    /**
     * @brief getReplayProtectionLevel returns the replay protection level provided by the current fork
     */
    inline ReplayProtectionLevel getReplayProtectionLevel() const { return RPLEVEL_BASIC; }

    /**
     * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
     */
    virtual bool isTransactionTypeAllowed(txnouttype transactionType) const;
};
}  // namespace zen
#endif  // REPLAYPROTECTIONFORK_H
