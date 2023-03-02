#include "fork11_shieldedpooldeprecationfork.h"

namespace zen {

ShieldedPoolDeprecationFork::ShieldedPoolDeprecationFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,2500001},            // PLACEHOLDER!
                  {CBaseChainParams::Network::REGTEST,481},             // PLACEHOLDER!
                  {CBaseChainParams::Network::TESTNET,2000001}});       // PLACEHOLDER!
}

//bool OriginalFork::mustCoinBaseBeShielded(CBaseChainParams::Network network) const {

bool ShieldedPoolDeprecationFork::mustCoinBaseBeShielded(CBaseChainParams::Network network) const {
    if (network == CBaseChainParams::Network::REGTEST)
    {
        if (mapArgs.count("-regtestprotectcoinbase"))
        {
            return true;
        }
    }

    return false;
}
}