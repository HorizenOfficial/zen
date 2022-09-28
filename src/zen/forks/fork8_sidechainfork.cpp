#include "fork8_sidechainfork.h"

namespace zen {

SidechainFork::SidechainFork() {
    setHeightMap({{CBaseChainParams::Network::MAIN, 1047624},
                  {CBaseChainParams::Network::REGTEST, 420},
                  {CBaseChainParams::Network::TESTNET, 926225}});
}

}  // namespace zen
