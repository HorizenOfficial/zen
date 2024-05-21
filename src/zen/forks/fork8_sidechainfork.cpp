// Copyright (c) 2020-2021 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fork8_sidechainfork.h"

namespace zen {

SidechainFork::SidechainFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,1047624},
                  {CBaseChainParams::Network::REGTEST,420},
                  {CBaseChainParams::Network::TESTNET,926225}});

}

}
