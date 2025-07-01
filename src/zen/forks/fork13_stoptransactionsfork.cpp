// Copyright (c) 2024 The Horizen Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fork13_stoptransactionsfork.h"

namespace zen
{

StopTransactionsFork::StopTransactionsFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN, 1807300},
                  {CBaseChainParams::Network::REGTEST, 5020},       // Must be high enough so not to make some testnet script fail because the threshold is crossed
                  {CBaseChainParams::Network::TESTNET, 1700420}});
}
} // namespace zen
