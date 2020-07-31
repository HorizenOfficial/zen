#include "fork7_replayprotectionfixfork.h"

namespace zen {

ReplayProtectionFixFork::ReplayProtectionFixFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,800000}, // TODO set it properly
                  {CBaseChainParams::Network::REGTEST,400},
                  {CBaseChainParams::Network::TESTNET,660000}}); // TODO set it properly

}

}
