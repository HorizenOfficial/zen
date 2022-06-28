#include <gtest/gtest.h>

#include "tx_creation_utils.h"
#include "zen/forks/fork9_sidechainversionfork.h"

using namespace blockchain_test_utils;

class BlockchainHelperTest: public ::testing::Test
{
protected:

    int sidechainForkHeight;

    BlockchainHelperTest()
    {
        SidechainFork sidechainFork = SidechainFork();
        sidechainForkHeight = sidechainFork.getHeight(CBaseChainParams::REGTEST);
    }

    void SetUp() override
    {
        SelectParams(CBaseChainParams::REGTEST);
    }

    void TearDown() override
    {
        chainActive.SetTip(nullptr);
    }
};

/**
 * This test is intended to check that the BlockchainTestManager class behaves as expected
 * also after calling the "Reset()" function.
 * In particular, it checks that the transaction creation works when requesting to generate
 * input coins.
 */
TEST_F(BlockchainHelperTest, CoinGeneration)
{
    BlockchainTestManager& testManager = BlockchainTestManager::GetInstance();

    // Initialize the sidechain keys
    testManager.GenerateSidechainTestParameters(ProvingSystem::CoboundaryMarlin, TestCircuitType::Certificate);

    // Go to the last block before the SidechainVersionFork
    
    testManager.ExtendChainActiveToHeight(sidechainForkHeight);

    // Create a transaction with any output
    blockchain_test_utils::CTransactionCreationArguments args;
    args.fGenerateValidInput = true;
    args.nVersion = SC_TX_VERSION;
    args.vsc_ccout.push_back(testManager.CreateScCreationOut(0, ProvingSystem::CoboundaryMarlin));
    CMutableTransaction tx = testManager.CreateTransaction(args);

    // Check that the transaction is accepted to mempool
    CValidationState state;
    EXPECT_EQ(MempoolReturnValue::VALID, testManager.TestAcceptTxToMemoryPool(state, tx));

    // Reset the manager
    testManager.Reset();

    // Check that the transaction creation (with input coins generation) still works as expected
    tx = testManager.CreateTransaction(args);

    // Check that the transaction is accepted to mempool
    EXPECT_EQ(MempoolReturnValue::VALID, testManager.TestAcceptTxToMemoryPool(state, tx));
}
