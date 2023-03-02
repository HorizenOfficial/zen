#include "fork11_shieldedpooldeprecationfork.h"

namespace zen {

ShieldedPoolDeprecationFork::ShieldedPoolDeprecationFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,2500001},            // PLACEHOLDER!
                  {CBaseChainParams::Network::REGTEST,481},             // PLACEHOLDER!
                  {CBaseChainParams::Network::TESTNET,2000001}});       // PLACEHOLDER!
}
}