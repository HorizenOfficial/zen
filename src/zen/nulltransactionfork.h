#ifndef NULLTRANSACTIONFORK_H
#define NULLTRANSACTIONFORK_H

#include "zen/communityfundandrpfixfork.h"
namespace zen {

class NullTransactionFork : public CommunityFundAndRPFixFork
{
public:
    NullTransactionFork();    
    /**
     * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
     */
    virtual CAmount getCommunityFundReward(const CAmount& amount) const;

    /**
     * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
     */
    virtual bool isTransactionTypeAllowed(txnouttype transactionType) const;
};

}
#endif // NULLTRANSACTIONFORK_H
