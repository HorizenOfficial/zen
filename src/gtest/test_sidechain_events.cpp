#include <gtest/gtest.h>
#include <chainparams.h>
#include <coins.h>
#include "tx_creation_utils.h"
#include <main.h>
#include <undo.h>
#include <consensus/validation.h>
#include <gtest/libzendoo_test_files.h>

class SidechainsEventsTestSuite: public ::testing::Test
{
public:
    SidechainsEventsTestSuite():
        dummyBackingView(nullptr), view(nullptr),
        dummyBlock(), dummyUndo(IncludeScAttributes::ON), dummyTxundo(), dummyInfo(), dummyHeight{1957} {};

    ~SidechainsEventsTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        dummyBackingView = new CCoinsView();
        view = new txCreationUtils::CNakedCCoinsViewCache(dummyBackingView);
    };

    void TearDown() override {
        delete view;
        view = nullptr;

        delete dummyBackingView;
        dummyBackingView = nullptr;
    };

protected:
    CCoinsView            *dummyBackingView;
    txCreationUtils::CNakedCCoinsViewCache *view;

    CBlock dummyBlock;
    CBlockUndo dummyUndo;
    CTxUndo dummyTxundo;
    int dummyHeight;
    std::vector<CScCertificateStatusUpdateInfo> dummyInfo;

    void storeSidechainWithCurrentHeight(txCreationUtils::CNakedCCoinsViewCache& view, const uint256& scId, const CSidechain& sidechain, int chainActiveHeight);
};

/////////////////////////////////////////////////////////////////////////////////
///////////////////////////// GetActiveCertDataHash /////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, CertDataHash_EndWindowToEndWindows_WithoutCert) {
    CSidechain initialScState;
    uint256 scId = uint256S("abab");
    int initialEpochReferencedByCert = 0;

    initialScState.creationBlockHeight = 1;
    initialScState.fixedParams.withdrawalEpochLength = 5;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = initialEpochReferencedByCert;

    initialScState.pastEpochTopQualityCertView.certDataHash = CFieldElement{std::vector<unsigned char>(CFieldElement::ByteSize(), 'a')};
    initialScState.lastTopQualityCertView.certDataHash = CFieldElement{std::vector<unsigned char>(CFieldElement::ByteSize(), 'b')};
    initialScState.InitScFees();

    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.GetCertSubmissionWindowStart(initialEpochReferencedByCert));
    ASSERT_TRUE(view->GetSidechainState(scId) == CSidechain::State::ALIVE);

    // test
    int currWindowEnd = initialScState.GetCertSubmissionWindowEnd(initialEpochReferencedByCert);
    int nextWindowEnd = initialScState.GetCertSubmissionWindowEnd(initialEpochReferencedByCert+1);

    CFieldElement expectedActiveCertDataHash = initialScState.lastTopQualityCertView.certDataHash;

    for (int heightToInspect = currWindowEnd+1; heightToInspect <= nextWindowEnd; ++heightToInspect)
    {
        chainSettingUtils::ExtendChainActiveToHeight(heightToInspect-1); // connect block before
        view->SetBestBlock(chainActive.Tip()->GetBlockHash());
        EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::ALIVE);

        EXPECT_TRUE(view->GetActiveCertView(scId).certDataHash == expectedActiveCertDataHash)
        <<"Inspecting height "<<heightToInspect
        <<"\ncertDataHash is "<<view->GetActiveCertView(scId).certDataHash.GetHexRepr()
        <<"\ninstead of "<<expectedActiveCertDataHash.GetHexRepr();
    }
}

TEST_F(SidechainsEventsTestSuite, CertDataHash_EndWindowToEndWindows_WithCert) {
    CSidechain sidechain;
    uint256 scId = uint256S("abab");
    int initialEpochReferencedByCert = 0;

    sidechain.creationBlockHeight = 1;
    sidechain.fixedParams.withdrawalEpochLength = 100;
    sidechain.fixedParams.version = 0;
    sidechain.lastTopQualityCertReferencedEpoch = initialEpochReferencedByCert;
    sidechain.InitScFees();

    sidechain.pastEpochTopQualityCertView.certDataHash = CFieldElement{std::vector<unsigned char>(CFieldElement::ByteSize(), 'a')};
    sidechain.lastTopQualityCertView.certDataHash = CFieldElement{std::vector<unsigned char>(CFieldElement::ByteSize(), 'b')};

    storeSidechainWithCurrentHeight(*view, scId, sidechain, sidechain.GetCertSubmissionWindowStart(initialEpochReferencedByCert));
    ASSERT_TRUE(view->GetSidechainState(scId) == CSidechain::State::ALIVE);

    CFieldElement expectedActiveCertDataHash = sidechain.lastTopQualityCertView.certDataHash;

    // test
    int currWindowEnd = sidechain.GetCertSubmissionWindowEnd(initialEpochReferencedByCert);
    int nextWindowEnd = sidechain.GetCertSubmissionWindowEnd(initialEpochReferencedByCert+1);

    int firstBlockWhereCertIsIncluded = sidechain.GetCertSubmissionWindowStart(initialEpochReferencedByCert+1) + 10;
    ASSERT_TRUE(firstBlockWhereCertIsIncluded <= nextWindowEnd);

    for (int heightToInspect = currWindowEnd+1; heightToInspect <= nextWindowEnd; ++heightToInspect)
    {
        if (heightToInspect < firstBlockWhereCertIsIncluded)
        {
            chainSettingUtils::ExtendChainActiveToHeight(heightToInspect-1); //connect block before
            view->SetBestBlock(chainActive.Tip()->GetBlockHash());
            EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::ALIVE);

            EXPECT_TRUE(view->GetActiveCertView(scId).certDataHash == expectedActiveCertDataHash)
            <<"Inspecting height "<<heightToInspect
            <<"\ncertDataHash is "<<view->GetActiveCertView(scId).certDataHash.GetHexRepr()
            <<"\ninstead of "<<expectedActiveCertDataHash.GetHexRepr();
        } else if (heightToInspect == firstBlockWhereCertIsIncluded)
        {
            chainSettingUtils::ExtendChainActiveToHeight(heightToInspect-1); //connect block before
            view->SetBestBlock(chainActive.Tip()->GetBlockHash());
            EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::ALIVE);

            EXPECT_TRUE(view->GetActiveCertView(scId).certDataHash == expectedActiveCertDataHash)
            <<"Inspecting height "<<heightToInspect
            <<"\ncertDataHash is "<<view->GetActiveCertView(scId).certDataHash.GetHexRepr()
            <<"\ninstead of "<<expectedActiveCertDataHash.GetHexRepr();

            // simulate certificate reception, as it happens in UpdateSidechain
            sidechain.lastTopQualityCertReferencedEpoch += 1;
            sidechain.pastEpochTopQualityCertView = sidechain.lastTopQualityCertView; // rotate past certDataHash
            sidechain.lastTopQualityCertView.certDataHash = CFieldElement{std::vector<unsigned char>(CFieldElement::ByteSize(), 'c')};
            txCreationUtils::storeSidechain(view->getSidechainMap(), scId, sidechain);
        } else
        {
            chainSettingUtils::ExtendChainActiveToHeight(heightToInspect-1); //connect block before
            view->SetBestBlock(chainActive.Tip()->GetBlockHash());
            EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::ALIVE);

            EXPECT_TRUE(view->GetActiveCertView(scId).certDataHash == expectedActiveCertDataHash)
            <<"Inspecting height "<<heightToInspect
            <<"\ncertDataHash is "<<view->GetActiveCertView(scId).certDataHash.GetHexRepr()
            <<"\ninstead of "<<expectedActiveCertDataHash.GetHexRepr();
        }
    }
}
///////////////////////////////////////////////////////////////////////////////
/////////////////////////// isSidechainCeased /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, UnknownSidechainIsNeitherAliveNorCeased) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1912;
    ASSERT_FALSE(view->HaveSidechain(scId));

    CSidechain::State state = view->GetSidechainState(scId);
    EXPECT_TRUE(state == CSidechain::State::NOT_APPLICABLE)
        <<"sc is in state "<<int(state);
}

TEST_F(SidechainsEventsTestSuite, SidechainInItsFirstEpochIsNotCeased) {
    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1912;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = CScCertificate::EPOCH_NULL;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int firstHeight = initialScState.creationBlockHeight;
    int lastHeight = sidechain.GetEndHeightForEpoch(initialScState.lastTopQualityCertReferencedEpoch+1);

    for(int height = firstHeight; height < lastHeight; ++height)
    {
        //Move sidechain ahead
        chainSettingUtils::ExtendChainActiveToHeight(height);
        view->SetBestBlock(chainActive.Tip()->GetBlockHash()); //this represents connecting the actual block
        CSidechain::State state = view->GetSidechainState(scId);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<CSidechain::stateToString(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite, SidechainIsNotCeasedBeforeNextEpochSafeguard) {
    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1912;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int firstHeight = sidechain.GetCertSubmissionWindowStart(initialScState.lastTopQualityCertReferencedEpoch+1);
    int lastHeight = sidechain.GetCertSubmissionWindowEnd(initialScState.lastTopQualityCertReferencedEpoch+1);

    for(int height = firstHeight; height < lastHeight; ++height)
    {
        //Move sidechain ahead
        chainSettingUtils::ExtendChainActiveToHeight(height);
        view->SetBestBlock(chainActive.Tip()->GetBlockHash()); //this represents connecting the actual block
        CSidechain::State state = view->GetSidechainState(scId);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<CSidechain::stateToString(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite, SidechainIsCeasedAtNextEpochSafeguard) {
    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1912;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int firstHeight = sidechain.GetCertSubmissionWindowEnd(initialScState.lastTopQualityCertReferencedEpoch+1);
    int lastHeight = firstHeight + 10;

    for(int height = firstHeight; height < lastHeight; ++height)
    {
        //Move sidechain ahead
        chainSettingUtils::ExtendChainActiveToHeight(height);
        view->SetBestBlock(chainActive.Tip()->GetBlockHash()); //this represents connecting the actual block
        CSidechain::State state = view->GetSidechainState(scId);

        EXPECT_TRUE(state == CSidechain::State::CEASED)
            <<"sc is in state "<<CSidechain::stateToString(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite, FullCertMovesSidechainTerminationToNextEpochSafeguard) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1912;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    // update sidechain with next epoch certificate
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch+1;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certEpoch,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(0),
        /*numBwt*/2, /*ftScFee*/0, /*mbtrScFee*/0);
    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    CSidechain sidechainAfterCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainAfterCert));
    int finalScheduledHeight = sidechainAfterCert.GetScheduledCeasingHeight();

    EXPECT_TRUE(initialScheduledHeight < finalScheduledHeight);
    for(int height = initialScheduledHeight; height < finalScheduledHeight; ++height)
    {
        //Move sidechain ahead
        chainSettingUtils::ExtendChainActiveToHeight(height);
        view->SetBestBlock(chainActive.Tip()->GetBlockHash()); //this represents connecting the actual block
        CSidechain::State state = view->GetSidechainState(scId);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<CSidechain::stateToString(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite, PureBwtCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1912;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.balance = CAmount{100};
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    // update sidechain with next epoch certificate
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch+1;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certEpoch,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(0),/*numChangeOut*/0, /*bwtAmount*/CAmount(10), /*numBwt*/1,
        /*ftScFee*/0, /*mbtrScFee*/0);
    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    CSidechain sidechainAfterCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainAfterCert));
    int finalScheduledHeight = sidechainAfterCert.GetScheduledCeasingHeight();

    EXPECT_TRUE(initialScheduledHeight < finalScheduledHeight);
    for(int height = initialScheduledHeight; height < finalScheduledHeight; ++height)
    {
        //Move sidechain ahead
        chainSettingUtils::ExtendChainActiveToHeight(height);
        view->SetBestBlock(chainActive.Tip()->GetBlockHash()); //this represents connecting the actual block
        CSidechain::State state = view->GetSidechainState(scId);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<CSidechain::stateToString(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite, NoBwtCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1912;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    // update sidechain with next epoch certificate
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch+1;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certEpoch,
             CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(10),/*numChangeOut*/1, /*bwtAmount*/CAmount(0), /*numBwt*/0,
             /*ftScFee*/0, /*mbtrScFee*/0);
    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    CSidechain sidechainAfterCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainAfterCert));
    int finalScheduledHeight = sidechainAfterCert.GetScheduledCeasingHeight();

    EXPECT_TRUE(initialScheduledHeight < finalScheduledHeight);
    for(int height = initialScheduledHeight; height < finalScheduledHeight; ++height)
    {
        //Move sidechain ahead
        chainSettingUtils::ExtendChainActiveToHeight(height);
        view->SetBestBlock(chainActive.Tip()->GetBlockHash()); //this represents connecting the actual block
        CSidechain::State state = view->GetSidechainState(scId);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<CSidechain::stateToString(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite, EmptyCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1912;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    // update sidechain with next epoch certificate
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch+1;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(0),/*numChangeOut*/0, /*bwtAmount*/CAmount(0), /*numBwt*/0,
            /*ftScFee*/0, /*mbtrScFee*/0);
    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    CSidechain sidechainAfterCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainAfterCert));
    int finalScheduledHeight = sidechainAfterCert.GetScheduledCeasingHeight();

    EXPECT_TRUE(initialScheduledHeight < finalScheduledHeight);
    for(int height = initialScheduledHeight; height < finalScheduledHeight; ++height)
    {
        //Move sidechain ahead
        chainSettingUtils::ExtendChainActiveToHeight(height);
        view->SetBestBlock(chainActive.Tip()->GetBlockHash()); //this represents connecting the actual block
        CSidechain::State state = view->GetSidechainState(scId);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<CSidechain::stateToString(state)<<" at height "<<height;
    }
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////// Ceasing Sidechain updates /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, CeasingHeightUpdateForScCreation) {
    int scCreationHeight {1492};
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyBlock;

    //test
    ASSERT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    //Checks
    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int ceasingHeight = sidechain.GetScheduledCeasingHeight();
    CSidechainEvents ceasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(ceasingHeight, ceasingScIds));
    EXPECT_TRUE(ceasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(SidechainsEventsTestSuite, CeasingHeightUpdateForFullCert) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1912;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    // update sidechain with next epoch certificate
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch+1;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(0), /*numBwt*/2,
            /*ftScFee*/0, /*mbtrScFee*/0);
    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    CSidechain sidechainAfterCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainAfterCert));
    int finalScheduledHeight = sidechainAfterCert.GetScheduledCeasingHeight();

    CSidechainEvents updatedCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(finalScheduledHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveSidechainEvents(initialScheduledHeight));
}

TEST_F(SidechainsEventsTestSuite, CeasingHeightUpdateForPureBwtCert) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1912;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.balance = CAmount(19);
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    // update sidechain with next epoch certificate
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch+1;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0,/*bwtAmount*/CAmount(0), /*numBwt*/4,
            /*ftScFee*/0, /*mbtrScFee*/0);
    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    CSidechain sidechainAfterCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainAfterCert));
    int finalScheduledHeight = sidechainAfterCert.GetScheduledCeasingHeight();

    CSidechainEvents updatedCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(finalScheduledHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveSidechainEvents(initialScheduledHeight));
}

TEST_F(SidechainsEventsTestSuite, CeasingHeightUpdateForNoBwtCert) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1912;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    // update sidechain with next epoch certificate
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch+1;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(3), /*numChangeOut*/3,/*bwtAmount*/CAmount(0), /*numBwt*/0,
            /*ftScFee*/0, /*mbtrScFee*/0);
    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    CSidechain sidechainAfterCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainAfterCert));
    int finalScheduledHeight = sidechainAfterCert.GetScheduledCeasingHeight();

    CSidechainEvents updatedCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(finalScheduledHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveSidechainEvents(initialScheduledHeight));
}

TEST_F(SidechainsEventsTestSuite, CeasingHeightUpdateForEmptyCertificate) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1912;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    // update sidechain with next epoch certificate
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch+1;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0,/*bwtAmount*/CAmount(0), /*numBwt*/0,
            /*ftScFee*/0, /*mbtrScFee*/0);
    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    CSidechain sidechainAfterCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainAfterCert));
    int finalScheduledHeight = sidechainAfterCert.GetScheduledCeasingHeight();

    CSidechainEvents updatedCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(finalScheduledHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveSidechainEvents(initialScheduledHeight));
}
/////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// HandleCeasingScs ///////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, FullCertCoinsHaveBwtStrippedOutWhenSidechainCeases) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1987;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.balance = CAmount{20};
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4), /*numChangeOut*/2, /*bwtTotalAmount*/CAmount(2), /*numBwt*/1,
        /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    //Generate coin from certificate
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxundo, initialScState.creationBlockHeight,/*isBlockTopQualityCert*/true);
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);

    int minimalCeaseHeight = sidechain.GetScheduledCeasingHeight();
    chainSettingUtils::ExtendChainActiveToHeight(minimalCeaseHeight);
    view->SetBestBlock(chainActive.Tip()->GetBlockHash());
    EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::CEASED);

    //test
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, dummyUndo, &dummyInfo));

    //Checks
    CCoins updatedCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),updatedCoin));
    updatedCoin.ClearUnspendable();
    EXPECT_TRUE(updatedCoin.vout.size() == cert.nFirstBwtPos);
    EXPECT_TRUE(updatedCoin.nFirstBwtPos == cert.nFirstBwtPos);

    EXPECT_FALSE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, PureBwtCoinsAreRemovedWhenSidechainCeases) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 201;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.balance = CAmount{20};
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(1), /*numBwt*/1,
            /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    //Generate coin from certificate
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxundo, dummyHeight,/*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int minimalCeaseHeight = sidechain.GetScheduledCeasingHeight();
    chainSettingUtils::ExtendChainActiveToHeight(minimalCeaseHeight);
    view->SetBestBlock(chainActive.Tip()->GetBlockHash());
    EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::CEASED);

    //test
    CBlockUndo coinsBlockUndo(IncludeScAttributes::ON);
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummyInfo));

    //Checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    unsigned int bwtCounter = 0;
    ASSERT_TRUE(coinsBlockUndo.scUndoDatabyScId.at(scId).ceasedBwts.size() == 1);
    for(int pos =  cert.nFirstBwtPos; pos < cert.GetVout().size(); ++pos)
    {
        const CTxOut& out = cert.GetVout()[pos];
        EXPECT_TRUE( (coinsBlockUndo.scUndoDatabyScId.at(scId).ceasedBwts.at(bwtCounter).nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f))
                     <<coinsBlockUndo.scUndoDatabyScId.at(scId).ceasedBwts.at(bwtCounter).nVersion;
        EXPECT_TRUE(coinsBlockUndo.scUndoDatabyScId.at(scId).ceasedBwts.at(bwtCounter).nBwtMaturityHeight == coinFromCert.nBwtMaturityHeight);
        EXPECT_TRUE(out == coinsBlockUndo.scUndoDatabyScId.at(scId).ceasedBwts.at(bwtCounter).txout);
        ++bwtCounter;
    }

    EXPECT_TRUE(cert.GetVout().size() == bwtCounter); //all cert outputs are handled
    EXPECT_FALSE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, NoBwtCertificatesCoinsAreNotAffectedByCeasedSidechainHandling) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 201;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4), /*numChangeOut*/2, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0,
            /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    //Generate coin from certificate
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxundo, dummyHeight,/*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int minimalCeaseHeight = sidechain.GetScheduledCeasingHeight();
    chainSettingUtils::ExtendChainActiveToHeight(minimalCeaseHeight);
    view->SetBestBlock(chainActive.Tip()->GetBlockHash());
    EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::CEASED);

    //test
    CBlockUndo coinsBlockUndo(IncludeScAttributes::ON);
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummyInfo));

    //Checks
    CCoins updatedCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),updatedCoin));
    updatedCoin.ClearUnspendable();
    EXPECT_TRUE(updatedCoin.vout.size() == cert.GetVout().size());
    EXPECT_TRUE(updatedCoin.nFirstBwtPos == cert.nFirstBwtPos);
    EXPECT_FALSE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, EmptyCertificatesCoinsAreNotAffectedByCeasedSidechainHandling) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 201;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0,
            /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    //Generate coin from certificate
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxundo, dummyHeight,/*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_FALSE(view->GetCoins(cert.GetHash(),coinFromCert));

    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int minimalCeaseHeight = sidechain.GetScheduledCeasingHeight();
    chainSettingUtils::ExtendChainActiveToHeight(minimalCeaseHeight);
    view->SetBestBlock(chainActive.Tip()->GetBlockHash());
    EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::CEASED);

    //test
    CBlockUndo coinsBlockUndo(IncludeScAttributes::ON);
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummyInfo));

    //Checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    EXPECT_FALSE(view->HaveSidechainEvents(minimalCeaseHeight));
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////////// RevertCeasingScs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, RestoreFullCertCeasedCoins) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1987;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.balance = CAmount{20};
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4), /*numChangeOut*/2, /*bwtTotalAmount*/CAmount(2), /*numBwt*/1,
        /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    //Generate coin from certificate
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxundo, initialScState.creationBlockHeight,/*isBlockTopQualityCert*/true);
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease, nulling the coin
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int minimalCeaseHeight = sidechain.GetScheduledCeasingHeight();
    chainSettingUtils::ExtendChainActiveToHeight(minimalCeaseHeight);
    view->SetBestBlock(chainActive.Tip()->GetBlockHash());
    EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo(IncludeScAttributes::ON);
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummyInfo));

    //test
    view->RevertSidechainEvents(coinsBlockUndo, minimalCeaseHeight, &dummyInfo);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.nHeight            == originalCoins.nHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f)  == (originalCoins.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.nBwtMaturityHeight == originalCoins.nBwtMaturityHeight);
    EXPECT_TRUE(rebuiltCoin.vout.size()        == originalCoins.vout.size());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout.at(pos) == originalCoins.vout.at(pos));
    }
    EXPECT_TRUE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, RestorePureBwtCeasedCoins) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1987;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.balance = CAmount{20};
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(1), /*numBwt*/1,
            /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    //Generate coin from certificate
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxundo, initialScState.creationBlockHeight,/*isBlockTopQualityCert*/true);
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease, nulling the coin
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int minimalCeaseHeight = sidechain.GetScheduledCeasingHeight();
    chainSettingUtils::ExtendChainActiveToHeight(minimalCeaseHeight);
    view->SetBestBlock(chainActive.Tip()->GetBlockHash());
    EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo(IncludeScAttributes::ON);
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummyInfo));

    //test
    view->RevertSidechainEvents(coinsBlockUndo, minimalCeaseHeight, &dummyInfo);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.nHeight            == originalCoins.nHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f)  == (originalCoins.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.nBwtMaturityHeight == originalCoins.nBwtMaturityHeight);
    EXPECT_TRUE(rebuiltCoin.vout.size()        == originalCoins.vout.size());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout[pos] == originalCoins.vout[pos]);
    }
    EXPECT_TRUE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, RestoreNoBwtCeasedCoins) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1987;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4), /*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0,
            /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    //Generate coin from certificate
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxundo, initialScState.creationBlockHeight,/*isBlockTopQualityCert*/true);
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease, nulling the coin
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int minimalCeaseHeight = sidechain.GetScheduledCeasingHeight();
    chainSettingUtils::ExtendChainActiveToHeight(minimalCeaseHeight);
    view->SetBestBlock(chainActive.Tip()->GetBlockHash());
    EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo(IncludeScAttributes::ON);
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummyInfo));

    //test
    view->RevertSidechainEvents(coinsBlockUndo, minimalCeaseHeight, &dummyInfo);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.nHeight            == originalCoins.nHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f)  == (originalCoins.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.nBwtMaturityHeight == originalCoins.nBwtMaturityHeight);
    EXPECT_TRUE(rebuiltCoin.vout.size()        == originalCoins.vout.size());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout[pos] == originalCoins.vout[pos]);
    }
    EXPECT_TRUE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, RestoreEmptyCertCeasedCoins) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1987;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0,
            /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    ASSERT_TRUE(view->UpdateSidechain(cert, dummyUndo, view->GetHeight()+1));

    //Generate coin from certificate
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxundo, initialScState.creationBlockHeight,/*isBlockTopQualityCert*/true);
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    //Make the sidechain cease, nulling the coin
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int minimalCeaseHeight = sidechain.GetScheduledCeasingHeight();
    chainSettingUtils::ExtendChainActiveToHeight(minimalCeaseHeight);
    view->SetBestBlock(chainActive.Tip()->GetBlockHash());
    EXPECT_TRUE(view->GetSidechainState(scId) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo(IncludeScAttributes::ON);
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummyInfo));

    //test
    view->RevertSidechainEvents(coinsBlockUndo, minimalCeaseHeight, &dummyInfo);

    //checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    EXPECT_TRUE(view->HaveSidechainEvents(minimalCeaseHeight));
}
/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////// UndoCeasingScs ///////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, CancelSidechainEvent) {
    int scCreationHeight = 1492;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyBlock;
    ASSERT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int ceasingHeight = sidechain.GetScheduledCeasingHeight();
    CSidechainEvents ceasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(ceasingHeight, ceasingScIds));
    EXPECT_TRUE(ceasingScIds.ceasingScs.count(scId) != 0);

    //test
    EXPECT_TRUE(view->RevertTxOutputs(scCreationTx, scCreationHeight));

    //checks
    CSidechainEvents restoredCeasingScIds;
    EXPECT_FALSE(view->HaveSidechainEvents(ceasingHeight));
}

TEST_F(SidechainsEventsTestSuite, UndoFullCertUpdatesToCeasingScs) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1987;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.balance = CAmount{20};
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4), /*numChangeOut*/2, /*bwtTotalAmount*/CAmount(2), /*numBwt*/1,
        /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    // update sidechain with the certificate
    CBlockUndo certUndo(IncludeScAttributes::ON);
    ASSERT_TRUE(view->UpdateSidechain(cert, certUndo, view->GetHeight()+1));
    CSidechain postCertSidechain;
    ASSERT_TRUE(view->GetSidechain(scId, postCertSidechain));
    int postCertCeasingHeight = postCertSidechain.GetScheduledCeasingHeight();

    CSidechainEvents postCertCeasingScIds;
    ASSERT_FALSE(view->HaveSidechainEvents(initialScheduledHeight));
    ASSERT_TRUE(view->GetSidechainEvents(postCertCeasingHeight, postCertCeasingScIds));
    ASSERT_TRUE(postCertCeasingScIds.ceasingScs.count(scId) != 0);

    // test
    view->RestoreSidechain(cert, certUndo.scUndoDatabyScId.at(scId));

    //Checks
    CSidechainEvents restoredCeasingScIds;
    EXPECT_FALSE(view->HaveSidechainEvents(postCertCeasingHeight));
    EXPECT_TRUE(view->GetSidechainEvents(initialScheduledHeight,restoredCeasingScIds));
    EXPECT_TRUE(restoredCeasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(SidechainsEventsTestSuite, UndoPureBwtCertUpdatesToCeasingScs) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1987;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.balance = CAmount{5};
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(0),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(3), /*numBwt*/3,
            /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    // update sidechain with the certificate
    CBlockUndo certUndo(IncludeScAttributes::ON);
    ASSERT_TRUE(view->UpdateSidechain(cert, certUndo, view->GetHeight()+1));
    CSidechain postCertSidechain;
    ASSERT_TRUE(view->GetSidechain(scId, postCertSidechain));
    int postCertCeasingHeight = postCertSidechain.GetScheduledCeasingHeight();

    CSidechainEvents postCertCeasingScIds;
    ASSERT_FALSE(view->HaveSidechainEvents(initialScheduledHeight));
    ASSERT_TRUE(view->GetSidechainEvents(postCertCeasingHeight, postCertCeasingScIds));
    ASSERT_TRUE(postCertCeasingScIds.ceasingScs.count(scId) != 0);

    // test
    view->RestoreSidechain(cert, certUndo.scUndoDatabyScId.at(scId));

    //Checks
    CSidechainEvents restoredCeasingScIds;
    EXPECT_FALSE(view->HaveSidechainEvents(postCertCeasingHeight));
    EXPECT_TRUE(view->GetSidechainEvents(initialScheduledHeight,restoredCeasingScIds));
    EXPECT_TRUE(restoredCeasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(SidechainsEventsTestSuite, UndoNoBwtCertUpdatesToCeasingScs) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1987;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4),/*numChangeOut*/4, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0,
            /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    // update sidechain with the certificate
    CBlockUndo certUndo(IncludeScAttributes::ON);
    ASSERT_TRUE(view->UpdateSidechain(cert, certUndo, view->GetHeight()+1));
    CSidechain postCertSidechain;
    ASSERT_TRUE(view->GetSidechain(scId, postCertSidechain));
    int postCertCeasingHeight = postCertSidechain.GetScheduledCeasingHeight();

    CSidechainEvents postCertCeasingScIds;
    ASSERT_FALSE(view->HaveSidechainEvents(initialScheduledHeight));
    ASSERT_TRUE(view->GetSidechainEvents(postCertCeasingHeight, postCertCeasingScIds));
    ASSERT_TRUE(postCertCeasingScIds.ceasingScs.count(scId) != 0);

    // test
    view->RestoreSidechain(cert, certUndo.scUndoDatabyScId.at(scId));

    //Checks
    CSidechainEvents restoredCeasingScIds;
    EXPECT_FALSE(view->HaveSidechainEvents(postCertCeasingHeight));
    EXPECT_TRUE(view->GetSidechainEvents(initialScheduledHeight,restoredCeasingScIds));
    EXPECT_TRUE(restoredCeasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(SidechainsEventsTestSuite, UndoEmptyCertUpdatesToCeasingScs) {
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1987;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    storeSidechainWithCurrentHeight(*view, scId, initialScState, initialScState.creationBlockHeight);

    //... and initial ceasing event too
    CSidechain sidechainBeforeCert;
    ASSERT_TRUE(view->GetSidechain(scId, sidechainBeforeCert));
    int initialScheduledHeight = sidechainBeforeCert.GetScheduledCeasingHeight();
    CSidechainEvents scEvent;
    scEvent.ceasingScs.insert(scId);
    txCreationUtils::storeSidechainEvent(view->getScEventsMap(), initialScheduledHeight, scEvent);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, initialScState.creationBlockHeight - COINBASE_MATURITY);
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch +1;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, certEpoch,
            CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(0),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0,
            /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    // update sidechain with the certificate
    CBlockUndo certUndo(IncludeScAttributes::ON);
    ASSERT_TRUE(view->UpdateSidechain(cert, certUndo, view->GetHeight()+1));
    CSidechain postCertSidechain;
    ASSERT_TRUE(view->GetSidechain(scId, postCertSidechain));
    int postCertCeasingHeight = postCertSidechain.GetScheduledCeasingHeight();

    CSidechainEvents postCertCeasingScIds;
    ASSERT_FALSE(view->HaveSidechainEvents(initialScheduledHeight));
    ASSERT_TRUE(view->GetSidechainEvents(postCertCeasingHeight, postCertCeasingScIds));
    ASSERT_TRUE(postCertCeasingScIds.ceasingScs.count(scId) != 0);

    // test
    view->RestoreSidechain(cert, certUndo.scUndoDatabyScId.at(scId));

    //Checks
    CSidechainEvents restoredCeasingScIds;
    EXPECT_FALSE(view->HaveSidechainEvents(postCertCeasingHeight));
    EXPECT_TRUE(view->GetSidechainEvents(initialScheduledHeight,restoredCeasingScIds));
    EXPECT_TRUE(restoredCeasingScIds.ceasingScs.count(scId) != 0);
}
/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////// ApplyTxInUndo ////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, Cert_CoinReconstructionFromBlockUndo_SpendChangeOutput)
{
    //Create sidechain
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 100);
    CBlock dummyBlock;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4), /*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1,
        /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight, /*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending the change output from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert);
}

TEST_F(SidechainsEventsTestSuite, Cert_CoinReconstructionFromBlockUndo_SpendBwtOutput)
{
    //Create sidechain
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 100);
    CBlock dummyBlock;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4), /*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1,
        /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight, /*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending only the bwt from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 1;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert);
}

TEST_F(SidechainsEventsTestSuite, Cert_CoinReconstructionFromBlockUndo_SpendFullCoinsByChangeOutput)
{
    //Create sidechain
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 100);
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4), /*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0,
        /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight, /*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending the change output (the only output) from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert)<<coinFromCert.ToString() + reconstructedCoinFromCert.ToString();
}

TEST_F(SidechainsEventsTestSuite, Cert_CoinReconstructionFromBlockUndo_SpendFullCoinsByBwt)
{
    //Create sidechain
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 100);
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1,
        /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight, /*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending the bwt (the only output) from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert);
}

TEST_F(SidechainsEventsTestSuite, TransparentTx_CoinReconstructionFromBlockUndo_SpendNonFinalOutput)
{
    //Generate transparent transaction and the associated coin to be spent
    CMutableTransaction txToBeSpent;
    txToBeSpent.addOut(CTxOut(CAmount(10),CScript(OP_TRUE))); //a dummy code to ensures the correct serialization
    txToBeSpent.addOut(CTxOut(CAmount(20),CScript(OP_TRUE))); //a dummy code to ensures the correct serialization

    CTxUndo dummyTxUndo;
    int coinHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(txToBeSpent.GetHash()));
    UpdateCoins(txToBeSpent, *view, dummyTxUndo, coinHeight);
    CCoins coinFromTx;
    EXPECT_TRUE(view->GetCoins(txToBeSpent.GetHash(),coinFromTx));

    //Create Tx spending the and output from txToBeSpent. Not a final one
    CMutableTransaction spendingTx;
    spendingTx.vin.resize(1);
    spendingTx.vin.at(0).prevout.hash = txToBeSpent.GetHash();
    spendingTx.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild spent coin output
    CTxUndo txUndo;
    int spendTxHeight = 2020;
    UpdateCoins(spendingTx, *view, txUndo, spendTxHeight);

    //Simulate serialization and deserialization
    CDataStream ssBlockUndo(SER_DISK, 111 /*AVersion*/);

    ssBlockUndo << txUndo;
    CTxUndo retrievedtxUndo;
    ssBlockUndo >> retrievedtxUndo;

    //Test
    for (unsigned int inPos = spendingTx.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = spendingTx.vin[inPos].prevout;
        const CTxInUndo &undo = retrievedtxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoin;
    EXPECT_TRUE(view->GetCoins(txToBeSpent.GetHash(),reconstructedCoin));
    EXPECT_TRUE(coinFromTx == reconstructedCoin)
        <<"\n coinFromTx        "<<coinFromTx.ToString()
        <<"\n reconstructedCoin "<<reconstructedCoin.ToString();
}

TEST_F(SidechainsEventsTestSuite, TransparentTx_CoinReconstructionFromBlockUndo_FullySpendOutput)
{
    //Generate transparent transaction and the associated coin to be spent
    CMutableTransaction txToBeSpent;
    txToBeSpent.addOut(CTxOut(CAmount(10),CScript(OP_TRUE)));  //a dummy code to ensures the correct serialization

    CTxUndo dummyTxUndo;
    int coinHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(txToBeSpent.GetHash()));
    UpdateCoins(txToBeSpent, *view, dummyTxUndo, coinHeight);
    CCoins coinFromTx;
    EXPECT_TRUE(view->GetCoins(txToBeSpent.GetHash(),coinFromTx));

    //Create Tx spending the last output from txToBeSpent
    CMutableTransaction spendingTx;
    spendingTx.vin.resize(1);
    spendingTx.vin.at(0).prevout.hash = txToBeSpent.GetHash();
    spendingTx.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild spent coin output
    CTxUndo txUndo;
    int spendTxHeight = 2020;
    UpdateCoins(spendingTx, *view, txUndo, spendTxHeight);

    //Simulate serialization and deserialization
    CDataStream ssBlockUndo(SER_DISK, 111 /*AVersion*/);

    ssBlockUndo << txUndo;
    CTxUndo retrievedtxUndo;
    ssBlockUndo >> retrievedtxUndo;

    //Test
    for (unsigned int inPos = spendingTx.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = spendingTx.vin[inPos].prevout;
        const CTxInUndo &undo = retrievedtxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoin;
    EXPECT_TRUE(view->GetCoins(txToBeSpent.GetHash(),reconstructedCoin));
    EXPECT_TRUE(coinFromTx == reconstructedCoin)
        <<"\n coinFromTx        "<<coinFromTx.ToString()
        <<"\n reconstructedCoin "<<reconstructedCoin.ToString();
}
/////////////////////////////////////////////////////////////////////////////////
///////////////////////////// Mature sidechain balance //////////////////////////
/////////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, UponScCreationMaturingEventForCreationAmountIsScheduled) {
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    int scCreationHeight = 5;
    CBlock dummyBlock;

    //test
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    //Checks
    int creationMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    CSidechainEvents scheduledEvent;
    EXPECT_TRUE(view->GetSidechainEvents(creationMaturityHeight, scheduledEvent));
    EXPECT_TRUE(scheduledEvent.maturingScs.count(scId));

    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts[creationMaturityHeight] == scCreationTx.GetVscCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, UponFwdMaturingEventForFwdAmountIsScheduled) {
    int scCreationHeight = 5;
    CBlock dummyBlock;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    CAmount fwdAmount = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    int fwdHeight = 20;

    //test
    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));

    //Checks
    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CSidechainEvents scheduledEvent;
    EXPECT_TRUE(view->GetSidechainEvents(fwdMaturityHeight, scheduledEvent));
    EXPECT_TRUE(scheduledEvent.maturingScs.count(scId));

    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts[fwdMaturityHeight] == fwdTx.GetVftCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, UponMbtrMaturingEventForScFeeIsScheduled) {
    int scCreationHeight = 5;
    CBlock dummyBlock;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    CAmount scFee = 2;

    CBwtRequestOut mcBwtReq;
    mcBwtReq.scId = scId;
    CMutableTransaction mutTx;
    mutTx.nVersion = SC_TX_VERSION;
    mutTx.vmbtr_out.push_back(mcBwtReq);

    int nHeight = 20;

    //test
    EXPECT_TRUE(view->UpdateSidechain(mutTx, dummyBlock, nHeight));

    //Checks
    int scFeeMaturityHeight = nHeight + Params().ScCoinsMaturity();
    CSidechainEvents scheduledEvent;
    EXPECT_TRUE(view->GetSidechainEvents(scFeeMaturityHeight, scheduledEvent));
    EXPECT_TRUE(scheduledEvent.maturingScs.count(scId));

    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts[scFeeMaturityHeight] == mutTx.vmbtr_out[0].scFee);
}

TEST_F(SidechainsEventsTestSuite, DoubleFwdSchedulingIsDoneCorrectly) {
    int scCreationHeight = 5;
    CBlock dummyBlock;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    //Create transaction with double fwd Tx
    CAmount fwdAmount1 = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount1);
    CMutableTransaction mutFwdTx(fwdTx);
    CAmount fwdAmount2 = 300;
    mutFwdTx.vft_ccout.push_back(CTxForwardTransferOut(scId, fwdAmount2, uint256S("add"), uint160S("abcd")));
    fwdTx = mutFwdTx;

    int fwdHeight = 20;

    // test
    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));

    //Checks
    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CSidechainEvents scheduledEvent;
    EXPECT_TRUE(view->GetSidechainEvents(fwdMaturityHeight, scheduledEvent));
    EXPECT_TRUE(scheduledEvent.maturingScs.count(scId));

    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts[fwdMaturityHeight] == fwdTx.GetVftCcOut()[0].nValue + fwdTx.GetVftCcOut()[1].nValue);
}

TEST_F(SidechainsEventsTestSuite, ScCreationAmountMaturesAtHeight) {
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    int scCreationHeight = 5;
    CBlock dummyBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    //test
    int creationMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo(IncludeScAttributes::ON);
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(creationMaturityHeight, blockUndo, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == scCreationTx.GetVscCcOut()[0].nValue);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(creationMaturityHeight) == 0);

    EXPECT_TRUE(blockUndo.scUndoDatabyScId.at(scId).appliedMaturedAmount == scCreationTx.GetVscCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, FwdAmountMaturesAtHeight) {
    //Create a sidechain
    CTransaction dummyScCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256 & scId = dummyScCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyBlock;
    EXPECT_TRUE(view->UpdateSidechain(dummyScCreationTx, dummyBlock, /*creationHeight*/5));

    // create a fwd
    CAmount fwdAmount = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    int fwdHeight = 20;
    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));

    //test
    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo(IncludeScAttributes::ON);
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(fwdMaturityHeight, blockUndo, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == fwdTx.GetVftCcOut()[0].nValue);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(fwdMaturityHeight) == 0);

    EXPECT_TRUE(blockUndo.scUndoDatabyScId.at(scId).appliedMaturedAmount == fwdTx.GetVftCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, DoubleFwdsMatureAtHeight) {
    int scCreationHeight = 5;
    CBlock dummyBlock;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    //Create transaction with double fwd Tx
    CAmount fwdAmount1 = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount1);
    CMutableTransaction mutFwdTx(fwdTx);
    CAmount fwdAmount2 = 300;
    mutFwdTx.vft_ccout.push_back(CTxForwardTransferOut(scId, fwdAmount2, uint256S("add"), uint160S("abcd")));
    fwdTx = mutFwdTx;

    int fwdHeight = 20;
    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));

    //test
    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo(IncludeScAttributes::ON);
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(fwdMaturityHeight, blockUndo, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == fwdTx.GetVftCcOut()[0].nValue + fwdTx.GetVftCcOut()[1].nValue);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(fwdMaturityHeight) == 0);

    EXPECT_TRUE(blockUndo.scUndoDatabyScId.at(scId).appliedMaturedAmount == fwdTx.GetVftCcOut()[0].nValue + fwdTx.GetVftCcOut()[1].nValue);
}

TEST_F(SidechainsEventsTestSuite, CreationAmountDoesNotMatureUponRevertSidechainEvents) {
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    int scCreationHeight = 5;
    CBlock dummyBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    int creationMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo(IncludeScAttributes::ON);
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(creationMaturityHeight, blockUndo, &dummy));

    //test
    EXPECT_TRUE(view->RevertSidechainEvents(blockUndo, creationMaturityHeight, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(creationMaturityHeight) != 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts.at(creationMaturityHeight) == scCreationTx.GetVscCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, FwdAmountsDoNotMatureUponRevertSidechainEvents) {
    //Create and mature fwd amount
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    int scCreationHeight = 5;
    CBlock dummyBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    CBlockUndo dummyBlockUndo(IncludeScAttributes::ON);
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(scCreationHeight + Params().ScCoinsMaturity(), dummyBlockUndo, &dummy));

    // create and mature a fwd
    CAmount fwdAmount = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    int fwdHeight = 20;
    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));

    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo(IncludeScAttributes::ON);
    EXPECT_TRUE(view->HandleSidechainEvents(fwdMaturityHeight, blockUndo, &dummy));

    //test
    EXPECT_TRUE(view->RevertSidechainEvents(blockUndo, fwdMaturityHeight, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == scCreationTx.GetVscCcOut()[0].nValue);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(fwdMaturityHeight) != 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts.at(fwdMaturityHeight) == fwdTx.GetVftCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, DoubleFwdsDoNotMatureUponRevertSidechainEvents) {
    int scCreationHeight = 5;
    CBlock dummyBlock;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    CBlockUndo dummyBlockUndo(IncludeScAttributes::ON);
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(scCreationHeight + Params().ScCoinsMaturity(), dummyBlockUndo, &dummy));

    //Create transaction with double fwd Tx
    CAmount fwdAmount1 = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount1);
    CMutableTransaction mutFwdTx(fwdTx);
    CAmount fwdAmount2 = 300;
    mutFwdTx.vft_ccout.push_back(CTxForwardTransferOut(scId, fwdAmount2, uint256S("add"), uint160S("abcd")));
    fwdTx = mutFwdTx;

    int fwdHeight = 20;
    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));

    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo(IncludeScAttributes::ON);
    EXPECT_TRUE(view->HandleSidechainEvents(fwdMaturityHeight, blockUndo, &dummy));

    //test
    EXPECT_TRUE(view->RevertSidechainEvents(blockUndo, fwdMaturityHeight, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == scCreationTx.GetVscCcOut()[0].nValue);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(fwdMaturityHeight) != 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts.at(fwdMaturityHeight) == fwdTx.GetVftCcOut()[0].nValue + fwdTx.GetVftCcOut()[1].nValue);
}

void SidechainsEventsTestSuite::storeSidechainWithCurrentHeight(txCreationUtils::CNakedCCoinsViewCache& view,
                                                                const uint256& scId,
                                                                const CSidechain& sidechain,
                                                                int chainActiveHeight)
{
    chainSettingUtils::ExtendChainActiveToHeight(chainActiveHeight);
    view.SetBestBlock(chainActive.Tip()->GetBlockHash());
    txCreationUtils::storeSidechain(view.getSidechainMap(), scId, sidechain);
}
