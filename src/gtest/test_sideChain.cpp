#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <consensus/validation.h>
#include <txmempool.h>

class SideChainTestSuite: public ::testing::Test {

public:
	SideChainTestSuite() :
			sideChainManager(Sidechain::ScMgr::instance()), coinViewCache(),
			aBlock(), aTransaction(), aMutableTransaction(), anHeight(1789),
			txState(), aFeeRate(), aMemPool(aFeeRate){};

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
	Sidechain::ScMgr& sideChainManager;
	Sidechain::ScCoinsViewCache coinViewCache;

	//Helpers
	CBlock aBlock;
	CTransaction aTransaction;
	CMutableTransaction aMutableTransaction;
	int anHeight;
	CValidationState  txState;

	CFeeRate   aFeeRate;
	CTxMemPool aMemPool;

	//TODO: Consider storing initial CBaseChainParam/CChainParam and reset it upon TearDown; try and handle assert
	//TODO: evaluate moving resetBaseParams to chainparamsbase.h
	void resetBaseParams() {
		//force reset of pCurrentBaseParams, very ugly way
		CBaseChainParams* nakedCurrentBaseParams = &const_cast<CBaseChainParams &>(BaseParams());
		nakedCurrentBaseParams = nullptr;
	}

	void resetParams() {
		//force reset of pCurrentParams, very ugly way
		CChainParams* nakedCurrentParams = &const_cast<CChainParams &>(Params());
		nakedCurrentParams = nullptr;
	}
};

TEST_F(SideChainTestSuite, Structural_ManagerIsSingleton) {
	//test
	Sidechain::ScMgr& rAnotherScMgrInstance = Sidechain::ScMgr::instance();

	//check
	EXPECT_TRUE(&sideChainManager == &rAnotherScMgrInstance)
			<< "ScManager Instances have different address:"
			<< &sideChainManager << " and " << &rAnotherScMgrInstance;
}

TEST_F(SideChainTestSuite, Structural_ManagerInitCanBePerformedWithZeroCacheAndWipe) {
	//Prerequisites
	size_t cacheSize(0);
	bool fWipe(false);

	//test
	bool bRet = sideChainManager.initialUpdateFromDb(cacheSize, fWipe);

	//checks
	EXPECT_TRUE(bRet) << "Db initialization failed";
	//not sure yet how to double check db creation/availability
}

TEST_F(SideChainTestSuite, Structural_ManagerDoubleInitializationIsForbidden) {
	//Prerequisites
	size_t cacheSize(0);
	bool fWipe(false);

	//test
	ASSERT_TRUE(sideChainManager.initialUpdateFromDb(cacheSize, fWipe))<<"Db first initialization should succeed";

	//checks
	bool bRet = sideChainManager.initialUpdateFromDb(cacheSize, fWipe);
	EXPECT_FALSE(bRet) << "Db double initialization should be forbidden";
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// UpdateScInfo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SideChainTestSuite, EmptyTxsAreProcessedButNotRegistered) {
	//Prerequisite
	ASSERT_TRUE(aTransaction.ccIsNull())<<"Test requires not Sc creation tx, nor forward transfer tx";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	//check
	EXPECT_TRUE(res) << "Empty tx should be processed"; //How to check for no side-effects (i.e. no register)
}

TEST_F(SideChainTestSuite, NewSCsAreRegisteredById) {
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1492");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	aTransaction = aMutableTransaction;

	//Prerequisite
	ASSERT_FALSE(coinViewCache.sidechainExists(aSideChainCreationTx.scId))
			<< "Test requires that sidechain is not registered";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	//check
	EXPECT_TRUE(res) << "New sidechain creation txs should be processed";
	EXPECT_TRUE(coinViewCache.sidechainExists(aSideChainCreationTx.scId))
			<< "New sidechain creation txs should be cached";
}

TEST_F(SideChainTestSuite, ScDoubleInsertionIsRejected) {
	//first,valid sideChain transaction
	CTxScCreationOut validScCreationTx;
	validScCreationTx.scId = uint256S("1492");
	validScCreationTx.withdrawalEpochLength = 1;
	aMutableTransaction.vsc_ccout.push_back(validScCreationTx);

	//second, id-duplicated, sideChain transaction
	CTxScCreationOut duplicatedScCreationTx;
	duplicatedScCreationTx.scId = uint256S("1492");
	duplicatedScCreationTx.withdrawalEpochLength = 2;
	aMutableTransaction.vsc_ccout.push_back(validScCreationTx);
	aTransaction = aMutableTransaction;

	//Prerequisites
	ASSERT_TRUE(validScCreationTx.scId == duplicatedScCreationTx.scId)<<"Test requires two SC Tx with same id";
	ASSERT_TRUE(validScCreationTx.withdrawalEpochLength != duplicatedScCreationTx.withdrawalEpochLength)
		<<"Test requires two SC Tx with different withdrawalEpochLength";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	//check
	EXPECT_FALSE(res)<< "Duplicated sidechain creation txs should be processed";
	EXPECT_TRUE(coinViewCache.sidechainExists(validScCreationTx.scId))
			<< "First, valid sidechain creation txs should be cached";
}

TEST_F(SideChainTestSuite, NoRollbackIsPerformedOnceInvalidTransactionIsEncountered) {
	//first,valid sideChain transaction
	CTxScCreationOut aValidScCreationTx;
	aValidScCreationTx.scId = uint256S("1492");
	aValidScCreationTx.withdrawalEpochLength = 1;
	aMutableTransaction.vsc_ccout.push_back(aValidScCreationTx);

	//second, id-duplicated, sideChain transaction
	CTxScCreationOut duplicatedScCreationTx;
	duplicatedScCreationTx.scId = uint256S("1492");
	duplicatedScCreationTx.withdrawalEpochLength = 2;
	aMutableTransaction.vsc_ccout.push_back(duplicatedScCreationTx);

	//third, valid, sideChain transaction
	CTxScCreationOut anotherValidScCreationTx;
	anotherValidScCreationTx.scId = uint256S("1912");
	anotherValidScCreationTx.withdrawalEpochLength = 2;
	aMutableTransaction.vsc_ccout.push_back(anotherValidScCreationTx);

	aTransaction = aMutableTransaction;

	//Prerequisites
	ASSERT_TRUE(aValidScCreationTx.scId == duplicatedScCreationTx.scId)<<"Test requires second tx to be a duplicate";
	ASSERT_TRUE(aValidScCreationTx.scId != anotherValidScCreationTx.scId)<<"Test requires third tx to be a valid one";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	//check
	EXPECT_FALSE(res)<< "Duplicated sidechain creation txs should be processed";
	EXPECT_TRUE(coinViewCache.sidechainExists(aValidScCreationTx.scId))
			<< "First, valid sidechain creation txs should be cached";
	EXPECT_FALSE(coinViewCache.sidechainExists(anotherValidScCreationTx.scId))
			<< "third, valid sidechain creation txs is currently not cached";
}

TEST_F(SideChainTestSuite, ForwardTransfersToNonExistentScAreRejected) {
	uint256 nonExistentId = uint256S("1492");

	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = nonExistentId;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);
	aTransaction = aMutableTransaction;

	//Prerequisite
	ASSERT_FALSE(coinViewCache.sidechainExists(nonExistentId))
		<<"Test requires target sidechain to be non-existent";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	//check
	EXPECT_FALSE(res)<< "Forward transfer to non existent side chain should be rejected";
	EXPECT_FALSE(coinViewCache.sidechainExists(nonExistentId));
}

TEST_F(SideChainTestSuite, ForwardTransfersToExistentSCsAreRegistered) {
	//insert the sidechain
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1912");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);
	aTransaction = aMutableTransaction;

	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight))
		<<"Test requires the sidechain to be available before forward transfer";
	aMutableTransaction.vsc_ccout.clear();

	//create forward transfer
	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = aSideChainCreationTx.scId;
	aForwardTransferTx.nValue = 1000; //Todo: find a way to check this
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);
	aTransaction = aMutableTransaction;

	//Prerequisite
	ASSERT_TRUE(coinViewCache.sidechainExists(aForwardTransferTx.scId))
	<<"Test requires Sc to exist before attempting the forward transfer tx";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	//check
	EXPECT_TRUE(res)<< "It should be possible to register a forward transfer to an existing sidechain";
	//Todo: find a way to check amount inserted
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////// IsTxApplicableToState ////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SideChainTestSuite, EmptyTxsAreApplicableToState) {
	//Prerequisite
	ASSERT_TRUE(aTransaction.ccIsNull())<<"Test requires not Sc creation tx, nor forward transfer tx";

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_TRUE(res)<<"Empty transaction should be applicable to state";
}

TEST_F(SideChainTestSuite, NewScCreationsAreApplicableToState) {
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1492");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	aTransaction = aMutableTransaction;

	//Prerequisite
	ASSERT_FALSE(coinViewCache.sidechainExists(aSideChainCreationTx.scId))
		<<"Test requires the Sc creation tx to be new";

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_TRUE(res)<<"new Sc creation txs should be applicable to state";
}

TEST_F(SideChainTestSuite, DuplicatedScCreationsAreNotApplicableToState) {
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1492");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);
	aTransaction = aMutableTransaction;

	//Prerequisite
	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight))
		<<"Test requires the Sc creation tx to be already registered";

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_FALSE(res)<<"Duplicated Sc creation txs should be applicable to state";
}

TEST_F(SideChainTestSuite, ForwardTransfersToExistingSCsAreApplicableToState) {
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1492");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);
	aTransaction = aMutableTransaction;

	//Prerequisite
	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight))
		<<"Test requires the Sc creation tx to be already registered";

	//create forward transfer
	aMutableTransaction.vsc_ccout.clear();
	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = aSideChainCreationTx.scId;
	aForwardTransferTx.nValue = 1000;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);
	aTransaction = aMutableTransaction;

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_TRUE(res)<<"Forward transaction to existent side chains should be applicable to state";
}

TEST_F(SideChainTestSuite, ForwardTrasferIsApplicableToStateIfScCreationBelongsToTheSameTx) {
	//Create both Sc creation and Forwar transfer in the same transaction
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1492");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = aSideChainCreationTx.scId;
	aForwardTransferTx.nValue = 1000;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);

	aTransaction = aMutableTransaction;

	//Prerequisite
	ASSERT_FALSE(coinViewCache.sidechainExists(aSideChainCreationTx.scId))
		<<"Test requires the Sc creation tx to be new in current transaction";

	//create forward transfer
	aTransaction = aMutableTransaction;

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_TRUE(res)<<"Sc creation and forward transfer to it may coexist in the same tx";
}

TEST_F(SideChainTestSuite, ForwardTransfersToNonExistingSCsAreNotApplicableToState) {
	uint256 nonExistentScId = uint256S("1492");

	//Prerequisite
	ASSERT_FALSE(coinViewCache.sidechainExists(nonExistentScId))
		<<"Test requires target sidechain to be non-existent";

	//create forward transfer
	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = nonExistentScId;
	aForwardTransferTx.nValue = 1000;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);
	aTransaction = aMutableTransaction;

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_FALSE(res)<<"Forward transactions to non existent side chains should not be applicable to state";
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// checkTxSemanticValidity ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SideChainTestSuite, NonSideChain_ccNull_TxAreSemanticallyValid) {
	aMutableTransaction.nVersion = TRANSPARENT_TX_VERSION;
	aTransaction = aMutableTransaction;

	//Prerequisites
	ASSERT_FALSE(aTransaction.IsScVersion())<<"Test requires non sidechain tx";
	ASSERT_TRUE(aTransaction.ccIsNull())<<"Test requires null tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//test
	bool res = sideChainManager.checkTxSemanticValidity(aTransaction, txState);

	//checks
	EXPECT_TRUE(res)<<"empty non sidechain tx should be considered semantically valid";
	EXPECT_TRUE(txState.IsValid())<<"Positive sematics checks should not alter tx validity";
}

TEST_F(SideChainTestSuite, NonSideChain_NonCcNull_TxAreNotSemanticallyValid) {
	aMutableTransaction.nVersion = TRANSPARENT_TX_VERSION;
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1492");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	aTransaction = aMutableTransaction;

	//Prerequisites
	ASSERT_FALSE(aTransaction.IsScVersion())<<"Test requires non sidechain tx";
	ASSERT_FALSE(aTransaction.ccIsNull())<<"Test requires null tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//test
	bool res = sideChainManager.checkTxSemanticValidity(aTransaction, txState);

	//checks
	EXPECT_FALSE(res)<<"non empty non sidechain tx should be considered semantically invalid";
	EXPECT_FALSE(txState.IsValid())<<"Negative sematics checks should alter tx validity";
	EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
		<<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SideChainTestSuite, SideChain_Shielded_TxAreNotCurrentlySupported) {
	aMutableTransaction.nVersion = SC_TX_VERSION;
	JSDescription  aShieldedTx; //Todo: verify naming and whether it should be filled somehow
	aMutableTransaction.vjoinsplit.push_back(aShieldedTx);

	aTransaction = aMutableTransaction;

	//Prerequisites
	ASSERT_TRUE(aTransaction.IsScVersion())<<"Test requires sidechain tx";
	ASSERT_TRUE(aTransaction.ccIsNull())<<"Test requires null tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//test
	bool res = sideChainManager.checkTxSemanticValidity(aTransaction, txState);

	//checks
	EXPECT_FALSE(res)<<"sidechain tx with shielded tx should be considered semantically invalid";
	EXPECT_FALSE(txState.IsValid())<<"Negative sematics checks should alter tx validity";
	EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
		<<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SideChainTestSuite, SideChain_ccNull_TxAreSemanticallyValid) {
	aMutableTransaction.nVersion = SC_TX_VERSION;
	aTransaction = aMutableTransaction;

	//Prerequisites
	ASSERT_TRUE(aTransaction.IsScVersion())<<"Test requires sidechain tx";
	ASSERT_TRUE(aTransaction.ccIsNull())<<"Test requires null tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//test
	bool res = sideChainManager.checkTxSemanticValidity(aTransaction, txState);

	//checks
	EXPECT_TRUE(res)<<"empty sidechain tx should be considered semantically valid";
	EXPECT_TRUE(txState.IsValid())<<"Positive semantics checks should not alter tx validity";
}

TEST_F(SideChainTestSuite, SideChainCreationsWithoutForwardTransferAreNotSemanticallyValid) {
	aMutableTransaction.nVersion = SC_TX_VERSION;

	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1492");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	aTransaction = aMutableTransaction;

	//Prerequisites
	ASSERT_TRUE(aTransaction.IsScVersion())<<"Test requires sidechain tx";
	ASSERT_FALSE(aTransaction.ccIsNull())<<"Test requires not null tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//test
	bool res = sideChainManager.checkTxSemanticValidity(aTransaction, txState);

	//checks
	EXPECT_FALSE(res)<<"sidechain creation without forward transfer should be considered semantically invalid";
	EXPECT_FALSE(txState.IsValid())<<"Negative semantics checks should alter tx validity";
	EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
		<<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SideChainTestSuite, SideChainCreationsWithForwardTransferAreSemanticallyValid) {
	aMutableTransaction.nVersion = SC_TX_VERSION;

	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1492");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = aSideChainCreationTx.scId;
	aForwardTransferTx.nValue = 1000;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);

	aTransaction = aMutableTransaction;

	//Prerequisites
	ASSERT_TRUE(aTransaction.IsScVersion())<<"Test requires sidechain tx";
	ASSERT_FALSE(aTransaction.ccIsNull())<<"Test requires non null tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//test
	bool res = sideChainManager.checkTxSemanticValidity(aTransaction, txState);

	//checks
	EXPECT_TRUE(res)<<"sidechain creation with forward transfer should be considered semantically valid";
	EXPECT_TRUE(txState.IsValid())<<"Positive semantics checks should not alter tx validity";

	//Todo: How to check rejection code? There is currently no default/valid code value
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////// IsTxAllowedInMempool /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SideChainTestSuite, EmptyTxsAreAllowedInEmptyMemPool) {
	//Prerequisites
	ASSERT_TRUE(aMemPool.size() == 0)<<"Test requires empty mempool";
	ASSERT_TRUE(aTransaction.ccIsNull())<<"Test requires not Sc creation tx, nor forward transfer tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//test
	bool res = sideChainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

	//check
	EXPECT_TRUE(res)<<"empty transactions should be allowed in empty mempool";
	EXPECT_TRUE(txState.IsValid())<<"Positive semantics checks should not alter tx validity";
}

TEST_F(SideChainTestSuite, EmptyTxsAreAllowedInNonEmptyMemPool) {
	//Todo: addUnchecked is the simplest way I have found to add a tx to mempool. Verify correctness

	CAmount txFee;
	double txPriority;

	CTxMemPoolEntry memPoolEntry(aTransaction, txFee, GetTime(), txPriority, anHeight);

	ASSERT_TRUE(aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry))
		<<"Test requires at least a tx in mempool. Could not insert it.";

	//Prerequisites
	ASSERT_TRUE(aMemPool.size() != 0)<<"Test requires non-empty mempool";
	ASSERT_TRUE(aTransaction.ccIsNull())<<"Test requires not Sc creation tx, nor forward transfer tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//test
	bool res = sideChainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

	//check
	EXPECT_TRUE(res)<<"empty transactions should be allowed in non-empty mempool";
	EXPECT_TRUE(txState.IsValid())<<"Positive semantics checks should not alter tx validity";
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
	EXPECT_TRUE(res)<< "Transactions are deemed null if they contains sidechain creation tx only";
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
	EXPECT_TRUE(res)<< "Transactions are deemed null if they contains forward transfer tx only";
}
