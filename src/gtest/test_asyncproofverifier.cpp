#include <gtest/gtest.h>
#include <gtest/libzendoo_test_files.h>

#include "primitives/certificate.h"
#include "primitives/transaction.h"
#include "sc/asyncproofverifier.h"
#include "coins.h"
#include "main.h"
#include "uint256.h"

#include "tx_creation_utils.h"

using namespace blockchain_test_utils;

/**
 * @brief A test suite class to unit test the CScAsyncProofVerifier.
 * 
 */
class AsyncProofVerifierTestSuite : public ::testing::Test
{
public:
    AsyncProofVerifierTestSuite() :
        dummyNode(INVALID_SOCKET, CAddress(), "", true),
        sidechainId(uint256S("aaaa"))
    {
        dummyNode.id = 7;

        sidechain.creationBlockHeight = 100;
        sidechain.fixedParams.withdrawalEpochLength = 20;
        sidechain.lastTopQualityCertHash = uint256S("cccc");
        sidechain.lastTopQualityCertQuality = 100;
        sidechain.lastTopQualityCertReferencedEpoch = -1;
        sidechain.lastTopQualityCertBwtAmount = 50;
        sidechain.balance = CAmount(100);
        sidechain.fixedParams.wCertVk = CScVKey{ParseHex(SAMPLE_VK_NO_BWT)};
    }

    void SetUp() override
    {
        SelectParams(CBaseChainParams::REGTEST);

        // clear globals
        UnloadBlockIndex();
        mGlobalForkTips.clear();
    };

    void TearDown() override
    {
        // clear globals
        UnloadBlockIndex();
        mGlobalForkTips.clear();
    };

protected:

    static const uint kDummyAmount = 1;

    CNode dummyNode;
    CSidechain sidechain;
    uint256 sidechainId;
};

TEST_F(AsyncProofVerifierTestSuite, Hash_Test)
{
    BlockchainTestManager& blockchain = BlockchainTestManager::GetInstance();
    
    CTxCeasedSidechainWithdrawalInput input1 = blockchain.CreateCswInput(sidechainId, 1);
    CTxCeasedSidechainWithdrawalInput input2 = blockchain.CreateCswInput(sidechainId, 2);
    ASSERT_NE(input1, input2);

    CTransactionCreationArguments args;
    args.nVersion = SC_TX_VERSION;
    args.vcsw_ccin.push_back(input1);
    ASSERT_EQ(args.vcsw_ccin.size(), 1);
    ASSERT_EQ(args.vcsw_ccin.at(0).nValue, 1);

    CMutableTransaction tx1 = blockchain.CreateTransaction(args);
    ASSERT_EQ(tx1.vcsw_ccin.size(), 1);
    ASSERT_EQ(tx1.vcsw_ccin.at(0).nValue, 1);

    args.vcsw_ccin.clear();
    args.vcsw_ccin.push_back(input2);
    ASSERT_EQ(args.vcsw_ccin.size(), 1);
    ASSERT_EQ(args.vcsw_ccin.at(0).nValue, 2);

    CMutableTransaction tx2 = blockchain.CreateTransaction(args);
    ASSERT_EQ(tx1.vcsw_ccin.size(), 1);
    ASSERT_EQ(tx1.vcsw_ccin.at(0).nValue, 1);
    ASSERT_EQ(tx2.vcsw_ccin.size(), 1);
    ASSERT_EQ(tx2.vcsw_ccin.at(0).nValue, 2);

    CTransaction tx3(tx1);
    CTransaction tx4(tx2);
    ASSERT_NE(tx3, tx4);
    uint256 hash1 = tx3.GetHash();
    uint256 hash2 = tx4.GetHash();
    ASSERT_NE(hash1, hash2);
}

/**
 * @brief Test the verification of a valid certificate proof.
 */
TEST_F(AsyncProofVerifierTestSuite, Check_Valid_Certificate_Proof_Processing)
{
    BlockchainTestManager& blockchain = BlockchainTestManager::GetInstance();
    blockchain.Reset();

    // Store the test sidechain and extend the blockchain to complete at least one epoch. 
    blockchain.StoreSidechainWithCurrentHeight(sidechainId, sidechain, sidechain.creationBlockHeight + sidechain.fixedParams.withdrawalEpochLength);

    int epochNumber = 0;
    int64_t quality = 1;
    
    // Generate a valid certificate.
    CMutableScCertificate cert = blockchain.GenerateCertificate(sidechainId, epochNumber, quality);

    // Check that the async proof verifier queues are empty.
    AsyncProofVerifierStatistics stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 0);
    ASSERT_EQ(stats.failedCswCounter, 0);
    ASSERT_EQ(stats.okCertCounter, 0);
    ASSERT_EQ(stats.okCswCounter, 0);

    // Add the certificate proof to the async queue.
    CScAsyncProofVerifier::GetInstance().LoadDataForCertVerification(*blockchain.CoinsViewCache(), cert, &dummyNode);

    // Check that the async proof verifier queue is not empty anymore.
    ASSERT_EQ(blockchain.PendingAsyncCertProves(), 1);
    ASSERT_EQ(blockchain.PendingAsyncCswProves(), 0);

    uint32_t counter = 0;
    const uint32_t delay = 100;

    // Wait until the certificate proof is processed for a specific maximum time (to avoid to get stuck).
    while (blockchain.PendingAsyncCertProves() > 0 || counter < blockchain.GetAsyncProofVerifierMaxBatchVerifyDelay() * 2)
    {
        MilliSleep(delay);
        counter += delay;
    }

    // Check that the async proof verifier queue is empty again.
    ASSERT_EQ(blockchain.PendingAsyncCertProves(), 0);
    ASSERT_EQ(blockchain.PendingAsyncCswProves(), 0);

    // Check that the certificate proof has been correctly verified.
    stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 0);
    ASSERT_EQ(stats.okCertCounter, 1);
    ASSERT_EQ(stats.failedCswCounter, 0);
    ASSERT_EQ(stats.okCswCounter, 0);
}

/**
 * @brief Test the verification of an invalid certificate proof.
 */
TEST_F(AsyncProofVerifierTestSuite, Check_Invalid_Certificate_Proof_Processing)
{
    BlockchainTestManager& blockchain = BlockchainTestManager::GetInstance();
    blockchain.Reset();

    // Store the test sidechain and extend the blockchain to complete at least one epoch. 
    blockchain.StoreSidechainWithCurrentHeight(sidechainId, sidechain, sidechain.creationBlockHeight + sidechain.fixedParams.withdrawalEpochLength);

    int epochNumber = 0;
    int64_t quality = 1;
    
    // Generate a valid certificate.
    CMutableScCertificate cert = blockchain.GenerateCertificate(sidechainId, epochNumber, quality);
    
    // A "null" proof is currently the only kind of invalid proof.
    cert.scProof = CScProof();
    ASSERT_TRUE(cert.scProof.IsNull());

    // Check that the async proof verifier queues are empty.
    AsyncProofVerifierStatistics stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 0);
    ASSERT_EQ(stats.failedCswCounter, 0);
    ASSERT_EQ(stats.okCertCounter, 0);
    ASSERT_EQ(stats.okCswCounter, 0);

    // Add the certificate proof to the async queue.
    CScAsyncProofVerifier::GetInstance().LoadDataForCertVerification(*blockchain.CoinsViewCache(), cert, &dummyNode);

    // Check that the async proof verifier queue is not empty anymore.
    ASSERT_EQ(blockchain.PendingAsyncCertProves(), 1);
    ASSERT_EQ(blockchain.PendingAsyncCswProves(), 0);

    uint32_t counter = 0;
    const uint32_t delay = 100;

    // Wait until the certificate proof is processed for a specific maximum time (to avoid to get stuck).
    while (blockchain.PendingAsyncCertProves() > 0 || counter < blockchain.GetAsyncProofVerifierMaxBatchVerifyDelay() * 2)
    {
        MilliSleep(delay);
        counter += delay;
    }

    // Check that the async proof verifier queue is empty again.
    ASSERT_EQ(blockchain.PendingAsyncCertProves(), 0);
    ASSERT_EQ(blockchain.PendingAsyncCswProves(), 0);

    // Check that the certificate proof has been detected as invalid.
    stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 1);
    ASSERT_EQ(stats.okCertCounter, 0);
    ASSERT_EQ(stats.failedCswCounter, 0);
    ASSERT_EQ(stats.okCswCounter, 0);
}

/**
 * @brief Test the verification of a valid CSW proof.
 */
TEST_F(AsyncProofVerifierTestSuite, Check_Valid_CSW_Proof_Processing)
{
    BlockchainTestManager& blockchain = BlockchainTestManager::GetInstance();
    blockchain.Reset();

    // Store the test sidechain.
    blockchain.StoreSidechainWithCurrentHeight(sidechainId, sidechain, sidechain.creationBlockHeight);
    ASSERT_EQ(blockchain.CoinsViewCache()->getSidechainMap().count(sidechainId), 1);

    // Create a new CSW input with valid proof.
    CTxCeasedSidechainWithdrawalInput cswInput = blockchain.CreateCswInput(sidechainId, kDummyAmount);

    // Add the CSW input to the transaction creation arguments.
    CTransactionCreationArguments args;
    args.nVersion = SC_TX_VERSION;
    args.vcsw_ccin.push_back(cswInput);

    // Create the transaction.
    CMutableTransaction tx = blockchain.CreateTransaction(args);

    // Check that the async proof verifier queues are empty.
    AsyncProofVerifierStatistics stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 0);
    ASSERT_EQ(stats.failedCswCounter, 0);
    ASSERT_EQ(stats.okCertCounter, 0);
    ASSERT_EQ(stats.okCswCounter, 0);

    // Add the CSW proof to the async queue.
    CScAsyncProofVerifier::GetInstance().LoadDataForCswVerification(*blockchain.CoinsViewCache(), tx, &dummyNode);

    uint32_t counter = 0;
    const uint32_t delay = 100;

    // Wait until the CSW proof is processed for a specific maximum time (to avoid to get stuck).
    while (blockchain.PendingAsyncCswProves() > 0 || counter < blockchain.GetAsyncProofVerifierMaxBatchVerifyDelay() * 2)
    {
        MilliSleep(delay);
        counter += delay;
    }

    // Check that the async proof verifier queue is empty again.
    ASSERT_EQ(blockchain.PendingAsyncCertProves(), 0);
    ASSERT_EQ(blockchain.PendingAsyncCswProves(), 0);

    // Check that the CSW proof has been correctly verified.
    stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 0);
    ASSERT_EQ(stats.okCertCounter, 0);
    ASSERT_EQ(stats.failedCswCounter, 0);
    ASSERT_EQ(stats.okCswCounter, 1);
}

/**
 * @brief Test the verification of an invalid CSW proof.
 */
TEST_F(AsyncProofVerifierTestSuite, Check_Invalid_CSW_Proof_Processing)
{
    BlockchainTestManager& blockchain = BlockchainTestManager::GetInstance();
    blockchain.Reset();

    // Store the test sidechain.
    blockchain.StoreSidechainWithCurrentHeight(sidechainId, sidechain, sidechain.creationBlockHeight);
    ASSERT_EQ(blockchain.CoinsViewCache()->getSidechainMap().count(sidechainId), 1);

    // Create a new CSW input with valid proof.
    CTxCeasedSidechainWithdrawalInput cswInput = blockchain.CreateCswInput(sidechainId, kDummyAmount);

    // Make the proof invalid.
    cswInput.scProof = CScProof();

    // Add the CSW input to the transaction creation arguments.
    CTransactionCreationArguments args;
    args.nVersion = SC_TX_VERSION;
    args.vcsw_ccin.push_back(cswInput);

    // Create the transaction.
    CMutableTransaction tx = blockchain.CreateTransaction(args);

    // Check that the async proof verifier queues are empty.
    AsyncProofVerifierStatistics stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 0);
    ASSERT_EQ(stats.failedCswCounter, 0);
    ASSERT_EQ(stats.okCertCounter, 0);
    ASSERT_EQ(stats.okCswCounter, 0);

    // Add the CSW proof to the async queue.
    CScAsyncProofVerifier::GetInstance().LoadDataForCswVerification(*blockchain.CoinsViewCache(), tx, &dummyNode);

    uint32_t counter = 0;
    const uint32_t delay = 100;

    // Wait until the CSW proof is processed for a specific maximum time (to avoid to get stuck).
    while (blockchain.PendingAsyncCswProves() > 0 || counter < blockchain.GetAsyncProofVerifierMaxBatchVerifyDelay() * 2)
    {
        MilliSleep(delay);
        counter += delay;
    }

    // Check that the async proof verifier queue is empty again.
    ASSERT_EQ(blockchain.PendingAsyncCertProves(), 0);
    ASSERT_EQ(blockchain.PendingAsyncCswProves(), 0);

    // Check that the CSW proof has been detected as invalid.
    stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 0);
    ASSERT_EQ(stats.okCertCounter, 0);
    ASSERT_EQ(stats.failedCswCounter, 1);
    ASSERT_EQ(stats.okCswCounter, 0);
}

/**
 * @brief Test that a transaction containing several CSW inputs is rejected as invalid
 * if at least one CSW input proof is not verified.
 */
TEST_F(AsyncProofVerifierTestSuite, Check_Tx_With_Several_Csw_Inputs)
{
    BlockchainTestManager& blockchain = BlockchainTestManager::GetInstance();
    blockchain.Reset();

    // Store the test sidechain.
    blockchain.StoreSidechainWithCurrentHeight(sidechainId, sidechain, sidechain.creationBlockHeight);
    ASSERT_EQ(blockchain.CoinsViewCache()->getSidechainMap().count(sidechainId), 1);

    // Create a new CSW input with valid proof.
    CTxCeasedSidechainWithdrawalInput cswInput1 = blockchain.CreateCswInput(sidechainId, kDummyAmount);
    CTxCeasedSidechainWithdrawalInput cswInput2 = blockchain.CreateCswInput(sidechainId, kDummyAmount);

    // Make the first proof invalid.
    cswInput1.scProof = CScProof();

    // Add the CSW input to the transaction creation arguments.
    CTransactionCreationArguments args;
    args.nVersion = SC_TX_VERSION;
    args.vcsw_ccin.push_back(cswInput1);
    args.vcsw_ccin.push_back(cswInput2);

    // Create the transaction.
    CMutableTransaction tx = blockchain.CreateTransaction(args);

    // Check that the async proof verifier queues are empty.
    AsyncProofVerifierStatistics stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 0);
    ASSERT_EQ(stats.failedCswCounter, 0);
    ASSERT_EQ(stats.okCertCounter, 0);
    ASSERT_EQ(stats.okCswCounter, 0);

    // Add the CSW proof to the async queue.
    CScAsyncProofVerifier::GetInstance().LoadDataForCswVerification(*blockchain.CoinsViewCache(), tx, &dummyNode);

    uint32_t counter = 0;
    const uint32_t delay = 100;

    // Wait until the CSW proof is processed for a specific maximum time (to avoid to get stuck).
    while (blockchain.PendingAsyncCswProves() > 0 || counter < blockchain.GetAsyncProofVerifierMaxBatchVerifyDelay() * 2)
    {
        MilliSleep(delay);
        counter += delay;
    }

    // Check that the async proof verifier queue is empty again.
    ASSERT_EQ(blockchain.PendingAsyncCertProves(), 0);
    ASSERT_EQ(blockchain.PendingAsyncCswProves(), 0);

    // Check that the CSW proof has been detected as invalid.
    stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 0);
    ASSERT_EQ(stats.okCertCounter, 0);
    ASSERT_EQ(stats.failedCswCounter, 1);
    ASSERT_EQ(stats.okCswCounter, 0);
}

/**
 * @brief Test that in case of failure during the batch verification
 * the verifier processes the proves one by one.
 */
TEST_F(AsyncProofVerifierTestSuite, Check_One_By_One_Verification)
{
    const uint numberOfValidTransactions = 3;

    BlockchainTestManager& blockchain = BlockchainTestManager::GetInstance();
    blockchain.Reset();

    // Store the test sidechain.
    blockchain.StoreSidechainWithCurrentHeight(sidechainId, sidechain, sidechain.creationBlockHeight);
    ASSERT_EQ(blockchain.CoinsViewCache()->getSidechainMap().count(sidechainId), 1);

    std::vector<CTransaction> transactions;
    
    // Create a new CSW input with invalid proof.
    CTxCeasedSidechainWithdrawalInput cswInputInvalid = blockchain.CreateCswInput(sidechainId, kDummyAmount);
    cswInputInvalid.scProof = CScProof();

    // Add the CSW input to the transaction creation arguments.
    CTransactionCreationArguments invalidArgs;
    invalidArgs.nVersion = SC_TX_VERSION;
    invalidArgs.vcsw_ccin.push_back(cswInputInvalid);

    // Create the invalid transaction.
    transactions.push_back(CTransaction(blockchain.CreateTransaction(invalidArgs)));

    uint amount = 1;
    for (int i = 0; i < numberOfValidTransactions; i++)
    {
        // Create a new CSW input with valid proof.
        CTxCeasedSidechainWithdrawalInput cswInputValid = blockchain.CreateCswInput(sidechainId, kDummyAmount + i);

        // Add the CSW input to the transaction creation arguments.
        CTransactionCreationArguments validArgs;
        validArgs.nVersion = SC_TX_VERSION;
        validArgs.vcsw_ccin.push_back(cswInputValid);

        // Create the valid transaction.
        transactions.push_back(blockchain.CreateTransaction(validArgs));
    }

    // Check that the async proof verifier queues are empty.
    AsyncProofVerifierStatistics stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 0);
    ASSERT_EQ(stats.failedCswCounter, 0);
    ASSERT_EQ(stats.okCertCounter, 0);
    ASSERT_EQ(stats.okCswCounter, 0);

    // Add the CSW proves to the async queue.
    for (CTransaction tx : transactions)
    {
        std::string hash = tx.GetHash().ToString();
        CScAsyncProofVerifier::GetInstance().LoadDataForCswVerification(*blockchain.CoinsViewCache(), tx, &dummyNode);
    }

    // Check that the async proof verifier queue contains all the pushed transactions.
    ASSERT_EQ(blockchain.PendingAsyncCertProves(), 0);
    ASSERT_EQ(blockchain.PendingAsyncCswProves(), numberOfValidTransactions + 1);

    uint32_t counter = 0;
    const uint32_t delay = 100;

    // Wait until the CSW proof is processed for a specific maximum time (to avoid to get stuck).
    while (blockchain.PendingAsyncCswProves() > 0 || counter < blockchain.GetAsyncProofVerifierMaxBatchVerifyDelay() * 2)
    {
        MilliSleep(delay);
        counter += delay;
    }

    // Check that the async proof verifier queue is empty again.
    ASSERT_EQ(blockchain.PendingAsyncCertProves(), 0);
    ASSERT_EQ(blockchain.PendingAsyncCswProves(), 0);

    // Check that the CSW proof has been detected as invalid.
    stats = blockchain.GetAsyncProofVerifierStatistics();
    ASSERT_EQ(stats.failedCertCounter, 0);
    ASSERT_EQ(stats.okCertCounter, 0);
    ASSERT_EQ(stats.failedCswCounter, 1);
    ASSERT_EQ(stats.okCswCounter, numberOfValidTransactions);
}

/**
 * @brief Test the move of elements from one queue map to another.
 * 
 * This test is mainly intended for checking the std::move() function
 * (used by the async proof verifier).
 */
TEST_F(AsyncProofVerifierTestSuite, Csw_Queue_Move)
{
    std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>> cswEnqueuedData;

    std::map</*outputPos*/unsigned int, CCswProofVerifierInput> element;

    CTxCeasedSidechainWithdrawalInput cswInput1, cswInput2;

    CMutableTransaction cswMutTransaction;
    cswMutTransaction.vcsw_ccin.push_back(cswInput1);
    cswMutTransaction.vcsw_ccin.push_back(cswInput2);

    CTransaction cswTransaction = cswMutTransaction;

    CCswProofVerifierInput input1 = { .transactionPtr = std::make_shared<CTransaction>(cswTransaction), .cswInput = cswInput1 };
    CCswProofVerifierInput input2 = { .transactionPtr = std::make_shared<CTransaction>(cswTransaction), .cswInput = cswInput2 };
    element.insert(std::make_pair(0, input1));
    element.insert(std::make_pair(1, input2));

    cswEnqueuedData.insert(std::make_pair(uint256S("aaaa"), element));

    std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>> tempQueue;

    ASSERT_EQ(tempQueue.size(), 0);
    ASSERT_EQ(cswEnqueuedData.size(), 1);
    ASSERT_EQ(cswEnqueuedData.begin()->first, uint256S("aaaa"));
    ASSERT_EQ(cswEnqueuedData.begin()->second.size(), 2);

    tempQueue = std::move(cswEnqueuedData);

    ASSERT_EQ(cswEnqueuedData.size(), 0);
    ASSERT_EQ(tempQueue.size(), 1);
    ASSERT_EQ(tempQueue.begin()->first, uint256S("aaaa"));

    std::map</*outputPos*/unsigned int, CCswProofVerifierInput> tempElement = tempQueue.begin()->second;
    ASSERT_EQ(tempElement.size(), 2);
    
    ASSERT_NE(tempElement.at(0).transactionPtr, nullptr);
    ASSERT_EQ(tempElement.at(0).transactionPtr->GetVcswCcIn().size(), 2);
    ASSERT_EQ(tempElement.at(0).cswInput, cswInput1);
    ASSERT_NE(tempElement.at(1).transactionPtr, nullptr);
    ASSERT_EQ(tempElement.at(1).transactionPtr->GetVcswCcIn().size(), 2);
    ASSERT_EQ(tempElement.at(1).cswInput, cswInput2);
}
