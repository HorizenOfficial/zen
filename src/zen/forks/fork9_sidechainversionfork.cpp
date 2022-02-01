#include "fork9_sidechainversionfork.h"

namespace zen {

SidechainVersionFork::SidechainVersionFork()
{
    // TODO: set proper fork height values.
    setHeightMap({{CBaseChainParams::Network::MAIN,1100000},
                  {CBaseChainParams::Network::REGTEST,450},
                  {CBaseChainParams::Network::TESTNET,1000000}});

}

}