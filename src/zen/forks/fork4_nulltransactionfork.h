#ifndef NULLTRANSACTIONFORK_H
#define NULLTRANSACTIONFORK_H

#include "fork3_communityfundandrpfixfork.h"
namespace zen {

class NullTransactionFork : public CommunityFundAndRPFixFork
{
public:
    NullTransactionFork();    
    /**
     * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
     */
    virtual CAmount getCommunityFundReward(const CAmount& amount, CommunityFundType cfType) const;

    /**
     * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
     */
    virtual const std::string& getCommunityFundAddress(CBaseChainParams::Network network, int height, int maxHeight, CommunityFundType cfType) const;

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
#endif // NULLTRANSACTIONFORK_H
