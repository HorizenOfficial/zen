#include "fork8_sidechainfork.h"

namespace zen {

SidechainFork::SidechainFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,999999}, // TODO set it properly
                  {CBaseChainParams::Network::REGTEST,420},
                  {CBaseChainParams::Network::TESTNET,750000}}); //MOVED UP TODO set it properly

}

}
