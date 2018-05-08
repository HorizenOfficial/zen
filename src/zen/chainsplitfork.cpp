#include "chainsplitfork.h"

namespace zen {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PUBLIC MEMBERS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief ChainsplitFork constructor
 */
ChainsplitFork::ChainsplitFork() {
    setHeightMap({{CBaseChainParams::Network::MAIN,110001},
                  {CBaseChainParams::Network::REGTEST,1},
                  {CBaseChainParams::Network::TESTNET,70001}});
    setMinimumTimeMap({
                               {CBaseChainParams::Network::MAIN,1496187000},
                               {CBaseChainParams::Network::REGTEST,0},
                               {CBaseChainParams::Network::TESTNET,1494616813}
                           });
}

/**
 * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
 * @param reward the main reward
 * @return the community reward
 */
CAmount ChainsplitFork::getCommunityFundReward(const CAmount& amount) const {
    return (CAmount)amount*85/1000;
}

/**
 * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
 * @param transactionType transaction type
 * @return true if allowed, false otherwise
 */
bool ChainsplitFork::isTransactionTypeAllowed(txnouttype transactionType) const {
    switch (transactionType) {
    case TX_NONSTANDARD:
    case TX_PUBKEY_REPLAY:
    case TX_PUBKEYHASH_REPLAY:
    case TX_MULTISIG_REPLAY:
        return true;
    default:
        return false;
    }
}

}
