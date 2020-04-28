#include <gtest/gtest.h>
#include <chainparams.h>
#include <coins.h>
#include <sc/sidechain_handler.h>
#include "tx_creation_utils.h"
#include <main.h>
#include <undo.h>

class SidechainHandlerTestSuite: public ::testing::Test {

public:
    SidechainHandlerTestSuite():
        dummyBackingView(nullptr)
        , view(nullptr)
        , scHandler(nullptr) {};

    ~SidechainHandlerTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        dummyBackingView = new CCoinsView();
        view = new CCoinsViewCache(dummyBackingView);
        scHandler = new CSidechainHandler(*view);

        chainSettingUtils::GenerateChainActive(220);
    };

    void TearDown() override {
        chainActive.SetTip(nullptr);
        mapBlockIndex.clear();

        delete scHandler;
        scHandler = nullptr;

        delete view;
        view = nullptr;

        delete dummyBackingView;
        dummyBackingView = nullptr;
    };

protected:
    CCoinsView        *dummyBackingView;
    CCoinsViewCache   *view;
    CSidechainHandler *scHandler;
};


///////////////////////////////////////////////////////////////////////////////
/////////////////////////// isSidechainCeased /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainHandlerTestSuite, UnknownSidechainIsNeitherAliveNorCeased) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    ASSERT_FALSE(view->HaveSidechain(scId));

    EXPECT_TRUE(scHandler->isSidechainCeased(scId) == sidechainState::NOT_APPLICABLE);
}

TEST_F(SidechainHandlerTestSuite, SidechainInItsFirstEpochIsNotCeased) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    int endEpochHeight = scInfo.StartHeightForEpoch(currentEpoch+1)-1;

    for(int height = chainActive.Height(); height <= endEpochHeight; ++height) {
        chainSettingUtils::GenerateChainActive(height);
        EXPECT_TRUE(scHandler->isSidechainCeased(scId) == sidechainState::ALIVE);
    }
}

TEST_F(SidechainHandlerTestSuite, SidechainIsNotCeasedBeforeNextEpochSafeguard) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);

    for(int height = nextEpochStart; height < nextEpochStart + scInfo.SafeguardMargin(); ++height) {
        chainSettingUtils::GenerateChainActive(height);
        EXPECT_TRUE(scHandler->isSidechainCeased(scId) == sidechainState::ALIVE);
    }
}

TEST_F(SidechainHandlerTestSuite, SidechainIsCeasedAftereNextEpochSafeguard) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochEnd = scInfo.StartHeightForEpoch(currentEpoch+2)-1;

    for(int height = nextEpochStart + scInfo.SafeguardMargin(); height < nextEpochEnd; ++height) {
        chainSettingUtils::GenerateChainActive(height);
        EXPECT_TRUE(scHandler->isSidechainCeased(scId) == sidechainState::CEASED);
    }
}

TEST_F(SidechainHandlerTestSuite, CertificateMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());

    //Prove it would expire without certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();

    chainSettingUtils::GenerateChainActive(nextEpochSafeguard);
    ASSERT_TRUE(scHandler->isSidechainCeased(scId) == sidechainState::CEASED)<<"Sc should cease without certificate";

    //Prove that certificate reception keeps Sc alive for another epoch
    chainSettingUtils::GenerateChainActive(nextEpochSafeguard -1);
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, chainActive[nextEpochStart-1]->GetBlockHash(), CAmount(0));
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    int certReceptionHeight = chainActive.Height();
    for(int height = certReceptionHeight; height < certReceptionHeight +scInfo.creationData.withdrawalEpochLength; ++height) {
        chainSettingUtils::GenerateChainActive(height);
        EXPECT_TRUE(scHandler->isSidechainCeased(scId) == sidechainState::ALIVE);
    }
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// registerSidechain /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainHandlerTestSuite, SimpleSidechainRegistration) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());

    //test
    bool res = scHandler->registerSidechain(scId);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainHandlerTestSuite, ReRegistrationsAreForbidden) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());
    ASSERT_TRUE(scHandler->registerSidechain(scId));

    //test
    bool res = scHandler->registerSidechain(scId);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainHandlerTestSuite, UnknownSidechainsCannotBeRegistered) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    ASSERT_FALSE(view->HaveSidechain(scId));

    //test
    bool res = scHandler->registerSidechain(scId);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainHandlerTestSuite, CeasedSidechainsCannotBeRegistered) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());

    //cease the sidechain
    chainSettingUtils::GenerateChainActive(chainActive.Height() + 2*scCreationTx.vsc_ccout[0].withdrawalEpochLength);
    ASSERT_TRUE(scHandler->isSidechainCeased(scId) == sidechainState::CEASED);
    //test
    bool res = scHandler->registerSidechain(scId);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainHandlerTestSuite, FutureSidechainsCannotBeRegistered) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height() + 10);

    //test
    bool res = scHandler->registerSidechain(scId);

    //checks
    EXPECT_FALSE(res);
}

