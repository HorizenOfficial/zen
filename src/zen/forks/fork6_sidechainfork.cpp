#include "fork6_sidechainfork.h"

namespace zen {

SidechainFork::SidechainFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,655555}, // TODO set it properly
                  {CBaseChainParams::Network::REGTEST,220},
                  {CBaseChainParams::Network::TESTNET,569900}}); // TODO set it properly

}

}
