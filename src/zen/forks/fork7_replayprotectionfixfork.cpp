#include "fork7_replayprotectionfixfork.h"

namespace zen {

ReplayProtectionFixFork::ReplayProtectionFixFork() {
    setHeightMap({{CBaseChainParams::Network::MAIN, 835968},
                  {CBaseChainParams::Network::REGTEST, 400},  // MOVED BACK WRT TO ZEN UPON BACKPORT TO ZENDOO
                  {CBaseChainParams::Network::TESTNET, 735700}});
}

}  // namespace zen
