#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <chainparamsbase.h>
#include <chainparamsbase.cpp>

class ScManagerTestSuite: public ::testing::Test {

public:
	ScManagerTestSuite() :
			rSideChainManager(Sidechain::ScMgr::instance()) {
	};

	~ScManagerTestSuite() {
		rSideChainManager.reset();
	};

	void SetUp() override {
		SelectBaseParams(CBaseChainParams::TESTNET);
		ASSERT_TRUE(pCurrentBaseParams != nullptr)<<"Chain type must be specified before actual tests start";
	};

	void TearDown() override {
		pCurrentBaseParams = nullptr; //global variable, which needs to be reset test by test
	};

protected:
	Sidechain::ScMgr& rSideChainManager;
};

TEST_F(ScManagerTestSuite, ManagerIsSingleton) {
	Sidechain::ScMgr& rAnotherScMgrInstance = Sidechain::ScMgr::instance();
	EXPECT_TRUE(&rSideChainManager == &rAnotherScMgrInstance)
			<< "ScManager Instances have different address:"
			<< &rSideChainManager << " and " << &rAnotherScMgrInstance;
}

TEST_F(ScManagerTestSuite, InitCanBePerformedWithZeroCacheAndWipe) {

	size_t cacheSize(0);
	bool fWipe(false);
	bool bRet = rSideChainManager.initialUpdateFromDb(cacheSize, fWipe);

	EXPECT_TRUE(bRet) << "Db initialization failed";
	//not sure yet how to double check db creation/availability
}

TEST_F(ScManagerTestSuite, DoubleInitializationIsForbidden) {
	size_t cacheSize(0);
	bool fWipe(false);
	ASSERT_TRUE(rSideChainManager.initialUpdateFromDb(cacheSize, fWipe))<<"Db first initialization should succeed";

	bool bRet = rSideChainManager.initialUpdateFromDb(cacheSize, fWipe);
	EXPECT_FALSE(bRet) << "Db double initialization should be forbidden";
}
