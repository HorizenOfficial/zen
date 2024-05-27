// Copyright (c) 2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fork10_nonceasingsidechainfork.h"

namespace zen {

NonCeasingSidechainFork::NonCeasingSidechainFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,1363115},
                  {CBaseChainParams::Network::REGTEST,480},
                  {CBaseChainParams::Network::TESTNET,1228700}});
}
}
