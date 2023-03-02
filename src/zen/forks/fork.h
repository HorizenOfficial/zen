// Copyright (c) 2017 The Zen Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FORK_H
#define FORK_H

#include "chainparamsbase.h"
#include "script/standard.h"
#include "amount.h"
#include <map>
#include <vector>
#include "zen/replayprotectionlevel.h"

namespace zen {

/**
 * @brief The Fork class is the base abstract class for all forks
 * This base class only supports registration and storage of fork definition parameters
 * Actual forks should derive this fork an implement the abstract methods.
 * Note that it will make sense for most forks to derive the previous fork as to inherit
 * their functionality but it is not a requirement.
 */
class Fork
{
public:
    enum CommunityFundType {
        FOUNDATION,
        SECURENODE,
        SUPERNODE,
        ENDTYPE
    };

    /**
     * @brief ~Fork public destructor
     */
    virtual ~Fork();
    
    /**
     * @brief getHeight returns the start height of this fork based on the network
     */
    int getHeight(CBaseChainParams::Network network) const;

    /**
     * @brief getMinimumTime returns the minimum time at which a block of a given height can be processed.
     */
    int getMinimumTime(CBaseChainParams::Network network) const;

    /**
     * @brief getCommunityFundAddresses returns the community fund addresses for this fork based on the network
     */
    const std::vector<std::string>& getCommunityFundAddresses(CBaseChainParams::Network network, CommunityFundType cfType) const;

    // abstract methods
    /**
     * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
     */
    virtual const std::string& getCommunityFundAddress(CBaseChainParams::Network network, int height, int maxHeight, CommunityFundType cfType) const=0;

    /**
     * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
     */
    virtual CAmount getCommunityFundReward(const CAmount& amount, CommunityFundType cfType) const = 0;

    /**
     * @brief canSendCommunityFundsToTransparentAddress true if community funds can be sent to a transparent address
     */
    virtual bool canSendCommunityFundsToTransparentAddress() const=0;

    /**
     * @brief getReplayProtectionLevel returns the replay protection level provided by the current fork
     */
    virtual ReplayProtectionLevel getReplayProtectionLevel() const=0;
    
    /**
     * @brief isAfterChainsplit returns true if this height is after the original chain split, false otherwise
     */
    virtual bool isAfterChainsplit() const=0;
    
    /**
     * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
     * @param transactionType transaction type
     * @return true if allowed, false otherwise
     */
    virtual bool isTransactionTypeAllowed(txnouttype transactionType) const=0;

    /**
	 * @brief returns phpgr,groth,... tx version based on block height
	 */
    virtual int getShieldedTxVersion() const = 0;

    /**
	 * @brief returns sidechain tx version based on block height
	 */
	virtual int getSidechainTxVersion() const = 0;

    /**
	 * @brief returns sidechain cert version based on block height
	 */
	virtual int getCertificateVersion() const = 0;

    /**
	 * @brief returns true sidechains are supported based on block height, false otherwise
	 */
	virtual bool areSidechainsSupported() const = 0;

    /**
	 * @brief returns new block version based on block height
	 */
	virtual int getNewBlockVersion() const = 0;

    /**
     * @brief returns true if the nVersion is valid at input block height
     */
    virtual bool isValidBlockVersion(int nVersion) const = 0;

    /**
	 * @brief returns true or false if the miner has to use MAX_FUTURE_BLOCK_TIME_MTP
	 */
	virtual bool isFutureMiningTimeStampActive() const=0;

    /**
	 * @brief returns true or false if the contextualcheckblockheader uses the MAX_FUTURE_BLOCK_TIME_MTP check block time
	 */
	virtual bool isFutureTimeStampActive(int height, CBaseChainParams::Network network) const=0;

    /**
     * @brief Get the maximum allowed sidechain version for a specific block height
     */
    virtual uint8_t getMaxSidechainVersion() const = 0;

    /**
     * @brief returns true is the non ceasing sidechains exist at input block height
     */
    virtual bool isNonCeasingSidechainActive() const = 0;

    /**
     * @brief returns true if the coin base transactions must be shielded (i.e. sent to a z-address)
     */
    virtual bool mustCoinBaseBeShielded(CBaseChainParams::Network network) const = 0;

protected:
    
    /**
     * @brief setHeightMap sets the fork height per network map
     */
    void setHeightMap(const std::map<CBaseChainParams::Network,int>& heightMap);

    /**
     * @brief setCommunityFundAddressMap sets the list of community addresses per network map
     */
    void setCommunityFundAddressMap(const std::map<CBaseChainParams::Network,std::vector<std::string>>& communityFundAddressMap, CommunityFundType cfType);
    
    /**
     * @brief setMinimumTimeMap sets the minimum required system time per network map
     */
    void setMinimumTimeMap(const std::map<CBaseChainParams::Network,int>& minimumTimeMap);

private:

    // Note: there variables are not consolidated into one map with a structure because some get changed in certain forks and not others
    // and therefore they inherit the parameters from the parent fork
    std::map<CBaseChainParams::Network,int> heightMap; /**< the height at which this fork takes effect depending on network */
    std::map<CBaseChainParams::Network,int> minimumTimeMap; /**< the time at which this fork takes effects depending on network */
    std::map<CBaseChainParams::Network,std::vector<std::string>> communityFundAddressMap; /**< the community fund addresses to use depending on network */

    std::map<CBaseChainParams::Network,std::vector<std::string>> secureNodeFundAddressMap; /**< the securenode fund addresses to use depending on network */
    std::map<CBaseChainParams::Network,std::vector<std::string>> superNodeFundAddressMap; /**< the supernode fund addresses to use depending on network */
};
}
#endif // FORK_H
