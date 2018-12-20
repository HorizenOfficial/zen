#ifndef SHIELDFORK_H
#define SHIELDFORK_H

#include "fork4_nulltransactionfork.h"
namespace zen {

class ShieldFork : public NullTransactionFork
{
public:
    ShieldFork();
    /**
     * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
     */
    virtual CAmount getCommunityFundReward(const CAmount& amount, CommunityFundType cfType) const;

    /**
	 * @brief returns phpgr,groth,... tx version based on block height
	 */
	inline virtual int getShieldedTxVersion() const { return GROTH_TX_VERSION; }
};

}
#endif // SHIELDFORK_H
