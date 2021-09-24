#include "fork8_sidechainfork.h"

namespace zen {

SidechainFork::SidechainFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,1111111}, // TODO set it properly
                  {CBaseChainParams::Network::REGTEST,420},
                  {CBaseChainParams::Network::TESTNET,870115}}); // TODO set it properly

}

}
