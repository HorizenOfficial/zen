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

class CBlockUndo_OldVersion
{
    public:
        std::vector<CTxUndo> vtxundo;
        uint256 old_tree_root;

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
            READWRITE(vtxundo);
            READWRITE(old_tree_root);
        }   
};



class CInMemorySidechainDb final: public CCoinsView {
public:
    CInMemorySidechainDb()  = default;
    virtual ~CInMemorySidechainDb() = default;

    bool HaveSidechain(const uint256& scId) const override { return inMemoryMap.count(scId); }
    bool GetSidechain(const uint256& scId, CSidechain& info) const override {
        if(!inMemoryMap.count(scId))
            return false;
        info = inMemoryMap[scId];
        return true;
    }

    virtual void GetScIds(std::set<uint256>& scIdsList) const override {
        for (auto& entry : inMemoryMap)
            scIdsList.insert(entry.first);
        return;
    }

    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap, CSidechainEventsMap& mapSidechainEvents) override
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
    mutable boost::unordered_map<uint256, CSidechain, ObjectHasher> inMemoryMap;
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
    CBlockUndo   createBlockUndoWith(const uint256 & scId, int height, CAmount amount, uint256 lastCertHash = uint256());
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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(0));
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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(1000));
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, SidechainCreationsWithTooLargePositiveForwardTransferAreNotSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(MAX_MONEY +1));
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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(0));
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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(-1));
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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(initialFwdTrasfer);
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
////////////////////////////// HaveScRequirements /////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, NewScCreationsHaveTheRightDependencies) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(1953));

    //test
    bool res = sidechainsView->HaveScRequirements(aTransaction, int(1789));

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistingSCsHaveTheRightDependencies) {
    int creationHeight = 1789;
    chainSettingUtils::GenerateChainActive(creationHeight);
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(1953));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, creationHeight);

    const uint256& scId = aTransaction.GetScIdFromScCcOut(0);
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(5));

    //test
    bool res = sidechainsView->HaveScRequirements(aTransaction, creationHeight);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistingSCsHaveNotTheRightDependencies) {
    int fwdHeight = 1789;
    chainSettingUtils::GenerateChainActive(fwdHeight);

    CTransaction aTransaction = txCreationUtils::createFwdTransferTxWith(uint256S("1492"), CAmount(1815));

    //test
    bool res = sidechainsView->HaveScRequirements(aTransaction, fwdHeight);

    //checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// RevertTxOutputs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, RevertingScCreationTxRemovesTheSc) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = aTransaction.GetScIdFromScCcOut(0);
    int scCreationHeight = 1;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //test
    bool res = sidechainsView->RevertTxOutputs(aTransaction, scCreationHeight + getScCoinsMaturity());

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(sidechainsView->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, RevertingFwdTransferRemovesCoinsFromImmatureBalance) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = aTransaction.GetScIdFromScCcOut(0);
    int scCreationHeight = 1;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int fwdTxHeight = 5;
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(7));
    sidechainsView->UpdateScInfo(aTransaction, aBlock, fwdTxHeight);

    //test
    bool res = sidechainsView->RevertTxOutputs(aTransaction, fwdTxHeight + getScCoinsMaturity());

    //checks
    EXPECT_TRUE(res);
    CSidechain viewInfos;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, viewInfos));
    EXPECT_TRUE(viewInfos.mImmatureAmounts.count(fwdTxHeight + Params().ScCoinsMaturity()) == 0)
        <<"resulting immature amount is "<< viewInfos.mImmatureAmounts.count(fwdTxHeight + Params().ScCoinsMaturity());
}

TEST_F(SidechainTestSuite, ScCreationTxCannotBeRevertedIfScIsNotPreviouslyCreated) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(15));

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
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = aTransaction.GetScIdFromScCcOut(0);
    int scCreationHeight = 1;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int fwdTxHeight = 5;
    CAmount fwdAmount = 7;
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    sidechainsView->UpdateScInfo(aTransaction, aBlock, fwdTxHeight);

    //test
    int faultyHeight = fwdTxHeight + getScCoinsMaturity() -1;
    bool res = sidechainsView->RevertTxOutputs(aTransaction, faultyHeight);

    //checks
    EXPECT_FALSE(res);
    CSidechain viewInfos;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, viewInfos));
    EXPECT_TRUE(viewInfos.mImmatureAmounts.at(fwdTxHeight + Params().ScCoinsMaturity()) == fwdAmount)
        <<"Immature amount is "<<viewInfos.mImmatureAmounts.at(fwdTxHeight + Params().ScCoinsMaturity())
        <<"instead of "<<fwdAmount;
}

TEST_F(SidechainTestSuite, RevertCertOutputsRestoresLastCertHash) {
    //Create sidechain and mature it to generate first block undo
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(34));
    const uint256& scId = aTransaction.GetScIdFromScCcOut(0);
    int scCreationHeight = 71;
    CBlock dummyBlock;
    sidechainsView->UpdateScInfo(aTransaction, dummyBlock, scCreationHeight);
    CSidechain scInfoAtCreation;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, scInfoAtCreation));

    CBlockUndo dummyBlockUndo;
    for(const CTxScCreationOut& scCreationOut: aTransaction.GetVscCcOut())
        ASSERT_TRUE(sidechainsView->ScheduleSidechainEvent(scCreationOut, scCreationHeight));

    std::vector<uint256> dummy;
    ASSERT_TRUE(sidechainsView->HandleSidechainEvents(scCreationHeight + Params().ScCoinsMaturity(), dummyBlockUndo, &dummy));


    //Update sc with cert and create the associate blockUndo
    int certEpoch = 19;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certEpoch, dummyBlock.GetHash());
    CTxUndo certUndoEntry;
    sidechainsView->UpdateScInfo(cert, certUndoEntry);
    CSidechain scInfoPostCert;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, scInfoPostCert));
    EXPECT_TRUE(scInfoPostCert.lastEpochReferencedByCertificate == certEpoch);
    EXPECT_TRUE(scInfoPostCert.lastCertificateHash == cert.GetHash());

    //test
    bool res = sidechainsView->RevertCertOutputs(cert,certUndoEntry);

    //checks
    EXPECT_TRUE(res);
    CSidechain scInfoPostCertUndo;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, scInfoPostCertUndo));
    EXPECT_TRUE(scInfoPostCertUndo.lastCertificateHash == scInfoAtCreation.lastCertificateHash);
    EXPECT_TRUE(scInfoPostCertUndo.lastEpochReferencedByCertificate == scInfoAtCreation.lastEpochReferencedByCertificate);
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// UpdateScInfo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, NewSCsAreRegistered) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(1));

    const uint256& scId = aTransaction.GetScIdFromScCcOut(0);
    CBlock aBlock;

    //test
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainsView->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, NoRollbackIsPerformedOnceInvalidTransactionIsEncountered) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    uint256 secondScId = uint256S("1912");
    CBlock aBlock;
    txCreationUtils::extendTransaction(aTransaction, secondScId, CAmount(30));
    const uint256& firstScId = aTransaction.GetScIdFromScCcOut(0);

    //test
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
    EXPECT_TRUE(sidechainsView->HaveSidechain(firstScId));
    EXPECT_FALSE(sidechainsView->HaveSidechain(secondScId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistentSCsAreRejected) {
    uint256 nonExistentId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createFwdTransferTxWith(nonExistentId, CAmount(10));
    CBlock aBlock;

    //test
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
    EXPECT_FALSE(sidechainsView->HaveSidechain(nonExistentId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistentSCsAreRegistered) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(5));
    const uint256& scId = aTransaction.GetScIdFromScCcOut(0);
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(15));

    //test
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, CertificateUpdatesLastCertificateHash) {
    //Create Sc
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(5));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyBlock;
    ASSERT_TRUE(sidechainsView->UpdateScInfo(scCreationTx, dummyBlock, scCreationHeight));

    CSidechain scInfo;
    EXPECT_TRUE(sidechainsView->GetSidechain(scId,scInfo));
    EXPECT_TRUE(scInfo.lastCertificateHash.IsNull());

    //Fully mature initial Sc balance
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        ASSERT_TRUE(sidechainsView->ScheduleSidechainEvent(scCreationOut, scCreationHeight));
    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    CBlockUndo dummyBlockUndo;
    std::vector<uint256> dummy;
    ASSERT_TRUE(sidechainsView->HandleSidechainEvents(coinMaturityHeight, dummyBlockUndo, &dummy));

    CTxUndo certUndoEntry;
    CScCertificate aCertificate = txCreationUtils::createCertificate(scId, /*epochNum*/0, dummyBlock.GetHash());
    EXPECT_TRUE(sidechainsView->UpdateScInfo(aCertificate, certUndoEntry));

    //check
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,scInfo));
    EXPECT_TRUE(scInfo.lastCertificateHash == aCertificate.GetHash());
    EXPECT_TRUE(certUndoEntry.replacedLastCertEpoch == -1);
    EXPECT_TRUE(certUndoEntry.replacedLastCertHash.IsNull());
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
    CSidechainEventsMap mapCeasingScs;


    uint256 scId = uint256S("aaaa");
    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    entry.scInfo = CSidechain();
    entry.flag   = CSidechainsCacheEntry::Flags::FRESH;

    mapToWrite[scId] = entry;

    //write new sidechain when backing view doesn't know about it
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite, mapCeasingScs);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainsView->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, FRESHSidechainsCanBeWrittenOnlyIfUnknownToBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;
    CSidechainEventsMap mapCeasingScs;


    //Prefill backing cache with sidechain
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    //attempt to write new sidechain when backing view already knows about it
    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    entry.scInfo = CSidechain();
    entry.flag   = CSidechainsCacheEntry::Flags::FRESH;

    mapToWrite[scId] = entry;

    ASSERT_DEATH(sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite,mapCeasingScs),"");
}

TEST_F(SidechainTestSuite, DIRTYSidechainsAreStoredInBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;
    CSidechainEventsMap mapCeasingScs;


    uint256 scId = uint256S("aaaa");
    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    entry.scInfo = CSidechain();
    entry.flag   = CSidechainsCacheEntry::Flags::FRESH;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view doesn't know about it
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite, mapCeasingScs);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainsView->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, DIRTYSidechainsUpdatesDirtyOnesInBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;
    CSidechainEventsMap mapCeasingScs;


    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    CSidechain updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::DIRTY;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view already knows about it
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite, mapCeasingScs);

    //checks
    EXPECT_TRUE(res);
    CSidechain cachedSc;
    EXPECT_TRUE(sidechainsView->GetSidechain(scId, cachedSc));
    EXPECT_TRUE(cachedSc.balance == CAmount(12) );
}

TEST_F(SidechainTestSuite, DIRTYSidechainsOverwriteErasedOnesInBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;
    CSidechainEventsMap mapCeasingScs;


    //Create sidechain...
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    //...then revert it to have it erased
    sidechainsView->RevertTxOutputs(scTx, /*nHeight*/1000 + getScCoinsMaturity());
    ASSERT_FALSE(sidechainsView->HaveSidechain(scId));

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    CSidechain updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::DIRTY;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view have it erased
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite, mapCeasingScs);

    //checks
    EXPECT_TRUE(res);
    CSidechain cachedSc;
    EXPECT_TRUE(sidechainsView->GetSidechain(scId, cachedSc));
    EXPECT_TRUE(cachedSc.balance == CAmount(12) );
}

TEST_F(SidechainTestSuite, ERASEDSidechainsSetExistingOnesInBackingCacheasErased) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;
    CSidechainEventsMap mapCeasingScs;

    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    CSidechain updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::ERASED;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view have it erased
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite, mapCeasingScs);

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(sidechainsView->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, DEFAULTSidechainsCanBeWrittenInBackingCacheasOnlyIfUnchanged) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;
    CSidechainEventsMap mapCeasingScs;

    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    CSidechain updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::DEFAULT;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view have it erased
    ASSERT_DEATH(sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite, mapCeasingScs),"");
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// Flush /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, FlushPersistsNewSidechains) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(1000));
    const uint256& scId = aTransaction.GetScIdFromScCcOut(0);
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //test
    bool res = sidechainsView->Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(fakeChainStateDb->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, FlushPersistsForwardTransfers) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256& scId = aTransaction.GetScIdFromScCcOut(0);
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

    CSidechain persistedInfo;
    ASSERT_TRUE(fakeChainStateDb->GetSidechain(scId, persistedInfo));
    ASSERT_TRUE(persistedInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdTxAmount)
        <<"Following flush, persisted fwd amount should equal the one in view";
}

TEST_F(SidechainTestSuite, FlushPersistsScErasureToo) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = aTransaction.GetScIdFromScCcOut(0);
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));
    sidechainsView->Flush();

    sidechainsView->RevertTxOutputs(aTransaction, /*height*/int(1789) + getScCoinsMaturity());

    //test
    bool res = sidechainsView->Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(fakeChainStateDb->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, FlushPersistsNewScsOnTopOfErasedOnes) {
    CBlock aBlock;

    //Create new sidechain and flush it
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    sidechainsView->UpdateScInfo(scCreationTx, aBlock, /*height*/int(1789));
    sidechainsView->Flush();
    ASSERT_TRUE(fakeChainStateDb->HaveSidechain(scId));

    //Remove it and flush again
    sidechainsView->RevertTxOutputs(scCreationTx, /*height*/int(1789) + getScCoinsMaturity());
    sidechainsView->Flush();
    ASSERT_FALSE(fakeChainStateDb->HaveSidechain(scId));

    //re-use sc with same scId as erased one
    CTransaction scReCreationTx = scCreationTx;
    sidechainsView->UpdateScInfo(scReCreationTx, aBlock, /*height*/int(1815));
    bool res = sidechainsView->Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(fakeChainStateDb->HaveSidechain(scId));
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// GetScIds //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, GetScIdsReturnsNonErasedSidechains) {
    CBlock aBlock;

    int sc1CreationHeight(11);
    CTransaction scTx1 = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    uint256 scId1 = scTx1.GetScIdFromScCcOut(0);
    ASSERT_TRUE(sidechainsView->UpdateScInfo(scTx1, aBlock, sc1CreationHeight));
    ASSERT_TRUE(sidechainsView->Flush());

    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId1, CAmount(3));
    int fwdTxHeight(22);
    sidechainsView->UpdateScInfo(fwdTx, aBlock, fwdTxHeight);

    int sc2CreationHeight(33);
    CTransaction scTx2 = txCreationUtils::createNewSidechainTxWith(CAmount(2));
    uint256 scId2 = scTx2.GetScIdFromScCcOut(0);
    ASSERT_TRUE(sidechainsView->UpdateScInfo(scTx2, aBlock,sc2CreationHeight));
    ASSERT_TRUE(sidechainsView->Flush());

    ASSERT_TRUE(sidechainsView->RevertTxOutputs(scTx2, sc2CreationHeight + getScCoinsMaturity()));

    //test
    std::set<uint256> knownScIdsSet;
    sidechainsView->GetScIds(knownScIdsSet);

    //check
    EXPECT_TRUE(knownScIdsSet.size() == 1)<<"Instead knowScIdSet size is "<<knownScIdsSet.size();
    EXPECT_TRUE(knownScIdsSet.count(scId1) == 1)<<"Actual count is "<<knownScIdsSet.count(scId1);
    EXPECT_TRUE(knownScIdsSet.count(scId2) == 0)<<"Actual count is "<<knownScIdsSet.count(scId2);
}

TEST_F(SidechainTestSuite, GetScIdsOnChainstateDbSelectOnlySidechains) {

    //init a tmp chainstateDb
    boost::filesystem::path pathTemp(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path());
    const unsigned int      chainStateDbSize(2 * 1024 * 1024);
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();

    CCoinsViewDB chainStateDb(chainStateDbSize,/*fWipe*/true);
    sidechainsView->SetBackend(chainStateDb);

    //prepare a sidechain
    CBlock aBlock;
    int sc1CreationHeight(11);
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
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
    CSidechainEventsMap mapCeasingScs;

    sidechainsView->BatchWrite(mapCoins, uint256(), uint256(), emptyAnchorsMap, emptyNullifiersMap, emptySidechainsMap, mapCeasingScs);

    //flush both the coin and the sidechain to the tmp chainstatedb
    ASSERT_TRUE(sidechainsView->Flush());

    //test
    std::set<uint256> knownScIdsSet;
    sidechainsView->GetScIds(knownScIdsSet);

    //check
    EXPECT_TRUE(knownScIdsSet.size() == 1)<<"Instead knowScIdSet size is "<<knownScIdsSet.size();
    EXPECT_TRUE(knownScIdsSet.count(scId) == 1)<<"Actual count is "<<knownScIdsSet.count(scId);

    ClearDatadirCache();
    boost::system::error_code ec;
    boost::filesystem::remove_all(pathTemp.string(), ec);
}
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// GetSidechain /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, CSidechainFromMempoolRetrievesUnconfirmedInformation) {
    CTxMemPool aMempool(CFeeRate(1));

    //Confirm a Sidechain
    CAmount creationAmount = 10;
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(creationAmount);
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    int scCreationHeight(11);
    CBlock aBlock;
    ASSERT_TRUE(sidechainsView->UpdateScInfo(scTx, aBlock, scCreationHeight));
    ASSERT_TRUE(sidechainsView->Flush());

    //Fully mature initial Sc balance
    CBlockUndo anEmptyBlockUndo;
    for(const CTxScCreationOut& scCreationOut: scTx.GetVscCcOut())
        ASSERT_TRUE(sidechainsView->ScheduleSidechainEvent(scCreationOut, scCreationHeight));
    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    CBlockUndo dummyBlockUndo;
    std::vector<uint256> dummy;
    ASSERT_TRUE(sidechainsView->HandleSidechainEvents(coinMaturityHeight, dummyBlockUndo, &dummy));

    //a fwd is accepted in mempool
    CAmount fwdAmount = 20;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    CTxMemPoolEntry fwdPoolEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdPoolEntry.GetTx().GetHash(), fwdPoolEntry);

    //a bwt cert is accepted in mempool too
    CAmount certAmount = 4;
    CMutableScCertificate cert;
    cert.scId = scId;
    CScript scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG;
    cert.addBwt(CTxOut(certAmount, scriptPubKey));

    CCertificateMemPoolEntry bwtPoolEntry(cert, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(bwtPoolEntry.GetCertificate().GetHash(), bwtPoolEntry);

    //test
    CCoinsViewMemPool viewMemPool(sidechainsView, aMempool);
    CSidechain retrievedInfo;
    viewMemPool.GetSidechain(scId, retrievedInfo);

    //check
    EXPECT_TRUE(retrievedInfo.creationBlockHeight == scCreationHeight);
    EXPECT_TRUE(retrievedInfo.balance == creationAmount - certAmount);
    EXPECT_TRUE(retrievedInfo.lastEpochReferencedByCertificate == -1); //certs in mempool do not affect lastEpochReferencedByCertificate
    EXPECT_TRUE(retrievedInfo.mImmatureAmounts.at(-1) == fwdAmount);
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// UndoBlock versioning /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, CSidechainBlockUndoVersioning) {

    static const std::string autofileName = "/tmp/test_block_undo_versioning.txt";
    CAutoFile fileout(fopen(autofileName.c_str(), "wb+") , SER_DISK, CLIENT_VERSION);
    EXPECT_TRUE(fileout.Get() != NULL);

    // write an old version undo block to the file
    //----------------------------------------------
    CBlockUndo_OldVersion buov;
    buov.vtxundo.reserve(1);
    buov.vtxundo.push_back(CTxUndo());

    fileout << buov;;

    uint256 h_buov;
    {
        CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
        hasher << buov;
        h_buov = hasher.GetHash();
    }
    fileout << h_buov;

    fseek(fileout.Get(), 0, SEEK_END);
    unsigned long len = (unsigned long)ftell(fileout.Get());

    unsigned long buov_sz = buov.GetSerializeSize(SER_DISK, CLIENT_VERSION);
    EXPECT_TRUE(len == buov_sz + sizeof(uint256));
    
    // write a new version undo block to the same file
    //-----------------------------------------------
    CBlockUndo buon;
    buon.vtxundo.reserve(1);
    buon.vtxundo.push_back(CTxUndo());

    fileout << buon;;

    uint256 h_buon;
    {
        CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
        hasher << buon;
        h_buon = hasher.GetHash();
    }
    fileout << h_buon;

    fseek(fileout.Get(), 0, SEEK_END);
    unsigned long len2 = (unsigned long)ftell(fileout.Get());

    unsigned long buon_sz = buon.GetSerializeSize(SER_DISK, CLIENT_VERSION);
    EXPECT_TRUE(len2 == len + buon_sz + sizeof(uint256));
    
    EXPECT_TRUE(buov_sz != buon_sz);

    fileout.fclose();

    // read both blocks and tell their version
    //-----------------------------------------------
    CAutoFile filein(fopen(autofileName.c_str(), "r+") , SER_DISK, CLIENT_VERSION);
    EXPECT_TRUE(filein.Get() != NULL);

    bool good_read = true;
    CBlockUndo b1, b2;
    uint256 h1, h2;
    try {
        filein >> b1;
        filein >> h1;
        filein >> b2;
        filein >> h2;
    }
    catch (const std::exception& e) {
        good_read = false;
    }

    EXPECT_TRUE(good_read == true);

    EXPECT_TRUE(b1.IncludesSidechainAttributes() == false);
    EXPECT_TRUE(h1 == h_buov);

    EXPECT_TRUE(b2.IncludesSidechainAttributes() == true);
    EXPECT_TRUE(h2 == h_buon);

    filein.fclose();
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////// Test Fixture definitions ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
CBlockUndo SidechainTestSuite::createBlockUndoWith(const uint256 & scId, int height, CAmount amount, uint256 lastCertHash)
{
    CBlockUndo retVal;
    CAmount AmountPerHeight = amount;
    ScUndoData data;
    data.appliedMaturedAmount = AmountPerHeight;
    retVal.scUndoMap[scId] = data;

    return retVal;
}

