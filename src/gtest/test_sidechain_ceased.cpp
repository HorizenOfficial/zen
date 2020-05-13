#include <gtest/gtest.h>
#include <chainparams.h>
#include <coins.h>
#include "tx_creation_utils.h"
#include <main.h>
#include <undo.h>
#include <consensus/validation.h>

class CeasedSidechainsTestSuite: public ::testing::Test {

public:
    CeasedSidechainsTestSuite():
        dummyBackingView(nullptr)
        , view(nullptr) {};

    ~CeasedSidechainsTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        dummyBackingView = new CCoinsView();
        view = new CCoinsViewCache(dummyBackingView);
    };

    void TearDown() override {
        delete view;
        view = nullptr;

        delete dummyBackingView;
        dummyBackingView = nullptr;
    };

protected:
    CCoinsView        *dummyBackingView;
    CCoinsViewCache   *view;
};


///////////////////////////////////////////////////////////////////////////////
/////////////////////////// isSidechainCeased /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CeasedSidechainsTestSuite, UnknownSidechainIsNeitherAliveNorCeased) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1912;
    ASSERT_FALSE(view->HaveSidechain(scId));

    CSidechain::state state = view->isCeasedAtHeight(scId, creationHeight);
    EXPECT_TRUE(state == CSidechain::state::NOT_APPLICABLE)
        <<"sc is in state "<<int(state);
}

TEST_F(CeasedSidechainsTestSuite, SidechainInItsFirstEpochIsNotCeased) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1912;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10), /*height*/10);
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int endEpochHeight = scInfo.StartHeightForEpoch(currentEpoch+1)-1;

    for(int height = creationHeight; height <= endEpochHeight; ++height) {
        CSidechain::state state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::state::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite, SidechainIsNotCeasedBeforeNextEpochSafeguard) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1945;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10), /*epochLength*/11);
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);

    for(int height = nextEpochStart; height <= nextEpochStart + scInfo.SafeguardMargin(); ++height) {
        CSidechain::state state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::state::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite, SidechainIsCeasedAftereNextEpochSafeguard) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10),/*epochLength*/100);
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochEnd = scInfo.StartHeightForEpoch(currentEpoch+2)-1;

    for(int height = nextEpochStart + scInfo.SafeguardMargin()+1; height <= nextEpochEnd; ++height) {
        CSidechain::state state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::state::CEASED)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite, FullCertMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();

    CSidechain::state state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::state::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(), /*numChangeOut*/2, /*bwtAmount*/CAmount(0), /*numBwt*/2);
    CBlockUndo blockUndo;
    view->UpdateScInfo(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +scInfo.creationData.withdrawalEpochLength; ++height) {
        CSidechain::state state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::state::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite, PureBwtCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();

    CSidechain::state state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::state::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(), /*numChangeOut*/10, /*bwtAmount*/CAmount(0), /*numBwt*/1);
    CBlockUndo blockUndo;
    view->UpdateScInfo(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +scInfo.creationData.withdrawalEpochLength; ++height) {
        CSidechain::state state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::state::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite, NoBwtCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();

    CSidechain::state state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::state::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(), /*numChangeOut*/1, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo blockUndo;
    view->UpdateScInfo(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +scInfo.creationData.withdrawalEpochLength; ++height) {
        CSidechain::state state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::state::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite,EmptyCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();

    CSidechain::state state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::state::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(), /*numChangeOut*/0, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo blockUndo;
    view->UpdateScInfo(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +scInfo.creationData.withdrawalEpochLength; ++height) {
        CSidechain::state state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::state::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}
///////////////////////////////////////////////////////////////////////////////
/////////////////////// Ceasing Sidechain updates /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CeasedSidechainsTestSuite, CeasingHeightUpdateForScCreation) {
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1492;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    ASSERT_TRUE(view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight));

    //test
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        EXPECT_TRUE(view->UpdateCeasingScs(scCreationOut));

    //Checks
    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int ceasingHeight = scInfo.StartHeightForEpoch(1)+scInfo.SafeguardMargin()+1;
    CCeasingSidechains ceasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(ceasingHeight, ceasingScIds));
    EXPECT_TRUE(ceasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(CeasedSidechainsTestSuite, CeasingHeightUpdateForFullCert) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,/*numChangeOut*/2,/*bwtAmount*/CAmount(0));

    CBlockUndo dummyUndo;
    EXPECT_TRUE(view->UpdateScInfo(cert, dummyUndo));

    //test
    view->UpdateCeasingScs(cert);

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));
}

TEST_F(CeasedSidechainsTestSuite, CeasingHeightUpdateForPureBwtCert) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,/*numChangeOut*/0,/*bwtAmount*/CAmount(0), /*numBwt*/4);

    CBlockUndo dummyUndo;
    EXPECT_TRUE(view->UpdateScInfo(cert, dummyUndo));

    //test
    view->UpdateCeasingScs(cert);

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));
}

TEST_F(CeasedSidechainsTestSuite, CeasingHeightUpdateForNoBwtCert) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,/*numChangeOut*/3,/*bwtAmount*/CAmount(0), /*numBwt*/0);

    CBlockUndo dummyUndo;
    EXPECT_TRUE(view->UpdateScInfo(cert, dummyUndo));

    //test
    view->UpdateCeasingScs(cert);

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));
}

TEST_F(CeasedSidechainsTestSuite, CeasingHeightUpdateForEmptyCertificate) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,/*numChangeOut*/0,/*bwtAmount*/CAmount(0), /*numBwt*/0);

    CBlockUndo dummyUndo;
    EXPECT_TRUE(view->UpdateScInfo(cert, dummyUndo));

    //test
    view->UpdateCeasingScs(cert);

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
TEST_F(CeasedSidechainsTestSuite, FullCertCoinsHaveBwtStrippedOutWhenSidechainCeases) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/2, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scCreationHeight);
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::state::CEASED);
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

TEST_F(CeasedSidechainsTestSuite, PureBwtCoinsAreRemovedWhenSidechainCeases) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scCreationHeight);
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::state::CEASED);
    CBlockUndo coinsBlockUndo;
    EXPECT_TRUE(view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo));

    //Checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    unsigned int bwtCounter = 0;
    EXPECT_TRUE(coinsBlockUndo.vtxundo.size() == 1);
    for(const CTxOut& out: cert.GetVout()) { //outputs in blockUndo are bwt
        if (out.isFromBackwardTransfer) {
            EXPECT_TRUE( (coinsBlockUndo.vtxundo[0].vprevout[bwtCounter].nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f))
                         <<coinsBlockUndo.vtxundo[0].vprevout[bwtCounter].nVersion;
            EXPECT_TRUE(coinsBlockUndo.vtxundo[0].vprevout[bwtCounter].originScId == scId);
            EXPECT_TRUE(out == coinsBlockUndo.vtxundo[0].vprevout[bwtCounter].txout);
            ++bwtCounter;
        }
    }

    EXPECT_TRUE(cert.GetVout().size() == bwtCounter); //all cert outputs are handled
}

TEST_F(CeasedSidechainsTestSuite, NoBwtCertificatesCoinsAreNotAffectedByCeasedSidechainHandling) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/2, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scCreationHeight);
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::state::CEASED);
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
    EXPECT_TRUE(coinsBlockUndo.vtxundo.size() == 0);
    EXPECT_TRUE(cert.GetVout().size() == changeCounter+bwtCounter); //all cert outputs are handled
}

TEST_F(CeasedSidechainsTestSuite, EmptyCertificatesCoinsAreNotAffectedByCeasedSidechainHandling) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scCreationHeight);
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::state::CEASED);
    CBlockUndo coinsBlockUndo;
    EXPECT_TRUE(view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo));

    //Checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
}
///////////////////////////////////////////////////////////////////////////////
////////////////////////////// RevertCeasingScs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CeasedSidechainsTestSuite, RestoreFullCertCeasedCoins) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock scCreationBlock;
    view->UpdateScInfo(scCreationTx, scCreationBlock, /*height*/1789);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, endEpochBlock.GetHash(),/*numChangeOut*/2, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scInfo.StartHeightForEpoch(1));
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::state::CEASED);

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

TEST_F(CeasedSidechainsTestSuite, RestorePureBwtCeasedCoins) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock scCreationBlock;
    view->UpdateScInfo(scCreationTx, scCreationBlock, /*height*/1789);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, endEpochBlock.GetHash(),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scInfo.StartHeightForEpoch(1));
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::state::CEASED);

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

TEST_F(CeasedSidechainsTestSuite, RestoreNoBwtCeasedCoins) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock scCreationBlock;
    view->UpdateScInfo(scCreationTx, scCreationBlock, /*height*/1789);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, endEpochBlock.GetHash(),/*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scInfo.StartHeightForEpoch(1));
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::state::CEASED);

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

TEST_F(CeasedSidechainsTestSuite, RestoreEmptyCertCeasedCoins) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock scCreationBlock;
    view->UpdateScInfo(scCreationTx, scCreationBlock, /*height*/1789);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, endEpochBlock.GetHash(),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, state, *view, txundo, scInfo.StartHeightForEpoch(1));
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::state::CEASED);

    // Null the coins
    CBlockUndo coinsBlockUndo;
    view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo);

    //test
    for (const CTxUndo& ceasedCoinUndo: coinsBlockUndo.vtxundo)
        view->RevertCeasingScs(ceasedCoinUndo);

    //checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
}
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// UndoCeasingScs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CeasedSidechainsTestSuite, UndoCeasingScs) {
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1492;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    ASSERT_TRUE(view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight));

    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        EXPECT_TRUE(view->UpdateCeasingScs(scCreationOut));

    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int ceasingHeight = scInfo.StartHeightForEpoch(1)+scInfo.SafeguardMargin()+1;
    CCeasingSidechains ceasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(ceasingHeight, ceasingScIds));
    EXPECT_TRUE(ceasingScIds.ceasingScs.count(scId) != 0);

    //test
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        EXPECT_TRUE(view->UndoCeasingScs(scCreationOut));

    //checks
    CCeasingSidechains restoredCeasingScIds;
    EXPECT_FALSE(view->HaveCeasingScs(ceasingHeight));
}

TEST_F(CeasedSidechainsTestSuite, UndoFullCertUpdatesToCeasingScs) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"), /*numChangeOut*/4, /*bwtTotalAmount*/CAmount(0), /*numBwt*/3);
    CBlockUndo dummyUndo;
    view->UpdateScInfo(cert, dummyUndo);
    view->UpdateCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));

    //test
    view->UndoCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);

    EXPECT_FALSE(view->HaveCeasingScs(newCeasingHeight));
    CCeasingSidechains restoredCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(CeasedSidechainsTestSuite, UndoPureBwtCertUpdatesToCeasingScs) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/3);
    CBlockUndo dummyUndo;
    view->UpdateScInfo(cert, dummyUndo);
    view->UpdateCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));

    //test
    view->UndoCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);

    EXPECT_FALSE(view->HaveCeasingScs(newCeasingHeight));
    CCeasingSidechains restoredCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(CeasedSidechainsTestSuite, UndoNoBwtCertUpdatesToCeasingScs) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"), /*numChangeOut*/4, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo dummyUndo;
    view->UpdateScInfo(cert, dummyUndo);
    view->UpdateCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));

    //test
    view->UndoCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);

    EXPECT_FALSE(view->HaveCeasingScs(newCeasingHeight));
    CCeasingSidechains restoredCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(CeasedSidechainsTestSuite, UndoEmptyCertUpdatesToCeasingScs) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo dummyUndo;
    view->UpdateScInfo(cert, dummyUndo);
    view->UpdateCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));

    //test
    view->UndoCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);

    EXPECT_FALSE(view->HaveCeasingScs(newCeasingHeight));
    CCeasingSidechains restoredCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
}
