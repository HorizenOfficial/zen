#include "fork10_nonceasingsidechainfork.h"

namespace zen {

NonCeasingSidechainFork::NonCeasingSidechainFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,2500000},            // PLACEHOLDER!
                  {CBaseChainParams::Network::REGTEST,480},
                  {CBaseChainParams::Network::TESTNET,1228700}});
}
}
