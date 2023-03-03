// Copyright (c) 2017 The Zen Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FORKMANAGER_H
#define FORKMANAGER_H

#include "chainparamsbase.h"
#include "amount.h"
#include <list>
#include "zen/replayprotectionlevel.h"
#include "script/standard.h"
#include "forks/fork.h"

namespace zen {
class Fork;

/**
 * @brief The ForkManager class handles fork registration 
 * It will redirect of each function to the proper fork based on the passed in height and currently selected network.
 * This class should be the main interface for all outside components interacting with forks. They should not try to
 * access individual forks directly. There currently are a few exceptions to this for unit tests and for backward compatibility.
 */
class ForkManager
{
public:
    
    /**
     * @brief getInstance returns the ForkManager static instance.
     */
    static ForkManager& getInstance();

    /**
     * @brief Get the fork that activates later than all the other ones.
     */
    const Fork* getHighestFork() const;

    /**
     * @brief selectNetwork is called by SelectParams in chainparams.cpp to select the current network
     */
    void selectNetwork(CBaseChainParams::Network network);
    
    /**
     * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
     */
    const std::string& getCommunityFundAddress(int height, int maxHeight, Fork::CommunityFundType cfType) const;
    
    /**
     * @brief getMinimumTime returns the minimum time at which a block of a given height can be processed.
     */
    int getMinimumTime(int height) const;
    
    /**
     * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
     */
    CAmount getCommunityFundReward(int height, CAmount reward, Fork::CommunityFundType cfType) const;
    
    /**
     * @brief getReplayProtectionLevel returns the replay protection level provided by the current fork
     */
    ReplayProtectionLevel getReplayProtectionLevel(int height) const;
    
    /**
     * @brief canSendCommunityFundsToTransparentAddress true if community funds can be sent to a transparent address
     */
    bool canSendCommunityFundsToTransparentAddress(int height) const;
    
    /**
     * @brief isAfterChainsplit returns true if this height is after the original chain split, false otherwise
     */
    bool isAfterChainsplit(int height) const;

    /**
     * @brief isTransactionTypeAllowed returns true if this transaction type is allowed at this block height, false otherwise
     */
    bool isTransactionTypeAllowedAtHeight(int height, txnouttype transactionType) const;

    /**
	 * @brief returns phpgr,groth,... tx version based on block height
	 */
	int getShieldedTxVersion(int height) const;

    /**
	 * @brief returns sidechain tx version based on block height
	 */
	int getSidechainTxVersion(int height) const;

    /**
	 * @brief returns sidechain cert version based on block height
	 */
	int getCertificateVersion(int height) const;

    /**
	 * @brief returns true sidechains are supported based on block height, false otherwise
	 */
	bool areSidechainsSupported(int height) const;

    /**
	 * @brief returns new block version based on block height
	 */
	int getNewBlockVersion(int height) const;

    /**
	 * @brief returns true is the nVersion is valid at input block height
	 */
    bool isValidBlockVersion(int height, int nVersion) const;

    /**
	 * @brief
	 */
	bool isFutureMiningTimeStampActive(int height) const;

    /**
	 * @brief
	 */
	bool isFutureTimeStampActive(int height) const;

    /**
     * @brief Get the maximum allowed sidechain version for a specific block height
     */
    uint8_t getMaxSidechainVersion(int height) const;

    /**
     * @brief returns true is the non ceasing sidechains exist at input block height
     */
    bool isNonCeasingSidechainActive(int height) const;

private:
    
    /**
     * @brief ForkManager constructor
     */
    ForkManager();
    
    /**
     * @brief ~ForkManager destructor
     */
    ~ForkManager();
    
    /**
     * @brief getForkAtHeight returns the active fork at the specified height. 
     */
    const Fork* getForkAtHeight(int height) const;
    
    /**
     * @brief registerFork used internally to register a new fork
     */
    void registerFork(Fork* fork);
    
    /**
     * @brief forks stores the list of all forks sorted by ascending height
     */
    std::list<Fork*> forks;
    
    /**
     * @brief currentNetwork currently selected network
     */
    CBaseChainParams::Network currentNetwork;
};
}

#endif // FORKMANAGER_H
