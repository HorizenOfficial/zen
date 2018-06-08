#ifndef REPLAYPROTECTIONFIXFORK_H
#define REPLAYPROTECTIONFIXFORK_H

#include "fork2_replayprotectionfork.h"

namespace zen {

/**
 * @brief The CommunityFundAndRPFixFork class represents the fork that modified the community fund from 8.5% to 12% and fixed the replay protection
 */
class CommunityFundAndRPFixFork : public ReplayProtectionFork
{
public:
    
    /**
     * @brief CommunityFundAndRPFixFork constructor
     */
    CommunityFundAndRPFixFork();
    
    /**
     * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
     */
    virtual CAmount getCommunityFundReward(const CAmount& amount, CommunityFundType cfType) const;

    /**
     * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
     */
    virtual const std::string& getCommunityFundAddress(CBaseChainParams::Network network, int height, int maxHeight, CommunityFundType cfType) const;

    /**
     * @brief getReplayProtectionLevel returns the replay protection level provided by the current fork
     */
    inline virtual ReplayProtectionLevel getReplayProtectionLevel() const { return RPLEVEL_FIXED; }

    /**
     * @brief canSendCommunityFundsToTransparentAddress true if community funds can be sent to a transparent address
     */
    inline virtual bool canSendCommunityFundsToTransparentAddress() const { return true; }

    /**
     * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
     */
    virtual bool isTransactionTypeAllowed(txnouttype transactionType) const;

private:
    
    /**
     * @brief addressChangeIntervals hardcoded address change intervals introduced with this fork
     */
    std::map<CBaseChainParams::Network,int> addressChangeIntervals;
};
}

#endif // REPLAYPROTECTIONFIXFORK_H
