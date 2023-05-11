#include "fork10_nonceasingsidechainfork.h"

namespace zen {

NonCeasingSidechainFork::NonCeasingSidechainFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,1363115},
                  {CBaseChainParams::Network::REGTEST,480},
                  {CBaseChainParams::Network::TESTNET,1228700}});
}
}
