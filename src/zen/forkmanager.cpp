// Copyright (c) 2017 The Zen Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "forkmanager.h"
#include "forks/fork.h"
#include "forks/fork0_originalfork.h"
#include "forks/fork1_chainsplitfork.h"
#include "forks/fork2_replayprotectionfork.h"
#include "forks/fork3_communityfundandrpfixfork.h"
#include "forks/fork4_nulltransactionfork.h"

namespace zen {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PUBLIC MEMBERS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief getInstance returns the ForkManager static instance.
 * Other than for testing purposes, it should not be necessary to create any other instance of the ForkManager class
 * @return the ForkManage instance
 */
ForkManager& ForkManager::getInstance() {
    static ForkManager instance;
    return instance;
}

/**
 * @brief selectNetwork is called by SelectParams in chainparams.cpp to select the current network
 * @param network the newly selected network
 */
void ForkManager::selectNetwork(const CBaseChainParams::Network network) {
    currentNetwork = network;
}

/**
 * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
 * @param height the height
 * @param maxHeight the maximum height sometimes used in the computation of the proper address
 * @return the community fund address for this height
 */
const std::string& ForkManager::getCommunityFundAddress(int height, int maxHeight, Fork::CommunityFundType cfType) const {
    return getForkAtHeight(height)->getCommunityFundAddress(currentNetwork,height,maxHeight, cfType);
}

/**
 * @brief getMinimumTime returns the minimum time at which a block of a given height can be processed.
 * Note that this is used only for checking nodes that were before the original chainsplit and might be obsolete
 * @param height the height to check against
 * @return the minimum time at which this block can be processed
 */
int ForkManager::getMinimumTime(int height) const {
    return getForkAtHeight(height)->getMinimumTime(currentNetwork);
}

/**
 * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
 * Note that the community reward is typically substracted from the main reward after calling this function
 * @param height the height
 * @param reward the main reward
 * @return the community reward
 */
CAmount ForkManager::getCommunityFundReward(int height, CAmount reward, Fork::CommunityFundType cfType) const {
    return getForkAtHeight(height)->getCommunityFundReward(reward, cfType);
}

/**
 * @brief getReplayProtectionLevel returns the replay protection level provided by the current fork
 * This value is used in various tests to determine what the replay protection logic should be
 * @param height height used to determine current fork
 * @return the replay protection level
 */
ReplayProtectionLevel ForkManager::getReplayProtectionLevel(int height) const {
    return getForkAtHeight(height)->getReplayProtectionLevel();
}

/**
 * @brief canSendCommunityFundsToTransparentAddress true if community funds can be sent to a transparent address
 * @param height height to test against
 * @return returns true if Community funds can be sent to a transparent address at this height
 */
bool ForkManager::canSendCommunityFundsToTransparentAddress(int height) const {
    return getForkAtHeight(height)->canSendCommunityFundsToTransparentAddress();
}

/**
 * @brief isAfterChainsplit returns true if this height is after the original chain split, false otherwise
 * @param height height to test against
 * @return true if this height is after the original chain split, false otherwise
 */
bool ForkManager::isAfterChainsplit(int height) const {
    return getForkAtHeight(height)->isAfterChainsplit();
}

/**
 * @brief isTransactionTypeAllowed returns true if this transaction type is allowed at this block height, false otherwise
 * @param height the block height to test against
 * @param transactionType transaction type
 * @return true if allowed, false otherwise
 */
bool ForkManager::isTransactionTypeAllowedAtHeight(int height, txnouttype transactionType) const {
    return getForkAtHeight(height)->isTransactionTypeAllowed(transactionType);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PRIVATE MEMBERS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief ForkManager constructor
 */
ForkManager::ForkManager() {
    selectNetwork(CBaseChainParams::Network::MAIN);     // selects MAIN as the default network
    // Registers each of the forks - the order should not matter as they get sorted during registration
    registerFork(new OriginalFork());
    registerFork(new ChainsplitFork());
    registerFork(new ReplayProtectionFork());
    registerFork(new CommunityFundAndRPFixFork());
    registerFork(new NullTransactionFork());
}

/**
 * @brief ~ForkManager destructor
 */
ForkManager::~ForkManager() {
    // release all forks and empty the list
    while(!forks.empty())    {
        Fork* fork = forks.front();
        forks.remove(fork);
        delete fork;
    }
}

/**
 * @brief getForkAtHeight returns the active fork at the specified height. 
 * Important Note: Forks were previously inconsistent in their handling of the boundary block. Some forks did not 
 * include their boundary block while others did. Fork heights have been adjusted so that all boundary blocks are included:
 * - the original chainsplit at 110000 was non-inclusive and is now 110001
 * - the original replay protection at 117575 was non-inclusive and is now 117576
 * - the community fund/replay protection fix fork at 139200 was already inclusive and is still 139200
 * @param height height to test against
 * @return the fork at that height
 */
const Fork* ForkManager::getForkAtHeight(int height) const {
    
    if (forks.empty()) {
        printf("no registered forks! returning nullptr!\n");
        return nullptr;
    }

    // Iterate through all the forks until fork height is higher than block height or there are no more forks
    std::list<Fork*>::const_iterator lastIterator = forks.begin();
    for (std::list<Fork*>::const_iterator iterator = forks.begin();iterator != forks.end();iterator++) {
        if ((*iterator)->getHeight(currentNetwork) > height) {
            break;
        }
        lastIterator = iterator;
    }
    // return the last fork before that fork
    return *lastIterator;
}

/**
 * @brief registerFork used internally to register a new fork
 * @param fork fork to register
 */
void ForkManager::registerFork(Fork* fork) {
    // add fork to list
    forks.push_back(fork);
    // sort list by height in the MAIN network. We assume that forks will always keep the same relative height order regardless of the network used
    forks.sort([](Fork* fork1, Fork* fork2) { return fork1->getHeight(CBaseChainParams::Network::MAIN) < fork2->getHeight(CBaseChainParams::Network::MAIN); });
}

}
