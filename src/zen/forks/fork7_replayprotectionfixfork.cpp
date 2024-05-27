// Copyright (c) 2020 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fork7_replayprotectionfixfork.h"

namespace zen {

ReplayProtectionFixFork::ReplayProtectionFixFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,835968},
                  {CBaseChainParams::Network::REGTEST,400}, // MOVED BACK WRT TO ZEN UPON BACKPORT TO ZENDOO
                  {CBaseChainParams::Network::TESTNET,735700}});
}

}
