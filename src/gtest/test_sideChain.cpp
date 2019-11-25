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
	CTxScCreationOut firstScCreationTx;
	firstScCreationTx.scId = uint256S("1492");
	firstScCreationTx.withdrawalEpochLength = 1;
	aMutableTransaction.vsc_ccout.push_back(firstScCreationTx);

	//second, id-duplicated, sideChain transaction
	CTxScCreationOut duplicatedScCreationTx;
	duplicatedScCreationTx.scId = uint256S("1492");
	duplicatedScCreationTx.withdrawalEpochLength = 2;

	ASSERT_TRUE(firstScCreationTx.scId == duplicatedScCreationTx.scId)<<"Test requires two SC Tx with same id";
	ASSERT_TRUE(firstScCreationTx.withdrawalEpochLength != duplicatedScCreationTx.withdrawalEpochLength)
		<<"Test requires two SC Tx with different withdrawalEpochLength"; //TODO: check if this is possible

	aMutableTransaction.vsc_ccout.push_back(firstScCreationTx);

	aTransaction = aMutableTransaction;
	ASSERT_TRUE(aTransaction.vsc_ccout.size() != 0)<<"Test requires a sidechain creation transaction";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, theBlockHeight);

	//check
	EXPECT_FALSE(res)<<"New sidechain creation txs should be processed";
	EXPECT_TRUE(coinViewCache.sidechainExists(firstScCreationTx.scId))<<"First, valid sidechain creation txs should be cached";
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
