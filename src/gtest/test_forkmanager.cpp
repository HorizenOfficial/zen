#include <gtest/gtest.h>
#include "zen/forkmanager.h"
#include "chainparams.h"

using namespace zen;
TEST(ForkManager, TestCommunityFundRewardTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    CAmount fakeReward = (CAmount)1000L;
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(100,fakeReward, Fork::CommunityFundType::FOUNDATION),0);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(70000,fakeReward, Fork::CommunityFundType::FOUNDATION),0);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(70001,fakeReward, Fork::CommunityFundType::FOUNDATION),85);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(85499,fakeReward, Fork::CommunityFundType::FOUNDATION),85);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(85500,fakeReward, Fork::CommunityFundType::FOUNDATION),120);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(260499,fakeReward, Fork::CommunityFundType::FOUNDATION),120);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(260500,fakeReward, Fork::CommunityFundType::FOUNDATION),100);
}

TEST(ForkManager, TestCommunityFundRewardMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    CAmount fakeReward = (CAmount)1000L;
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(100,fakeReward, Fork::CommunityFundType::FOUNDATION),0);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(110000,fakeReward, Fork::CommunityFundType::FOUNDATION),0);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(110001,fakeReward, Fork::CommunityFundType::FOUNDATION),85);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(139199,fakeReward, Fork::CommunityFundType::FOUNDATION),85);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(139200,fakeReward, Fork::CommunityFundType::FOUNDATION),120);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(344699,fakeReward, Fork::CommunityFundType::FOUNDATION),120);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(344700,fakeReward, Fork::CommunityFundType::FOUNDATION),100);
}

TEST(ForkManager, TestSecureNodeFundRewardTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    CAmount fakeReward = (CAmount)1000L;
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(260500,fakeReward, Fork::CommunityFundType::SECURENODE),100);
}

TEST(ForkManager, TestSecureNodeFundRewardMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    CAmount fakeReward = (CAmount)1000L;
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(344700,fakeReward, Fork::CommunityFundType::SECURENODE),100);
}

TEST(ForkManager, TestSuperNodeFundRewardTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    CAmount fakeReward = (CAmount)1000L;
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(260500,fakeReward, Fork::CommunityFundType::SUPERNODE),100);
}

TEST(ForkManager, TestSuperNodeFundRewardMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    CAmount fakeReward = (CAmount)1000L;
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(344700,fakeReward, Fork::CommunityFundType::SUPERNODE),100);
}

TEST(ForkManager, TestReplayProtectionTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(100),RPLEVEL_NONE);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(72649),RPLEVEL_NONE);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(72650),RPLEVEL_BASIC);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(85499),RPLEVEL_BASIC);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(85500),RPLEVEL_FIXED);
}

TEST(ForkManager, TestReplayProtectionMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(100),RPLEVEL_NONE);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(117575),RPLEVEL_NONE);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(117576),RPLEVEL_BASIC);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(139199),RPLEVEL_BASIC);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(139200),RPLEVEL_FIXED);
}

TEST(ForkManager, TestTransparentCFAddressTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_TRUE(!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(100));
    EXPECT_TRUE(!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(70001));
    EXPECT_TRUE(!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(72650));
    EXPECT_TRUE(!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(85499));
    EXPECT_TRUE(ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(85500));
    EXPECT_TRUE(ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(260500));
}

TEST(ForkManager, TestTransparentCFAddressMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    EXPECT_TRUE(!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(100));
    EXPECT_TRUE(!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(110001));
    EXPECT_TRUE(!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(117576));
    EXPECT_TRUE(!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(139199));
    EXPECT_TRUE(ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(139200));
    EXPECT_TRUE(ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(344700));
}

TEST(ForkManager, SelectNetworkAfterChainsplit) {
    SelectParams(CBaseChainParams::REGTEST);
    EXPECT_TRUE(ForkManager::getInstance().isAfterChainsplit(1));
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_TRUE(!ForkManager::getInstance().isAfterChainsplit(0));
    EXPECT_TRUE(!ForkManager::getInstance().isAfterChainsplit(70000));
    EXPECT_TRUE(ForkManager::getInstance().isAfterChainsplit(70001));
    SelectParams(CBaseChainParams::MAIN);
    EXPECT_TRUE(!ForkManager::getInstance().isAfterChainsplit(0));
    EXPECT_TRUE(!ForkManager::getInstance().isAfterChainsplit(110000));
    EXPECT_TRUE(ForkManager::getInstance().isAfterChainsplit(110001));
}

TEST(ForkManager, GetCommunityFundAddressTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(70000,840000, Fork::CommunityFundType::FOUNDATION),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(70001,840000, Fork::CommunityFundType::FOUNDATION),"zrBAG3pXCTDq14nivNK9mW8SfwMNcdmMQpb");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(85499,840000, Fork::CommunityFundType::FOUNDATION),"zrRLwpYRYky4wsvwLVrDp8fs89EBTRhNMB1");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(85500,840000, Fork::CommunityFundType::FOUNDATION),"zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(260499,840000, Fork::CommunityFundType::FOUNDATION),"zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(260500,840000, Fork::CommunityFundType::FOUNDATION),"zrFzxutppvxEdjyu4QNjogBMjtC1py9Hp1S");
}

TEST(ForkManager, GetCommunityFundAddressMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(110000,840000, Fork::CommunityFundType::FOUNDATION),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(110001,840000, Fork::CommunityFundType::FOUNDATION),"zsmncLmwEUdVmAGPUrUnNKmPGXyej7mbmdM");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(122506,840000, Fork::CommunityFundType::FOUNDATION),"zsmncLmwEUdVmAGPUrUnNKmPGXyej7mbmdM");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(122507,840000, Fork::CommunityFundType::FOUNDATION),"zsfa9VVJCEdjfPbku4XrFcRR8kTDm2T64rz");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(139199,840000, Fork::CommunityFundType::FOUNDATION),"zsfa9VVJCEdjfPbku4XrFcRR8kTDm2T64rz");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(139200,840000, Fork::CommunityFundType::FOUNDATION),"zsyF68hcYYNLPj5i4PfQJ1kUY6nsFnZkc82");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(189199,840000, Fork::CommunityFundType::FOUNDATION),"zsyF68hcYYNLPj5i4PfQJ1kUY6nsFnZkc82");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(189200,840000, Fork::CommunityFundType::FOUNDATION),"zsfULrmbX7xbhqhAFRffVqCw9RyGv2hqNNG");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(239199,840000, Fork::CommunityFundType::FOUNDATION),"zsfULrmbX7xbhqhAFRffVqCw9RyGv2hqNNG");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(239200,840000, Fork::CommunityFundType::FOUNDATION),"zsoemTfqjicem2QVU8cgBHquKb1o9JR5p4Z");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(289199,840000, Fork::CommunityFundType::FOUNDATION),"zsoemTfqjicem2QVU8cgBHquKb1o9JR5p4Z");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(289200,840000, Fork::CommunityFundType::FOUNDATION),"zt339oiGL6tTgc9Q71f5g1sFTZf6QiXrRUr");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(339199,840000, Fork::CommunityFundType::FOUNDATION),"zt339oiGL6tTgc9Q71f5g1sFTZf6QiXrRUr");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(339200,840000, Fork::CommunityFundType::FOUNDATION),"zsyF68hcYYNLPj5i4PfQJ1kUY6nsFnZkc82");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(344699,840000, Fork::CommunityFundType::FOUNDATION),"zsyF68hcYYNLPj5i4PfQJ1kUY6nsFnZkc82");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(344700,840000, Fork::CommunityFundType::FOUNDATION),"zszpcLB6C5B8QvfDbF2dYWXsrpac5DL9WRk");
}

TEST(ForkManager, GetSecureNodeFundAddressTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(260500,840000, Fork::CommunityFundType::SECURENODE),"zrS7QUB2eDbbKvyP43VJys3t7RpojW8GdxH");
}

TEST(ForkManager, GetSecureNodeFundAddressMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(344700,840000, Fork::CommunityFundType::SECURENODE),"zsxWnyDbU8pk2Vp98Uvkx5Nh33RFzqnCpWN");
}

TEST(ForkManager, GetSuperNodeFundAddressTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(260500,840000, Fork::CommunityFundType::SUPERNODE),"zrFr5HVm7woVq3oFzkMEdJdbfBchfPAPDsP");
}

TEST(ForkManager, GetSuperNodeFundAddressMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(344700,840000, Fork::CommunityFundType::SUPERNODE),"zsnL6pKdzvZ1BPVzALUoqw2KsY966XFs5CE");
}

TEST(ForkManager, GetMinimumTimeTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(60000),0);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(70000),0);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(70001),1494616813);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(260500),1494616813);
}

TEST(ForkManager, GetMinimumTimeMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(60000),0);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(110000),0);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(110001),1496187000);
    EXPECT_EQ(ForkManager::getInstance().getMinimumTime(344700),1496187000);
}
