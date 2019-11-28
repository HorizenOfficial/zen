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
		SelectBaseParams(CBaseChainParams::REGTEST);
		SelectParams(CBaseChainParams::REGTEST);

		sideChainManager.initialUpdateFromDb(0, true, Sidechain::ScMgr::mock);
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

	//TODO: Consider storing initial CBaseChainParam/CChainParam and reset it upon TearDown; try and handle assert
	//TODO: evaluate moving resetBaseParams to chainparamsbase.h
	void resetBaseParams() {
		//force reset of pCurrentBaseParams
		CBaseChainParams* nakedCurrentBaseParams = &const_cast<CBaseChainParams &>(BaseParams());
		nakedCurrentBaseParams = nullptr;
	}

	void resetParams() {
		//force reset of pCurrentParams
		CChainParams* nakedCurrentParams = &const_cast<CChainParams &>(Params());
		nakedCurrentParams = nullptr;
	}

	void preFillSidechainsCollection() {
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
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////// ApplyMatureBalances /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//TODO MISSING CHECKS ON BlockUndo
TEST_F(SideChainTestSuite, ForwardTransfersDoNotModifyScBalanceBeforeCoinMaturity) {
	int coinMaturityHeight = anHeight + Params().ScCoinsMaturity();

	//insert the sidechain
	uint256 newScId = uint256S("a1b2");
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = newScId;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	//Insert forward transfer at a certain height
	CAmount fwdTxAmount = 1000;
	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = aSideChainCreationTx.scId;
	aForwardTransferTx.nValue = fwdTxAmount;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);
	aTransaction = aMutableTransaction;

	int lookupBlockHeight = coinMaturityHeight - 1;

	//Prerequisites
	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight))
	    <<"Test requires a fwd transfer to happen";
	ASSERT_TRUE(lookupBlockHeight < coinMaturityHeight)
	    <<"Test requires attempting to mature balances before their maturity height";

	//test
	bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

	//check
	EXPECT_TRUE(res)<<"there should be no problem in attempting to applyMatureBalances before coin maturity";
	EXPECT_TRUE(coinViewCache.getScInfoMap().at(newScId).balance < fwdTxAmount)
	    <<"Forward Transfered coins should not alter immediately Sb balance if CbhMinimumAge is not null";
}

TEST_F(SideChainTestSuite, ForwardTransfersModifyScBalanceAtCoinMaturity) {
	int coinMaturityHeight = anHeight + Params().ScCoinsMaturity();

	//insert the sidechain
	uint256 newScId = uint256S("a1b2");
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = newScId;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	//Insert forward transfer at a certain height
	CAmount fwdTxAmount = 1000;
	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = aSideChainCreationTx.scId;
	aForwardTransferTx.nValue = fwdTxAmount;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);
	aTransaction = aMutableTransaction;

	int lookupBlockHeight = coinMaturityHeight;

	//Prerequisites
	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight))
	    <<"Test requires a fwd transfer to happen";
	ASSERT_TRUE(lookupBlockHeight == coinMaturityHeight)
	    <<"Test requires attempting to mature balances before their maturity height";

	//test
	bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

	//checks
	EXPECT_TRUE(res)<<"there should be no problem in attempting to applyMatureBalances at coin maturity";
	EXPECT_TRUE(coinViewCache.getScInfoMap().at(newScId).balance == fwdTxAmount)
	    <<"Forward Transfered coins should mature after CbhMinimumAge blocks";
}

TEST_F(SideChainTestSuite, ForwardTransfersDoNotModifyScBalanceAfterCoinMaturity) {
	int coinMaturityHeight = anHeight + Params().ScCoinsMaturity();

	//insert the sidechain
	uint256 newScId = uint256S("a1b2");
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = newScId;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	//Insert forward transfer at a certain height
	CAmount fwdTxAmount = 1000;
	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = aSideChainCreationTx.scId;
	aForwardTransferTx.nValue = fwdTxAmount;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);
	aTransaction = aMutableTransaction;

	int lookupBlockHeight = coinMaturityHeight + 1;

	//Prerequisites
	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight))
	    <<"Test requires a fwd transfer to happen";
	ASSERT_TRUE(lookupBlockHeight > coinMaturityHeight)
	    <<"Test requires attempting to mature balances before their maturity height";

	//test
	bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

	//check
	EXPECT_FALSE(res)<<"there should be a problem in attempting to applyMatureBalances after coin maturity";
	EXPECT_TRUE(coinViewCache.getScInfoMap().at(newScId).balance < fwdTxAmount)
	    <<"Forward Transfered coins should not alter immediately Sb balance if CbhMinimumAge is not null";
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// Flush /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SideChainTestSuite, EmptyFlushDoesNotPersistNewSideChain) {
	//Prerequisites
	const Sidechain::ScInfoMap & initialScCollection = sideChainManager.getScInfoMap();
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
	uint256 newScId = uint256S("a1b2");
	Sidechain::ScInfo infoHelper;

	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = newScId;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);
	aTransaction = aMutableTransaction;

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

TEST_F(SideChainTestSuite, FlushPersistsForwardTransfers) {
	//insert the sidechain
	uint256 newScId = uint256S("a1b2");
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = newScId;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);
	aTransaction = aMutableTransaction;

	ASSERT_TRUE(coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight))
		<<"Test requires the sidechain to be available before forward transfer";
	aMutableTransaction.vsc_ccout.clear();

	//create forward transfer
	CAmount fwdTxAmount = 1000;
	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = aSideChainCreationTx.scId;
	aForwardTransferTx.nValue = fwdTxAmount;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);
	aTransaction = aMutableTransaction;

	//test
	bool res = coinViewCache.Flush();

	//checks
	EXPECT_TRUE(res)<<"We should be allowed to flush a new sidechain";
	Sidechain::ScInfo infoHelper;
	EXPECT_TRUE(sideChainManager.getScInfo(newScId, infoHelper))
	    << "Once flushed, new sidechain should be made available by ScManager";

	//Todo: Add check on forward transfer amount
}

TEST_F(SideChainTestSuite, FlushAlignsMgrScCollectionToCoinViewOne) {
	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("c4d6");;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	CTxForwardTransferOut aForwardTransferTx;
	aForwardTransferTx.scId = aSideChainCreationTx.scId;
	aForwardTransferTx.nValue = 1000;
	aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);

	aTransaction = aMutableTransaction;

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
TEST_F(SideChainTestSuite, NonSideChain_ccNull_TxsAreSemanticallyValid) {
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

TEST_F(SideChainTestSuite, NonSideChain_NonCcNull_TxsAreNotSemanticallyValid) {
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

TEST_F(SideChainTestSuite, SideChain_Shielded_TxsAreNotCurrentlySupported) {
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

TEST_F(SideChainTestSuite, SideChain_ccNull_TxsAreSemanticallyValid) {
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

TEST_F(SideChainTestSuite, ScCreationTxsAreAllowedInEmptyMemPool) {
	aMutableTransaction.nVersion = SC_TX_VERSION;

	CTxScCreationOut aSideChainCreationTx;
	aSideChainCreationTx.scId = uint256S("1492");
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);

	aTransaction = aMutableTransaction;

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
	aSideChainCreationTx.scId = firstScTxId;
	aSideChainCreationTx.withdrawalEpochLength = 2;
	aMutableTransaction.vsc_ccout.push_back(aSideChainCreationTx);
	aTransaction = aMutableTransaction;

	//Prerequisites
	ASSERT_TRUE(firstScTxId == aSideChainCreationTx.scId)<<"Test requires two Sc creation tx with same ids";

	//test
	bool res = sideChainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

	//check
	EXPECT_FALSE(res)<<"duplicated Sc creation txs should be not allowed in non-empty mempool";
	EXPECT_FALSE(txState.IsValid())<<"Negative semantics checks should alter tx validity";
	EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
		<<"wrong reject code. Value returned: "<<txState.GetRejectCode();
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
