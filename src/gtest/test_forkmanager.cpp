#include <gtest/gtest.h>
#include "zen/forkmanager.h"
#include "chainparams.h"

using namespace zen;

TEST(ForkManager, TestCommunityFundReward) {
    SelectParams(CBaseChainParams::TESTNET);
    CAmount fakeReward = (CAmount)1000L;
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(100,fakeReward),0);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(70000,fakeReward),0);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(70001,fakeReward),85);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(75000,fakeReward),85);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(90000,fakeReward),120);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(120000,fakeReward),300);
}

TEST(ForkManager, TestReplayProtection) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(100),RPLEVEL_NONE);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(70000),RPLEVEL_NONE);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(75000),RPLEVEL_BASIC);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(100000),RPLEVEL_FIXED);
}

TEST(ForkManager, TestTransparentCFAddress) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_TRUE(!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(100));
    EXPECT_TRUE(!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(70000));
    EXPECT_TRUE(!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(75000));
    EXPECT_TRUE(ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(100000));
}

TEST(ForkManager, SelectNetworkAfterChainsplit) {
    SelectParams(CBaseChainParams::REGTEST);
    EXPECT_TRUE(ForkManager::getInstance().isAfterChainsplit(1));    
    SelectParams(CBaseChainParams::MAIN);
    EXPECT_TRUE(!ForkManager::getInstance().isAfterChainsplit(0));    
}

TEST(ForkManager, GetCommunityFundAddress) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(60000,1000000000),"zrH8KT8KUcpKKNBu3fjH4hA84jZBCawErqn");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(70001,1000000000),"zrH8KT8KUcpKKNBu3fjH4hA84jZBCawErqn");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(75000,1000000000),"zrH8KT8KUcpKKNBu3fjH4hA84jZBCawErqn");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(90000,1000000000),"zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1");
}

TEST(ForkManager, GetMinimumTime) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(60000),0);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(70000),0);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(70001),1494616813);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(75000),1494616813);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(90000),1494616813);
}
