#include <gtest/gtest.h>
#include <chainparams.h>
#include <coins.h>
#include <sc/sidechain_handler.h>
#include "tx_creation_utils.h"
#include <main.h>
#include <undo.h>
#include <consensus/validation.h>

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
        scHandler = new CSidechainHandler();
        scHandler->setView(*view);

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

    sidechainState state = scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height());
    EXPECT_TRUE(state == sidechainState::NOT_APPLICABLE)
        <<"sc is in state "<<int(state);
}

TEST_F(SidechainHandlerTestSuite, SidechainInItsFirstEpochIsNotCeased) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10), /*height*/10);
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    int endEpochHeight = scInfo.StartHeightForEpoch(currentEpoch+1)-1;

    for(int height = chainActive.Height(); height <= endEpochHeight; ++height) {
        chainSettingUtils::GenerateChainActive(height);
        sidechainState state = scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height());
        EXPECT_TRUE(state == sidechainState::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(SidechainHandlerTestSuite, SidechainIsNotCeasedBeforeNextEpochSafeguard) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10), /*epochLength*/11);
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);

    for(int height = nextEpochStart; height <= nextEpochStart + scInfo.SafeguardMargin(); ++height) {
        chainSettingUtils::GenerateChainActive(height);
        sidechainState state = scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height());
        EXPECT_TRUE(state == sidechainState::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(SidechainHandlerTestSuite, SidechainIsCeasedAftereNextEpochSafeguard) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10),/*epochLength*/100);
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochEnd = scInfo.StartHeightForEpoch(currentEpoch+2)-1;

    for(int height = nextEpochStart + scInfo.SafeguardMargin()+1; height <= nextEpochEnd; ++height) {
        chainSettingUtils::GenerateChainActive(height);
        sidechainState state = scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height());
        EXPECT_TRUE(state == sidechainState::CEASED)
            <<"sc is in state "<<int(state)<<" at height "<<height;
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

    chainSettingUtils::GenerateChainActive(nextEpochSafeguard+1);
    sidechainState state = scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height());
    ASSERT_TRUE(state == sidechainState::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<chainActive.Height();

    //Prove that certificate reception keeps Sc alive for another epoch
    chainSettingUtils::GenerateChainActive(nextEpochSafeguard -1);
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, chainActive[nextEpochStart-1]->GetBlockHash(), CAmount(0));
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    int certReceptionHeight = chainActive.Height();
    for(int height = certReceptionHeight; height < certReceptionHeight +scInfo.creationData.withdrawalEpochLength; ++height) {
        chainSettingUtils::GenerateChainActive(height);
        sidechainState state = scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height());
        EXPECT_TRUE(state == sidechainState::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
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
    bool res = scHandler->registerSidechain(scId, chainActive.Height());

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainHandlerTestSuite, ReregistrationsAreAllowed) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());
    ASSERT_TRUE(scHandler->registerSidechain(scId, chainActive.Height()));

    //test
    bool res = scHandler->registerSidechain(scId, chainActive.Height());

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainHandlerTestSuite, UnknownSidechainsCannotBeRegistered) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    ASSERT_FALSE(view->HaveSidechain(scId));

    //test
    bool res = scHandler->registerSidechain(scId, chainActive.Height());

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
    ASSERT_TRUE(scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height()) == sidechainState::CEASED);
    //test
    bool res = scHandler->registerSidechain(scId, chainActive.Height());

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainHandlerTestSuite, FutureSidechainsCannotBeRegistered) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height() + 10);

    //test
    bool res = scHandler->registerSidechain(scId, chainActive.Height());

    //checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////7/////// addCertificate //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainHandlerTestSuite, CertificateSimpleAddition) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());
    ASSERT_TRUE(scHandler->registerSidechain(scId, chainActive.Height()));

    //Create certificate and update Sidechain
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();

    chainSettingUtils::GenerateChainActive(nextEpochSafeguard -1);
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, chainActive[nextEpochStart-1]->GetBlockHash(), CAmount(0));
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    //test
    EXPECT_TRUE(scHandler->addCertificate(cert, chainActive.Height()));
}

TEST_F(SidechainHandlerTestSuite, CannotAddCertificateForUnregisteredSc) {
    //Create sidechain but do not register it
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());


    //Create certificate and update Sidechain
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();

    chainSettingUtils::GenerateChainActive(nextEpochSafeguard -1);
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, chainActive[nextEpochStart-1]->GetBlockHash(), CAmount(0));
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    //test
    EXPECT_FALSE(scHandler->addCertificate(cert, chainActive.Height()));
}

TEST_F(SidechainHandlerTestSuite, CannotAddCertificateForCeasedSidechains) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());
    ASSERT_TRUE(scHandler->registerSidechain(scId, chainActive.Height()));

    //Let Sidechain cease
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();
    chainSettingUtils::GenerateChainActive(nextEpochSafeguard+1);
    ASSERT_TRUE(scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height()) == sidechainState::CEASED);

    //Move to next epoch and try adding next epoch certificate
    int certEpoch = scInfo.EpochFor(nextEpochStart);
    int certEpochStart = scInfo.StartHeightForEpoch(certEpoch);
    CScCertificate cert = txCreationUtils::createCertificate(scId, certEpoch, chainActive[certEpochStart]->GetBlockHash(), CAmount(0));

    //test
    EXPECT_FALSE(scHandler->addCertificate(cert, chainActive.Height()));
}

///////////////////////////////////////////////////////////////////////////////
////////////////7/////// handleCeasingSidechains //////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainHandlerTestSuite, PureBwtCoinsAreRemovedWhenSidechainCeases) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());
    ASSERT_TRUE(scHandler->registerSidechain(scId, chainActive.Height()));

    //Generate certificate and register it
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    int Epoch1StartHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+1);
    chainSettingUtils::GenerateChainActive(Epoch1StartHeight);
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, chainActive[Epoch1StartHeight-1]->GetBlockHash(), CAmount(0), /*bwtOnly*/ true);
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, chainActive.Height());
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //Register certificate
    ASSERT_TRUE(scHandler->addCertificate(cert, chainActive.Height()));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    chainSettingUtils::GenerateChainActive(minimalCeaseHeight);
    EXPECT_TRUE(scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height()) == sidechainState::CEASED);

    //test
    scHandler->handleCeasingSidechains(blockUndo, chainActive.Height());

    //Checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    unsigned int bwtCounter = 0;
    EXPECT_TRUE(blockUndo.vtxundo.size() == 1);
    for(const CTxOut& out: cert.GetVout()) { //outputs in blockUndo are bwt
        if (out.isFromBackwardTransfer) {
            EXPECT_TRUE((blockUndo.vtxundo[0].vprevout[bwtCounter].nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f))<<blockUndo.vtxundo[0].vprevout[bwtCounter].nVersion;
            EXPECT_TRUE(blockUndo.vtxundo[0].vprevout[bwtCounter].originScId == scId);
            EXPECT_TRUE(out == blockUndo.vtxundo[0].vprevout[bwtCounter].txout);
            ++bwtCounter;
        }
    }

    EXPECT_TRUE(cert.GetVout().size() == bwtCounter); //all cert outputs are handled
}

TEST_F(SidechainHandlerTestSuite, ChangeOutputsArePreservedWhenSidechainCeases) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());
    ASSERT_TRUE(scHandler->registerSidechain(scId, chainActive.Height()));

    //Generate certificate and register it
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    int Epoch1StartHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+1);
    chainSettingUtils::GenerateChainActive(Epoch1StartHeight);
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, chainActive[Epoch1StartHeight-1]->GetBlockHash(), CAmount(0), /*bwtOnly*/ false);
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, chainActive.Height());
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //Register certificate
    ASSERT_TRUE(scHandler->addCertificate(cert, chainActive.Height()));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    chainSettingUtils::GenerateChainActive(minimalCeaseHeight);
    EXPECT_TRUE(scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height()) == sidechainState::CEASED);

    //test
    scHandler->handleCeasingSidechains(blockUndo, chainActive.Height());

    //Checks
    CCoins updatedCoin;
    unsigned int changeCounter = 0;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),updatedCoin));
    for (const CTxOut& out: updatedCoin.vout) {//outputs in coin are changes
        EXPECT_TRUE(out.isFromBackwardTransfer == false);
        ++changeCounter;
    }

    unsigned int bwtCounter = 0;
    EXPECT_TRUE(blockUndo.vtxundo.size() == 1);
    for(const CTxOut& out: cert.GetVout()) { //outputs in blockUndo are bwt
        if (out.isFromBackwardTransfer) {
            EXPECT_TRUE(out == blockUndo.vtxundo[0].vprevout[bwtCounter].txout);
            ++bwtCounter;
        }
    }

    EXPECT_TRUE(cert.GetVout().size() == changeCounter+bwtCounter); //all cert outputs are handled
}

TEST_F(SidechainHandlerTestSuite, UnregisteredSidechainsAreNotAffectedByHandling) {
    //Create sidechain without registering it
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());

    //Generate certificate but do not add it
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    int Epoch1StartHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+1);
    chainSettingUtils::GenerateChainActive(Epoch1StartHeight);
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, chainActive[Epoch1StartHeight-1]->GetBlockHash(), CAmount(0), /*bwtOnly*/ true);
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, chainActive.Height());
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    chainSettingUtils::GenerateChainActive(minimalCeaseHeight);
    EXPECT_TRUE(scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height()) == sidechainState::CEASED);

    //test
    scHandler->handleCeasingSidechains(blockUndo, chainActive.Height());

    //Checks
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////// restoreCeasedSidechains //////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainHandlerTestSuite, RestoreFullyNulledCeasedCoins) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());
    ASSERT_TRUE(scHandler->registerSidechain(scId, chainActive.Height()));

    //Generate certificate and register it
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    int Epoch1StartHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+1);
    chainSettingUtils::GenerateChainActive(Epoch1StartHeight);
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, chainActive[Epoch1StartHeight-1]->GetBlockHash(), CAmount(0), /*bwtOnly*/ true);
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, chainActive.Height());
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //Register certificate
    ASSERT_TRUE(scHandler->addCertificate(cert, chainActive.Height()));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    chainSettingUtils::GenerateChainActive(minimalCeaseHeight);
    EXPECT_TRUE(scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height()) == sidechainState::CEASED);

    // Null the coins
    scHandler->handleCeasingSidechains(blockUndo, chainActive.Height());
    ASSERT_FALSE(view->HaveCoins(cert.GetHash()));

    //test
    for (const CTxUndo& ceasedCoinUndo: blockUndo.vtxundo)
        scHandler->restoreCeasedSidechains(ceasedCoinUndo);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.vout.size()       == cert.GetVout().size());
    EXPECT_TRUE(rebuiltCoin.nHeight           == Epoch1StartHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f) == (cert.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.originScId        == cert.GetScId());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout[pos] == cert.GetVout()[pos]);
    }
}

TEST_F(SidechainHandlerTestSuite, RestorePartiallyNulledCeasedCoins) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());
    ASSERT_TRUE(scHandler->registerSidechain(scId, chainActive.Height()));

    //Generate certificate and register it
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    int Epoch1StartHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+1);
    chainSettingUtils::GenerateChainActive(Epoch1StartHeight);
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, chainActive[Epoch1StartHeight-1]->GetBlockHash(), CAmount(0), /*bwtOnly*/ false);
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, chainActive.Height());
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //Register certificate
    ASSERT_TRUE(scHandler->addCertificate(cert, chainActive.Height()));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    chainSettingUtils::GenerateChainActive(minimalCeaseHeight);
    EXPECT_TRUE(scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height()) == sidechainState::CEASED);

    // Null the coins
    scHandler->handleCeasingSidechains(blockUndo, chainActive.Height());

    //test
    for (const CTxUndo& ceasedCoinUndo: blockUndo.vtxundo)
        scHandler->restoreCeasedSidechains(ceasedCoinUndo);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.vout.size()       == cert.GetVout().size());
    EXPECT_TRUE(rebuiltCoin.nHeight           == Epoch1StartHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f) == (cert.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.originScId        == cert.GetScId());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout[pos] == cert.GetVout()[pos]);
    }
}
///////////////////////////////////////////////////////////////////////////////
////////////////////////// unregisterSidechain ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainHandlerTestSuite, UnregisteredSidechainsWontHaveTheirCeasedCoinsHandled) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());
    ASSERT_TRUE(scHandler->registerSidechain(scId, chainActive.Height()));

    //Generate certificate and register it
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    int Epoch1StartHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+1);
    chainSettingUtils::GenerateChainActive(Epoch1StartHeight);
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, chainActive[Epoch1StartHeight-1]->GetBlockHash(), CAmount(0), /*bwtOnly*/ false);
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, chainActive.Height());
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //Register certificate
    ASSERT_TRUE(scHandler->addCertificate(cert, chainActive.Height()));

    //Unregister sidechain
    scHandler->unregisterSidechain(scId);

    //Show that ceased coins are not removed
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    chainSettingUtils::GenerateChainActive(minimalCeaseHeight);
    EXPECT_TRUE(scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height()) == sidechainState::CEASED);

    // Attempt to null the ceased coins
    scHandler->handleCeasingSidechains(blockUndo, chainActive.Height());

    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));
}

TEST_F(SidechainHandlerTestSuite, ReregisteringSidechainsResumeCeasedSidechainsHandling) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height());
    ASSERT_TRUE(scHandler->registerSidechain(scId, chainActive.Height()));

    //Generate certificate and register it
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    int Epoch1StartHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+1);
    chainSettingUtils::GenerateChainActive(Epoch1StartHeight);
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, chainActive[Epoch1StartHeight-1]->GetBlockHash(), CAmount(0), /*bwtOnly*/ true);
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, chainActive.Height());
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //Register certificate
    ASSERT_TRUE(scHandler->addCertificate(cert, chainActive.Height()));

    //Unregister sidechain
    scHandler->unregisterSidechain(scId);

    //Move forward and re-register sidechain
    chainSettingUtils::GenerateChainActive(chainActive.Height()+1);
    ASSERT_TRUE(scHandler->registerSidechain(scId, chainActive.Height()));
    ASSERT_TRUE(scHandler->addCertificate(cert, chainActive.Height()));

    //Show that ceased coins are not removed
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    chainSettingUtils::GenerateChainActive(minimalCeaseHeight);
    EXPECT_TRUE(scHandler->isSidechainCeasedAtHeight(scId, chainActive.Height()) == sidechainState::CEASED);

    // Attempt to null the ceased coins
    scHandler->handleCeasingSidechains(blockUndo, chainActive.Height());

    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
}
