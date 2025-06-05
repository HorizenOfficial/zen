// Copyright (c) 2017 The Zen Core developers
// Copyright (c) 2024 The Horizen Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/utils.h"

#include "chainparams.h"
#include "consolecolors.h"
#include "zen/forks/fork11_shieldedpooldeprecationfork.h"
#include "zen/forks/fork12_shieldedpoolremovalfork.h"

extern CChain chainActive;

// utilities for managing features and methods deprecation / removal height detection
// and warning generation
const std::string GetDisablingErrorMessage(const std::string& forkName)
{
    return "This method is disabled due to " + forkName + " hard fork.";
}

//! Method for getting shielded pool deprecation hard fork height
/*!
    \return hard fork height
*/
int GetShieldedPoolDeprecationForkHeight()
{
    CBaseChainParams::Network network = Params().NetworkIDString() == "main" ? CBaseChainParams::Network::MAIN :
                                        Params().NetworkIDString() == "test" ? CBaseChainParams::Network::TESTNET :
                                                                                CBaseChainParams::Network::REGTEST;
    return ShieldedPoolDeprecationFork{}.getHeight(network);
}

//! Method for identifying if shielding RPC methods are disbaled
/*!
    \return shielding RPC methods disabled
*/
bool AreShieldingRPCMethodsDisabled()
{
    return chainActive.Height() + 1 >= GetShieldedPoolDeprecationForkHeight();
}

//! Method for providing warning message associated to shielding RPC methods disabling
/*!
    \param fullDeprecation the flag identifying if full disabling is considered (instead of partial disabling)
    \return warning message
*/
std::string ShieldingRPCMethodsDisablingWarning(bool fullDeprecation)
{
    int shieldedPoolDeprecationForkHeight = GetShieldedPoolDeprecationForkHeight();

    return std::string(TXT_BIRED "\nWARNING: " TXT_BIBLK "This method has been " +
                        std::string(fullDeprecation ? "fully " : "partially ") +
                        "disabled at block height " + std::to_string(shieldedPoolDeprecationForkHeight - 1) +
                        " due to shielded pool deprecation hard fork." TXT_NORML);
}


//! Method for getting shielded pool removal hard fork height
/*!
    \return hard fork height
*/
int GetShieldedPoolRemovalForkHeight()
{
    CBaseChainParams::Network network = Params().NetworkIDString() == "main" ? CBaseChainParams::Network::MAIN :
                                        Params().NetworkIDString() == "test" ? CBaseChainParams::Network::TESTNET :
                                                                                CBaseChainParams::Network::REGTEST;
    return ShieldedPoolRemovalFork{}.getHeight(network);
}

//! Method for identifying if shielded pool RPC methods are disabled
/*!
    \return shielded pool RPC methods disabled
*/
bool AreShieldedPoolRPCMethodsDisabled()
{
    return chainActive.Height() + 1 >= GetShieldedPoolRemovalForkHeight();
}

//! Method for providing warning message associated to shielded pool RPC methods deprecation/disabling
/*!
    \param deprecation the flag identifying if deprecation is considered (instead of disabling)
    \return warning message
*/
std::string ShieldedPoolRPCMethodsWarning(bool deprecation)
{
    int shieldedPoolRemovalForkHeight = GetShieldedPoolRemovalForkHeight();

    return std::string(TXT_BIRED "\nWARNING: " TXT_BIBLK "This method " +
            std::string(AreShieldedPoolRPCMethodsDisabled() ? "has been " : "will be ") +
            (deprecation ? "deprecated " : "partially disabled (only t->t allowed) ") +
            "at block height " + std::to_string(shieldedPoolRemovalForkHeight - 1) + " " +
            "due to shielded pool removal hard fork." TXT_NORML);
}