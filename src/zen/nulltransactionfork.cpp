#include "nulltransactionfork.h"

namespace zen {

NullTransactionFork::NullTransactionFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,300000},
                  {CBaseChainParams::Network::REGTEST,105},
                  {CBaseChainParams::Network::TESTNET,100000}});
}



/*
* @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
* @param reward the main reward
* @return the community reward
*/
CAmount NullTransactionFork::getCommunityFundReward(const CAmount& amount) const {
   return (CAmount)amount*300/1000;
}


/**
 * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
 * @param transactionType transaction type
 * @return true if allowed, false otherwise
 */
bool NullTransactionFork::isTransactionTypeAllowed(txnouttype transactionType) const {
    if (transactionType == TX_NULL_DATA_REPLAY)
        return true;
    return CommunityFundAndRPFixFork::isTransactionTypeAllowed(transactionType);
}

}
