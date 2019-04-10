#ifndef OVERWINTERFORK_H
#define OVERWINTERFORK_H

#include "fork5_shieldfork.h"
namespace zen {

class OverWinterFork : public ShieldFork
{
public:
	OverWinterFork();
    /**
	 * @brief returns phpgr,groth,... tx version based on block height
	 */
    virtual bool isTransactionUpgradeActive(TransactionTypeActive  txType) const;


};

}
#endif // OVERWINTERFORK_H
