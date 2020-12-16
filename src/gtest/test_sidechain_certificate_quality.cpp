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

class SidechainMultipleCertsTestSuite : public ::testing::Test {
public:
    SidechainMultipleCertsTestSuite(): fakeChainStateDb(nullptr), sidechainsView(nullptr),
                                       dummyBlock(), dummyUndo(), dummyBlockUndo(),
                                       dummyScriptPubKey() {
        dummyScriptPubKey = GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/false);
    }

    ~SidechainMultipleCertsTestSuite() = default;

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
    CInMemorySidechainDb *fakeChainStateDb;
    CCoinsViewCache      *sidechainsView;

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

    CValidationState    dummyState;

    uint256 storeSidechain(const uint256& scId, const CSidechain& sidechain);
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// UpdateScInfo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainMultipleCertsTestSuite, Cert_HigherQuality_SameEpoch_SidechainIsUpdated) {
    uint256 scId = uint256S("aaa");

    CSidechain initialScState;
    initialScState.prevBlockTopQualityCertHash = uint256S("cccc");
    initialScState.prevBlockTopQualityCertQuality = 100;
    initialScState.prevBlockTopQualityCertReferencedEpoch = 1987;
    initialScState.prevBlockTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    storeSidechain(scId, initialScState);

    //Insert high quality Certificate
    CMutableScCertificate highQualityCert;
    highQualityCert.scId        = scId;
    highQualityCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;
    highQualityCert.quality     = initialScState.prevBlockTopQualityCertQuality * 2;
    highQualityCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));
    CAmount highQualityCert_TotalBwtAmount = CScCertificate(highQualityCert).GetValueOfBackwardTransfers();

    //test
    ASSERT_TRUE(sidechainsView->UpdateScInfo(highQualityCert, dummyUndo));

    //check
    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.prevBlockTopQualityCertHash == highQualityCert.GetHash());
    EXPECT_TRUE(sidechain.prevBlockTopQualityCertQuality == highQualityCert.quality);
    EXPECT_TRUE(sidechain.prevBlockTopQualityCertBwtAmount == highQualityCert_TotalBwtAmount);
    EXPECT_TRUE(sidechain.balance == initialScState.balance + initialScState.prevBlockTopQualityCertBwtAmount -highQualityCert_TotalBwtAmount);
}

TEST_F(SidechainMultipleCertsTestSuite, Cert_HigherQuality_SameEpoch_SidechainIsNOTUpdated) {
    uint256 scId = uint256S("aaa");

    CSidechain initialScState;
    initialScState.prevBlockTopQualityCertHash = uint256S("cccc");
    initialScState.prevBlockTopQualityCertQuality = 100;
    initialScState.prevBlockTopQualityCertReferencedEpoch = 1987;
    initialScState.prevBlockTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    storeSidechain(scId, initialScState);

    //Insert low quality Certificate
    CMutableScCertificate lowQualityCert;
    lowQualityCert.scId        = scId;
    lowQualityCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;
    lowQualityCert.quality     = initialScState.prevBlockTopQualityCertQuality / 2;
    lowQualityCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));

    //test
    EXPECT_FALSE(sidechainsView->UpdateScInfo(lowQualityCert, dummyUndo));

    //check
    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.prevBlockTopQualityCertHash      == initialScState.prevBlockTopQualityCertHash);
    EXPECT_TRUE(sidechain.prevBlockTopQualityCertQuality   == initialScState.prevBlockTopQualityCertQuality);
    EXPECT_TRUE(sidechain.prevBlockTopQualityCertBwtAmount == initialScState.prevBlockTopQualityCertBwtAmount);
    EXPECT_TRUE(sidechain.balance                   == initialScState.balance);
}

TEST_F(SidechainMultipleCertsTestSuite, Cert_LowerQuality_DifferentEpoch_SidechainIsUpdated) {
    uint256 scId = uint256S("aaa");

    CSidechain initialScState;
    initialScState.prevBlockTopQualityCertHash = uint256S("cccc");
    initialScState.prevBlockTopQualityCertQuality = 100;
    initialScState.prevBlockTopQualityCertReferencedEpoch = 1987;
    initialScState.prevBlockTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    storeSidechain(scId, initialScState);

    //Insert next epoch Certificate
    CMutableScCertificate nextEpochCert;
    nextEpochCert.scId        = scId;
    nextEpochCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch + 1;
    nextEpochCert.quality     = initialScState.prevBlockTopQualityCertQuality / 2;
    nextEpochCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));
    CAmount nextEpochCert_TotalBwtAmount = CScCertificate(nextEpochCert).GetValueOfBackwardTransfers();

    //test
    EXPECT_TRUE(sidechainsView->UpdateScInfo(nextEpochCert, dummyUndo));

    //check
    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.prevBlockTopQualityCertHash == nextEpochCert.GetHash());
    EXPECT_TRUE(sidechain.prevBlockTopQualityCertQuality == nextEpochCert.quality);
    EXPECT_TRUE(sidechain.prevBlockTopQualityCertBwtAmount == nextEpochCert_TotalBwtAmount);
    EXPECT_TRUE(sidechain.balance == initialScState.balance - nextEpochCert_TotalBwtAmount);
}

TEST_F(SidechainMultipleCertsTestSuite, Cert_HigherQuality_SameEpoch_UndoDataCheck) {
    uint256 scId = uint256S("aaa");

    CSidechain initialScState;
    initialScState.prevBlockTopQualityCertHash = uint256S("cccc");
    initialScState.prevBlockTopQualityCertQuality = 100;
    initialScState.prevBlockTopQualityCertReferencedEpoch = 1987;
    initialScState.prevBlockTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    storeSidechain(scId, initialScState);

    //Insert high quality Certificate and generate undo data
    CMutableScCertificate highQualityCert;
    highQualityCert.scId        = scId;
    highQualityCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;
    highQualityCert.quality     = initialScState.prevBlockTopQualityCertQuality * 2;
    highQualityCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));
    CAmount highQualityCert_TotalBwtAmount = CScCertificate(highQualityCert).GetValueOfBackwardTransfers();

    CBlockUndo blockUndo;
    ASSERT_TRUE(sidechainsView->UpdateScInfo(highQualityCert, blockUndo));

    //test
    EXPECT_TRUE(sidechainsView->RestoreScInfo(highQualityCert, blockUndo.scUndoDatabyScId.at(scId)));

    CSidechain revertedSidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,revertedSidechain));
    EXPECT_TRUE(initialScState == revertedSidechain);
}

TEST_F(SidechainMultipleCertsTestSuite, Cert_LowerQuality_DifferentEpoch_UndoDataCheck) {
    uint256 scId = uint256S("aaa");

    CSidechain initialScState;
    initialScState.prevBlockTopQualityCertHash = uint256S("cccc");
    initialScState.prevBlockTopQualityCertQuality = 100;
    initialScState.prevBlockTopQualityCertReferencedEpoch = 1987;
    initialScState.prevBlockTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    storeSidechain(scId, initialScState);

    //Insert next epoch Certificate
    CMutableScCertificate nextEpochCert;
    nextEpochCert.scId        = scId;
    nextEpochCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch + 1;
    nextEpochCert.quality     = initialScState.prevBlockTopQualityCertQuality / 2;
    nextEpochCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));

    CBlockUndo blockUndo;
    ASSERT_TRUE(sidechainsView->UpdateScInfo(nextEpochCert, blockUndo));

    //test
    EXPECT_TRUE(sidechainsView->RestoreScInfo(nextEpochCert, blockUndo.scUndoDatabyScId.at(scId)));

    CSidechain revertedSidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,revertedSidechain));
    EXPECT_TRUE(initialScState == revertedSidechain);
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// CheckQuality ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainMultipleCertsTestSuite, CheckQualityRejectsLowerQualityCertsInSameEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.prevBlockTopQualityCertHash = uint256S("ddd");
    initialScState.prevBlockTopQualityCertQuality = 100;
    initialScState.prevBlockTopQualityCertReferencedEpoch = 12;
    uint256 scId = uint256S("aaa");
    storeSidechain(scId, initialScState);

    CMutableScCertificate lowQualityCert;
    lowQualityCert.scId = scId;
    lowQualityCert.quality = initialScState.prevBlockTopQualityCertQuality / 2;
    lowQualityCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;

    EXPECT_FALSE(sidechainsView->CheckQuality(lowQualityCert));
}

TEST_F(SidechainMultipleCertsTestSuite, CheckQualityRejectsEqualQualityCertsInSameEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.prevBlockTopQualityCertHash = uint256S("ddd");
    initialScState.prevBlockTopQualityCertQuality = 100;
    initialScState.prevBlockTopQualityCertReferencedEpoch = 12;
    uint256 scId = uint256S("aaa");
    storeSidechain(scId, initialScState);

    CMutableScCertificate equalQualityCert;
    equalQualityCert.scId = scId;
    equalQualityCert.quality = initialScState.prevBlockTopQualityCertQuality;
    equalQualityCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;

    EXPECT_FALSE(sidechainsView->CheckQuality(equalQualityCert));
}

TEST_F(SidechainMultipleCertsTestSuite, CheckQualityAcceptsHigherQualityCertsInSameEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.prevBlockTopQualityCertHash = uint256S("ddd");
    initialScState.prevBlockTopQualityCertQuality = 100;
    initialScState.prevBlockTopQualityCertReferencedEpoch = 12;
    uint256 scId = uint256S("aaa");
    storeSidechain(scId, initialScState);

    CMutableScCertificate highQualityCert;
    highQualityCert.scId = scId;
    highQualityCert.quality = initialScState.prevBlockTopQualityCertQuality*2;
    highQualityCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;

    EXPECT_TRUE(sidechainsView->CheckQuality(highQualityCert));
}

TEST_F(SidechainMultipleCertsTestSuite, CheckAcceptsLowerQualityCertsInDifferentEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.prevBlockTopQualityCertHash = uint256S("ddd");
    initialScState.prevBlockTopQualityCertQuality = 100;
    initialScState.prevBlockTopQualityCertReferencedEpoch = 12;
    uint256 scId = uint256S("aaa");
    storeSidechain(scId, initialScState);

    CMutableScCertificate highQualityCert;
    highQualityCert.scId = scId;
    highQualityCert.quality = initialScState.prevBlockTopQualityCertQuality / 2;
    highQualityCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch + 1;

    EXPECT_TRUE(sidechainsView->CheckQuality(highQualityCert));
}

TEST_F(SidechainMultipleCertsTestSuite, CheckInMempoolDelegateToBackingView) {
    CTxMemPool aMempool(::minRelayTxFee);
    CCoinsViewMemPool viewMempool(sidechainsView, aMempool);

    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.prevBlockTopQualityCertHash = uint256S("ddd");
    initialScState.prevBlockTopQualityCertQuality = 100;
    initialScState.prevBlockTopQualityCertReferencedEpoch = 12;
    uint256 scId = uint256S("aaa");
    storeSidechain(scId, initialScState);

    CMutableScCertificate cert;
    cert.scId = scId;

    //Lower quality, same epoch
    cert.quality = initialScState.prevBlockTopQualityCertQuality -1;
    cert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(cert));

    //Equal quality, same epoch
    cert.quality = initialScState.prevBlockTopQualityCertQuality;
    cert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(cert));

    //Higher quality, same epoch
    cert.quality = initialScState.prevBlockTopQualityCertQuality +1;
    cert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;
    EXPECT_TRUE(viewMempool.CheckQuality(cert));

    //Lower quality, different epoch
    cert.quality = initialScState.prevBlockTopQualityCertQuality - 1;
    cert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch + 1;
    EXPECT_TRUE(viewMempool.CheckQuality(cert));
}

TEST_F(SidechainMultipleCertsTestSuite, CertsInMempoolDoNotAffectCheckQuality) {
    CTxMemPool aMempool(::minRelayTxFee);
    CCoinsViewMemPool viewMempool(sidechainsView, aMempool);

    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.prevBlockTopQualityCertHash = uint256S("ddd");
    initialScState.prevBlockTopQualityCertQuality = 100;
    initialScState.prevBlockTopQualityCertReferencedEpoch = 12;
    uint256 scId = uint256S("aaa");
    storeSidechain(scId, initialScState);

    // add certificate to mempool
    CMutableScCertificate mempoolCert;
    mempoolCert.scId = scId;
    mempoolCert.quality = initialScState.prevBlockTopQualityCertQuality * 2;
    mempoolCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch + 1 ;
    CCertificateMemPoolEntry certEntry(mempoolCert, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(mempoolCert.GetHash(), certEntry));

    CMutableScCertificate trialCert;
    trialCert.scId = scId;

    //Lower quality, same epoch
    trialCert.quality = initialScState.prevBlockTopQualityCertQuality -1;
    trialCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(trialCert));

    //Equal quality, same epoch
    trialCert.quality = initialScState.prevBlockTopQualityCertQuality;
    trialCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(trialCert));

    //Higher quality, same epoch
    trialCert.quality = initialScState.prevBlockTopQualityCertQuality +1;
    trialCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch;
    EXPECT_TRUE(viewMempool.CheckQuality(trialCert));

    //Lower quality, different epoch
    trialCert.quality = initialScState.prevBlockTopQualityCertQuality - 1;
    trialCert.epochNumber = initialScState.prevBlockTopQualityCertReferencedEpoch + 1;
    EXPECT_TRUE(viewMempool.CheckQuality(trialCert));
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////// CheckCertificatesOrdering //////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST(SidechainMultipleCerts, BlocksWithCertsOfDifferentEpochsAreRejected) {
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

TEST(SidechainMultipleCerts, BlocksWithCertsWithEqualQualitiesAreRejected) {
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

TEST(SidechainMultipleCerts, BlocksWithCertsOrderedByDecreasingQualitiesAreRejected) {
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

TEST(SidechainMultipleCerts, BlocksWithSameEpochCertssOrderedByIncreasingQualityAreAccepted) {
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

///////////////////////////////////////////////////////////////////////////////
///////////////////////////// HighQualityCertData /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainMultipleCertsTestSuite, HighQualityCertData_EmptyBlock)
{
    CSidechain sidechain;
    sidechain.prevBlockTopQualityCertQuality = 100;
    sidechain.prevBlockTopQualityCertHash = uint256S("999");
    sidechain.prevBlockTopQualityCertReferencedEpoch = 15;
    uint256 scId = uint256S("aaa");
    storeSidechain(scId, sidechain);

    CBlock emptyBlock;
    EXPECT_TRUE(HighQualityCertData(emptyBlock, *sidechainsView).empty());
}

TEST_F(SidechainMultipleCertsTestSuite, HighQualityCertData_FirstCert)
{
    CSidechain sidechain;
    sidechain.prevBlockTopQualityCertQuality = 100;
    sidechain.prevBlockTopQualityCertHash = uint256S("aaa");
    sidechain.prevBlockTopQualityCertReferencedEpoch = -1;
    uint256 scId = uint256S("aaa");
    storeSidechain(scId, sidechain);

    CMutableScCertificate firstCert;
    firstCert.scId = scId;
    firstCert.epochNumber = 0;
    firstCert.quality = 10;

    CBlock aBlock;
    aBlock.vcert.push_back(firstCert);

    EXPECT_TRUE(HighQualityCertData(aBlock, *sidechainsView).at(firstCert.GetHash()).IsNull());
}

TEST_F(SidechainMultipleCertsTestSuite, LowQualityCerts_SameScId_DifferentEpoch)
{
    CSidechain sidechain;
    sidechain.prevBlockTopQualityCertQuality = 100;
    sidechain.prevBlockTopQualityCertHash = uint256S("aaa");
    sidechain.prevBlockTopQualityCertReferencedEpoch = 15;
    uint256 scId = uint256S("aaa");
    storeSidechain(scId, sidechain);

    CMutableScCertificate lowQualityCert;
    lowQualityCert.scId = scId;
    lowQualityCert.epochNumber = sidechain.prevBlockTopQualityCertReferencedEpoch +1;
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

TEST_F(SidechainMultipleCertsTestSuite, LowQualityCerts_SameScId_SameEpoch)
{
    CSidechain sidechain;
    sidechain.prevBlockTopQualityCertQuality = 10;
    sidechain.prevBlockTopQualityCertHash = uint256S("aaa");
    sidechain.prevBlockTopQualityCertReferencedEpoch = 15;
    uint256 scId = uint256S("aaa");
    storeSidechain(scId, sidechain);

    CMutableScCertificate lowQualityCert;
    lowQualityCert.scId = scId;
    lowQualityCert.epochNumber = sidechain.prevBlockTopQualityCertReferencedEpoch;
    lowQualityCert.quality = sidechain.prevBlockTopQualityCertQuality * 2;

    CMutableScCertificate highQualityCert;
    highQualityCert.scId = scId;
    highQualityCert.epochNumber = lowQualityCert.epochNumber;
    highQualityCert.quality = lowQualityCert.quality * 2;

    CBlock aBlock;
    aBlock.vcert.push_back(lowQualityCert);
    aBlock.vcert.push_back(highQualityCert);
    ASSERT_TRUE(CheckCertificatesOrdering(aBlock.vcert, dummyState));

    EXPECT_TRUE(HighQualityCertData(aBlock, *sidechainsView).at(highQualityCert.GetHash()) == sidechain.prevBlockTopQualityCertHash);
}

TEST_F(SidechainMultipleCertsTestSuite, LowQualityCerts_MultipleScIds)
{
    CSidechain sidechain_A;
    sidechain_A.prevBlockTopQualityCertHash = uint256S("aaa");
    sidechain_A.prevBlockTopQualityCertQuality = 10;
    sidechain_A.prevBlockTopQualityCertReferencedEpoch = 15;
    uint256 scId_A = uint256S("aaa");
    storeSidechain(scId_A, sidechain_A);

    CSidechain sidechain_B;
    sidechain_B.prevBlockTopQualityCertHash = uint256S("bbb");
    sidechain_B.prevBlockTopQualityCertQuality = 2;
    sidechain_B.prevBlockTopQualityCertReferencedEpoch = 200;
    uint256 scId_B = uint256S("bbb");
    storeSidechain(scId_B, sidechain_B);

    CMutableScCertificate cert_A_1;
    cert_A_1.scId = scId_A;
    cert_A_1.epochNumber = sidechain_A.prevBlockTopQualityCertReferencedEpoch;
    cert_A_1.quality = sidechain_A.prevBlockTopQualityCertQuality * 2;

    CMutableScCertificate cert_A_2;
    cert_A_2.scId = scId_A;
    cert_A_2.epochNumber = sidechain_A.prevBlockTopQualityCertReferencedEpoch;
    cert_A_2.quality = sidechain_A.prevBlockTopQualityCertQuality * 3;

    CMutableScCertificate cert_A_3;
    cert_A_3.scId = scId_A;
    cert_A_3.epochNumber = sidechain_A.prevBlockTopQualityCertReferencedEpoch;
    cert_A_3.quality = sidechain_A.prevBlockTopQualityCertQuality * 4;

    CMutableScCertificate cert_B_1;
    cert_B_1.scId = scId_B;
    cert_B_1.epochNumber = sidechain_B.prevBlockTopQualityCertReferencedEpoch + 1;
    cert_B_1.quality = sidechain_B.prevBlockTopQualityCertQuality + 1;

    CMutableScCertificate cert_B_2;
    cert_B_2.scId = scId_B;
    cert_B_2.epochNumber = sidechain_B.prevBlockTopQualityCertReferencedEpoch + 1;
    cert_B_2.quality = sidechain_B.prevBlockTopQualityCertQuality + 2;

    CBlock aBlock;
    aBlock.vcert.push_back(cert_A_1);
    aBlock.vcert.push_back(cert_B_1);
    aBlock.vcert.push_back(cert_A_2);
    aBlock.vcert.push_back(cert_B_2);
    aBlock.vcert.push_back(cert_A_3);
    ASSERT_TRUE(CheckCertificatesOrdering(aBlock.vcert, dummyState));

    EXPECT_TRUE(HighQualityCertData(aBlock, *sidechainsView).at(cert_A_3.GetHash()) == sidechain_A.prevBlockTopQualityCertHash);
    EXPECT_TRUE(HighQualityCertData(aBlock, *sidechainsView).at(cert_B_2.GetHash()).IsNull());
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// HELPERS ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
uint256 SidechainMultipleCertsTestSuite::storeSidechain(const uint256& scId, const CSidechain& sidechain)
{
    CSidechainsMap mapSidechain;
    mapSidechain[scId] = CSidechainsCacheEntry(sidechain,CSidechainsCacheEntry::Flags::FRESH);

    sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyAnchor, dummyAnchors,
                               dummyNullifiers, mapSidechain, dummyScEvents);

    return scId;
}
