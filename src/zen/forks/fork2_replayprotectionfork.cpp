#include "fork2_replayprotectionfork.h"

namespace zen {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PUBLIC MEMBERS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief ReplayProtectionFork constructor
 */
ReplayProtectionFork::ReplayProtectionFork() {
    setHeightMap({{CBaseChainParams::Network::MAIN, 117576},
                  {CBaseChainParams::Network::REGTEST, 100},
                  {CBaseChainParams::Network::TESTNET, 72650}});
}

/**
 * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
 * @param transactionType transaction type
 * @return true if allowed, false otherwise
 */
bool ReplayProtectionFork::isTransactionTypeAllowed(txnouttype transactionType) const {
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

}  // namespace zen
