#include "fork7_replayprotectionfixfork.h"

namespace zen {

ReplayProtectionFixFork::ReplayProtectionFixFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,835968},
                  {CBaseChainParams::Network::REGTEST,400},
                  {CBaseChainParams::Network::TESTNET,735700}});

}

}
