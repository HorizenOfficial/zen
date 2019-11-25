#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <chainparamsbase.h>

class ScManagerTestSuite: public ::testing::Test {

public:
	ScManagerTestSuite() :
			sideChainManager(Sidechain::ScMgr::instance()),coinViewCache(),
			aTransaction(), aMutableTransaction() {
	};

	~ScManagerTestSuite() {
		sideChainManager.reset();
	};

	void SetUp() override {
		SelectBaseParams(CBaseChainParams::TESTNET);
	};

	void TearDown() override {
		//This means that at the exit of current test suite, following test will have to setup BaseParams again
		resetBaseParams();
	};

protected:
	//Subjects under test
	Sidechain::ScMgr&           sideChainManager;
	Sidechain::ScCoinsViewCache coinViewCache;

	//Helpers
	CTransaction                aTransaction;
	CMutableTransaction         aMutableTransaction;

	//TODO: Consider storing initial CBaseChainParam and reset it upon TearDown; try and handle assert
	//TODO: evaluate moving resetBaseParams to chainparamsbase.h
	void resetBaseParams() {
		//force reset of pCurrentBaseParams, very ugly way
		CBaseChainParams* nakedCurrentBaseParams = &const_cast <CBaseChainParams &>(BaseParams());
		nakedCurrentBaseParams = nullptr;
	}
};

TEST_F(ScManagerTestSuite, ManagerIsSingleton) {
	Sidechain::ScMgr& rAnotherScMgrInstance = Sidechain::ScMgr::instance();
	EXPECT_TRUE(&sideChainManager == &rAnotherScMgrInstance)
			<< "ScManager Instances have different address:"
			<< &sideChainManager << " and " << &rAnotherScMgrInstance;
}

TEST_F(ScManagerTestSuite, InitCanBePerformedWithZeroCacheAndWipe) {
	size_t cacheSize(0);
	bool fWipe(false);
	bool bRet = sideChainManager.initialUpdateFromDb(cacheSize, fWipe);

	EXPECT_TRUE(bRet) << "Db initialization failed";
	//not sure yet how to double check db creation/availability
}

TEST_F(ScManagerTestSuite, DoubleInitializationIsForbidden) {
	size_t cacheSize(0);
	bool fWipe(false);
	ASSERT_TRUE(sideChainManager.initialUpdateFromDb(cacheSize, fWipe))<<"Db first initialization should succeed";

	bool bRet = sideChainManager.initialUpdateFromDb(cacheSize, fWipe);
	EXPECT_FALSE(bRet) << "Db double initialization should be forbidden";
}

//TODO: to verify with AlSala, AlGar
TEST_F(ScManagerTestSuite, TransactionWithSideChainCreationOnlyIsDeemedNull) {

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

TEST_F(ScManagerTestSuite, TransactionWithForwardTransferOnlyIsDeemedNull) {

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

TEST_F(ScManagerTestSuite, EmptyTransactionsAreApplicableToState) {
	//Prerequisite
	ASSERT_TRUE(aTransaction.vsc_ccout.size() == 0)<<"Test requires no sidechain creation transactions";
	ASSERT_TRUE(aTransaction.vft_ccout.size() == 0)<<"Test requires no forward transactions";

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_TRUE(res)<<"Empty transaction should be applicable to state";
}
