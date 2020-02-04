#include <gtest/gtest.h>
#include "tx_creation_utils.h"
#include <sc/sidechain.h>
#include <boost/filesystem.hpp>
#include <txdb.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <txmempool.h>
#include <undo.h>
#include <main.h>

class CInMemorySidechainDb final: public CCoinsView {
public:
    CInMemorySidechainDb()  = default;
    ~CInMemorySidechainDb() = default;

    bool HaveScInfo(const uint256& scId) const { return inMemoryMap.count(scId); }
    bool GetScInfo(const uint256& scId, ScInfo& info) const {
        if(!inMemoryMap.count(scId))
            return false;
        info = inMemoryMap[scId];
        return true;
    }

    virtual void queryScIds(std::set<uint256>& scIdsList) const {
        for (auto& entry : inMemoryMap)
            scIdsList.insert(entry.first);
        return;
    }

    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap)
    {
        for (auto& entry : sidechainMap)
            switch (entry.second.flag) {
                case CSidechainsCacheEntry::Flags::FRESH:
                case CSidechainsCacheEntry::Flags::DIRTY:
                    inMemoryMap[entry.first] = entry.second.scInfo;
                    break;
                case CSidechainsCacheEntry::Flags::ERASED:
                    inMemoryMap.erase(entry.first);
                    break;
                case CSidechainsCacheEntry::Flags::DEFAULT:
                    break;
                default:
                    return false;
            }
        sidechainMap.clear();
        return true;
    }

private:
    mutable boost::unordered_map<uint256, ScInfo, ObjectHasher> inMemoryMap;
};

class SidechainTestSuite: public ::testing::Test {

public:
    SidechainTestSuite():
          fakeChainStateDb(nullptr)
        , sidechainsView(nullptr) {};

    ~SidechainTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        fakeChainStateDb   = new CInMemorySidechainDb();
        sidechainsView     = new CCoinsViewCache(fakeChainStateDb);
    };

    void TearDown() override {
        delete sidechainsView;
        sidechainsView = nullptr;

        delete fakeChainStateDb;
        fakeChainStateDb = nullptr;
    };

protected:
    CInMemorySidechainDb            *fakeChainStateDb;
    CCoinsViewCache                 *sidechainsView;

    //Helpers
    CBlockUndo   createBlockUndoWith(const uint256 & scId, int height, CAmount amount);
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
    bool res = sidechainsView->HaveDependencies(aTransaction);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, DuplicatedScCreationsHaveNotTheRightDependencies) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1953));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    CTransaction duplicatedTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1815));

    //test
    bool res = sidechainsView->HaveDependencies(duplicatedTx);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistingSCsHaveTheRightDependencies) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1953));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(5));

    //test
    bool res = sidechainsView->HaveDependencies(aTransaction);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistingSCsHaveNotTheRightDependencies) {
    CTransaction aTransaction = txCreationUtils::createFwdTransferTxWith(uint256S("1492"), CAmount(1815));

    //test
    bool res = sidechainsView->HaveDependencies(aTransaction);

    //checks
    EXPECT_FALSE(res);
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
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight - 1;
    CBlockUndo anEmptyBlockUndo;

    //test
    bool res = sidechainsView->ApplyMatureBalances(lookupBlockHeight, anEmptyBlockUndo);

    //check
    EXPECT_TRUE(res);

    sidechainsView->Flush();
    ScInfo mgrInfos;
    ASSERT_TRUE(fakeChainStateDb->GetScInfo(scId, mgrInfos));
    EXPECT_TRUE(mgrInfos.balance < initialAmount)
        <<"resulting balance is "<<mgrInfos.balance <<" while initial amount is "<<initialAmount;
}

TEST_F(SidechainTestSuite, InitialCoinsTransferModifiesScBalanceAtCoinMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, initialAmount);
    int scCreationHeight = 7;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight;
    CBlockUndo anEmptyBlockUndo;

    //test
    bool res = sidechainsView->ApplyMatureBalances(lookupBlockHeight, anEmptyBlockUndo);

    //checks
    EXPECT_TRUE(res);

    sidechainsView->Flush();
    ScInfo mgrInfos;
    ASSERT_TRUE(fakeChainStateDb->GetScInfo(scId, mgrInfos));
    EXPECT_TRUE(mgrInfos.balance == initialAmount)
        <<"resulting balance is "<<mgrInfos.balance <<" expected one is "<<initialAmount;
}

TEST_F(SidechainTestSuite, InitialCoinsTransferDoesNotModifyScBalanceAfterCoinsMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, initialAmount);
    int scCreationHeight = 11;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = /*height*/int(1789) + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight + 1;
    CBlockUndo anEmptyBlockUndo;

    //test
    bool res = sidechainsView->ApplyMatureBalances(lookupBlockHeight, anEmptyBlockUndo);

    //check
    EXPECT_FALSE(res);

    sidechainsView->Flush();
    ScInfo mgrInfos;
    ASSERT_TRUE(fakeChainStateDb->GetScInfo(scId, mgrInfos));
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
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    CBlockUndo aBlockUndo;

    sidechainsView->ApplyMatureBalances(scCreationHeight + Params().ScCoinsMaturity(), aBlockUndo);

    ScInfo viewInfos;
    ASSERT_TRUE(sidechainsView->GetScInfo(scId, viewInfos));
    CAmount scBalance = viewInfos.balance;

    CAmount amountToUndo = 17;
    aBlockUndo = createBlockUndoWith(scId,scCreationHeight,amountToUndo);

    //test
    bool res = sidechainsView->RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(sidechainsView->GetScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.balance == scBalance - amountToUndo)
        <<"balance after restore is "<<viewInfos.balance
        <<" instead of"<< scBalance - amountToUndo;
}

TEST_F(SidechainTestSuite, YouCannotRestoreMoreCoinsThanAvailableBalance) {
    uint256 scId = uint256S("ca1985");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(34));
    int scCreationHeight = 1991;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    CBlockUndo aBlockUndo;

    sidechainsView->ApplyMatureBalances(scCreationHeight + Params().ScCoinsMaturity(), aBlockUndo);
    ScInfo viewInfos;
    ASSERT_TRUE(sidechainsView->GetScInfo(scId, viewInfos));
    CAmount scBalance = viewInfos.balance;

    aBlockUndo = createBlockUndoWith(scId,scCreationHeight,CAmount(50));

    //test
    bool res = sidechainsView->RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
    ASSERT_TRUE(sidechainsView->GetScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.balance == scBalance)
        <<"balance after restore is "<<viewInfos.balance <<" instead of"<< scBalance;
}

TEST_F(SidechainTestSuite, RestoringBeforeBalanceMaturesHasNoEffects) {
    uint256 scId = uint256S("ca1985");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(34));
    int scCreationHeight = 71;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    CBlockUndo aBlockUndo;

    sidechainsView->ApplyMatureBalances(scCreationHeight + Params().ScCoinsMaturity() -1, aBlockUndo);

    aBlockUndo = createBlockUndoWith(scId,scCreationHeight,CAmount(17));

    //test
    bool res = sidechainsView->RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
    ScInfo viewInfos;
    ASSERT_TRUE(sidechainsView->GetScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.balance == 0)
        <<"balance after restore is "<<viewInfos.balance <<" instead of 0";
}

TEST_F(SidechainTestSuite, YouCannotRestoreCoinsFromInexistentSc) {
    uint256 inexistentScId = uint256S("ca1985");
    int scCreationHeight = 71;

    CAmount amountToUndo = 10;

    CBlockUndo aBlockUndo = createBlockUndoWith(inexistentScId,scCreationHeight,amountToUndo);

    //test
    bool res = sidechainsView->RestoreImmatureBalances(scCreationHeight, aBlockUndo);

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
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //test
    bool res = sidechainsView->RevertTxOutputs(aTransaction, scCreationHeight);

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(sidechainsView->HaveScInfo(scId));
}

TEST_F(SidechainTestSuite, RevertingFwdTransferRemovesCoinsFromImmatureBalance) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    int scCreationHeight = 1;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int fwdTxHeight = 5;
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(7));
    sidechainsView->UpdateScInfo(aTransaction, aBlock, fwdTxHeight);

    //test
    bool res = sidechainsView->RevertTxOutputs(aTransaction, fwdTxHeight);

    //checks
    EXPECT_TRUE(res);
    ScInfo viewInfos;
    ASSERT_TRUE(sidechainsView->GetScInfo(scId, viewInfos));
    EXPECT_TRUE(viewInfos.mImmatureAmounts.count(fwdTxHeight + Params().ScCoinsMaturity()) == 0)
        <<"resulting immature amount is "<< viewInfos.mImmatureAmounts.count(fwdTxHeight + Params().ScCoinsMaturity());
}

TEST_F(SidechainTestSuite, ScCreationTxCannotBeRevertedIfScIsNotPreviouslyCreated) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("a1b2"),CAmount(15));

    //test
    bool res = sidechainsView->RevertTxOutputs(aTransaction, /*height*/int(1789));

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, FwdTransferTxToUnexistingScCannotBeReverted) {
    CTransaction aTransaction = txCreationUtils::createFwdTransferTxWith(uint256S("a1b2"), CAmount(999));

    //test
    bool res = sidechainsView->RevertTxOutputs(aTransaction, /*height*/int(1789));

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, RevertingAFwdTransferOnTheWrongHeightHasNoEffect) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    int scCreationHeight = 1;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int fwdTxHeight = 5;
    CAmount fwdAmount = 7;
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    sidechainsView->UpdateScInfo(aTransaction, aBlock, fwdTxHeight);

    //test
    int faultyHeight = fwdTxHeight -1;
    bool res = sidechainsView->RevertTxOutputs(aTransaction, faultyHeight);

    //checks
    EXPECT_FALSE(res);
    ScInfo viewInfos;
    ASSERT_TRUE(sidechainsView->GetScInfo(scId, viewInfos));
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
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainsView->HaveScInfo(newScId));
}

TEST_F(SidechainTestSuite, DuplicatedSCsAreRejected) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    CTransaction duplicatedTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(999));

    //test
    bool res = sidechainsView->UpdateScInfo(duplicatedTx, aBlock, /*height*/int(1789));

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
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
    EXPECT_TRUE(sidechainsView->HaveScInfo(firstScId));
    EXPECT_FALSE(sidechainsView->HaveScInfo(secondScId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistentSCsAreRejected) {
    uint256 nonExistentId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createFwdTransferTxWith(nonExistentId, CAmount(10));
    CBlock aBlock;

    //test
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
    EXPECT_FALSE(sidechainsView->HaveScInfo(nonExistentId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistentSCsAreRegistered) {
    uint256 newScId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(newScId, CAmount(5));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    aTransaction = txCreationUtils::createFwdTransferTxWith(newScId, CAmount(15));

    //test
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_TRUE(res);
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// BatchWrite ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, FRESHSidechainsGetWrittenInBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    uint256 scId = uint256S("aaaa");
    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    entry.scInfo = ScInfo();
    entry.flag   = CSidechainsCacheEntry::Flags::FRESH;

    mapToWrite[scId] = entry;

    //write new sidechain when backing view doesn't know about it
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainsView->HaveScInfo(scId));
}

TEST_F(SidechainTestSuite, FRESHSidechainsCanBeWrittenOnlyIfUnknownToBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    //Prefill backing cache with sidechain
    uint256 scId = uint256S("aaaa");
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    //attempt to write new sidechain when backing view already knows about it
    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    entry.scInfo = ScInfo();
    entry.flag   = CSidechainsCacheEntry::Flags::FRESH;

    mapToWrite[scId] = entry;

    ASSERT_DEATH(sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite),"");
}

TEST_F(SidechainTestSuite, DIRTYSidechainsAreStoredInBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    uint256 scId = uint256S("aaaa");
    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    entry.scInfo = ScInfo();
    entry.flag   = CSidechainsCacheEntry::Flags::FRESH;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view doesn't know about it
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainsView->HaveScInfo(scId));
}

TEST_F(SidechainTestSuite, DIRTYSidechainsUpdatesDirtyOnesInBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    uint256 scId = uint256S("aaaa");
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    ScInfo updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::DIRTY;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view already knows about it
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite);

    //checks
    EXPECT_TRUE(res);
    ScInfo cachedSc;
    EXPECT_TRUE(sidechainsView->GetScInfo(scId, cachedSc));
    EXPECT_TRUE(cachedSc.balance == CAmount(12) );
}

TEST_F(SidechainTestSuite, DIRTYSidechainsOverwriteErasedOnesInBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    //Create sidechain...
    uint256 scId = uint256S("aaaa");
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    //...then revert it to have it erased
    sidechainsView->RevertTxOutputs(scTx, /*nHeight*/1000);
    ASSERT_FALSE(sidechainsView->HaveScInfo(scId));

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    ScInfo updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::DIRTY;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view have it erased
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite);

    //checks
    EXPECT_TRUE(res);
    ScInfo cachedSc;
    EXPECT_TRUE(sidechainsView->GetScInfo(scId, cachedSc));
    EXPECT_TRUE(cachedSc.balance == CAmount(12) );
}

TEST_F(SidechainTestSuite, ERASEDSidechainsSetExistingOnesInBackingCacheasErased) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    uint256 scId = uint256S("aaaa");
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    ScInfo updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::ERASED;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view have it erased
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite);

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(sidechainsView->HaveScInfo(scId));
}

TEST_F(SidechainTestSuite, DEFAULTSidechainsCanBeWrittenInBackingCacheasOnlyIfUnchanged) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    uint256 scId = uint256S("aaaa");
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    ScInfo updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::DEFAULT;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view have it erased
    ASSERT_DEATH(sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite),"");
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// Flush /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, FlushPersistsNewSidechains) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1000));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //test
    bool res = sidechainsView->Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(fakeChainStateDb->HaveScInfo(scId));
}

TEST_F(SidechainTestSuite, FlushPersistsForwardTransfers) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1));
    int scCreationHeight = 1;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    sidechainsView->Flush();

    CAmount fwdTxAmount = 1000;
    int fwdTxHeght = scCreationHeight + 10;
    int fwdTxMaturityHeight = fwdTxHeght + Params().ScCoinsMaturity();
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(1000));
    sidechainsView->UpdateScInfo(aTransaction, aBlock, fwdTxHeght);

    //test
    bool res = sidechainsView->Flush();

    //checks
    EXPECT_TRUE(res);

    ScInfo persistedInfo;
    ASSERT_TRUE(fakeChainStateDb->GetScInfo(scId, persistedInfo));
    ASSERT_TRUE(persistedInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdTxAmount)
        <<"Following flush, persisted fwd amount should equal the one in view";
}

TEST_F(SidechainTestSuite, FlushPersistsScErasureToo) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));
    sidechainsView->Flush();

    sidechainsView->RevertTxOutputs(aTransaction, /*height*/int(1789));

    //test
    bool res = sidechainsView->Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(fakeChainStateDb->HaveScInfo(scId));
}

//TEST_F(SidechainTestSuite, FlushPropagatesErrorsInPersist) {
//    sidechainsDb->reset();
//    ASSERT_TRUE(sidechainsDb->initPersistence(new FaultyPersistance()));
//
//    uint256 scId = uint256S("a1b2");
//    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
//    CBlock aBlock;
//    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));
//
//    //test
//    bool res = sidechainsView->Flush();
//
//    //checks
//    EXPECT_FALSE(res);
//    EXPECT_FALSE(sidechainsDb->HaveScInfo(scId));
//}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// queryScIds //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, queryScIdsReturnsNonErasedSidechains) {
    CBlock aBlock;

    uint256 scId1 = uint256S("aaaa");
    int sc1CreationHeight(11);
    CTransaction scTx1 = txCreationUtils::createNewSidechainTxWith(scId1, CAmount(1));
    ASSERT_TRUE(sidechainsView->UpdateScInfo(scTx1, aBlock, sc1CreationHeight));
    ASSERT_TRUE(sidechainsView->Flush());

    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId1, CAmount(3));
    int fwdTxHeight(22);
    sidechainsView->UpdateScInfo(fwdTx, aBlock, fwdTxHeight);

    uint256 scId2 = uint256S("bbbb");
    int sc2CreationHeight(33);
    CTransaction scTx2 = txCreationUtils::createNewSidechainTxWith(scId2, CAmount(2));
    ASSERT_TRUE(sidechainsView->UpdateScInfo(scTx2, aBlock,sc2CreationHeight));
    ASSERT_TRUE(sidechainsView->Flush());

    ASSERT_TRUE(sidechainsView->RevertTxOutputs(scTx2, sc2CreationHeight));

    //test
    std::set<uint256> knownScIdsSet;
    sidechainsView->queryScIds(knownScIdsSet);

    //check
    EXPECT_TRUE(knownScIdsSet.size() == 1)<<"Instead knowScIdSet size is "<<knownScIdsSet.size();
    EXPECT_TRUE(knownScIdsSet.count(scId1) == 1)<<"Actual count is "<<knownScIdsSet.count(scId1);
    EXPECT_TRUE(knownScIdsSet.count(scId2) == 0)<<"Actual count is "<<knownScIdsSet.count(scId2);
}

TEST_F(SidechainTestSuite, queryScIdsOnChainstateDbSelectOnlySidechains) {

    //init a tmp chainstateDb
    boost::filesystem::path pathTemp(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path());
    const unsigned int      chainStateDbSize(2 * 1024 * 1024);
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();

    CCoinsViewDB chainStateDb(chainStateDbSize,/*fWipe*/true);
    sidechainsView->SetBackend(chainStateDb);

    //prepare a sidechain
    CBlock aBlock;
    uint256 scId = uint256S("aaaa");
    int sc1CreationHeight(11);
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1));
    ASSERT_TRUE(sidechainsView->UpdateScInfo(scTx, aBlock, sc1CreationHeight));

    //prepare a coin
    CCoinsCacheEntry aCoin;
    aCoin.flags = CCoinsCacheEntry::FRESH | CCoinsCacheEntry::DIRTY;
    aCoin.coins.fCoinBase = false;
    aCoin.coins.nVersion = TRANSPARENT_TX_VERSION;
    aCoin.coins.vout.resize(1);
    aCoin.coins.vout[0].nValue = CAmount(10);

    CCoinsMap mapCoins;
    mapCoins[uint256S("aaaa")] = aCoin;
    CAnchorsMap    emptyAnchorsMap;
    CNullifiersMap emptyNullifiersMap;
    CSidechainsMap emptySidechainsMap;

    sidechainsView->BatchWrite(mapCoins, uint256(), uint256(), emptyAnchorsMap, emptyNullifiersMap, emptySidechainsMap);

    //flush both the coin and the sidechain to the tmp chainstatedb
    ASSERT_TRUE(sidechainsView->Flush());

    //test
    std::set<uint256> knownScIdsSet;
    sidechainsView->queryScIds(knownScIdsSet);

    //check
    EXPECT_TRUE(knownScIdsSet.size() == 1)<<"Instead knowScIdSet size is "<<knownScIdsSet.size();
    EXPECT_TRUE(knownScIdsSet.count(scId) == 1)<<"Actual count is "<<knownScIdsSet.count(scId);

    ClearDatadirCache();
    boost::system::error_code ec;
    boost::filesystem::remove_all(pathTemp.string(), ec);
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////// Test Fixture definitions ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
CBlockUndo SidechainTestSuite::createBlockUndoWith(const uint256 & scId, int height, CAmount amount)
{
    CBlockUndo retVal;
    std::map<int, CAmount> AmountPerHeight;
    AmountPerHeight[height] = amount;
    retVal.msc_iaundo[scId] = AmountPerHeight;

    return retVal;
}
