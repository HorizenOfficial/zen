#include "fork11_shieldedpooldeprecationfork.h"

namespace zen {

ShieldedPoolDeprecationFork::ShieldedPoolDeprecationFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,2500001},            // TODO: MODIFY PLACEHOLDER!
                  {CBaseChainParams::Network::REGTEST,990},             // TODO: MODIFY PLACEHOLDER!
                  {CBaseChainParams::Network::TESTNET,2000001}});       // TODO: MODIFY PLACEHOLDER!
}
}