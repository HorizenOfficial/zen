#include "fork12_unshieldingtoscriptonlyfork.h"

namespace zen {

UnshieldingToScriptOnlyFork::UnshieldingToScriptOnlyFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,2000000},      // PLACEHOLDER
                  {CBaseChainParams::Network::REGTEST,1010},      // PLACEHOLDER
                  {CBaseChainParams::Network::TESTNET,2000000}}); // PLACEHOLDER
}
}