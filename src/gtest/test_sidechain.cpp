#include <gtest/gtest.h>
#include "tx_creation_utils.h"
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
    bool exists(const uint256& scId) {return inMemoryMap.count(scId);}
    bool read(const uint256& scId, Sidechain::ScInfo& info) {
        if(!inMemoryMap.count(scId))
            return false;
        info = inMemoryMap[scId];
        return true;
    }
    bool readAllKeys(std::set<uint256>& keysSet) {
        for (auto& entry : inMemoryMap)
            keysSet.insert(entry.first);
        return true;
    }
    bool persist(const uint256& scId, const Sidechain::ScInfo& info) {inMemoryMap[scId] = info; return true;}
    bool erase(const uint256& scId) {inMemoryMap.erase(scId); return true;}

    void dump_info() {return;}
private:
    boost::unordered_map<uint256, Sidechain::ScInfo, ObjectHasher> inMemoryMap;
};

class FaultyPersistance final : public Sidechain::PersistenceLayer {
public:
    FaultyPersistance() = default;
    ~FaultyPersistance() = default;
    bool exists(const uint256& scId) {return false; }
    bool read(const uint256& scId, Sidechain::ScInfo& info) {return false; }
    bool readAllKeys(std::set<uint256>& keysSet) {return false; }
    bool persist(const uint256& scId, const Sidechain::ScInfo& info) { return false; }
    bool erase(const uint256& scId) { return false; }
    void dump_info() {return;}
private:
    boost::unordered_map<uint256, Sidechain::ScInfo, ObjectHasher> inMemoryMap;
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

        ASSERT_TRUE(sidechainManager.initPersistence(new FakePersistance()));
    };

    void TearDown() override {};

protected:
    //Subjects under test
    Sidechain::ScMgr&           sidechainManager;
    Sidechain::ScCoinsViewCache coinViewCache;

    //Helpers
    void preFillSidechainsCollection();
    CBlockUndo   createBlockUndoWith(const uint256 & scId, int height, CAmount amount);
    CBlockUndo   createEmptyBlockUndo();
};

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// checkTxSemanticValidity ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, TransparentCcNullTxsAreSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createTransparentTx(/*ccIsNull = */true);
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, TransparentNonCcNullTxsAreNotSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createTransparentTx(/*ccIsNull = */false);
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SproutCcNullTxsAreCurrentlySupported) {
    CTransaction aTransaction = txCreationUtils::createSproutTx(/*ccIsNull = */true);
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, SproutNonCcNullTxsAreCurrentlySupported) {
    CTransaction aTransaction = txCreationUtils::createSproutTx(/*ccIsNull = */false);
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithoutForwardTransferAreNotSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWithNoFwdTransfer(uint256S("1492"));
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithPositiveForwardTransferAreSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith( uint256S("1492"), CAmount(1000));
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, SidechainCreationsWithTooLargePositiveForwardTransferAreNotSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("1492"), CAmount(MAX_MONEY +1));
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithZeroForwardTransferAreNotSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("1492"), CAmount(0));
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithNegativeForwardTransferNotAreSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("1492"), CAmount(-1));
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, FwdTransferCumulatedAmountDoesNotOverFlow) {
    uint256 scId = uint256S("1492");
    CAmount initialFwdTrasfer(1);
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, initialFwdTrasfer);
    txCreationUtils::extendTransaction(aTransaction, scId, MAX_MONEY);
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// HaveDependencies //////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, NewScCreationsHaveTheRightDependencies) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("1492"), CAmount(1953));

    //test
    bool res = coinViewCache.HaveDependencies(aTransaction);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, DuplicatedScCreationsHaveNotTheRightDependencies) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1953));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    CTransaction duplicatedTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1815));

    //test
    bool res = coinViewCache.HaveDependencies(duplicatedTx);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistingSCsHaveTheRightDependencies) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1953));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(5));

    //test
    bool res = coinViewCache.HaveDependencies(aTransaction);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistingSCsHaveNotTheRightDependencies) {
    CTransaction aTransaction = txCreationUtils::createFwdTransferTxWith(uint256S("1492"), CAmount(1815));

    //test
    bool res = coinViewCache.HaveDependencies(aTransaction);

    //checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////////// existsInMempool ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, ScCreationTxsAreAllowedInEmptyMemPool) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("1492"), CAmount(1953));
    CValidationState txState;
    CFeeRate   aFeeRate;
    CTxMemPool aMemPool(aFeeRate);

    //test
    bool res = Sidechain::existsInMempool(aMemPool, aTransaction, txState);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, NewScCreationTxsAreAllowedInMemPool) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("1987"), CAmount(1994));
    CTxMemPoolEntry memPoolEntry(aTransaction, CAmount(), GetTime(), double(0.0), /*height*/int(1789));
    CFeeRate   aFeeRate;
    CTxMemPool aMemPool(aFeeRate);
    aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry);

    CTransaction aNewTx = txCreationUtils::createNewSidechainTxWith(uint256S("1991"), CAmount(5));
    CValidationState txState;

    //test
    bool res = Sidechain::existsInMempool(aMemPool, aNewTx, txState);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, DuplicatedScCreationTxsAreNotAllowedInMemPool) {
    uint256 scId = uint256S("1987");

    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CTxMemPoolEntry memPoolEntry(aTransaction, CAmount(), GetTime(), double(0.0), /*height*/int(1789));
    CFeeRate   aFeeRate;
    CTxMemPool aMemPool(aFeeRate);
    aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry);

    CTransaction duplicatedTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(15));
    CValidationState txState;

    //test
    bool res = Sidechain::existsInMempool(aMemPool, aTransaction, txState);

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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, initialAmount);
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
    ASSERT_TRUE(sidechainManager.GetScInfo(scId, mgrInfos));
    EXPECT_TRUE(mgrInfos.balance < initialAmount)
        <<"resulting balance is "<<mgrInfos.balance <<" while initial amount is "<<initialAmount;
}

TEST_F(SidechainTestSuite, InitialCoinsTransferModifiesScBalanceAtCoinMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, initialAmount);
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
    ASSERT_TRUE(sidechainManager.GetScInfo(scId, mgrInfos));
    EXPECT_TRUE(mgrInfos.balance == initialAmount)
        <<"resulting balance is "<<mgrInfos.balance <<" expected one is "<<initialAmount;
}

TEST_F(SidechainTestSuite, InitialCoinsTransferDoesNotModifyScBalanceAfterCoinsMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, initialAmount);
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
    ASSERT_TRUE(sidechainManager.GetScInfo(scId, mgrInfos));
    EXPECT_TRUE(mgrInfos.balance < initialAmount)
        <<"resulting balance is "<<mgrInfos.balance <<" while initial amount is "<<initialAmount;
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// RestoreImmatureBalances ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, RestoreImmatureBalancesAffectsScBalance) {
    uint256 scId = uint256S("ca1985");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(34));
    int scCreationHeight = 71;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    CBlockUndo aBlockUndo = createEmptyBlockUndo();

    coinViewCache.ApplyMatureBalances(scCreationHeight + Params().ScCoinsMaturity(), aBlockUndo);

    Sidechain::ScInfo viewInfos;
    ASSERT_TRUE(coinViewCache.GetScInfo(scId, viewInfos));
    CAmount scBalance = viewInfos.balance;

    CAmount amountToUndo = 17;
    aBlockUndo = createBlockUndoWith(scId,scCreationHeight,amountToUndo);

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(coinViewCache.GetScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.balance == scBalance - amountToUndo)
        <<"balance after restore is "<<viewInfos.balance
        <<" instead of"<< scBalance - amountToUndo;
}

TEST_F(SidechainTestSuite, YouCannotRestoreMoreCoinsThanAvailableBalance) {
    uint256 scId = uint256S("ca1985");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(34));
    int scCreationHeight = 1991;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    CBlockUndo aBlockUndo = createEmptyBlockUndo();

    coinViewCache.ApplyMatureBalances(scCreationHeight + Params().ScCoinsMaturity(), aBlockUndo);
    Sidechain::ScInfo viewInfos;
    ASSERT_TRUE(coinViewCache.GetScInfo(scId, viewInfos));
    CAmount scBalance = viewInfos.balance;

    aBlockUndo = createBlockUndoWith(scId,scCreationHeight,CAmount(50));

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
    ASSERT_TRUE(coinViewCache.GetScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.balance == scBalance)
        <<"balance after restore is "<<viewInfos.balance <<" instead of"<< scBalance;
}

TEST_F(SidechainTestSuite, RestoringBeforeBalanceMaturesHasNoEffects) {
    uint256 scId = uint256S("ca1985");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(34));
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
    ASSERT_TRUE(coinViewCache.GetScInfo(scId, viewInfos));
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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    int scCreationHeight = 1;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int fwdTxHeight = 5;
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(7));
    coinViewCache.UpdateScInfo(aTransaction, aBlock, fwdTxHeight);

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, fwdTxHeight);

    //checks
    EXPECT_TRUE(res);
    Sidechain::ScInfo viewInfos;
    ASSERT_TRUE(coinViewCache.GetScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.mImmatureAmounts.count(fwdTxHeight + Params().ScCoinsMaturity()) == 0)
        <<"resulting immature amount is "<< viewInfos.mImmatureAmounts.count(fwdTxHeight + Params().ScCoinsMaturity());
}

TEST_F(SidechainTestSuite, ScCreationTxCannotBeRevertedIfScIsNotPreviouslyCreated) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("a1b2"),CAmount(15));

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, /*height*/int(1789));

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, FwdTransferTxToUnexistingScCannotBeReverted) {
    CTransaction aTransaction = txCreationUtils::createFwdTransferTxWith(uint256S("a1b2"), CAmount(999));

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, /*height*/int(1789));

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, RevertingAFwdTransferOnTheWrongHeightHasNoEffect) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    int scCreationHeight = 1;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int fwdTxHeight = 5;
    CAmount fwdAmount = 7;
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, fwdTxHeight);

    //test
    int faultyHeight = fwdTxHeight -1;
    bool res = coinViewCache.RevertTxOutputs(aTransaction, faultyHeight);

    //checks
    EXPECT_FALSE(res);
    Sidechain::ScInfo viewInfos;
    ASSERT_TRUE(coinViewCache.GetScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.mImmatureAmounts.at(fwdTxHeight + Params().ScCoinsMaturity()) == fwdAmount)
        <<"Immature amount is "<<viewInfos.mImmatureAmounts.at(fwdTxHeight + Params().ScCoinsMaturity())
        <<"instead of "<<fwdAmount;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// UpdateScInfo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, NewSCsAreRegistered) {
    uint256 newScId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(newScId, CAmount(1));
    CBlock aBlock;

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(coinViewCache.sidechainExists(newScId));
}

TEST_F(SidechainTestSuite, DuplicatedSCsAreRejected) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    CTransaction duplicatedTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(999));

    //test
    bool res = coinViewCache.UpdateScInfo(duplicatedTx, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, NoRollbackIsPerformedOnceInvalidTransactionIsEncountered) {
    uint256 firstScId = uint256S("1492");
    uint256 secondScId = uint256S("1912");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(firstScId, CAmount(10));
    CBlock aBlock;
    txCreationUtils::extendTransaction(aTransaction, firstScId, CAmount(20));
    txCreationUtils::extendTransaction(aTransaction, secondScId, CAmount(30));

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
    EXPECT_TRUE(coinViewCache.sidechainExists(firstScId));
    EXPECT_FALSE(coinViewCache.sidechainExists(secondScId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistentSCsAreRejected) {
    uint256 nonExistentId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createFwdTransferTxWith(nonExistentId, CAmount(10));
    CBlock aBlock;

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
    EXPECT_FALSE(coinViewCache.sidechainExists(nonExistentId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistentSCsAreRegistered) {
    uint256 newScId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(newScId, CAmount(5));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    aTransaction = txCreationUtils::createFwdTransferTxWith(newScId, CAmount(15));

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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1000));
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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1));
    int scCreationHeight = 1;
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    coinViewCache.Flush();

    CAmount fwdTxAmount = 1000;
    int fwdTxHeght = scCreationHeight + 10;
    int fwdTxMaturityHeight = fwdTxHeght + Params().ScCoinsMaturity();
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(1000));
    coinViewCache.UpdateScInfo(aTransaction, aBlock, fwdTxHeght);

    //test
    bool res = coinViewCache.Flush();

    //checks
    EXPECT_TRUE(res);

    Sidechain::ScInfo persistedInfo;
    ASSERT_TRUE(sidechainManager.GetScInfo(scId, persistedInfo));
    ASSERT_TRUE(persistedInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdTxAmount)
        <<"Following flush, persisted fwd amount should equal the one in view";
}

TEST_F(SidechainTestSuite, FlushPersistsScErasureToo) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    coinViewCache.UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //test
    bool res = coinViewCache.Flush();

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(sidechainManager.sidechainExists(scId));
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// queryScIds //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, queryScIdsReturnsNewSidechains) {
    CBlock aBlock;

    uint256 scId1 = uint256S("aaaa");
    int sc1CreationHeight(11);
    CTransaction scTx1 = txCreationUtils::createNewSidechainTxWith(scId1, CAmount(1));
    ASSERT_TRUE(coinViewCache.UpdateScInfo(scTx1, aBlock, sc1CreationHeight));
    ASSERT_TRUE(coinViewCache.Flush());

    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId1, CAmount(3));
    int fwdTxHeight(22);
    coinViewCache.UpdateScInfo(fwdTx, aBlock, fwdTxHeight);

    uint256 scId2 = uint256S("bbbb");
    int sc2CreationHeight(33);
    CTransaction scTx2 = txCreationUtils::createNewSidechainTxWith(scId2, CAmount(2));
    ASSERT_TRUE(coinViewCache.UpdateScInfo(scTx2, aBlock,sc2CreationHeight));
    ASSERT_TRUE(coinViewCache.Flush());

    ASSERT_TRUE(coinViewCache.RevertTxOutputs(scTx2, sc2CreationHeight));

    //test
    std::set<uint256> knownScIdsSet;
    coinViewCache.queryScIds(knownScIdsSet);

    //check
    EXPECT_TRUE(knownScIdsSet.size() == 1)<<"Instead knowScIdSet size is "<<knownScIdsSet.size();
    EXPECT_TRUE(knownScIdsSet.count(scId1) == 1)<<"Actual count is "<<knownScIdsSet.count(scId1);
    EXPECT_TRUE(knownScIdsSet.count(scId2) == 0)<<"Actual count is "<<knownScIdsSet.count(scId2);
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

    CTransaction tmpTx = txCreationUtils::createNewSidechainTxWith(uint256S("a123"), CAmount(10));
    CBlock tmpBlock;
    tmpCoinViewCache.UpdateScInfo(tmpTx, tmpBlock, /*height*/int(1992));
    tmpCoinViewCache.Flush();

    tmpTx = txCreationUtils::createNewSidechainTxWith(uint256S("b987"), CAmount(10));
    tmpCoinViewCache.UpdateScInfo(tmpTx, tmpBlock, /*height*/int(1993));
    tmpCoinViewCache.Flush();
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
