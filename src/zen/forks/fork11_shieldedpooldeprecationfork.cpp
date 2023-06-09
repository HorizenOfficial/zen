#include "fork11_shieldedpooldeprecationfork.h"

namespace zen {

ShieldedPoolDeprecationFork::ShieldedPoolDeprecationFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,2500001},            // TODO: MODIFY PLACEHOLDER!
                  {CBaseChainParams::Network::REGTEST,990},             // TODO: MODIFY PLACEHOLDER!
                  {CBaseChainParams::Network::TESTNET,1282000}});       // TODO: FOR PRIVATE TESTNET
}
}