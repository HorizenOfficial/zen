#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <consensus/validation.h>
#include <txmempool.h>
#include <undo.h>

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
		//TODO: Consider storing initial CBaseChainParam/CChainParam and reset it upon TearDown; try and handle assert
		SelectBaseParams(CBaseChainParams::REGTEST);
		SelectParams(CBaseChainParams::REGTEST);

		sideChainManager.initialUpdateFromDb(0, true, Sidechain::ScMgr::create);
	};

	void TearDown() override {
		//This means that at the exit of current test suite, following test will have to setup BaseParams/Params again
		resetParams();
		resetBaseParams();
	};

protected:
	//Subjects under test
	Sidechain::ScMgr&           sideChainManager;
	Sidechain::ScCoinsViewCache coinViewCache;

	//Helpers
	CBlock              aBlock;
	CTransaction        aTransaction;
	CMutableTransaction aMutableTransaction;
	int                 anHeight;
	CValidationState    txState;

	CFeeRate   aFeeRate;
	CTxMemPool aMemPool;
	CBlockUndo aBlockUndo;

	void resetBaseParams();
	void resetParams();

	void preFillSidechainsCollection();

	CTransaction createSideChainTxWith(const uint256 & newScId, const CAmount & fwdTxAmount);
	CTransaction createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount);

	CTransaction createEmptyScTx();
	CTransaction createSideChainTxWithNoFwdTransfer(const uint256 & newScId);
	CTransaction createNonScTx(bool ccIsNull = true);
	CTransaction createShieldedTx();

};

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// checkTxSemanticValidity ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SideChainTestSuite, NonSideChainTxsAreSemanticallyValid) {
	aTransaction = createNonScTx();

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

TEST_F(SideChainTestSuite, NonSideChain_NonCcNull_TxsAreNotSemanticallyValid) {
	aTransaction = createNonScTx(/*ccIsNull = */false);

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

TEST_F(SideChainTestSuite, SideChain_Shielded_TxsAreNotCurrentlySupported) {
	aTransaction = createShieldedTx();

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

TEST_F(SideChainTestSuite, SideChain_ccNull_TxsAreSemanticallyValid) {
	aTransaction = createEmptyScTx();

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
	//create a sidechain withouth fwd transfer
	uint256 newScId = uint256S("1492");
	aTransaction = createSideChainTxWithNoFwdTransfer(newScId);

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
	//insert a sidechain
	uint256 newScId = uint256S("1492");
	CAmount initialFwdAmount = 1000;
	aTransaction = createSideChainTxWith(newScId, initialFwdAmount);

	//Prerequisites
	ASSERT_TRUE(aTransaction.IsScVersion())<<"Test requires sidechain tx";
	ASSERT_FALSE(aTransaction.ccIsNull())<<"Test requires non null tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//test
	bool res = sideChainManager.checkTxSemanticValidity(aTransaction, txState);

	//checks
	EXPECT_TRUE(res)<<"sidechain creation with forward transfer should be considered semantically valid";
	EXPECT_TRUE(txState.IsValid())<<"Positive semantics checks should not alter tx validity";
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// RevertTxOutputs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SideChainTestSuite, FwdTransferTxToUnexistingScCannotBeReverted) {
	uint256 unexistingScId = uint256S("a1b2");

	//create fwd transaction to be reverted
	CAmount fwdAmount = 999;
	aTransaction = createFwdTransferTxWith(unexistingScId, fwdAmount);

	//Prerequisites
	ASSERT_FALSE(coinViewCache.sidechainExists(unexistingScId))
		<<"Test requires unexisting sideChain";

	//test
	bool res = coinViewCache.RevertTxOutputs(aTransaction, anHeight);

	//checks
	EXPECT_FALSE(res)<<"it should not be possible to revert an fwd tx from unexisting sidechain";
}

TEST_F(SideChainTestSuite, ScCreationTxCannotBeRevertedIfScIsNotPreviouslyCreated) {
	uint256 unexistingScId = uint256S("a1b2");

	//create Sc transaction to be reverted
	CAmount fwdAmount = 999;
	aTransaction = createSideChainTxWithNoFwdTransfer(unexistingScId);

	//Prerequisites
	ASSERT_FALSE(coinViewCache.sidechainExists(unexistingScId))
		<<"Test requires unexisint sideChain";

	//test
	bool res = coinViewCache.RevertTxOutputs(aTransaction, anHeight);

	//checks
	EXPECT_FALSE(res)<<"it should not be possible to revert an Sc creation tx if Sc creation has not happened before";
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////// ApplyMatureBalances /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//TODO MISSING CHECKS ON BlockUndo
TEST_F(SideChainTestSuite, CoinsInScCreationDoNotModifyScBalanceBeforeCoinMaturity) {
	//Insert Sc
	uint256 newScId = uint256S("a1b2");
	CAmount initialAmount = 1000;
	int scCreationHeight = 5;
	aTransaction = createSideChainTxWith(newScId, initialAmount);
	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight))
	    <<"Test requires a sc creation to happen";

	int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
	int lookupBlockHeight = coinMaturityHeight - 1;
	ASSERT_TRUE(lookupBlockHeight < coinMaturityHeight)
	    <<"Test requires attempting to mature coins before their maturity height";

	//test
	bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

	//check
	EXPECT_TRUE(res)<<"it should be possible to applyMatureBalances before coin maturity";
	EXPECT_TRUE(coinViewCache.getScInfoMap().at(newScId).balance < initialAmount)
	    <<"Coins should not alter Sc balance before coin maturity height comes";
}

TEST_F(SideChainTestSuite, CoinsInScCreationModifyScBalanceAtCoinMaturity) {
	//Insert Sc
	uint256 newScId = uint256S("a1b2");
	CAmount initialAmount = 1000;
	int scCreationHeight = 7;
	aTransaction = createSideChainTxWith(newScId, initialAmount);
	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight))
	    <<"Test requires a sc creation to happen";

	int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
	int lookupBlockHeight = coinMaturityHeight;
	ASSERT_TRUE(lookupBlockHeight == coinMaturityHeight)
	<<"Test requires attempting to mature coins at maturity height";

	//test
	bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

	//checks
	EXPECT_TRUE(res)<<"it should be possible to applyMatureBalances at coin maturity height";
	EXPECT_TRUE(coinViewCache.getScInfoMap().at(newScId).balance == initialAmount)
	    <<"Coins should alter Sc balance when coin maturity their height comes";
}

TEST_F(SideChainTestSuite, CoinsInScCreationDoNotModifyScBalanceAfterCoinMaturity) {
	//Insert Sc
	uint256 newScId = uint256S("a1b2");
	CAmount initialAmount = 1000;
	int scCreationHeight = 11;
	aTransaction = createSideChainTxWith(newScId, initialAmount);
	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight))
	    <<"Test requires a sc creation to happen";

	int coinMaturityHeight = anHeight + Params().ScCoinsMaturity();
	int lookupBlockHeight = coinMaturityHeight + 1;
	//Prerequisites
	ASSERT_TRUE(lookupBlockHeight > coinMaturityHeight)
	<<"Test requires attempting to mature coins after their maturity height";

	//test
	bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

	//check
	EXPECT_FALSE(res)<<"it should not be possible to applyMatureBalances after coin maturity height";
	EXPECT_TRUE(coinViewCache.getScInfoMap().at(newScId).balance < initialAmount)
	    <<"Coins should not alter Sc balance after coin maturity height has come";
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// Flush /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SideChainTestSuite, EmptyFlushDoesNotPersistNewSideChain) {
	const Sidechain::ScInfoMap & initialScCollection = sideChainManager.getScInfoMap();

	//Prerequisites
	ASSERT_TRUE(initialScCollection.size() == 0)<<"Test requires no sidechains initially";

	//test
	bool res = coinViewCache.Flush();

	//checks
	EXPECT_TRUE(res)<<"We should be allowed to empty flush";

	const Sidechain::ScInfoMap & finalScCollection = sideChainManager.getScInfoMap();
	EXPECT_TRUE(finalScCollection == initialScCollection)
	    <<"Sidechains collection should not have changed with empty flush";
}

TEST_F(SideChainTestSuite, EmptyFlushDoesNotAlterExistingSideChainsCollection) {
	//Prerequisites
	preFillSidechainsCollection();

	const Sidechain::ScInfoMap & initialScCollection = sideChainManager.getScInfoMap();
	ASSERT_TRUE(initialScCollection.size() != 0)<<"Test requires some sidechains initially";

	//test
	bool res = coinViewCache.Flush();

	//checks
	EXPECT_TRUE(res)<<"We should be allowed to empty flush";

	const Sidechain::ScInfoMap & finalScCollection = sideChainManager.getScInfoMap();
	EXPECT_TRUE(finalScCollection == initialScCollection)
	    <<"Sidechains collection should not have changed with empty flush";
}

TEST_F(SideChainTestSuite, FlushPersistsNewSideChains) {
	Sidechain::ScInfo infoHelper;

	//create the sidechain
	uint256 newScId = uint256S("a1b2");
	CAmount fwdTransfer = 1000;
	aTransaction = createSideChainTxWith(newScId, fwdTransfer);

	//Prerequisite
	ASSERT_FALSE(sideChainManager.getScInfo(newScId, infoHelper))
	    << "Test requires sidechain not to be previously persisted";

	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight))
	    << "Test requires new sidechain to be successfully processed";

	//test
	bool res = coinViewCache.Flush();

	//checks
	EXPECT_TRUE(res)<<"We should be allowed to flush a new sidechain";
	EXPECT_TRUE(sideChainManager.getScInfo(newScId, infoHelper))
	    << "Once flushed, new sidechain should be made available by ScManager";
}

TEST_F(SideChainTestSuite, FlushPersistsForwardTransfersToo) {
	Sidechain::ScInfo infoHelper;

	//create the sidechain
	uint256 newScId = uint256S("a1b2");
	CAmount initialFwdTxAmount = 1;
	aTransaction = createSideChainTxWith(newScId, initialFwdTxAmount);

	//Prerequisites
	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight))
		<<"Test requires the sidechain to be available before forward transfer";

	//create forward transfer
	CAmount fwdTxAmount = 1000;
	aTransaction = createFwdTransferTxWith(newScId, fwdTxAmount);

	//test
	bool res = coinViewCache.Flush();

	//checks
	EXPECT_TRUE(res)<<"We should be allowed to flush a new sidechain";
	EXPECT_TRUE(sideChainManager.getScInfo(newScId, infoHelper))
	    << "Once flushed, new sidechain should be made available by ScManager";

	//Todo: Add check on forward transfer amount maybe resetting to zero coinMaturity
}

TEST_F(SideChainTestSuite, FlushAlignsMgrScCollectionToCoinViewOne) {
	uint256 newScId = uint256S("a1b2");
	CAmount initialFwdTxAmount = 1;
	aTransaction = createSideChainTxWith(newScId, initialFwdTxAmount);

	//prerequisites
	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight))
		<<"Test requires view side chain collection to be filled";

	//test
	bool res = coinViewCache.Flush();

	//check
	EXPECT_TRUE(sideChainManager.getScInfoMap() == coinViewCache.getScInfoMap())
	    <<"flush should align sidechain manager scCollection to coinViewCache one";
}

TEST_F(SideChainTestSuite, UponCreationCoinViewIsAlignedToMgrScCollection) {
	//Prerequisites
	preFillSidechainsCollection();
	ASSERT_TRUE(sideChainManager.getScInfoMap().size() != 0)<<"Test requires some sidechains initially";

	//test
	Sidechain::ScCoinsViewCache newView;

	//check
	EXPECT_TRUE(sideChainManager.getScInfoMap() == newView.getScInfoMap())
	    <<"when new coinViewCache is create, it should be aligned with sidechain manager";
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Structural UTs ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SideChainTestSuite, Structural_ManagerIsSingleton) {
	//test
	Sidechain::ScMgr& rAnotherScMgrInstance = Sidechain::ScMgr::instance();

	//check
	EXPECT_TRUE(&sideChainManager == &rAnotherScMgrInstance)
			<< "ScManager Instances have different address:"
			<< &sideChainManager << " and " << &rAnotherScMgrInstance;
}

TEST_F(SideChainTestSuite, Structural_ManagerDoubleInitializationIsForbidden) {
	size_t cacheSize(0);
	bool fWipe(false);

	//Prerequisites: first initialization happens in fixture's setup

	//test
	bool bRet = sideChainManager.initialUpdateFromDb(cacheSize, fWipe, Sidechain::ScMgr::mock);

	//Checks
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
	uint256 newScId = uint256S("1492");
	CAmount initialFwdTxAmount = 1;
	aTransaction = createSideChainTxWith(newScId, initialFwdTxAmount);

	//Prerequisite
	ASSERT_FALSE(coinViewCache.sidechainExists(newScId))
			<< "Test requires that sidechain is not registered";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	//check
	EXPECT_TRUE(res) << "New sidechain creation txs should be processed";
	EXPECT_TRUE(coinViewCache.sidechainExists(newScId))
			<< "New sidechain creation txs should be cached";
}

TEST_F(SideChainTestSuite, ScDoubleInsertionIsRejected) {
	//first,valid sideChain transaction
	uint256 newScId = uint256S("1492");
	CAmount initialFwdTxAmount = 1;
	aTransaction = createSideChainTxWith(newScId, initialFwdTxAmount);
	coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	//second, id-duplicated, sideChain transaction
	CAmount aFwdTxAmount = 999;
	CTransaction duplicatedTx = createSideChainTxWith(newScId, aFwdTxAmount);

	//Prerequisites
	ASSERT_TRUE(aTransaction.vsc_ccout[0].scId == duplicatedTx.vsc_ccout[0].scId)
	    <<"Test requires two SC Tx with same id";
	ASSERT_TRUE(coinViewCache.sidechainExists(newScId))
		<<"Test requires first Sc to be successfully registered";

	//test
	bool res = coinViewCache.UpdateScInfo(duplicatedTx, aBlock, anHeight);

	//check
	EXPECT_FALSE(res)<< "Duplicated sidechain creation txs should not be processed";
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
	CAmount initialFwdAmount = 1987;
	aTransaction = createFwdTransferTxWith(nonExistentId, initialFwdAmount);

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
	uint256 newScId = uint256S("1492");
	CAmount initialFwdAmount = 1953;
	aTransaction = createSideChainTxWith(newScId, initialFwdAmount);

	coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	//create forward transfer
	CAmount anotherFwdAmount = 1987;
	aTransaction = createFwdTransferTxWith(newScId, anotherFwdAmount);

	//Prerequisite
	ASSERT_TRUE(coinViewCache.sidechainExists(newScId))
	    <<"Test requires Sc to exist before attempting the forward transfer tx";

	//test
	bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	//check
	EXPECT_TRUE(res)<< "It should be possible to register a forward transfer to an existing sidechain";
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

TEST_F(SideChainTestSuite, ScCreationWithoutForwardTrasferIsApplicableToState) {
	//create a sidechain without forward transfer
	uint256 newScId = uint256S("1492");
	aTransaction = createSideChainTxWithNoFwdTransfer(newScId);

	//Prerequisite
	ASSERT_FALSE(coinViewCache.sidechainExists(newScId))
		<<"Test requires the Sc creation tx to be new in current transaction";

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_TRUE(res)<<"Sc creation and forward transfer to it may coexist in the same tx";
}

TEST_F(SideChainTestSuite, NewScCreationsAreApplicableToState) {
	//create a new sidechain
	uint256 newScId = uint256S("1492");
	CAmount initialFwdAmount = 1953;
	aTransaction = createSideChainTxWith(newScId, initialFwdAmount);

	//Prerequisite
	ASSERT_FALSE(coinViewCache.sidechainExists(newScId))
		<<"Test requires the Sc creation tx to be new";

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_TRUE(res)<<"new Sc creation txs should be applicable to state";
}

TEST_F(SideChainTestSuite, DuplicatedScCreationsAreNotApplicableToState) {
	//insert a sidechain
	uint256 newScId = uint256S("1492");
	CAmount initialFwdAmount = 1953;
	aTransaction = createSideChainTxWith(newScId, initialFwdAmount);
	coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	CAmount anotherFwdTransfer = 1815;
	CTransaction duplicatedTx = createSideChainTxWith(newScId, anotherFwdTransfer);

	//Prerequisite
	ASSERT_TRUE(coinViewCache.sidechainExists(newScId))
		<<"Test requires the Sc creation tx to be new";

	//test
	bool res = sideChainManager.IsTxApplicableToState(duplicatedTx, &coinViewCache);

	//checks
	EXPECT_FALSE(res)<<"Duplicated Sc creation txs should be applicable to state";
}

TEST_F(SideChainTestSuite, ForwardTransfersToExistingSCsAreApplicableToState) {
	//insert a sidechain
	uint256 newScId = uint256S("1492");
	CAmount initialFwdAmount = 1953;
	aTransaction = createSideChainTxWith(newScId, initialFwdAmount);
	coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

	CAmount aFwdTransfer = 5;
	aTransaction = createFwdTransferTxWith(newScId, aFwdTransfer);

	//Prerequisite
	ASSERT_TRUE(coinViewCache.sidechainExists(newScId))
		<<"Test requires the Sc creation tx to be new";

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_TRUE(res)<<"Forward transaction to existent side chains should be applicable to state";
}

TEST_F(SideChainTestSuite, ForwardTransfersToNonExistingSCsAreNotApplicableToState) {
	uint256 nonExistentScId = uint256S("1492");

	CAmount aFwdTransfer = 1815;
	aTransaction = createFwdTransferTxWith(nonExistentScId, aFwdTransfer);

	//Prerequisite
	ASSERT_FALSE(coinViewCache.sidechainExists(nonExistentScId))
		<<"Test requires target sidechain to be non-existent";

	//test
	bool res = sideChainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

	//checks
	EXPECT_FALSE(res)<<"Forward transactions to non existent side chains should not be applicable to state";
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

TEST_F(SideChainTestSuite, ScCreationTxsAreAllowedInEmptyMemPool) {
	//create a sidechain
	uint256 newScId = uint256S("1492");
	CAmount initialFwdAmount = 1953;
	aTransaction = createSideChainTxWith(newScId, initialFwdAmount);

	//Prerequisites
	ASSERT_TRUE(aMemPool.size() == 0)<<"Test requires empty mempool";
	ASSERT_FALSE(aTransaction.ccIsNull())<<"Test requires a Sc creation tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//test
	bool res = sideChainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

	//check
	EXPECT_TRUE(res)<<"Sc creation tx should be allowed in empty mempool";
	EXPECT_TRUE(txState.IsValid())<<"Positive semantics checks should not alter tx validity";
}

TEST_F(SideChainTestSuite, NewScCreationTxsAreAllowedInMemPool) {
	//A Sc tx should be already in mem pool
	uint256 firstScTxId = uint256S("1987");
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = firstScTxId;
	aSideChainCreationTx.withdrawalEpochLength = 1;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);
	aTransaction = aMutableTransaction;

	CAmount txFee;
	double txPriority;

	CTxMemPoolEntry memPoolEntry(aTransaction, txFee, GetTime(), txPriority, anHeight);
	ASSERT_TRUE(aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry))
		<<"Test requires at least a tx in mempool. Could not insert it.";

	//Prerequisites
	ASSERT_TRUE(aMemPool.size() != 0)<<"Test requires non-empty mempool";
	ASSERT_FALSE(aTransaction.ccIsNull())<<"Test requires a Sc creation tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//Prepare a new Sc tx, with differentId
	aMutableTransaction.vsc_ccout.clear();
	aSideChainCreationTx.scId = uint256S("1991");
	aSideChainCreationTx.withdrawalEpochLength = 2;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);
	aTransaction = aMutableTransaction;

	//Prerequisites
	ASSERT_TRUE(firstScTxId != aSideChainCreationTx.scId)<<"Test requires two Sc creation tx with different ids";

	//test
	bool res = sideChainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

	//check
	EXPECT_TRUE(res)<<"new Sc creation txs should be allowed in non-empty mempool";
	EXPECT_TRUE(txState.IsValid())<<"Positive semantics checks should not alter tx validity";
}

TEST_F(SideChainTestSuite, DuplicatedScCreationTxsAreNotAllowedInMemPool) {
	//create a sidechain tx and insert in mempool
	uint256 firstScId = uint256S("1987");
	CAmount initialFwdAmount = 1953;
	aTransaction = createSideChainTxWith(firstScId, initialFwdAmount);

	CAmount txFee;
	double txPriority;

	CTxMemPoolEntry memPoolEntry(aTransaction, txFee, GetTime(), txPriority, anHeight);
	ASSERT_TRUE(aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry))
		<<"Test requires at least a tx in mempool. Could not insert it.";

	//Prerequisites
	ASSERT_TRUE(aMemPool.size() != 0)<<"Test requires non-empty mempool";
	ASSERT_FALSE(aTransaction.ccIsNull())<<"Test requires a Sc creation tx";
	ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

	//Prepare a new Sc tx, with differentId
	uint256 duplicatedScId = firstScId;
	CAmount anotherAmount = 1492;
	CTransaction duplicatedTx = createSideChainTxWith(duplicatedScId, anotherAmount);

	//Prerequisites
	ASSERT_TRUE(duplicatedScId == firstScId)<<"Test requires two Sc creation tx with same ids";

	//test
	bool res = sideChainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

	//check
	EXPECT_FALSE(res)<<"duplicated Sc creation txs should be not allowed in non-empty mempool";
	EXPECT_FALSE(txState.IsValid())<<"Negative semantics checks should alter tx validity";
	EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
		<<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////// Test Fixture definitions ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
void SideChainTestSuite::resetBaseParams() {
	//TODO: evaluate moving resetBaseParams to chainparamsbase.h

	//force reset of pCurrentBaseParams
	CBaseChainParams* nakedCurrentBaseParams = &const_cast<CBaseChainParams &>(BaseParams());
	nakedCurrentBaseParams = nullptr;
}

void SideChainTestSuite::resetParams() {
	//TODO: evaluate moving resetBaseParams to chainparams.h

	//force reset of pCurrentParams
	CChainParams* nakedCurrentParams = &const_cast<CChainParams &>(Params());
	nakedCurrentParams = nullptr;
}

void SideChainTestSuite::preFillSidechainsCollection() {
    //force access to manager in-memory data structure to fill it up for testing purposes
	//Todo: make it work for the case Sidechain::ScMgr::create, i.e. push this info to db

    Sidechain::ScInfoMap & rManagerInternalMap
	    = const_cast<Sidechain::ScInfoMap&>(sideChainManager.getScInfoMap());

    //create a couple of ScInfo to fill data struct
    Sidechain::ScInfo info;
    uint256 scId;

    scId = uint256S("a123");
    info.creationBlockHash = uint256S("aaaa");
    info.creationBlockHeight = 1992;
    info.creationTxHash = uint256S("bbbb");
    rManagerInternalMap[scId] = info;

    scId = uint256S("b987");
    info.creationBlockHash = uint256S("1111");
    info.creationBlockHeight = 1993;
    info.creationTxHash = uint256S("2222");
    rManagerInternalMap[scId] = info;
}

CTransaction SideChainTestSuite::createSideChainTxWith(const uint256 & newScId, const CAmount & fwdTxAmount)
{
	CMutableTransaction aMutableTransaction;
	aMutableTransaction.nVersion = SC_TX_VERSION;

	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = newScId;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = aSideChainCreationTx.scId;
	aForwardTransferTx.nValue = fwdTxAmount;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);

	return CTransaction(aMutableTransaction);
}

CTransaction SideChainTestSuite::createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount)
{
	CMutableTransaction aMutableTransaction;
	aMutableTransaction.nVersion = SC_TX_VERSION;

	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = newScId;
	aForwardTransferTx.nValue = fwdTxAmount;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);

	return CTransaction(aMutableTransaction);
}

CTransaction SideChainTestSuite::createEmptyScTx() {
	CMutableTransaction aMutableTransaction;
	aMutableTransaction.nVersion = SC_TX_VERSION;

	return CTransaction(aMutableTransaction);
}

CTransaction SideChainTestSuite::createSideChainTxWithNoFwdTransfer(const uint256 & newScId)
{
	CMutableTransaction aMutableTransaction;
	aMutableTransaction.nVersion = SC_TX_VERSION;

	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = newScId;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	return CTransaction(aMutableTransaction);
}

CTransaction SideChainTestSuite::createNonScTx(bool ccIsNull) {
	CMutableTransaction aMutableTransaction;
	aMutableTransaction.nVersion = TRANSPARENT_TX_VERSION;

	if (!ccIsNull)
	{
		CTxScCreationOut aSideChainCreationTx;
		aSideChainCreationTx.scId = uint256S("1492");
		aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);
	}

	return CTransaction(aMutableTransaction);
}

CTransaction SideChainTestSuite::createShieldedTx()
{
	CMutableTransaction aMutableTransaction;
	aMutableTransaction.nVersion = SC_TX_VERSION;
	JSDescription  aShieldedTx; //Todo: verify naming and whether it should be filled somehow
	aMutableTransaction.vjoinsplit.push_back(aShieldedTx);

	return CTransaction(aMutableTransaction);
}
