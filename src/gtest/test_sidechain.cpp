#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <consensus/validation.h>
#include <txmempool.h>
#include <undo.h>
#include <main.h>

class SidechainTestSuite: public ::testing::Test {

public:
    SidechainTestSuite() :
            sidechainManager(Sidechain::ScMgr::instance()), coinViewCache(),
            aBlock(), aTransaction(), anHeight(1789),
            txState(), aFeeRate(), aMemPool(aFeeRate){};

    ~SidechainTestSuite() {
        sidechainManager.reset();
    };

    void SetUp() override {
        SelectBaseParams(CBaseChainParams::REGTEST);
        SelectParams(CBaseChainParams::REGTEST);

        sidechainManager.initialUpdateFromDb(0, true, Sidechain::ScMgr::mock);
    };

    void TearDown() override {};

protected:
    //Subjects under test
    Sidechain::ScMgr&           sidechainManager;
    Sidechain::ScCoinsViewCache coinViewCache;

    //Helpers
    CBlock              aBlock;
    CTransaction        aTransaction;
    int                 anHeight;
    CValidationState    txState;

    CFeeRate   aFeeRate;
    CTxMemPool aMemPool;
    CBlockUndo aBlockUndo;

    void preFillSidechainsCollection();

    CTransaction createSidechainTxWith(const uint256 & newScId, const CAmount & fwdTxAmount);
    CTransaction createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount);

    CTransaction createSidechainTxWithNoFwdTransfer(const uint256 & newScId);
    CTransaction createTransparentTx(bool ccIsNull);
    CTransaction createSproutTx(bool ccIsNull);
    void         extendTransaction(CTransaction & tx, const uint256 & scId, const CAmount & amount);

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
    aTransaction = createTransparentTx(/*ccIsNull = */true);

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, TransparentNonCcNullTxsAreNotSemanticallyValid) {
    aTransaction = createTransparentTx(/*ccIsNull = */false);

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SproutCcNullTxsAreCurrentlySupported) {
    aTransaction = createSproutTx(/*ccIsNull = */true);

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, SproutNonCcNullTxsAreCurrentlySupported) {
    aTransaction = createSproutTx(/*ccIsNull = */false);

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithoutForwardTransferAreNotSemanticallyValid) {
    aTransaction = createSidechainTxWithNoFwdTransfer(uint256S("1492"));

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithPositiveForwardTransferAreSemanticallyValid) {
    aTransaction = createSidechainTxWith( uint256S("1492"), CAmount(1000));

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, SidechainCreationsWithTooLargePositiveForwardTransferAreNotSemanticallyValid) {
    aTransaction = createSidechainTxWith(uint256S("1492"), CAmount(MAX_MONEY +1));

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithZeroForwardTransferAreNotSemanticallyValid) {
    aTransaction = createSidechainTxWith(uint256S("1492"), CAmount(0));

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithNegativeForwardTransferNotAreSemanticallyValid) {
    aTransaction = createSidechainTxWith(uint256S("1492"), CAmount(-1));

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

//TODO: FROM HERE ONWARD ONLY TX SEMANTICALLY VALID SHOULD BE USED

TEST_F(SidechainTestSuite, NewScCreationsAreApplicableToState) {
    aTransaction = createSidechainTxWith(uint256S("1492"), CAmount(1953));

    //test
    bool res = sidechainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, DuplicatedScCreationsAreNotApplicableToState) {
    uint256 scId = uint256S("1492");
    aTransaction = createSidechainTxWith(scId, CAmount(1953));
    coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    CTransaction duplicatedTx = createSidechainTxWith(scId, CAmount(1815));

    //test
    bool res = sidechainManager.IsTxApplicableToState(duplicatedTx, &coinViewCache);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistingSCsAreApplicableToState) {
    uint256 scId = uint256S("1492");
    aTransaction = createSidechainTxWith(scId, CAmount(1953));
    coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    aTransaction = createFwdTransferTxWith(scId, CAmount(5));

    //test
    bool res = sidechainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistingSCsAreNotApplicableToState) {
    aTransaction = createFwdTransferTxWith(uint256S("1492"), CAmount(1815));

    //test
    bool res = sidechainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

    //checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////// IsTxAllowedInMempool /////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, ScCreationTxsAreAllowedInEmptyMemPool) {
    aTransaction = createSidechainTxWith(uint256S("1492"), CAmount(1953));

    //test
    bool res = sidechainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, NewScCreationTxsAreAllowedInMemPool) {
    aTransaction = createSidechainTxWith(uint256S("1987"), CAmount(1994));
    CTxMemPoolEntry memPoolEntry(aTransaction, CAmount(), GetTime(), double(0.0), anHeight);
    aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry);

    CTransaction aNewTx = createSidechainTxWith(uint256S("1991"), CAmount(5));

    //test
    bool res = sidechainManager.IsTxAllowedInMempool(aMemPool, aNewTx, txState);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, DuplicatedScCreationTxsAreNotAllowedInMemPool) {
    uint256 scId = uint256S("1987");

    aTransaction = createSidechainTxWith(scId, CAmount(10));
    CTxMemPoolEntry memPoolEntry(aTransaction, CAmount(), GetTime(), double(0.0), anHeight);
    aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry);

    CTransaction duplicatedTx = createSidechainTxWith(scId, CAmount(15));

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
    int scCreationHeight = 5;
    aTransaction = createSidechainTxWith(scId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight - 1;

    //test
    bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(coinViewCache.getScInfoMap().at(scId).balance < initialAmount)
        <<"resulting balance is "<<coinViewCache.getScInfoMap().at(scId).balance
        <<" while initial amount is "<<initialAmount;
}

TEST_F(SidechainTestSuite, InitialCoinsTransferModifiesScBalanceAtCoinMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    int scCreationHeight = 7;
    aTransaction = createSidechainTxWith(scId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight;

    //test
    bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(coinViewCache.getScInfoMap().at(scId).balance == initialAmount)
        <<"resulting balance is "<<coinViewCache.getScInfoMap().at(scId).balance
        <<" expected one is "<<initialAmount;
}

TEST_F(SidechainTestSuite, InitialCoinsTransferDoesNotModifyScBalanceAfterCoinsMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    int scCreationHeight = 11;
    aTransaction = createSidechainTxWith(scId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = anHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight + 1;

    //test
    bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

    //check
    EXPECT_FALSE(res);
    EXPECT_TRUE(coinViewCache.getScInfoMap().at(scId).balance < initialAmount)
        <<"resulting balance is "<<coinViewCache.getScInfoMap().at(scId).balance
        <<" while initial amount is "<<initialAmount;
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// RestoreImmatureBalances ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, RestoringFromUndoBlockAffectBalance) {
    //insert a sidechain
    uint256 newScId = uint256S("ca1985");
    CAmount initialAmount = 34;
    int scCreationHeight = 71;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //let balance mature
    int maturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    coinViewCache.ApplyMatureBalances(maturityHeight, aBlockUndo);
    CAmount scBalance = coinViewCache.getScInfoMap().at(newScId).balance;

    CAmount amountToUndo = 17;
    aBlockUndo = createBlockUndoWith(newScId,scCreationHeight,amountToUndo);

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exists";
    ASSERT_TRUE(scBalance == initialAmount) <<"Test context: initial coins to have matured";
    ASSERT_TRUE(amountToUndo <= scBalance)
         <<"Test context: not attempting to restore more than initial value";

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_TRUE(res);
    CAmount restoredBalance = coinViewCache.getScInfoMap().at(newScId).balance;
    EXPECT_TRUE(restoredBalance == scBalance - amountToUndo)
        <<"balance after restore is "<<restoredBalance<<" instead of"<< scBalance - amountToUndo;
}

TEST_F(SidechainTestSuite, YouCannotRestoreMoreCoinsThanAvailableBalance) {
    //insert a sidechain
    uint256 newScId = uint256S("ca1985");
    CAmount initialAmount = 34;
    int scCreationHeight = 1991;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //let balance mature
    int maturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    coinViewCache.ApplyMatureBalances(maturityHeight, aBlockUndo);
    CAmount scBalance = coinViewCache.getScInfoMap().at(newScId).balance;

    CAmount amountToUndo = 50;
    aBlockUndo = createBlockUndoWith(newScId,scCreationHeight,amountToUndo);

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exists";
    ASSERT_TRUE(scBalance == initialAmount) <<"Test context: initial coins to have matured";
    ASSERT_TRUE(amountToUndo > scBalance)
         <<"Test context: attempting to restore more than initial value";

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
    CAmount restoredBalance = coinViewCache.getScInfoMap().at(newScId).balance;
    EXPECT_TRUE(restoredBalance == scBalance)
        <<"balance after restore is "<<restoredBalance<<" instead of"<< scBalance;
}

TEST_F(SidechainTestSuite, RestoringBeforeBalanceMaturesHasNoEffects) {
    //insert a sidechain
    uint256 newScId = uint256S("ca1985");
    CAmount initialAmount = 34;
    int scCreationHeight = 71;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    CAmount scBalance = coinViewCache.getScInfoMap().at(newScId).balance;

    CAmount amountToUndo = 17;
    aBlockUndo = createBlockUndoWith(newScId,scCreationHeight,amountToUndo);

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exists";
    ASSERT_TRUE(scBalance == 0) <<"Test context: initial coins to have not matured";
    ASSERT_TRUE(amountToUndo != 0)
         <<"Test context: attempting to restore some non-zero coins";

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
    CAmount restoredBalance = coinViewCache.getScInfoMap().at(newScId).balance;
    EXPECT_TRUE(restoredBalance == 0)
        <<"balance after restore is "<<restoredBalance<<" instead of 0";
}

TEST_F(SidechainTestSuite, RestoringFromEmptyUndoBlockHasEffect) {
    //insert a sidechain
    uint256 newScId = uint256S("ca1985");
    CAmount initialAmount = 34;
    int scCreationHeight = 71;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //let balance mature
    int maturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    coinViewCache.ApplyMatureBalances(maturityHeight, aBlockUndo);
    CAmount scBalance = coinViewCache.getScInfoMap().at(newScId).balance;

    aBlockUndo = createEmptyBlockUndo();

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exists";
    ASSERT_TRUE(scBalance == initialAmount) <<"Test context: initial coins to have matured";
    ASSERT_TRUE(aBlockUndo.msc_iaundo.size() == 0)<<"Test context: an empty undo block";

    //test
    bool res = coinViewCache.RestoreImmatureBalances(anHeight, aBlockUndo);

    //checks
    EXPECT_TRUE(res);
    CAmount restoredBalance = coinViewCache.getScInfoMap().at(newScId).balance;
    EXPECT_TRUE(restoredBalance == scBalance)
        <<"balance after restore is "<<restoredBalance<<" instead of"<< scBalance;
}

TEST_F(SidechainTestSuite, YouCannotRestoreCoinsFromInexistentSc) {
    //insert a sidechain
    uint256 inexistentScId = uint256S("ca1985");
    int scCreationHeight = 71;

    CAmount amountToUndo = 10;
    aBlockUndo = createBlockUndoWith(inexistentScId,scCreationHeight,amountToUndo);

    //prerequisites
    ASSERT_FALSE(coinViewCache.sidechainExists(inexistentScId))<<"Test context: sc to be missing";

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// RevertTxOutputs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, RevertingScCreationTxRemovesTheSc) {
    //create sidechain to be rollbacked and register it
    uint256 newScId = uint256S("a1b2");
    CAmount initialAmount = 1;
    int scCreationHeight = 1;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //create fwd transaction to be rollbacked
    int initialAmountMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    Sidechain::ScInfo viewInfo = coinViewCache.getScInfoMap().at(newScId);

    int revertHeight = scCreationHeight;

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exist";
    ASSERT_TRUE(revertHeight == scCreationHeight)
        <<"Test context: attempting a revert on the height where sc creation tx was stored";
    ASSERT_TRUE(viewInfo.mImmatureAmounts.at(initialAmountMaturityHeight) == initialAmount)
        <<"Test context: an initial amount amenable to be reverted";

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, revertHeight);

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(coinViewCache.sidechainExists(newScId));
}

TEST_F(SidechainTestSuite, RevertingFwdTransferRemovesCoinsFromImmatureBalance) {
    //insert sidechain
    uint256 newScId = uint256S("a1b2");
    CAmount initialAmount = 1;
    int scCreationHeight = 1;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //create fwd transaction to be rollbacked
    CAmount fwdAmount = 7;
    int fwdTxHeight = 5;
    int fwdTxMaturityHeight = fwdTxHeight + Params().ScCoinsMaturity();
    aTransaction = createFwdTransferTxWith(newScId, fwdAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, fwdTxHeight);
    Sidechain::ScInfo viewInfo = coinViewCache.getScInfoMap().at(newScId);

    int revertHeight = fwdTxHeight;

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exist";
    ASSERT_TRUE(revertHeight == fwdTxHeight)
        <<"Test context: attempting a revert on the height where fwd tx was stored";
    ASSERT_TRUE(viewInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdAmount)
        <<"Test context: a fwd amount amenable to be reverted";

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, revertHeight);

    //checks
    EXPECT_TRUE(res);
    viewInfo = coinViewCache.getScInfoMap().at(newScId);
    EXPECT_TRUE(viewInfo.mImmatureAmounts.count(fwdTxMaturityHeight) == 0);
}

TEST_F(SidechainTestSuite, FwdTransferTxToUnexistingScCannotBeReverted) {
    uint256 unexistingScId = uint256S("a1b2");

    //create fwd transaction to be reverted
    CAmount fwdAmount = 999;
    aTransaction = createFwdTransferTxWith(unexistingScId, fwdAmount);

    //prerequisites
    ASSERT_FALSE(coinViewCache.sidechainExists(unexistingScId))
        <<"Test context: unexisting sideChain";

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, anHeight);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, ScCreationTxCannotBeRevertedIfScIsNotPreviouslyCreated) {
    uint256 unexistingScId = uint256S("a1b2");

    //create Sc transaction to be reverted
    aTransaction = createSidechainTxWithNoFwdTransfer(unexistingScId);

    //prerequisites
    ASSERT_FALSE(coinViewCache.sidechainExists(unexistingScId))
        <<"Test context: unexisint sideChain";

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, anHeight);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, RevertingAFwdTransferOnTheWrongHeightHasNoEffect) {
    //insert sidechain
    uint256 newScId = uint256S("a1b2");
    CAmount initialAmount = 1;
    int scCreationHeight = 1;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //create fwd transaction to be rollbacked
    CAmount fwdAmount = 7;
    int fwdTxHeight = 5;
    int fwdTxMaturityHeight = fwdTxHeight + Params().ScCoinsMaturity();
    aTransaction = createFwdTransferTxWith(newScId, fwdAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, fwdTxHeight);
    Sidechain::ScInfo viewInfo = coinViewCache.getScInfoMap().at(newScId);

    int revertHeight = fwdTxHeight -1;

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exist";
    ASSERT_TRUE(revertHeight != fwdTxHeight)
        <<"Test context: attempting a revert on the height where fwd tx was stored";
    ASSERT_TRUE(viewInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdAmount)
        <<"Test context: a fwd amount amenable to be reverted";

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, revertHeight);

    //checks
    EXPECT_FALSE(res);
    viewInfo = coinViewCache.getScInfoMap().at(newScId);
    EXPECT_TRUE(viewInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdAmount)
        <<"Immature amount is "<<viewInfo.mImmatureAmounts.at(fwdTxMaturityHeight)
        <<"instead of "<<fwdAmount;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// UpdateScInfo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, NewSCsAreRegistered) {
    uint256 newScId = uint256S("1492");
    aTransaction = createSidechainTxWith(newScId, CAmount(1));

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(coinViewCache.sidechainExists(newScId));
}

TEST_F(SidechainTestSuite, DuplicatedSCsAreRejected) {
    uint256 scId = uint256S("1492");
    aTransaction = createSidechainTxWith(scId, CAmount(1));
    coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    CTransaction duplicatedTx = createSidechainTxWith(scId, CAmount(999));

    //test
    bool res = coinViewCache.UpdateScInfo(duplicatedTx, aBlock, anHeight);

    //check
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, NoRollbackIsPerformedOnceInvalidTransactionIsEncountered) {
    uint256 firstScId = uint256S("1492");
    uint256 secondScId = uint256S("1912");
    aTransaction = createSidechainTxWith(firstScId, CAmount(10));
    extendTransaction(aTransaction, firstScId, CAmount(20));
    extendTransaction(aTransaction, secondScId, CAmount(30));

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //check
    EXPECT_FALSE(res);
    EXPECT_TRUE(coinViewCache.sidechainExists(firstScId));
    EXPECT_FALSE(coinViewCache.sidechainExists(secondScId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistentSCsAreRejected) {
    uint256 nonExistentId = uint256S("1492");
    aTransaction = createFwdTransferTxWith(nonExistentId, CAmount(10));

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //check
    EXPECT_FALSE(res);
    EXPECT_FALSE(coinViewCache.sidechainExists(nonExistentId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistentSCsAreRegistered) {
    uint256 newScId = uint256S("1492");
    aTransaction = createSidechainTxWith(newScId, CAmount(5));
    coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    aTransaction = createFwdTransferTxWith(newScId, CAmount(15));

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //check
    EXPECT_TRUE(res);
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// Flush /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, FlushAlignsPersistedTxsWithViewOnes) {
    aTransaction = createSidechainTxWith(uint256S("a1b2"), CAmount(1));
    coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //test
    bool res = coinViewCache.Flush();

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainManager.getScInfoMap() == coinViewCache.getScInfoMap());
}

TEST_F(SidechainTestSuite, UponViewCreationAllPersistedTxsAreLoaded) {
    preFillSidechainsCollection();

    //test
    Sidechain::ScCoinsViewCache newView;

    //check
    EXPECT_TRUE(sidechainManager.getScInfoMap() == newView.getScInfoMap());
}

TEST_F(SidechainTestSuite, FlushPersistsNewSidechains) {
    uint256 scId = uint256S("a1b2");
    aTransaction = createSidechainTxWith(scId, CAmount(1000));
    coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //test
    bool res = coinViewCache.Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainManager.sidechainExists(scId));
}

TEST_F(SidechainTestSuite, FlushPersistsForwardTransfers) {
    uint256 scId = uint256S("a1b2");
    CAmount initialFwdTxAmount = 1;
    int scCreationHeight = 1;
    aTransaction = createSidechainTxWith(scId, CAmount(1));
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

    Sidechain::ScInfo persistedInfo = sidechainManager.getScInfoMap().at(scId);
    ASSERT_TRUE(persistedInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdTxAmount)
        <<"Following flush, persisted fwd amount should equal the one in view";
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
    bool res = sidechainManager.initialUpdateFromDb(size_t(0), false, Sidechain::ScMgr::mock);

    //Checks
    EXPECT_FALSE(res) << "Db double initialization should be forbidden";
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////// Test Fixture definitions ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
void SidechainTestSuite::preFillSidechainsCollection() {
    Sidechain::ScInfoMap & rManagerInternalMap
        = const_cast<Sidechain::ScInfoMap&>(sidechainManager.getScInfoMap());

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

CTransaction SidechainTestSuite::createSidechainTxWith(const uint256 & newScId, const CAmount & fwdTxAmount)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, newScId, fwdTxAmount);
    mtx.vout.resize(0);
    mtx.vjoinsplit.resize(0);
    signTx(mtx);

    assert(CheckTransactionWithoutProofVerification(mtx, txState));
    return CTransaction(mtx);
}

CTransaction SidechainTestSuite::createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, newScId, fwdTxAmount);
    mtx.vout.resize(0);
    mtx.vjoinsplit.resize(0);
    mtx.vsc_ccout.resize(0);
    signTx(mtx);

    assert(CheckTransactionWithoutProofVerification(mtx, txState));
    return CTransaction(mtx);
}

CTransaction SidechainTestSuite::createSidechainTxWithNoFwdTransfer(const uint256 & newScId)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, newScId);
    mtx.vout.resize(0);
    mtx.vjoinsplit.resize(0);
    mtx.vft_ccout.resize(0);
    signTx(mtx);

    assert(CheckTransactionWithoutProofVerification(mtx, txState));
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

    assert(CheckTransactionWithoutProofVerification(mtx, txState));
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

    assert(CheckTransactionWithoutProofVerification(mtx, txState));
    return CTransaction(mtx);
}

void  SidechainTestSuite::extendTransaction(CTransaction & tx, const uint256 & scId, const CAmount & amount) {
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
