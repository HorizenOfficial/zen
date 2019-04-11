#include "fork6_overwinterfork.h"

namespace zen {

OverWinterFork::OverWinterFork() {
	setHeightMap( { { CBaseChainParams::Network::MAIN, 655555 }, {
			CBaseChainParams::Network::REGTEST, 220 }, {
			CBaseChainParams::Network::TESTNET, 569900 } });

}

bool OverWinterFork::isTransactionUpgradeActive(TransactionTypeActive txType) const {
	if (txType == TransactionTypeActive::OVERWINTER_TX) {
		return true;
	}
	if (txType == TransactionTypeActive::SAPLING_TX) {
		return false;
	}
	return false;
}

}
