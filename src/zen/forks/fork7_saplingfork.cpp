#include "fork7_saplingfork.h"

namespace zen {

SaplingFork::SaplingFork() {
	setHeightMap( { { CBaseChainParams::Network::MAIN, 665555 }, {
			CBaseChainParams::Network::REGTEST, 230 }, {
			CBaseChainParams::Network::TESTNET, 579900 } });

}

bool SaplingFork::isTransactionUpgradeActive(TransactionTypeActive txType) const {
	if (txType == TransactionTypeActive::OVERWINTER_TX) {
		return true;
	}
	if (txType == TransactionTypeActive::SAPLING_TX) {
		return true;
	}
	return false;
}

}
