#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <consensus/validation.h>
#include <txmempool.h>
#include <undo.h>
#include <main.h>

class FakePersistance final : public Sidechain::PersistenceLayer {
public:
    FakePersistance() = default;
    ~FakePersistance() = default;
    bool loadPersistedDataInto(Sidechain::ScInfoMap & _mapToFill) {return true;}
    bool persist(const uint256& scId, const Sidechain::ScInfo& info) {return true;}
    bool erase(const uint256& scId) {return true;}
    void dump_info() {return;}
};

class FaultyPersistance final : public Sidechain::PersistenceLayer {
public:
    FaultyPersistance() = default;
    ~FaultyPersistance() = default;
    bool loadPersistedDataInto(Sidechain::ScInfoMap & _mapToFill) {return true;} //This allows correct initialization
    bool persist(const uint256& scId, const Sidechain::ScInfo& info) {return false;}
    bool erase(const uint256& scId) {return false;}
    void dump_info() {return;}
};

class SidechainTestSuite: public ::testing::Test {

public:
    SidechainTestSuite() :
            sidechainManager(Sidechain::ScMgr::instance()), coinViewCache(sidechainManager){};

    ~SidechainTestSuite() {
        sidechainManager.reset();
    };

    void SetUp() override {
        SelectBaseParams(CBaseChainParams::REGTEST);
        SelectParams(CBaseChainParams::REGTEST);

        ASSERT_TRUE(sidechainManager.initPersistence(/*cacheSize*/0, /*fWipe*/true));
        //ASSERT_TRUE(sidechainManager.initPersistence(new FakePersistance()));
    };

    void TearDown() override {};

protected:
    //Subjects under test
    Sidechain::ScMgr&           sidechainManager;
    Sidechain::ScCoinsViewCache coinViewCache;

    //Helpers
    void preFillSidechainsCollection();

    CTransaction createNewSidechainTxWith(const uint256 & newScId, const CAmount & fwdTxAmount);
    CTransaction createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount);

    CTransaction createNewSidechainTxWithNoFwdTransfer(const uint256 & newScId);
    CTransaction createTransparentTx(bool ccIsNull);
    CTransaction createSproutTx(bool ccIsNull);
    void         appendTxTo(CTransaction & tx, const uint256 & scId, const CAmount & amount);

    CBlockUndo   createBlockUndoWith(const uint256 & scId, int height, CAmount amount);
    CBlockUndo   createEmptyBlockUndo();

private:
    CMutableTransaction populateTx(int txVersion, const uint256 & newScId = uint256S("0"), const CAmount & fwdTxAmount = CAmount(0));
    void signTx(CMutableTransaction& mtx);
};

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// checkTxSemanticValidity ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, TransparentCcNullTxsAreSemanticallyValid) {
    CTransaction aTransaction = createTransparentTx(/*ccIsNull = */true);
    CValidationState txState;

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, TransparentNonCcNullTxsAreNotSemanticallyValid) {
    CTransaction aTransaction = createTransparentTx(/*ccIsNull = */false);
    CValidationState txState;

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SproutCcNullTxsAreCurrentlySupported) {
    CTransaction aTransaction = createSproutTx(/*ccIsNull = */true);
    CValidationState txState;

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, SproutNonCcNullTxsAreCurrentlySupported) {
    CTransaction aTransaction = createSproutTx(/*ccIsNull = */false);
    CValidationState txState;

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithoutForwardTransferAreNotSemanticallyValid) {
    CTransaction aTransaction = createNewSidechainTxWithNoFwdTransfer(uint256S("1492"));
    CValidationState txState;

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithPositiveForwardTransferAreSemanticallyValid) {
    CTransaction aTransaction = createNewSidechainTxWith( uint256S("1492"), CAmount(1000));
    CValidationState txState;

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, SidechainCreationsWithTooLargePositiveForwardTransferAreNotSemanticallyValid) {
    CTransaction aTransaction = createNewSidechainTxWith(uint256S("1492"), CAmount(MAX_MONEY +1));
    CValidationState txState;

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithZeroForwardTransferAreNotSemanticallyValid) {
    CTransaction aTransaction = createNewSidechainTxWith(uint256S("1492"), CAmount(0));
    CValidationState txState;

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithNegativeForwardTransferNotAreSemanticallyValid) {
    CTransaction aTransaction = createNewSidechainTxWith(uint256S("1492"), CAmount(-1));
    CValidationState txState;

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, FwdTransferCumulatedAmountDoesNotOverFlow) {
    uint256 scId = uint256S("1492");
    CAmount initialFwdTrasfer(1);
    CTransaction aTransaction = createNewSidechainTxWith(scId, initialFwdTrasfer);
    appendTxTo(aTransaction, scId, MAX_MONEY);
    CValidationState txState;

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////// IsTxApplicableToState ////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, NewScCreationsAreApplicableToState) {
    CTransaction aTransaction = createNewSidechainTxWith(uint256S("1492"), CAmount(1953));

    //test
    bool res = coinViewCache.IsTxApplicableToState(aTransaction);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, DuplicatedScCreationsAreNotApplicableToState) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(1953));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    CTransaction duplicatedTx = createNewSidechainTxWith(scId, CAmount(1815));

    //test
    bool res = coinViewCache.IsTxApplicableToState(duplicatedTx);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistingSCsAreApplicableToState) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(1953));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    aTransaction = createFwdTransferTxWith(scId, CAmount(5));

    //test
    bool res = coinViewCache.IsTxApplicableToState(aTransaction);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistingSCsAreNotApplicableToState) {
    CTransaction aTransaction = createFwdTransferTxWith(uint256S("1492"), CAmount(1815));

    //test
    bool res = coinViewCache.IsTxApplicableToState(aTransaction);

    //checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////// IsTxAllowedInMempool /////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, ScCreationTxsAreAllowedInEmptyMemPool) {
    CTransaction aTransaction = createNewSidechainTxWith(uint256S("1492"), CAmount(1953));
    CValidationState txState;
    CFeeRate   aFeeRate;
    CTxMemPool aMemPool(aFeeRate);

    //test
    bool res = sidechainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, NewScCreationTxsAreAllowedInMemPool) {
    CTransaction aTransaction = createNewSidechainTxWith(uint256S("1987"), CAmount(1994));
    CTxMemPoolEntry memPoolEntry(aTransaction, CAmount(), GetTime(), double(0.0), /*height*/int(1789));
    CFeeRate   aFeeRate;
    CTxMemPool aMemPool(aFeeRate);
    aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry);

    CTransaction aNewTx = createNewSidechainTxWith(uint256S("1991"), CAmount(5));
    CValidationState txState;

    //test
    bool res = sidechainManager.IsTxAllowedInMempool(aMemPool, aNewTx, txState);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, DuplicatedScCreationTxsAreNotAllowedInMemPool) {
    uint256 scId = uint256S("1987");

    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(10));
    CTxMemPoolEntry memPoolEntry(aTransaction, CAmount(), GetTime(), double(0.0), /*height*/int(1789));
    CFeeRate   aFeeRate;
    CTxMemPool aMemPool(aFeeRate);
    aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry);

    CTransaction duplicatedTx = createNewSidechainTxWith(scId, CAmount(15));
    CValidationState txState;

    //test
    bool res = sidechainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

    //check
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////// ApplyMatureBalances /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, InitialCoinsTransferDoesNotModifyScBalanceBeforeCoinsMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    CTransaction aTransaction = createNewSidechainTxWith(scId, initialAmount);
    int scCreationHeight = 5;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight - 1;
    CBlockUndo aBlockUndo = createEmptyBlockUndo();

    //test
    bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

    //check
    EXPECT_TRUE(res);

    coinViewCache.Flush();
    Sidechain::ScInfo mgrInfos;
    ASSERT_TRUE(sidechainManager.getScInfo(scId, mgrInfos));
    EXPECT_TRUE(mgrInfos.balance < initialAmount)
        <<"resulting balance is "<<mgrInfos.balance <<" while initial amount is "<<initialAmount;
}

TEST_F(SidechainTestSuite, InitialCoinsTransferModifiesScBalanceAtCoinMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    CTransaction aTransaction = createNewSidechainTxWith(scId, initialAmount);
    int scCreationHeight = 7;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight;
    CBlockUndo aBlockUndo = createEmptyBlockUndo();

    //test
    bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

    //checks
    EXPECT_TRUE(res);

    coinViewCache.Flush();
    Sidechain::ScInfo mgrInfos;
    ASSERT_TRUE(sidechainManager.getScInfo(scId, mgrInfos));
    EXPECT_TRUE(mgrInfos.balance == initialAmount)
        <<"resulting balance is "<<mgrInfos.balance <<" expected one is "<<initialAmount;
}

TEST_F(SidechainTestSuite, InitialCoinsTransferDoesNotModifyScBalanceAfterCoinsMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    CTransaction aTransaction = createNewSidechainTxWith(scId, initialAmount);
    int scCreationHeight = 11;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = /*height*/int(1789) + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight + 1;
    CBlockUndo aBlockUndo = createEmptyBlockUndo();

    //test
    bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

    //check
    EXPECT_FALSE(res);

    coinViewCache.Flush();
    Sidechain::ScInfo mgrInfos;
    ASSERT_TRUE(sidechainManager.getScInfo(scId, mgrInfos));
    EXPECT_TRUE(mgrInfos.balance < initialAmount)
        <<"resulting balance is "<<mgrInfos.balance <<" while initial amount is "<<initialAmount;
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// RestoreImmatureBalances ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, RestoreImmatureBalancesAffectsScBalance) {
    uint256 scId = uint256S("ca1985");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(34));
    int scCreationHeight = 71;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    CBlockUndo aBlockUndo = createEmptyBlockUndo();

    coinViewCache.ApplyMatureBalances(scCreationHeight + Params().ScCoinsMaturity(), aBlockUndo);

    Sidechain::ScInfo viewInfos;
    ASSERT_TRUE(coinViewCache.getScInfo(scId, viewInfos));
    CAmount scBalance = viewInfos.balance;

    CAmount amountToUndo = 17;
    aBlockUndo = createBlockUndoWith(scId,scCreationHeight,amountToUndo);

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(coinViewCache.getScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.balance == scBalance - amountToUndo)
        <<"balance after restore is "<<viewInfos.balance
        <<" instead of"<< scBalance - amountToUndo;
}

TEST_F(SidechainTestSuite, YouCannotRestoreMoreCoinsThanAvailableBalance) {
    uint256 scId = uint256S("ca1985");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(34));
    int scCreationHeight = 1991;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    CBlockUndo aBlockUndo = createEmptyBlockUndo();

    coinViewCache.ApplyMatureBalances(scCreationHeight + Params().ScCoinsMaturity(), aBlockUndo);
    Sidechain::ScInfo viewInfos;
    ASSERT_TRUE(coinViewCache.getScInfo(scId, viewInfos));
    CAmount scBalance = viewInfos.balance;

    aBlockUndo = createBlockUndoWith(scId,scCreationHeight,CAmount(50));

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
    ASSERT_TRUE(coinViewCache.getScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.balance == scBalance)
        <<"balance after restore is "<<viewInfos.balance <<" instead of"<< scBalance;
}

TEST_F(SidechainTestSuite, RestoringBeforeBalanceMaturesHasNoEffects) {
    uint256 scId = uint256S("ca1985");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(34));
    int scCreationHeight = 71;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    CBlockUndo aBlockUndo = createEmptyBlockUndo();

    coinViewCache.ApplyMatureBalances(scCreationHeight + Params().ScCoinsMaturity() -1, aBlockUndo);

    aBlockUndo = createBlockUndoWith(scId,scCreationHeight,CAmount(17));

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
    Sidechain::ScInfo viewInfos;
    ASSERT_TRUE(coinViewCache.getScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.balance == 0)
        <<"balance after restore is "<<viewInfos.balance <<" instead of 0";
}

TEST_F(SidechainTestSuite, YouCannotRestoreCoinsFromInexistentSc) {
    uint256 inexistentScId = uint256S("ca1985");
    int scCreationHeight = 71;

    CAmount amountToUndo = 10;

    CBlockUndo aBlockUndo = createBlockUndoWith(inexistentScId,scCreationHeight,amountToUndo);

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// RevertTxOutputs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, RevertingScCreationTxRemovesTheSc) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(10));
    int scCreationHeight = 1;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, scCreationHeight);

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(coinViewCache.sidechainExists(scId));
}

TEST_F(SidechainTestSuite, RevertingFwdTransferRemovesCoinsFromImmatureBalance) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(10));
    int scCreationHeight = 1;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int fwdTxHeight = 5;
    aTransaction = createFwdTransferTxWith(scId, CAmount(7));
    coinViewCache.UpdateScInfo(aTransaction, aBlock, fwdTxHeight);

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, fwdTxHeight);

    //checks
    EXPECT_TRUE(res);
    Sidechain::ScInfo viewInfos;
    ASSERT_TRUE(coinViewCache.getScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.mImmatureAmounts.count(fwdTxHeight + Params().ScCoinsMaturity()) == 0)
        <<"resulting immature amount is "<< viewInfos.mImmatureAmounts.count(fwdTxHeight + Params().ScCoinsMaturity());
}

TEST_F(SidechainTestSuite, ScCreationTxCannotBeRevertedIfScIsNotPreviouslyCreated) {
    CTransaction aTransaction = createNewSidechainTxWith(uint256S("a1b2"),CAmount(15));

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, /*height*/int(1789));

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, FwdTransferTxToUnexistingScCannotBeReverted) {
    CTransaction aTransaction = createFwdTransferTxWith(uint256S("a1b2"), CAmount(999));

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, /*height*/int(1789));

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, RevertingAFwdTransferOnTheWrongHeightHasNoEffect) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(10));
    int scCreationHeight = 1;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int fwdTxHeight = 5;
    CAmount fwdAmount = 7;
    aTransaction = createFwdTransferTxWith(scId, fwdAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, fwdTxHeight);

    //test
    int faultyHeight = fwdTxHeight -1;
    bool res = coinViewCache.RevertTxOutputs(aTransaction, faultyHeight);

    //checks
    EXPECT_FALSE(res);
    Sidechain::ScInfo viewInfos;
    ASSERT_TRUE(coinViewCache.getScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.mImmatureAmounts.at(fwdTxHeight + Params().ScCoinsMaturity()) == fwdAmount)
        <<"Immature amount is "<<viewInfos.mImmatureAmounts.at(fwdTxHeight + Params().ScCoinsMaturity())
        <<"instead of "<<fwdAmount;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// UpdateScInfo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, NewSCsAreRegistered) {
    uint256 newScId = uint256S("1492");
    CTransaction aTransaction = createNewSidechainTxWith(newScId, CAmount(1));
    CBlock aBlock;

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(coinViewCache.sidechainExists(newScId));
}

TEST_F(SidechainTestSuite, DuplicatedSCsAreRejected) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(1));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    CTransaction duplicatedTx = createNewSidechainTxWith(scId, CAmount(999));

    //test
    bool res = coinViewCache.UpdateScInfo(duplicatedTx, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, NoRollbackIsPerformedOnceInvalidTransactionIsEncountered) {
    uint256 firstScId = uint256S("1492");
    uint256 secondScId = uint256S("1912");
    CTransaction aTransaction = createNewSidechainTxWith(firstScId, CAmount(10));
    CBlock aBlock;
    appendTxTo(aTransaction, firstScId, CAmount(20));
    appendTxTo(aTransaction, secondScId, CAmount(30));

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
    EXPECT_TRUE(coinViewCache.sidechainExists(firstScId));
    EXPECT_FALSE(coinViewCache.sidechainExists(secondScId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistentSCsAreRejected) {
    uint256 nonExistentId = uint256S("1492");
    CTransaction aTransaction = createFwdTransferTxWith(nonExistentId, CAmount(10));
    CBlock aBlock;

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
    EXPECT_FALSE(coinViewCache.sidechainExists(nonExistentId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistentSCsAreRegistered) {
    uint256 newScId = uint256S("1492");
    CTransaction aTransaction = createNewSidechainTxWith(newScId, CAmount(5));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    aTransaction = createFwdTransferTxWith(newScId, CAmount(15));

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_TRUE(res);
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// Flush /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, FlushPersistsNewSidechains) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(1000));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //test
    bool res = coinViewCache.Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainManager.sidechainExists(scId));
}

TEST_F(SidechainTestSuite, FlushPersistsForwardTransfers) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(1));
    int scCreationHeight = 1;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    coinViewCache.Flush();

    CAmount fwdTxAmount = 1000;
    int fwdTxHeght = scCreationHeight + 10;
    int fwdTxMaturityHeight = fwdTxHeght + Params().ScCoinsMaturity();
    aTransaction = createFwdTransferTxWith(scId, CAmount(1000));
    coinViewCache.UpdateScInfo(aTransaction, aBlock, fwdTxHeght);

    //test
    bool res = coinViewCache.Flush();

    //checks
    EXPECT_TRUE(res);

    Sidechain::ScInfo persistedInfo;
    ASSERT_TRUE(sidechainManager.getScInfo(scId, persistedInfo));
    ASSERT_TRUE(persistedInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdTxAmount)
        <<"Following flush, persisted fwd amount should equal the one in view";
}

TEST_F(SidechainTestSuite, FlushPersistScErasureToo) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));
    coinViewCache.Flush();

    coinViewCache.RevertTxOutputs(aTransaction, /*height*/int(1789));

    //test
    bool res = coinViewCache.Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(sidechainManager.sidechainExists(scId));
}

TEST_F(SidechainTestSuite, FlushPropagatesErrorsInPersist) {
    sidechainManager.reset();
    ASSERT_TRUE(sidechainManager.initPersistence(new FaultyPersistance()));

    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //test
    bool res = coinViewCache.Flush();

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(sidechainManager.sidechainExists(scId));
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Structural UTs ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, ManagerIsSingleton) {
    //test
    Sidechain::ScMgr& rAnotherScMgrInstance = Sidechain::ScMgr::instance();

    //check
    EXPECT_TRUE(&sidechainManager == &rAnotherScMgrInstance)
            << "ScManager Instances have different address:"
            << &sidechainManager << " and " << &rAnotherScMgrInstance;
}

TEST_F(SidechainTestSuite, ManagerDoubleInitializationIsForbidden) {
    //test
    bool res = sidechainManager.initPersistence(/*cacheSize*/0, /*fWipe*/false);

    //Checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////// Test Fixture definitions ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
void SidechainTestSuite::preFillSidechainsCollection() {
    Sidechain::ScCoinsViewCache tmpCoinViewCache(sidechainManager);

    CTransaction tmpTx = createNewSidechainTxWith(uint256S("a123"), CAmount(10));
    CBlock tmpBlock;
    tmpCoinViewCache.UpdateScInfo(tmpTx, tmpBlock, /*height*/int(1992));
    tmpCoinViewCache.Flush();

    tmpTx = createNewSidechainTxWith(uint256S("b987"), CAmount(10));
    tmpCoinViewCache.UpdateScInfo(tmpTx, tmpBlock, /*height*/int(1993));
    tmpCoinViewCache.Flush();
}

CTransaction SidechainTestSuite::createNewSidechainTxWith(const uint256 & newScId, const CAmount & fwdTxAmount)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, newScId, fwdTxAmount);
    mtx.vout.resize(0);
    mtx.vjoinsplit.resize(0);
    signTx(mtx);

    //CValidationState txState;
    //assert(CheckTransactionWithoutProofVerification(mtx, txState));
    return CTransaction(mtx);
}

CTransaction SidechainTestSuite::createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, newScId, fwdTxAmount);
    mtx.vout.resize(0);
    mtx.vjoinsplit.resize(0);
    mtx.vsc_ccout.resize(0);
    signTx(mtx);

    //CValidationState txState;
    //assert(CheckTransactionWithoutProofVerification(mtx, txState));
    return CTransaction(mtx);
}

CTransaction SidechainTestSuite::createNewSidechainTxWithNoFwdTransfer(const uint256 & newScId)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, newScId);
    mtx.vout.resize(0);
    mtx.vjoinsplit.resize(0);
    mtx.vft_ccout.resize(0);
    signTx(mtx);

    //CValidationState txState;
    //assert(CheckTransactionWithoutProofVerification(mtx, txState));
    return CTransaction(mtx);
}

CTransaction SidechainTestSuite::createTransparentTx(bool ccIsNull) {
    CMutableTransaction mtx = populateTx(TRANSPARENT_TX_VERSION);
    mtx.vjoinsplit.resize(0);

    if (ccIsNull)
    {
        mtx.vsc_ccout.resize(0);
        mtx.vft_ccout.resize(0);
    }
    signTx(mtx);

    //CValidationState txState;
    //assert(CheckTransactionWithoutProofVerification(mtx, txState));
    return CTransaction(mtx);
}

CTransaction SidechainTestSuite::createSproutTx(bool ccIsNull)
{
    CMutableTransaction mtx;

    if (ccIsNull)
    {
        mtx = populateTx(PHGR_TX_VERSION);
        mtx.vsc_ccout.resize(0);
        mtx.vft_ccout.resize(0);
    } else
    {
        mtx = populateTx(SC_TX_VERSION);
    }
    signTx(mtx);

    //CValidationState txState;
    //assert(CheckTransactionWithoutProofVerification(mtx, txState));
    return CTransaction(mtx);
}

void  SidechainTestSuite::appendTxTo(CTransaction & tx, const uint256 & scId, const CAmount & amount) {
    CMutableTransaction mtx = tx;

    mtx.nVersion = SC_TX_VERSION;

    CTxScCreationOut aSidechainCreationTx;
    aSidechainCreationTx.scId = scId;
    mtx.vsc_ccout.push_back(aSidechainCreationTx);

    CTxForwardTransferOut aForwardTransferTx;
    aForwardTransferTx.scId = aSidechainCreationTx.scId;
    aForwardTransferTx.nValue = amount;
    mtx.vft_ccout.push_back(aForwardTransferTx);

    tx = mtx;
    return;
}

CBlockUndo SidechainTestSuite::createBlockUndoWith(const uint256 & scId, int height, CAmount amount)
{
    CBlockUndo retVal;
    std::map<int, CAmount> AmountPerHeight;
    AmountPerHeight[height] = amount;
    retVal.msc_iaundo[scId] = AmountPerHeight;

    return retVal;
}

CBlockUndo SidechainTestSuite::createEmptyBlockUndo()
{
    return CBlockUndo();
}

CMutableTransaction SidechainTestSuite::populateTx(int txVersion, const uint256 & newScId, const CAmount & fwdTxAmount) {
    CMutableTransaction mtx;
    mtx.nVersion = txVersion;

    mtx.vin.resize(2);
    mtx.vin[0].prevout.hash = uint256S("1");
    mtx.vin[0].prevout.n = 0;
    mtx.vin[1].prevout.hash = uint256S("2");
    mtx.vin[1].prevout.n = 0;

    mtx.vout.resize(2);
    mtx.vout[0].nValue = 0;
    mtx.vout[1].nValue = 0;

    mtx.vjoinsplit.push_back(
            JSDescription::getNewInstance(txVersion == GROTH_TX_VERSION));
    mtx.vjoinsplit.push_back(
            JSDescription::getNewInstance(txVersion == GROTH_TX_VERSION));
    mtx.vjoinsplit[0].nullifiers.at(0) = uint256S("0");
    mtx.vjoinsplit[0].nullifiers.at(1) = uint256S("1");
    mtx.vjoinsplit[1].nullifiers.at(0) = uint256S("2");
    mtx.vjoinsplit[1].nullifiers.at(1) = uint256S("3");

    mtx.vsc_ccout.resize(1);
    mtx.vsc_ccout[0].scId = newScId;

    mtx.vft_ccout.resize(1);
    mtx.vft_ccout[0].scId = mtx.vsc_ccout[0].scId;
    mtx.vft_ccout[0].nValue = fwdTxAmount;

    return mtx;
}

void SidechainTestSuite::signTx(CMutableTransaction& mtx) {
    // Generate an ephemeral keypair.
    uint256 joinSplitPubKey;
    unsigned char joinSplitPrivKey[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(joinSplitPubKey.begin(), joinSplitPrivKey);
    mtx.joinSplitPubKey = joinSplitPubKey;
    // Compute the correct hSig.
    // TODO: #966.
    static const uint256 one(uint256S("1"));
    // Empty output script.
    CScript scriptCode;
    CTransaction signTx(mtx);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);
    if (dataToBeSigned == one) {
        throw std::runtime_error("SignatureHash failed");
    }
    // Add the signature
    assert(crypto_sign_detached(&mtx.joinSplitSig[0], NULL, dataToBeSigned.begin(), 32, joinSplitPrivKey ) == 0);
}
