#include "fork12_shieldedpoolremovalfork.h"

namespace zen {

ShieldedPoolRemovalFork::ShieldedPoolRemovalFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,1502800},
                  {CBaseChainParams::Network::REGTEST,1010},
                  {CBaseChainParams::Network::TESTNET,1404200}});
}
}