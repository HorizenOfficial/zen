#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <chainparams.h>
#include <chainparamsbase.h>

class SideChainTestSuite: public ::testing::Test {

public:
	SideChainTestSuite() :
			sideChainManager(Sidechain::ScMgr::instance()),coinViewCache(),
			aBlock(), aTransaction(), aMutableTransaction(),theBlockHeight(0) {
	};

	~SideChainTestSuite() {
		sideChainManager.reset();
	};

	void SetUp() override {
		SelectBaseParams(CBaseChainParams::TESTNET);
		SelectParams(CBaseChainParams::TESTNET);
	};

	void TearDown() override {
		//This means that at the exit of current test suite, following test will have to setup BaseParams/Params again
		resetParams();
		resetBaseParams();
	};

protected:
	//Subjects under test
	Sidechain::ScMgr&              sideChainManager;
	Sidechain::ScCoinsViewCache    coinViewCache;

	//Helpers
	CBlock                  aBlock;
	CTransaction            aTransaction;
	CMutableTransaction     aMutableTransaction;
	int                     theBlockHeight;

	//TODO: Consider storing initial CBaseChainParam/CChainParam and reset it upon TearDown; try and handle assert
	//TODO: evaluate moving resetBaseParams to chainparamsbase.h
	void resetBaseParams() {
		//force reset of pCurrentBaseParams, very ugly way
		CBaseChainParams* nakedCurrentBaseParams = &const_cast <CBaseChainParams &>(BaseParams());
		nakedCurrentBaseParams = nullptr;
	}

	void resetParams() {
		//force reset of pCurrentParams, very ugly way
		CChainParams* nakedCurrentParams = &const_cast <CChainParams &>(Params());
		nakedCurrentParams = nullptr;
	}
};

TEST_F(SideChainTestSuite, ManagerIsSingleton) {
	//test
	Sidechain::ScMgr& rAnotherScMgrInstance = Sidechain::ScMgr::instance();

	//check
	EXPECT_TRUE(&sideChainManager == &rAnotherScMgrInstance)
			<< "ScManager Instances have different address:"
			<< &sideChainManager << " and " << &rAnotherScMgrInstance;
}

TEST_F(SideChainTestSuite, InitCanBePerformedWithZeroCacheAndWipe) {
	//prerequisites
	size_t cacheSize(0);
	bool fWipe(false);

	//test
	bool bRet = sideChainManager.initialUpdateFromDb(cacheSize, fWipe);

	//checks
	EXPECT_TRUE(bRet) << "Db initialization failed";
	//not sure yet how to double check db creation/availability
}

TEST_F(SideChainTestSuite, DoubleInitializationIsForbidden) {
	//prerequisites
	size_t cacheSize(0);
	bool fWipe(false);

	//test
	ASSERT_TRUE(sideChainManager.initialUpdateFromDb(cacheSize, fWipe))<<"Db first initialization should succeed";

	//checks
	bool bRet = sideChainManager.initialUpdateFromDb(cacheSize, fWipe);
	EXPECT_FALSE(bRet) << "Db double initialization should be forbidden";
}

TEST_F(SideChainTestSuite, EmptyTxsAreDulyProcessedButNotRegistered) {
	//Prerequisite
	theBlockHeight = 1987;

	ASSERT_TRUE(aTransaction.vsc_ccout.size() == 0)<<"Test requires no sidechain creation transactions";
	ASSERT_TRUE(aTransaction.vft_ccout.size() == 0)<<"Test requires no forward transactions";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, theBlockHeight);

	//check
	EXPECT_TRUE(res)<<"Empty tx should be processed";
	EXPECT_FALSE(coinViewCache.sidechainExists(aTransaction.GetHash()))<<"Empty transactions should not be cached";
}

TEST_F(SideChainTestSuite, NewSideChainCreationTxsAreRegisteredById) {
	//Prerequisite
	theBlockHeight = 1789;

	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1492");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	aTransaction = aMutableTransaction;
	ASSERT_TRUE(aTransaction.vsc_ccout.size() != 0)<<"Test requires a sidechain creation transaction";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, theBlockHeight);

	//check
	EXPECT_TRUE(res)<<"New sidechain creation txs should be processed";
	EXPECT_TRUE(coinViewCache.sidechainExists(aSideChainCreationTx.scId))<<"New sidechain creation txs should be cached";
}

TEST_F(SideChainTestSuite, SideChainDoubleInsertionIsRejected) {
	//Prerequisites
	theBlockHeight = 1789;

	//first,valid sideChain transaction
	CTxScCreationOut validScCreationTx;
	validScCreationTx.scId = uint256S("1492");
	validScCreationTx.withdrawalEpochLength = 1;
	aMutableTransaction.vsc_ccout.push_back(validScCreationTx);

	//second, id-duplicated, sideChain transaction
	CTxScCreationOut duplicatedScCreationTx;
	duplicatedScCreationTx.scId = uint256S("1492");
	duplicatedScCreationTx.withdrawalEpochLength = 2;

	ASSERT_TRUE(validScCreationTx.scId == duplicatedScCreationTx.scId)<<"Test requires two SC Tx with same id";
	ASSERT_TRUE(validScCreationTx.withdrawalEpochLength != duplicatedScCreationTx.withdrawalEpochLength)
		<<"Test requires two SC Tx with different withdrawalEpochLength"; //TODO: check if this is possible

	aMutableTransaction.vsc_ccout.push_back(validScCreationTx);

	aTransaction = aMutableTransaction;
	ASSERT_TRUE(aTransaction.vsc_ccout.size() != 0)<<"Test requires a sidechain creation transaction";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, theBlockHeight);

	//check
	EXPECT_FALSE(res)<<"Duplicated sidechain creation txs should be processed";
	EXPECT_TRUE(coinViewCache.sidechainExists(validScCreationTx.scId))<<"First, valid sidechain creation txs should be cached";
}

TEST_F(SideChainTestSuite, NoRollbackIsPerformedOnceInvalidTransactionIsEncountered) {
	//Prerequisites
	theBlockHeight = 1815;

	//first,valid sideChain transaction
	CTxScCreationOut aValidScCreationTx;
	aValidScCreationTx.scId = uint256S("1492");
	aValidScCreationTx.withdrawalEpochLength = 1;
	aMutableTransaction.vsc_ccout.push_back(aValidScCreationTx);

	//second, id-duplicated, sideChain transaction
	CTxScCreationOut duplicatedScCreationTx;
	duplicatedScCreationTx.scId = uint256S("1492");
	duplicatedScCreationTx.withdrawalEpochLength = 2;

	//third, valid, sideChain transaction
	CTxScCreationOut anotherValidScCreationTx;
	anotherValidScCreationTx.scId = uint256S("1912");
	anotherValidScCreationTx.withdrawalEpochLength = 2;

	ASSERT_TRUE(aValidScCreationTx.scId == duplicatedScCreationTx.scId)<<"Test requires second tx to be a duplicate";
	ASSERT_TRUE(aValidScCreationTx.scId != anotherValidScCreationTx.scId)<<"Test requires third tx to be a valid one";

	aMutableTransaction.vsc_ccout.push_back(aValidScCreationTx);

	aTransaction = aMutableTransaction;
	ASSERT_TRUE(aTransaction.vsc_ccout.size() != 0)<<"Test requires a sidechain creation transaction";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, theBlockHeight);

	//check
	EXPECT_FALSE(res)<<"Duplicated sidechain creation txs should be processed";
	EXPECT_TRUE(coinViewCache.sidechainExists(aValidScCreationTx.scId))<<"First, valid sidechain creation txs should be cached";
	EXPECT_FALSE(coinViewCache.sidechainExists(anotherValidScCreationTx.scId))<<"third, valid sidechain creation txs is currently not cached";
}

TEST_F(SideChainTestSuite, ForwardTransfersToNonExistentScAreRejected) {
	//Prerequisite
	theBlockHeight = 1789;

	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = uint256S("1492");
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);

	aTransaction = aMutableTransaction;
	ASSERT_TRUE(aTransaction.vft_ccout.size() != 0)<<"Test requires a forward transfer transaction";
	ASSERT_FALSE(coinViewCache.sidechainExists(aForwardTransferTx.scId))<<"Test requires target sidechain to be non-existent";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, theBlockHeight);

	//check
	EXPECT_FALSE(res)<<"Forward transfer to non existent side chain should be rejected";
	EXPECT_FALSE(coinViewCache.sidechainExists(aForwardTransferTx.scId));
}

TEST_F(SideChainTestSuite, ForwardTransfersToExistentScAreRegistered) {
	//Prerequisite

	//insert the sidechain
	theBlockHeight = 1789;

	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1912");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);
	aTransaction = aMutableTransaction;
	ASSERT_TRUE(aTransaction.vsc_ccout.size() != 0)<<"Test requires a sidechain creation transaction";

	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, theBlockHeight))
		<<"Test requires the sidechain to be available before forward transfer";
	aMutableTransaction.vsc_ccout.clear();

	//insert forward transfer
	aMutableTransaction.vsc_ccout.clear();
	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = uint256S(aSideChainCreationTx.scId.ToString());
	aForwardTransferTx.nValue = 1000; //Todo: find a way to check this
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);
	aTransaction = aMutableTransaction;
	ASSERT_TRUE(aTransaction.vft_ccout.size() != 0)<<"Test requires a forward transfer transaction";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, theBlockHeight);

	//check
	EXPECT_TRUE(res)<<"It should be possible to register a forward transfer to an existing sidechain";
	//Todo: find a way to check amount inserted

}


TEST_F(SideChainTestSuite, EmptyTransactionsAreApplicableToState) {
	//Prerequisite
	ASSERT_TRUE(aTransaction.vsc_ccout.size() == 0)<<"Test requires no sidechain creation transactions";
	ASSERT_TRUE(aTransaction.vft_ccout.size() == 0)<<"Test requires no forward transactions";

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_TRUE(res)<<"Empty transaction should be applicable to state";
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//TODO: Check whether these are really useful
TEST_F(SideChainTestSuite, TransactionWithSideChainCreationOnlyIsDeemedNull) { //TODO: to verify with AlSala, AlGar
	//Prerequisites
	CTxScCreationOut aSideChainCreationTx;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	aTransaction = aMutableTransaction;
	ASSERT_TRUE(aTransaction.vsc_ccout.size() != 0)<<"Test requires a sidechain creation transaction";

	//test
	bool res = aTransaction.IsNull();

	//check
	EXPECT_TRUE(res)<<"Transactions are deemed null if they contains sidechain creation tx only";
}

TEST_F(SideChainTestSuite, TransactionWithForwardTransferOnlyIsDeemedNull) { //TODO: to verify with AlSala, AlGar
	//Prerequisites
	CTxForwardTransferOut aForwardTransferTx;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);

	aTransaction = aMutableTransaction;
	ASSERT_TRUE(aTransaction.vft_ccout.size() != 0)<<"Test requires a forward transfer transaction";

	//test
	bool res = aTransaction.IsNull();

	//check
	EXPECT_TRUE(res)<<"Transactions are deemed null if they contains forward transfer tx only";
}
