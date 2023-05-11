#include <gtest/gtest.h>
#include "zen/forkmanager.h"
#include "chainparams.h"
#include "zen/forks/fork10_nonceasingsidechainfork.h"

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
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(369899,fakeReward, Fork::CommunityFundType::FOUNDATION),100);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(369900,fakeReward, Fork::CommunityFundType::FOUNDATION),200);
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
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(455554,fakeReward, Fork::CommunityFundType::FOUNDATION),100);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(455555,fakeReward, Fork::CommunityFundType::FOUNDATION),200);
}

TEST(ForkManager, TestSecureNodeFundRewardTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    CAmount fakeReward = (CAmount)1000L;
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(260500,fakeReward, Fork::CommunityFundType::SECURENODE),100);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(369900,fakeReward, Fork::CommunityFundType::SECURENODE),100);
}

TEST(ForkManager, TestSecureNodeFundRewardMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    CAmount fakeReward = (CAmount)1000L;
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(344700,fakeReward, Fork::CommunityFundType::SECURENODE),100);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(455555,fakeReward, Fork::CommunityFundType::SECURENODE),100);
}

TEST(ForkManager, TestSuperNodeFundRewardTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    CAmount fakeReward = (CAmount)1000L;
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(260500,fakeReward, Fork::CommunityFundType::SUPERNODE),100);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(369900,fakeReward, Fork::CommunityFundType::SUPERNODE),100);
}

TEST(ForkManager, TestSuperNodeFundRewardMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    CAmount fakeReward = (CAmount)1000L;
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(344700,fakeReward, Fork::CommunityFundType::SUPERNODE),100);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundReward(455555,fakeReward, Fork::CommunityFundType::SUPERNODE),100);
}

TEST(ForkManager, TestReplayProtectionTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(100),RPLEVEL_NONE);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(72649),RPLEVEL_NONE);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(72650),RPLEVEL_BASIC);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(85499),RPLEVEL_BASIC);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(85500),RPLEVEL_FIXED_1);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(735699),RPLEVEL_FIXED_1);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(735700),RPLEVEL_FIXED_2);
}

TEST(ForkManager, TestReplayProtectionMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(100),RPLEVEL_NONE);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(117575),RPLEVEL_NONE);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(117576),RPLEVEL_BASIC);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(139199),RPLEVEL_BASIC);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(139200),RPLEVEL_FIXED_1);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(835967),RPLEVEL_FIXED_1);
    EXPECT_EQ(ForkManager::getInstance().getReplayProtectionLevel(835968),RPLEVEL_FIXED_2);
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

TEST(ForkManager, GetCommunityFundAddressRegtest) {
    SelectParams(CBaseChainParams::REGTEST);
    Fork::CommunityFundType foundation = Fork::CommunityFundType::FOUNDATION;

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(0, 840000, foundation),"");

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1 - 1, 840000, foundation),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1, 840000, foundation),"zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1 + 1, 840000, foundation),"zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD");

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(101 - 1, 840000, foundation),"zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(101, 840000, foundation),"zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(101 + 1, 840000, foundation),"zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD");

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(105 - 1, 840000, foundation),"zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(105, 840000, foundation),"zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(105 + 1, 840000, foundation),"zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD");

    // Test the new addresses introduced in the hard fork 9 (sidechain version)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(450 - 1, 0, foundation),"zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(450, 0, foundation),"zrLaR63UYCHVvo5BJHoMUTuZFPmcUu866wB");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(450 + 1, 0, foundation),"zrLaR63UYCHVvo5BJHoMUTuZFPmcUu866wB");

    // Test the highest possible block (for spotting potential regressions)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(INT_MAX, 0, foundation),"zrLaR63UYCHVvo5BJHoMUTuZFPmcUu866wB");
}

TEST(ForkManager, GetCommunityFundAddressTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(70000,840000, Fork::CommunityFundType::FOUNDATION),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(70001,840000, Fork::CommunityFundType::FOUNDATION),"zrBAG3pXCTDq14nivNK9mW8SfwMNcdmMQpb");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(85499,840000, Fork::CommunityFundType::FOUNDATION),"zrRLwpYRYky4wsvwLVrDp8fs89EBTRhNMB1");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(85500,840000, Fork::CommunityFundType::FOUNDATION),"zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(260499,840000, Fork::CommunityFundType::FOUNDATION),"zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(260500,840000, Fork::CommunityFundType::FOUNDATION),"zrFzxutppvxEdjyu4QNjogBMjtC1py9Hp1S");

    // Test the new addresses introduced in the hard fork 9 (sidechain version)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1028900 - 1, 0, Fork::CommunityFundType::FOUNDATION),"zrFzxutppvxEdjyu4QNjogBMjtC1py9Hp1S");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1028900, 0, Fork::CommunityFundType::FOUNDATION),"zrFwQjR613EuvLSufoNvUzZrfKvjSQx5a23");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1028900 + 1, 0, Fork::CommunityFundType::FOUNDATION),"zrFwQjR613EuvLSufoNvUzZrfKvjSQx5a23");

    // Test the highest possible block (for spotting potential regressions)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(INT_MAX, 0, Fork::CommunityFundType::FOUNDATION),"zrFwQjR613EuvLSufoNvUzZrfKvjSQx5a23");
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

    // Test the new addresses introduced in the hard fork 9 (sidechain version)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1127000 - 1, 0, Fork::CommunityFundType::FOUNDATION),"zszpcLB6C5B8QvfDbF2dYWXsrpac5DL9WRk");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1127000, 0, Fork::CommunityFundType::FOUNDATION),"zshX5BAgUvNgM1VoBVKZyFVVozTDjjJvRxJ");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1127000 + 1, 0, Fork::CommunityFundType::FOUNDATION),"zshX5BAgUvNgM1VoBVKZyFVVozTDjjJvRxJ");

    // Test the highest possible block (for spotting potential regressions)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(INT_MAX, 0, Fork::CommunityFundType::FOUNDATION),"zshX5BAgUvNgM1VoBVKZyFVVozTDjjJvRxJ");
}


TEST(ForkManager, GetSecureNodeFundAddressRegtest) {
    SelectParams(CBaseChainParams::REGTEST);
    Fork::CommunityFundType securenode = Fork::CommunityFundType::SECURENODE;

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(0, 840000, securenode),"");

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1 - 1, 840000, securenode),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1, 840000, securenode),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1 + 1, 840000, securenode),"");

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(101 - 1, 840000, securenode),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(101, 840000, securenode),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(101 + 1, 840000, securenode),"");

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(105 - 1, 840000, securenode),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(105, 840000, securenode),"zrQG6x9j33DLbCfzAqu3qKMe7z1VDL1z2L7");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(105 + 1, 840000, securenode),"zrQG6x9j33DLbCfzAqu3qKMe7z1VDL1z2L7");

    // Test the new addresses introduced in the hard fork 9 (sidechain version)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(450 - 1, 0, securenode),"zrQG6x9j33DLbCfzAqu3qKMe7z1VDL1z2L7");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(450, 0, securenode),"zrPaU1KWpNrg5fcLsSk17z7cc71FvnVnXxi");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(450 + 1, 0, securenode),"zrPaU1KWpNrg5fcLsSk17z7cc71FvnVnXxi");

    // Test the highest possible block (for spotting potential regressions)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(INT_MAX, 0, securenode),"zrPaU1KWpNrg5fcLsSk17z7cc71FvnVnXxi");
}

TEST(ForkManager, GetSecureNodeFundAddressTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(260500,840000, Fork::CommunityFundType::SECURENODE),"zrS7QUB2eDbbKvyP43VJys3t7RpojW8GdxH");

    // Test the new addresses introduced in the hard fork 9 (sidechain version)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1028900 - 1, 0, Fork::CommunityFundType::SECURENODE),"zrS7QUB2eDbbKvyP43VJys3t7RpojW8GdxH");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1028900, 0, Fork::CommunityFundType::SECURENODE),"zrQM7AZ1qpm9TPzLc2YinGhWePt7vaHz4Rg");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1028900 + 1, 0, Fork::CommunityFundType::SECURENODE),"zrQM7AZ1qpm9TPzLc2YinGhWePt7vaHz4Rg");

    // Test the highest possible block (for spotting potential regressions)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(INT_MAX, 0, Fork::CommunityFundType::SECURENODE),"zrQM7AZ1qpm9TPzLc2YinGhWePt7vaHz4Rg");
}

TEST(ForkManager, GetSecureNodeFundAddressMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(344700,840000, Fork::CommunityFundType::SECURENODE),"zsxWnyDbU8pk2Vp98Uvkx5Nh33RFzqnCpWN");

    // Test the new addresses introduced in the hard fork 9 (sidechain version)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1127000 - 1, 0, Fork::CommunityFundType::SECURENODE),"zsxWnyDbU8pk2Vp98Uvkx5Nh33RFzqnCpWN");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1127000, 0, Fork::CommunityFundType::SECURENODE),"zsx68qSKMNoc1ZPQpGwNFZXVzgf27KN6a9u");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1127000 + 1, 0, Fork::CommunityFundType::SECURENODE),"zsx68qSKMNoc1ZPQpGwNFZXVzgf27KN6a9u");

    // Test the highest possible block (for spotting potential regressions)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(INT_MAX, 0, Fork::CommunityFundType::SECURENODE),"zsx68qSKMNoc1ZPQpGwNFZXVzgf27KN6a9u");
}


TEST(ForkManager, GetSuperNodeFundAddressRegtest) {
    SelectParams(CBaseChainParams::REGTEST);
    Fork::CommunityFundType supernode = Fork::CommunityFundType::SUPERNODE;

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(0, 840000, supernode),"");

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1 - 1, 840000, supernode),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1, 840000, supernode),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1 + 1, 840000, supernode),"");

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(101 - 1, 840000, supernode),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(101, 840000, supernode),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(101 + 1, 840000, supernode),"");

    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(105 - 1, 840000, supernode),"");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(105, 840000, supernode),"zrMasbhB1yyfQ5RBUm7NPcEjGWZdRneWCEx");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(105 + 1, 840000, supernode),"zrMasbhB1yyfQ5RBUm7NPcEjGWZdRneWCEx");

    // Test the new addresses introduced in the hard fork 9 (sidechain version)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(450 - 1, 0, supernode),"zrMasbhB1yyfQ5RBUm7NPcEjGWZdRneWCEx");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(450, 0, supernode),"zrMna8FbuTyrvFikAsmQMyAfufF3WoGksFu");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(450 + 1, 0, supernode),"zrMna8FbuTyrvFikAsmQMyAfufF3WoGksFu");

    // Test the highest possible block (for spotting potential regressions)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(INT_MAX, 0, supernode),"zrMna8FbuTyrvFikAsmQMyAfufF3WoGksFu");
}

TEST(ForkManager, GetSuperNodeFundAddressTestnet) {
    SelectParams(CBaseChainParams::TESTNET);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(260500,840000, Fork::CommunityFundType::SUPERNODE),"zrFr5HVm7woVq3oFzkMEdJdbfBchfPAPDsP");

    // Test the new addresses introduced in the hard fork 9 (sidechain version)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1028900 - 1, 0, Fork::CommunityFundType::SUPERNODE),"zrFr5HVm7woVq3oFzkMEdJdbfBchfPAPDsP");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1028900, 0, Fork::CommunityFundType::SUPERNODE),"zrSRNSqeBNEtXqn8NkAgJ9gwhLTJmXjKqoX");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1028900 + 1, 0, Fork::CommunityFundType::SUPERNODE),"zrSRNSqeBNEtXqn8NkAgJ9gwhLTJmXjKqoX");

    // Test the highest possible block (for spotting potential regressions)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(INT_MAX, 0, Fork::CommunityFundType::SUPERNODE),"zrSRNSqeBNEtXqn8NkAgJ9gwhLTJmXjKqoX");
}

TEST(ForkManager, GetSuperNodeFundAddressMainnet) {
    SelectParams(CBaseChainParams::MAIN);
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(344700,840000, Fork::CommunityFundType::SUPERNODE),"zsnL6pKdzvZ1BPVzALUoqw2KsY966XFs5CE");

    // Test the new addresses introduced in the hard fork 9 (sidechain version)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1127000 - 1, 0, Fork::CommunityFundType::SUPERNODE),"zsnL6pKdzvZ1BPVzALUoqw2KsY966XFs5CE");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1127000, 0, Fork::CommunityFundType::SUPERNODE),"zszMgcogAqz49sLHGV22YCDFSvwzwkfog4k");
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(1127000 + 1, 0, Fork::CommunityFundType::SUPERNODE),"zszMgcogAqz49sLHGV22YCDFSvwzwkfog4k");

    // Test the highest possible block (for spotting potential regressions)
    EXPECT_EQ(ForkManager::getInstance().getCommunityFundAddress(INT_MAX, 0, Fork::CommunityFundType::SUPERNODE),"zszMgcogAqz49sLHGV22YCDFSvwzwkfog4k");
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

TEST(ForkManager, FutureTimeStampMainet) {
	SelectParams(CBaseChainParams::MAIN);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(0), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(2), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(110001), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(455555), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(740599), false);
	int futureTimeStampActivation = 740600;
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(futureTimeStampActivation), true);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(futureTimeStampActivation+144), true);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(futureTimeStampActivation+576), true);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(futureTimeStampActivation+1152), true);
	EXPECT_EQ(ForkManager::getInstance().isFutureTimeStampActive(futureTimeStampActivation), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureTimeStampActive(futureTimeStampActivation+144), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureTimeStampActive(futureTimeStampActivation+576), true);
	EXPECT_EQ(ForkManager::getInstance().isFutureTimeStampActive(futureTimeStampActivation+1152), true);
}

TEST(ForkManager, FutureTimeStampTestnet) {
	SelectParams(CBaseChainParams::TESTNET);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(0), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(2), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(70001), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(369900), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(651099), false);
	int futureTimeStampActivation = 651100;
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(futureTimeStampActivation), true);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(futureTimeStampActivation+144), true);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(futureTimeStampActivation+576), true);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(futureTimeStampActivation+1152), true);
	EXPECT_EQ(ForkManager::getInstance().isFutureTimeStampActive(futureTimeStampActivation), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureTimeStampActive(futureTimeStampActivation+144), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureTimeStampActive(futureTimeStampActivation+576), true);
	EXPECT_EQ(ForkManager::getInstance().isFutureTimeStampActive(futureTimeStampActivation+1152), true);
}

TEST(ForkManager, FutureTimeStampRegtest) {
	SelectParams(CBaseChainParams::REGTEST);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(0), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(2), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(200), false);
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(209), false);
	int futureTimeStampActivation = 210;
	EXPECT_EQ(ForkManager::getInstance().isFutureMiningTimeStampActive(futureTimeStampActivation), true);
	EXPECT_EQ(ForkManager::getInstance().isFutureTimeStampActive(futureTimeStampActivation), true);
}

TEST(ForkManager, SidechainForkRegtest) {
	SelectParams(CBaseChainParams::REGTEST);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(0), false);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(419), false);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(420), true);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(421), true);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(0), 0);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(419), 0);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(420), SC_TX_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(421), SC_TX_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(0), 0);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(419), 0);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(420), SC_CERT_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(421), SC_CERT_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(0), BLOCK_VERSION_ORIGINAL);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(419), BLOCK_VERSION_BEFORE_SC);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(420), BLOCK_VERSION_SC_SUPPORT);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(421), BLOCK_VERSION_SC_SUPPORT);
}

TEST(ForkManager, SidechainForkTestnet) {
	SelectParams(CBaseChainParams::TESTNET);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(0), false);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(926224), false);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(926225), true);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(926226), true);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(0), 0);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(926224), 0);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(926225), SC_TX_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(926226), SC_TX_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(0), 0);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(926224), 0);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(926225), SC_CERT_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(926226), SC_CERT_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(0), BLOCK_VERSION_ORIGINAL);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(926224), BLOCK_VERSION_BEFORE_SC);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(926225), BLOCK_VERSION_SC_SUPPORT);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(926226), BLOCK_VERSION_SC_SUPPORT);
}

TEST(ForkManager, SidechainForkMainnet) {
	SelectParams(CBaseChainParams::MAIN);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(0), false);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(1047623), false);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(1047624), true);
	EXPECT_EQ(ForkManager::getInstance().areSidechainsSupported(1047625), true);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(0), 0);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(1047623), 0);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(1047624), SC_TX_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getSidechainTxVersion(1047625), SC_TX_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(0), 0);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(1047623), 0);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(1047624), SC_CERT_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getCertificateVersion(1047625), SC_CERT_VERSION);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(0), BLOCK_VERSION_ORIGINAL);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(1047623), BLOCK_VERSION_BEFORE_SC);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(1047624), BLOCK_VERSION_SC_SUPPORT);
	EXPECT_EQ(ForkManager::getInstance().getNewBlockVersion(1047625), BLOCK_VERSION_SC_SUPPORT);
}

TEST(ForkManager, SidechainVersionForkMainnet) {
    SelectParams(CBaseChainParams::MAIN);

    int sidechainVersionForkHeight = 1127000;
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(0), 0);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(sidechainVersionForkHeight - 1), 0);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(sidechainVersionForkHeight), 1);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(sidechainVersionForkHeight + 1), 1);
}

TEST(ForkManager, SidechainVersionForkTestnet) {
    SelectParams(CBaseChainParams::TESTNET);

    int sidechainVersionForkHeight = 1028900;
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(0), 0);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(sidechainVersionForkHeight - 1), 0);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(sidechainVersionForkHeight), 1);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(sidechainVersionForkHeight + 1), 1);
}

TEST(ForkManager, SidechainVersionForkRegtest) {
    SelectParams(CBaseChainParams::REGTEST);

    int sidechainVersionForkHeight = 450;
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(0), 0);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(sidechainVersionForkHeight - 1), 0);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(sidechainVersionForkHeight), 1);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(sidechainVersionForkHeight + 1), 1);
}

TEST(ForkManager, NonCeasingSCVersionForkMainnet) {
    SelectParams(CBaseChainParams::MAIN);

    int nonCeasingSCVersionForkHeight = 1363115;
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(nonCeasingSCVersionForkHeight - 1), 1);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(nonCeasingSCVersionForkHeight), 2);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(nonCeasingSCVersionForkHeight + 1), 2);
}

TEST(ForkManager, NonCeasingSCVersionForkTestnet) {
    SelectParams(CBaseChainParams::TESTNET);

    int nonCeasingSCVersionForkHeight = 1228700;
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(nonCeasingSCVersionForkHeight - 1), 1);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(nonCeasingSCVersionForkHeight), 2);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(nonCeasingSCVersionForkHeight + 1), 2);
}

TEST(ForkManager, NonCeasingSCVersionForkRegtest) {
    SelectParams(CBaseChainParams::REGTEST);

    int nonCeasingSCVersionForkHeight = 480;
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(nonCeasingSCVersionForkHeight - 1), 1);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(nonCeasingSCVersionForkHeight), 2);
    EXPECT_EQ(ForkManager::getInstance().getMaxSidechainVersion(nonCeasingSCVersionForkHeight + 1), 2);
}

TEST(ForkManager, HighestFork) {
    SelectParams(CBaseChainParams::MAIN);
    const Fork* highestFork = ForkManager::getInstance().getHighestFork();
    EXPECT_EQ(typeid(*highestFork), typeid(NonCeasingSidechainFork));
}
