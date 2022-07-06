#include <gtest/gtest.h>
#include "tx_creation_utils.h"
#include <coins.h>
#include <chainparams.h>
#include <undo.h>
#include <pubkey.h>
#include <main.h>
#include <txdb.h>
#include <main.h>
#include <consensus/validation.h>

class SidechainsMultipleCertsTestSuite : public ::testing::Test {
public:
    SidechainsMultipleCertsTestSuite(): fakeChainStateDb(nullptr), sidechainsView(nullptr), dummyBlock(),
                                       dummyUndo(IncludeScAttributes::ON), dummyBlockUndo(IncludeScAttributes::ON),
                                       dummyScriptPubKey() {
        dummyScriptPubKey = GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/false);
    }

    ~SidechainsMultipleCertsTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        fakeChainStateDb   = new blockchain_test_utils::CInMemorySidechainDb();
        sidechainsView     = new txCreationUtils::CNakedCCoinsViewCache(fakeChainStateDb);
    };

    void TearDown() override {
        delete sidechainsView;
        sidechainsView = nullptr;

        delete fakeChainStateDb;
        fakeChainStateDb = nullptr;
    };
protected:
    blockchain_test_utils::CInMemorySidechainDb   *fakeChainStateDb;
    txCreationUtils::CNakedCCoinsViewCache  *sidechainsView;

    //helpers
    CBlock                  dummyBlock;
    CBlockUndo              dummyUndo;
    CBlockUndo              dummyBlockUndo;
    CScript                 dummyScriptPubKey;

    CCoinsMap           dummyCoins;
    uint256             dummyHash;
    uint256             dummyAnchor;
    CAnchorsMap         dummyAnchors;
    CNullifiersMap      dummyNullifiers;
    CSidechainsMap      dummySidechains;
    CSidechainEventsMap dummyScEvents;
    CCswNullifiersMap   dummyCswNullifiers;

    CValidationState    dummyState;

    void storeSidechainWithCurrentHeight(txCreationUtils::CNakedCCoinsViewCache& view,
                                         const uint256& scId,
                                         const CSidechain& sidechain,
                                         int chainActiveHeight);
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// UpdateSidechain ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsMultipleCertsTestSuite, Cert_HigherQuality_SameEpoch_SidechainIsUpdated) {
    uint256 scId = uint256S("aaa");

    // setup sidechain initial state...
    CSidechain initialScState;
    initialScState.fixedParams.withdrawalEpochLength = 10;
    initialScState.fixedParams.version = 0;
    initialScState.creationBlockHeight = 400;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 1987;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*sidechainsView, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(sidechainsView->getScEventsMap(), initialScheduledHeight, scEvent);

    //Insert high quality Certificate
    CMutableScCertificate highQualityCert;
    highQualityCert.scId        = scId;
    highQualityCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    highQualityCert.quality     = initialScState.lastTopQualityCertQuality * 2;
    highQualityCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));
    CAmount highQualityCert_TotalBwtAmount = CScCertificate(highQualityCert).GetValueOfBackwardTransfers();

    //test
    ASSERT_TRUE(sidechainsView->UpdateSidechain(highQualityCert, dummyUndo));

    //check
    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.lastTopQualityCertHash == highQualityCert.GetHash());
    EXPECT_TRUE(sidechain.lastTopQualityCertQuality == highQualityCert.quality);
    EXPECT_TRUE(sidechain.lastTopQualityCertBwtAmount == highQualityCert_TotalBwtAmount);
    EXPECT_TRUE(sidechain.balance == initialScState.balance + initialScState.lastTopQualityCertBwtAmount -highQualityCert_TotalBwtAmount);
}

TEST_F(SidechainsMultipleCertsTestSuite, Cert_HigherQuality_SameEpoch_SidechainIsNOTUpdated) {
    uint256 scId = uint256S("aaa");

    // setup sidechain initial state...
    CSidechain initialScState;
    initialScState.fixedParams.withdrawalEpochLength = 10;
    initialScState.fixedParams.version = 0;
    initialScState.creationBlockHeight = 400;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 1987;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*sidechainsView, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(sidechainsView->getScEventsMap(), initialScheduledHeight, scEvent);

    //Insert low quality Certificate
    CMutableScCertificate lowQualityCert;
    lowQualityCert.scId        = scId;
    lowQualityCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    lowQualityCert.quality     = initialScState.lastTopQualityCertQuality / 2;
    lowQualityCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));

    //test
    EXPECT_FALSE(sidechainsView->UpdateSidechain(lowQualityCert, dummyUndo));

    //check
    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.lastTopQualityCertHash      == initialScState.lastTopQualityCertHash);
    EXPECT_TRUE(sidechain.lastTopQualityCertQuality   == initialScState.lastTopQualityCertQuality);
    EXPECT_TRUE(sidechain.lastTopQualityCertBwtAmount == initialScState.lastTopQualityCertBwtAmount);
    EXPECT_TRUE(sidechain.balance                   == initialScState.balance);
}

TEST_F(SidechainsMultipleCertsTestSuite, Cert_LowerQuality_DifferentEpoch_SidechainIsUpdated) {
    uint256 scId = uint256S("aaa");

    // setup sidechain initial state...
    CSidechain initialScState;
    initialScState.fixedParams.withdrawalEpochLength = 10;
    initialScState.fixedParams.version = 0;
    initialScState.creationBlockHeight = 400;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 1987;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*sidechainsView, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(sidechainsView->getScEventsMap(), initialScheduledHeight, scEvent);

    //Insert next epoch Certificate
    CMutableScCertificate nextEpochCert;
    nextEpochCert.scId        = scId;
    nextEpochCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch + 1;
    nextEpochCert.quality     = initialScState.lastTopQualityCertQuality / 2;
    nextEpochCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));
    CAmount nextEpochCert_TotalBwtAmount = CScCertificate(nextEpochCert).GetValueOfBackwardTransfers();

    //test
    EXPECT_TRUE(sidechainsView->UpdateSidechain(nextEpochCert, dummyUndo));

    //check
    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.lastTopQualityCertHash == nextEpochCert.GetHash());
    EXPECT_TRUE(sidechain.lastTopQualityCertQuality == nextEpochCert.quality);
    EXPECT_TRUE(sidechain.lastTopQualityCertBwtAmount == nextEpochCert_TotalBwtAmount);
    EXPECT_TRUE(sidechain.balance == initialScState.balance - nextEpochCert_TotalBwtAmount);
}

TEST_F(SidechainsMultipleCertsTestSuite, Cert_HigherQuality_SameEpoch_UndoDataCheck) {
    uint256 scId = uint256S("aaa");

    // setup sidechain initial state...
    CSidechain initialScState;
    initialScState.fixedParams.withdrawalEpochLength = 10;
    initialScState.fixedParams.version = 0;
    initialScState.creationBlockHeight = 400;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 1987;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*sidechainsView, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(sidechainsView->getScEventsMap(), initialScheduledHeight, scEvent);

    //Insert high quality Certificate and generate undo data
    CMutableScCertificate highQualityCert;
    highQualityCert.scId        = scId;
    highQualityCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    highQualityCert.quality     = initialScState.lastTopQualityCertQuality * 2;
    highQualityCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));
    CAmount highQualityCert_TotalBwtAmount = CScCertificate(highQualityCert).GetValueOfBackwardTransfers();

    CBlockUndo blockUndo(IncludeScAttributes::ON);
    ASSERT_TRUE(sidechainsView->UpdateSidechain(highQualityCert, blockUndo));

    //test
    EXPECT_TRUE(sidechainsView->RestoreSidechain(highQualityCert, blockUndo.scUndoDatabyScId.at(scId)));

    CSidechain revertedSidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,revertedSidechain));
    EXPECT_TRUE(initialScState == revertedSidechain)<<
            initialScState.ToString()<<revertedSidechain.ToString();
}

TEST_F(SidechainsMultipleCertsTestSuite, Cert_LowerQuality_DifferentEpoch_UndoDataCheck) {
    uint256 scId = uint256S("aaa");

    // setup sidechain initial state...
    CSidechain initialScState;
    initialScState.fixedParams.withdrawalEpochLength = 10;
    initialScState.fixedParams.version = 0;
    initialScState.creationBlockHeight = 400;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 1987;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    initialScState.lastTopQualityCertView.forwardTransferScFee = CAmount(10);
    initialScState.lastTopQualityCertView.mainchainBackwardTransferRequestScFee = CAmount(20);
    initialScState.InitScFees();
    
    storeSidechainWithCurrentHeight(*sidechainsView, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    int initialScheduledHeight = initialScState.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(sidechainsView->getScEventsMap(), initialScheduledHeight, scEvent);

    //Insert next epoch Certificate
    CMutableScCertificate nextEpochCert;
    nextEpochCert.scId        = scId;
    nextEpochCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch + 1;
    nextEpochCert.quality     = initialScState.lastTopQualityCertQuality / 2;
    nextEpochCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));

    CBlockUndo blockUndo(IncludeScAttributes::ON);
    ASSERT_TRUE(sidechainsView->UpdateSidechain(nextEpochCert, blockUndo));

    //test
    EXPECT_TRUE(sidechainsView->RestoreSidechain(nextEpochCert, blockUndo.scUndoDatabyScId.at(scId)));

    CSidechain revertedSidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,revertedSidechain));
    EXPECT_TRUE(initialScState == revertedSidechain);
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// CheckQuality ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsMultipleCertsTestSuite, CheckQualityRejectsLowerQualityCertsInSameEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.lastTopQualityCertHash = uint256S("ddd");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 12;
    initialScState.fixedParams.version = 0;
    uint256 scId = uint256S("aaa");
    storeSidechainWithCurrentHeight(*sidechainsView, scId, initialScState, initialScState.creationBlockHeight);

    CMutableScCertificate lowQualityCert;
    lowQualityCert.scId = scId;
    lowQualityCert.quality = initialScState.lastTopQualityCertQuality / 2;
    lowQualityCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;

    EXPECT_FALSE(sidechainsView->CheckQuality(lowQualityCert));
}

TEST_F(SidechainsMultipleCertsTestSuite, CheckQualityRejectsEqualQualityCertsInSameEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.lastTopQualityCertHash = uint256S("ddd");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 12;
    initialScState.fixedParams.version = 0;
    uint256 scId = uint256S("aaa");
    storeSidechainWithCurrentHeight(*sidechainsView, scId, initialScState, initialScState.creationBlockHeight);

    CMutableScCertificate equalQualityCert;
    equalQualityCert.scId = scId;
    equalQualityCert.quality = initialScState.lastTopQualityCertQuality;
    equalQualityCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;

    EXPECT_FALSE(sidechainsView->CheckQuality(equalQualityCert));
}

TEST_F(SidechainsMultipleCertsTestSuite, CheckQualityAcceptsHigherQualityCertsInSameEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.lastTopQualityCertHash = uint256S("ddd");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 12;
    initialScState.fixedParams.version = 0;
    uint256 scId = uint256S("aaa");
    storeSidechainWithCurrentHeight(*sidechainsView, scId, initialScState, initialScState.creationBlockHeight);

    CMutableScCertificate highQualityCert;
    highQualityCert.scId = scId;
    highQualityCert.quality = initialScState.lastTopQualityCertQuality*2;
    highQualityCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;

    EXPECT_TRUE(sidechainsView->CheckQuality(highQualityCert));
}

TEST_F(SidechainsMultipleCertsTestSuite, CheckAcceptsLowerQualityCertsInDifferentEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.lastTopQualityCertHash = uint256S("ddd");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 12;
    initialScState.fixedParams.version = 0;
    uint256 scId = uint256S("aaa");
    storeSidechainWithCurrentHeight(*sidechainsView, scId, initialScState, initialScState.creationBlockHeight);

    CMutableScCertificate highQualityCert;
    highQualityCert.scId = scId;
    highQualityCert.quality = initialScState.lastTopQualityCertQuality / 2;
    highQualityCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch + 1;

    EXPECT_TRUE(sidechainsView->CheckQuality(highQualityCert));
}

TEST_F(SidechainsMultipleCertsTestSuite, CheckInMempoolDelegateToBackingView) {
    CTxMemPool aMempool(::minRelayTxFee);
    CCoinsViewMemPool viewMempool(sidechainsView, aMempool);

    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.lastTopQualityCertHash = uint256S("ddd");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 12;
    initialScState.fixedParams.version = 0;
    uint256 scId = uint256S("aaa");
    storeSidechainWithCurrentHeight(*sidechainsView, scId, initialScState, initialScState.creationBlockHeight);

    CMutableScCertificate cert;
    cert.scId = scId;

    //Lower quality, same epoch
    cert.quality = initialScState.lastTopQualityCertQuality -1;
    cert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(cert));

    //Equal quality, same epoch
    cert.quality = initialScState.lastTopQualityCertQuality;
    cert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(cert));

    //Higher quality, same epoch
    cert.quality = initialScState.lastTopQualityCertQuality +1;
    cert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    EXPECT_TRUE(viewMempool.CheckQuality(cert));

    //Lower quality, different epoch
    cert.quality = initialScState.lastTopQualityCertQuality - 1;
    cert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch + 1;
    EXPECT_TRUE(viewMempool.CheckQuality(cert));
}

TEST_F(SidechainsMultipleCertsTestSuite, CertsInMempoolDoNotAffectCheckQuality) {
    CTxMemPool aMempool(::minRelayTxFee);
    CCoinsViewMemPool viewMempool(sidechainsView, aMempool);

    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.lastTopQualityCertHash = uint256S("ddd");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 12;
    initialScState.fixedParams.version = 0;
    uint256 scId = uint256S("aaa");
    storeSidechainWithCurrentHeight(*sidechainsView, scId, initialScState, initialScState.creationBlockHeight);

    // add certificate to mempool
    CMutableScCertificate mempoolCert;
    mempoolCert.scId = scId;
    mempoolCert.quality = initialScState.lastTopQualityCertQuality * 2;
    mempoolCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch + 1 ;
    CCertificateMemPoolEntry certEntry(mempoolCert, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(mempoolCert.GetHash(), certEntry));

    CMutableScCertificate trialCert;
    trialCert.scId = scId;

    //Lower quality, same epoch
    trialCert.quality = initialScState.lastTopQualityCertQuality -1;
    trialCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(trialCert));

    //Equal quality, same epoch
    trialCert.quality = initialScState.lastTopQualityCertQuality;
    trialCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(trialCert));

    //Higher quality, same epoch
    trialCert.quality = initialScState.lastTopQualityCertQuality +1;
    trialCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    EXPECT_TRUE(viewMempool.CheckQuality(trialCert));

    //Lower quality, different epoch
    trialCert.quality = initialScState.lastTopQualityCertQuality - 1;
    trialCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch + 1;
    EXPECT_TRUE(viewMempool.CheckQuality(trialCert));
}

/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// CheckCertificatesOrdering //////////////////////////
/////////////////////////////////////////////////////////////////////////////////
TEST(SidechainsMultipleCerts, BlocksWithCertsOfDifferentEpochsAreRejected) {
    CMutableScCertificate cert_1;
    cert_1.scId = uint256S("aaa");
    cert_1.quality = 100;
    cert_1.epochNumber = 12;

    CMutableScCertificate cert_2;
    cert_2.scId = cert_1.scId;
    cert_2.quality = cert_1.quality;
    cert_2.epochNumber = cert_1.epochNumber+1;

    CBlock aBlock;
    aBlock.vcert = {cert_1, cert_2};

    CValidationState dummyState;
    EXPECT_FALSE(CheckCertificatesOrdering(aBlock.vcert, dummyState));
}

TEST(SidechainsMultipleCerts, BlocksWithCertsWithEqualQualitiesAreRejected) {
    CMutableScCertificate cert_1;
    cert_1.scId = uint256S("aaa");
    cert_1.epochNumber = 12;
    cert_1.quality = 100;

    CMutableScCertificate cert_2;
    cert_2.scId = cert_1.scId;
    cert_2.epochNumber = cert_1.epochNumber;
    cert_2.quality = cert_1.quality * 2;

    CMutableScCertificate cert_3;
    cert_3.scId = cert_1.scId;
    cert_3.epochNumber = cert_1.epochNumber;
    cert_3.quality = cert_1.quality;

    CBlock aBlock;
    aBlock.vcert = {cert_1, cert_2, cert_3};

    CValidationState dummyState;
    EXPECT_FALSE(CheckCertificatesOrdering(aBlock.vcert, dummyState));
}

TEST(SidechainsMultipleCerts, BlocksWithCertsOrderedByDecreasingQualitiesAreRejected) {
    CMutableScCertificate cert_1;
    cert_1.scId = uint256S("aaa");
    cert_1.epochNumber = 12;
    cert_1.quality = 100;

    CMutableScCertificate cert_2;
    cert_2.scId = cert_1.scId;
    cert_2.epochNumber = cert_1.epochNumber;
    cert_2.quality = cert_1.quality/2;

    CBlock aBlock;
    aBlock.vcert = {cert_1, cert_2};

    CValidationState dummyState;
    EXPECT_FALSE(CheckCertificatesOrdering(aBlock.vcert, dummyState));
}

TEST(SidechainsMultipleCerts, BlocksWithSameEpochCertssOrderedByIncreasingQualityAreAccepted) {
    CMutableScCertificate cert_A_1, cert_A_2, cert_A_3;
    cert_A_1.scId = uint256S("aaa");
    cert_A_1.epochNumber = 12;
    cert_A_1.quality = 100;

    cert_A_2.scId = cert_A_1.scId;
    cert_A_2.epochNumber = cert_A_1.epochNumber;
    cert_A_2.quality = cert_A_1.quality * 2;

    cert_A_3.scId = cert_A_2.scId;
    cert_A_3.epochNumber = cert_A_2.epochNumber;
    cert_A_3.quality = cert_A_2.quality + 1;

    CMutableScCertificate cert_B_1, cert_B_2;
    cert_B_1.scId = uint256S("bbb");
    cert_B_1.epochNumber = 90;
    cert_B_1.quality = 20;

    cert_B_2.scId = cert_B_1.scId;
    cert_B_2.epochNumber = cert_B_1.epochNumber;
    cert_B_2.quality = 2000;

    CBlock aBlock;
    aBlock.vcert = {cert_B_1, cert_A_1, cert_A_2, cert_B_2, cert_A_3};

    CValidationState dummyState;
    EXPECT_TRUE(CheckCertificatesOrdering(aBlock.vcert, dummyState));
}

/////////////////////////////////////////////////////////////////////////////////
/////////////////////////////// HighQualityCertData /////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsMultipleCertsTestSuite, HighQualityCertData_EmptyBlock)
{
    CSidechain sidechain;
    sidechain.creationBlockHeight = 10;
    sidechain.lastTopQualityCertQuality = 100;
    sidechain.lastTopQualityCertHash = uint256S("999");
    sidechain.lastTopQualityCertReferencedEpoch = 15;
    sidechain.fixedParams.version = 0;
    uint256 scId = uint256S("aaa");
    storeSidechainWithCurrentHeight(*sidechainsView, scId, sidechain, sidechain.creationBlockHeight);

    CBlock emptyBlock;
    EXPECT_TRUE(HighQualityCertData(emptyBlock, *sidechainsView).empty());
}

TEST_F(SidechainsMultipleCertsTestSuite, HighQualityCertData_FirstCert)
{
    CSidechain sidechain;
    sidechain.creationBlockHeight = 11;
    sidechain.lastTopQualityCertQuality = 100;
    sidechain.lastTopQualityCertHash = uint256S("aaa");
    sidechain.lastTopQualityCertReferencedEpoch = -1;
    sidechain.fixedParams.version = 0;
    uint256 scId = uint256S("aaa");
    storeSidechainWithCurrentHeight(*sidechainsView, scId, sidechain, sidechain.creationBlockHeight);

    CMutableScCertificate firstCert;
    firstCert.scId = scId;
    firstCert.epochNumber = 0;
    firstCert.quality = 10;

    CBlock aBlock;
    aBlock.vcert.push_back(firstCert);

    EXPECT_TRUE(HighQualityCertData(aBlock, *sidechainsView).at(firstCert.GetHash()).IsNull());
}

TEST_F(SidechainsMultipleCertsTestSuite, LowQualityCerts_SameScId_DifferentEpoch)
{
    CSidechain sidechain;
    sidechain.creationBlockHeight = 1;
    sidechain.lastTopQualityCertQuality = 100;
    sidechain.lastTopQualityCertHash = uint256S("aaa");
    sidechain.lastTopQualityCertReferencedEpoch = 15;
    sidechain.fixedParams.version = 0;
    uint256 scId = uint256S("aaa");
    storeSidechainWithCurrentHeight(*sidechainsView, scId, sidechain, sidechain.creationBlockHeight);

    CMutableScCertificate lowQualityCert;
    lowQualityCert.scId = scId;
    lowQualityCert.epochNumber = sidechain.lastTopQualityCertReferencedEpoch +1;
    lowQualityCert.quality = 10;

    CMutableScCertificate highQualityCert;
    highQualityCert.scId = scId;
    highQualityCert.epochNumber = lowQualityCert.epochNumber;
    highQualityCert.quality = lowQualityCert.quality * 2;

    CBlock aBlock;
    aBlock.vcert.push_back(lowQualityCert);
    aBlock.vcert.push_back(highQualityCert);
    ASSERT_TRUE(CheckCertificatesOrdering(aBlock.vcert, dummyState));

    EXPECT_TRUE(HighQualityCertData(aBlock, *sidechainsView).at(highQualityCert.GetHash()).IsNull());
}

TEST_F(SidechainsMultipleCertsTestSuite, LowQualityCerts_SameScId_SameEpoch)
{
    CSidechain sidechain;
    sidechain.creationBlockHeight = 5;
    sidechain.lastTopQualityCertQuality = 10;
    sidechain.lastTopQualityCertHash = uint256S("aaa");
    sidechain.lastTopQualityCertReferencedEpoch = 15;
    sidechain.fixedParams.version = 0;
    uint256 scId = uint256S("aaa");
    storeSidechainWithCurrentHeight(*sidechainsView, scId, sidechain, sidechain.creationBlockHeight);

    CMutableScCertificate lowQualityCert;
    lowQualityCert.scId = scId;
    lowQualityCert.epochNumber = sidechain.lastTopQualityCertReferencedEpoch;
    lowQualityCert.quality = sidechain.lastTopQualityCertQuality * 2;

    CMutableScCertificate highQualityCert;
    highQualityCert.scId = scId;
    highQualityCert.epochNumber = lowQualityCert.epochNumber;
    highQualityCert.quality = lowQualityCert.quality * 2;

    CBlock aBlock;
    aBlock.vcert.push_back(lowQualityCert);
    aBlock.vcert.push_back(highQualityCert);
    ASSERT_TRUE(CheckCertificatesOrdering(aBlock.vcert, dummyState));

    EXPECT_TRUE(HighQualityCertData(aBlock, *sidechainsView).at(highQualityCert.GetHash()) == sidechain.lastTopQualityCertHash);
}

TEST_F(SidechainsMultipleCertsTestSuite, LowQualityCerts_MultipleScIds)
{
    int allScsCreationBlockHeight = 22;

    CSidechain sidechain_A;
    sidechain_A.creationBlockHeight = allScsCreationBlockHeight;
    sidechain_A.lastTopQualityCertHash = uint256S("aaa");
    sidechain_A.lastTopQualityCertQuality = 10;
    sidechain_A.lastTopQualityCertReferencedEpoch = 15;
    sidechain_A.fixedParams.version = 0;
    uint256 scId_A = uint256S("aaa");
    storeSidechainWithCurrentHeight(*sidechainsView, scId_A, sidechain_A, allScsCreationBlockHeight);

    CSidechain sidechain_B;
    sidechain_B.creationBlockHeight = allScsCreationBlockHeight;        //// AP: SISTEMARE!
    sidechain_B.lastTopQualityCertHash = uint256S("bbb");
    sidechain_B.lastTopQualityCertQuality = 2;
    sidechain_B.lastTopQualityCertReferencedEpoch = 200;
    sidechain_B.fixedParams.version = 0;
    uint256 scId_B = uint256S("bbb");
    storeSidechainWithCurrentHeight(*sidechainsView, scId_B, sidechain_B, allScsCreationBlockHeight);

    CMutableScCertificate cert_A_1;
    cert_A_1.scId = scId_A;
    cert_A_1.epochNumber = sidechain_A.lastTopQualityCertReferencedEpoch;
    cert_A_1.quality = sidechain_A.lastTopQualityCertQuality * 2;

    CMutableScCertificate cert_A_2;
    cert_A_2.scId = scId_A;
    cert_A_2.epochNumber = sidechain_A.lastTopQualityCertReferencedEpoch;
    cert_A_2.quality = sidechain_A.lastTopQualityCertQuality * 3;

    CMutableScCertificate cert_A_3;
    cert_A_3.scId = scId_A;
    cert_A_3.epochNumber = sidechain_A.lastTopQualityCertReferencedEpoch;
    cert_A_3.quality = sidechain_A.lastTopQualityCertQuality * 4;

    CMutableScCertificate cert_B_1;
    cert_B_1.scId = scId_B;
    cert_B_1.epochNumber = sidechain_B.lastTopQualityCertReferencedEpoch + 1;
    cert_B_1.quality = sidechain_B.lastTopQualityCertQuality + 1;

    CMutableScCertificate cert_B_2;
    cert_B_2.scId = scId_B;
    cert_B_2.epochNumber = sidechain_B.lastTopQualityCertReferencedEpoch + 1;
    cert_B_2.quality = sidechain_B.lastTopQualityCertQuality + 2;

    CBlock aBlock;
    aBlock.vcert.push_back(cert_A_1);
    aBlock.vcert.push_back(cert_B_1);
    aBlock.vcert.push_back(cert_A_2);
    aBlock.vcert.push_back(cert_B_2);
    aBlock.vcert.push_back(cert_A_3);
    ASSERT_TRUE(CheckCertificatesOrdering(aBlock.vcert, dummyState));

    EXPECT_TRUE(HighQualityCertData(aBlock, *sidechainsView).at(cert_A_3.GetHash()) == sidechain_A.lastTopQualityCertHash);
    EXPECT_TRUE(HighQualityCertData(aBlock, *sidechainsView).at(cert_B_2.GetHash()).IsNull());
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// HELPERS ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void SidechainsMultipleCertsTestSuite::storeSidechainWithCurrentHeight(txCreationUtils::CNakedCCoinsViewCache& view,
                                                                       const uint256& scId,
                                                                       const CSidechain& sidechain,
                                                                       int chainActiveHeight)
{
    chainSettingUtils::ExtendChainActiveToHeight(chainActiveHeight);
    view.SetBestBlock(chainActive.Tip()->GetBlockHash());
    txCreationUtils::storeSidechain(view.getSidechainMap(), scId, sidechain);
}
