// Copyright (c) 2024 The Horizen Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fork13_stopsccreationandfwdtfork.h"

namespace zen
{

StopScCreationAndFwdtFork::StopScCreationAndFwdtFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN, 10000000},      // TODO check this
                  {CBaseChainParams::Network::REGTEST, 5020},       // Must be high enough so not to make some testnet script fail because the threshold is crossed
                  {CBaseChainParams::Network::TESTNET, 10000000}}); // TODO check this
}
} // namespace zen