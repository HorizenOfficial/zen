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

    Sidechain::state state = Sidechain::isCeasedAtHeight(*view, scId, chainActive.Height());
    EXPECT_TRUE(state == Sidechain::state::NOT_APPLICABLE)
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
        Sidechain::state state = Sidechain::isCeasedAtHeight(*view, scId, chainActive.Height());
        EXPECT_TRUE(state == Sidechain::state::ALIVE)
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
        Sidechain::state state = Sidechain::isCeasedAtHeight(*view, scId, chainActive.Height());
        EXPECT_TRUE(state == Sidechain::state::ALIVE)
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
        Sidechain::state state = Sidechain::isCeasedAtHeight(*view, scId, chainActive.Height());
        EXPECT_TRUE(state == Sidechain::state::CEASED)
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
    Sidechain::state state = Sidechain::isCeasedAtHeight(*view, scId, chainActive.Height());
    ASSERT_TRUE(state == Sidechain::state::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<chainActive.Height();

    //Prove that certificate reception keeps Sc alive for another epoch
    chainSettingUtils::GenerateChainActive(nextEpochSafeguard -1);
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, chainActive[nextEpochStart-1]->GetBlockHash(), CAmount(0));
    CBlockUndo blockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, blockUndo));

    int certReceptionHeight = chainActive.Height();
    for(int height = certReceptionHeight; height < certReceptionHeight +scInfo.creationData.withdrawalEpochLength; ++height) {
        chainSettingUtils::GenerateChainActive(height);
        Sidechain::state state = Sidechain::isCeasedAtHeight(*view, scId, chainActive.Height());
        EXPECT_TRUE(state == Sidechain::state::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////// Ceasing Sidechain handling ////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainHandlerTestSuite, CeasingHeightUpdateForScCreation) {
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;

    //test
    EXPECT_TRUE(view->UpdateScInfo(scCreationTx, aBlock, chainActive.Height()+1));

    //Checks
    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int ceasingHeight = scInfo.StartHeightForEpoch(1)+scInfo.SafeguardMargin()+1;
    CCeasingSidechains ceasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(ceasingHeight, ceasingScIds));
    EXPECT_TRUE(ceasingScIds.ceasingScs.count(scId) != 0);

}

TEST_F(SidechainHandlerTestSuite, CeasingHeightUpdateForCertificate) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    view->UpdateScInfo(scCreationTx, aBlock, creationHeight);

    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash, CAmount(0));

    //test
    CBlockUndo dummyUndo;
    EXPECT_TRUE(view->UpdateScInfo(cert, dummyUndo));

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////////// HandleCeasingScs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainHandlerTestSuite, PureBwtCoinsAreRemovedWhenSidechainCeases) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    ASSERT_TRUE(view->UpdateScInfo(scCreationTx, aBlock, scCreationHeight));

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(), CAmount(0), /*bwtOnly*/ true);
    CBlockUndo certBlockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, certBlockUndo));

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scCreationHeight);
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(Sidechain::isCeasedAtHeight(*view, scId, minimalCeaseHeight) == Sidechain::state::CEASED);
    CBlockUndo coinsBlockUndo;
    EXPECT_TRUE(view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo));

    //Checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    unsigned int bwtCounter = 0;
    EXPECT_TRUE(coinsBlockUndo.vtxundo.size() == 1);
    for(const CTxOut& out: cert.GetVout()) { //outputs in blockUndo are bwt
        if (out.isFromBackwardTransfer) {
            EXPECT_TRUE((coinsBlockUndo.vtxundo[0].vprevout[bwtCounter].nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f))<<coinsBlockUndo.vtxundo[0].vprevout[bwtCounter].nVersion;
            EXPECT_TRUE(coinsBlockUndo.vtxundo[0].vprevout[bwtCounter].originScId == scId);
            EXPECT_TRUE(out == coinsBlockUndo.vtxundo[0].vprevout[bwtCounter].txout);
            ++bwtCounter;
        }
    }

    EXPECT_TRUE(cert.GetVout().size() == bwtCounter); //all cert outputs are handled
}

TEST_F(SidechainHandlerTestSuite, ChangeOutputsArePreservedWhenSidechainCeases) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    ASSERT_TRUE(view->UpdateScInfo(scCreationTx, aBlock, scCreationHeight));

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(), CAmount(0), /*bwtOnly*/ false);
    CBlockUndo certBlockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, certBlockUndo));

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scInfo.StartHeightForEpoch(1));
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(Sidechain::isCeasedAtHeight(*view, scId, minimalCeaseHeight) == Sidechain::state::CEASED);
    CBlockUndo coinsBlockUndo;
    EXPECT_TRUE(view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo));

    //Checks
    CCoins updatedCoin;
    unsigned int changeCounter = 0;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),updatedCoin));
    for (const CTxOut& out: updatedCoin.vout) {//outputs in coin are changes
        EXPECT_TRUE(out.isFromBackwardTransfer == false);
        ++changeCounter;
    }

    unsigned int bwtCounter = 0;
    EXPECT_TRUE(coinsBlockUndo.vtxundo.size() == 1);
    for(const CTxOut& out: cert.GetVout()) { //outputs in blockUndo are bwt
        if (out.isFromBackwardTransfer) {
            EXPECT_TRUE(out == coinsBlockUndo.vtxundo[0].vprevout[bwtCounter].txout);
            ++bwtCounter;
        }
    }

    EXPECT_TRUE(cert.GetVout().size() == changeCounter+bwtCounter); //all cert outputs are handled
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////////// RevertCeasingScs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainHandlerTestSuite, RestoreFullyNulledCeasedCoins) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock scCreationBlock;
    view->UpdateScInfo(scCreationTx, scCreationBlock, /*height*/1789);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, endEpochBlock.GetHash(), CAmount(0), /*bwtOnly*/ true);
    CBlockUndo certBlockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, certBlockUndo));

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scInfo.StartHeightForEpoch(1));
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(Sidechain::isCeasedAtHeight(*view, scId, minimalCeaseHeight) == Sidechain::state::CEASED);

    // Null the coins
    CBlockUndo coinsBlockUndo;
    view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo);
    ASSERT_FALSE(view->HaveCoins(cert.GetHash()));

    //test
    for (const CTxUndo& ceasedCoinUndo: coinsBlockUndo.vtxundo)
        view->RevertCeasingScs(ceasedCoinUndo);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.nHeight           == originalCoins.nHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f) == (originalCoins.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.originScId        == originalCoins.originScId);
    EXPECT_TRUE(rebuiltCoin.vout.size()       == originalCoins.vout.size());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout[pos] == originalCoins.vout[pos]);
    }
}

TEST_F(SidechainHandlerTestSuite, RestorePartiallyNulledCeasedCoins) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock scCreationBlock;
    view->UpdateScInfo(scCreationTx, scCreationBlock, /*height*/1789);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, endEpochBlock.GetHash(), CAmount(0), /*bwtOnly*/ false);
    CBlockUndo certBlockUndo;
    ASSERT_TRUE(view->UpdateScInfo(cert, certBlockUndo));

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scInfo.StartHeightForEpoch(1));
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(Sidechain::isCeasedAtHeight(*view, scId, minimalCeaseHeight) == Sidechain::state::CEASED);

    // Null the coins
    CBlockUndo coinsBlockUndo;
    view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo);

    //test
    for (const CTxUndo& ceasedCoinUndo: coinsBlockUndo.vtxundo)
        view->RevertCeasingScs(ceasedCoinUndo);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.nHeight           == originalCoins.nHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f) == (originalCoins.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.originScId        == originalCoins.originScId);
    EXPECT_TRUE(rebuiltCoin.vout.size()       == originalCoins.vout.size());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout[pos] == originalCoins.vout[pos]);
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
    EXPECT_TRUE(Sidechain::isCeasedAtHeight(*view, scId, chainActive.Height()) == Sidechain::state::CEASED);

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
    EXPECT_TRUE(Sidechain::isCeasedAtHeight(*view, scId, chainActive.Height()) == Sidechain::state::CEASED);

    // Attempt to null the ceased coins
    scHandler->handleCeasingSidechains(blockUndo, chainActive.Height());

    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
}
