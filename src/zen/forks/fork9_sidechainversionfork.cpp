#include "fork9_sidechainversionfork.h"

namespace zen {

SidechainVersionFork::SidechainVersionFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,1100000},
                  {CBaseChainParams::Network::REGTEST,450},
                  {CBaseChainParams::Network::TESTNET,1000000}});

}

}