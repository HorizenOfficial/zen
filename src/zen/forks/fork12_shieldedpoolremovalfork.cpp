// Copyright (c) 2024 The Horizen Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fork12_shieldedpoolremovalfork.h"

namespace zen {

ShieldedPoolRemovalFork::ShieldedPoolRemovalFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,1502800},
                  {CBaseChainParams::Network::REGTEST,1010},
                  {CBaseChainParams::Network::TESTNET,1404200}});
}
}