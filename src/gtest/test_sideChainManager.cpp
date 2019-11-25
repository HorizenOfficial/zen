#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <chainparamsbase.h>

class ScManagerTestSuite: public ::testing::Test {

public:
	ScManagerTestSuite() :
			sideChainManager(Sidechain::ScMgr::instance()) {
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
	Sidechain::ScMgr&           sideChainManager;
	Sidechain::ScCoinsViewCache coinViewCache;

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
