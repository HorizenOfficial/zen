#include "fork8_sidechainfork.h"

namespace zen {

SidechainFork::SidechainFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,999999}, // TODO set it properly
                  {CBaseChainParams::Network::REGTEST,220},
                  {CBaseChainParams::Network::TESTNET,660000}}); //MOVED UP TODO set it properly

}

}
