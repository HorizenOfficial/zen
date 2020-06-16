#include "fork7_sidechainfork.h"

namespace zen {

SidechainFork::SidechainFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,855555}, // TODO set it properly
                  {CBaseChainParams::Network::REGTEST,220},
                  {CBaseChainParams::Network::TESTNET,769900}}); // TODO set it properly

}

}
