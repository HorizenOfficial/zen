// Copyright (c) 2017 The Zen Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPCUTILS_H
#define BITCOIN_RPCUTILS_H

#include <string>

const std::string GetDisablingErrorMessage(const std::string& forkName);
int GetShieldedPoolDeprecationForkHeight();
bool AreShieldingRPCMethodsDisabled();
std::string ShieldingRPCMethodsDisablingWarning(bool fullDeprecation);
int GetShieldedPoolRemovalForkHeight();
bool AreShieldedPoolRPCMethodsDisabled();
std::string ShieldedPoolRPCMethodsWarning(bool deprecation);

#endif // BITCOIN_RPCUTILS_H