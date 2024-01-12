#include "fork12_shieldedpoolremovalfork.h"

namespace zen {

ShieldedPoolRemovalFork::ShieldedPoolRemovalFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,2000000},      // PLACEHOLDER
                  {CBaseChainParams::Network::REGTEST,1010},
                  {CBaseChainParams::Network::TESTNET,1404200}});
}
}