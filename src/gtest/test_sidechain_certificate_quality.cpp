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
                                       dummyVoidedCertMap(), dummyScriptPubKey() {
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
    CTxUndo                 dummyUndo;
    CBlockUndo              dummyBlockUndo;
    std::map<uint256, bool> dummyVoidedCertMap;
    CScript                 dummyScriptPubKey;

    CCoinsMap           dummyCoins;
    uint256             dummyHash;
    CAnchorsMap         dummyAnchors;
    CNullifiersMap      dummyNullifiers;
    CSidechainEventsMap dummyCeasedScs;

    uint256 storeSidechain(const CSidechain& sidechain);
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// UpdateScInfo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainMultipleCertsTestSuite, InsertionOfTwoIncreasingQualitiesCertsInSameEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    uint256 scId = storeSidechain(initialScState);

    //Insert low quality Certificate
    CMutableScCertificate lowQualityCert = txCreationUtils::createCertificate(scId, /*epochNum*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/1, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    lowQualityCert.quality = 100;
    lowQualityCert.epochNumber = 0; // NEEDED IN CURRENT IMPLENTATION

    //test
    ASSERT_TRUE(sidechainsView->UpdateScInfo(lowQualityCert, dummyUndo));

    //check
    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.topCommittedCertHash == lowQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == lowQualityCert.quality);
    EXPECT_TRUE(sidechain.topCommittedCertBwtAmount == CScCertificate(lowQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.balance == initialScState.balance - sidechain.topCommittedCertBwtAmount);

    //Insert high quality Certificate
    CMutableScCertificate highQualityCert = lowQualityCert;
    highQualityCert.addBwt(CTxOut(CAmount(2), dummyScriptPubKey));
    highQualityCert.quality = lowQualityCert.quality * 2;
    highQualityCert.epochNumber = lowQualityCert.epochNumber;

    //test
    ASSERT_TRUE(sidechainsView->UpdateScInfo(highQualityCert, dummyUndo));

    //check
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.topCommittedCertHash == highQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == highQualityCert.quality);
    EXPECT_TRUE(sidechain.topCommittedCertBwtAmount == CScCertificate(highQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.balance == initialScState.balance - sidechain.topCommittedCertBwtAmount);
}

TEST_F(SidechainMultipleCertsTestSuite, InsertionOfTwoDecreasingQualitiesCertsInSameEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    uint256 scId = storeSidechain(initialScState);

    //Insert high quality Certificate
    CMutableScCertificate highQualityCert = txCreationUtils::createCertificate(scId, /*epochNum*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/1, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    highQualityCert.addBwt(CTxOut(CAmount(2), dummyScriptPubKey));
    highQualityCert.quality = 200;
    highQualityCert.epochNumber = 0; // NEEDED IN CURRENT IMPLENTATION

    //test
    ASSERT_TRUE(sidechainsView->UpdateScInfo(highQualityCert, dummyUndo));

    //check
    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.topCommittedCertHash == highQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == highQualityCert.quality);
    EXPECT_TRUE(sidechain.topCommittedCertBwtAmount == CScCertificate(highQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.balance == initialScState.balance - sidechain.topCommittedCertBwtAmount);

    //Insert low quality Certificate
    CMutableScCertificate lowQualityCert = txCreationUtils::createCertificate(scId, /*epochNum*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(2), /*numBwt*/2);
    lowQualityCert.quality = highQualityCert.quality /2;
    lowQualityCert.epochNumber = highQualityCert.epochNumber;

    //test
    ASSERT_FALSE(sidechainsView->UpdateScInfo(lowQualityCert, dummyUndo));

    //check
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.topCommittedCertHash == highQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == highQualityCert.quality);
    EXPECT_TRUE(sidechain.topCommittedCertBwtAmount == CScCertificate(highQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.balance == initialScState.balance - sidechain.topCommittedCertBwtAmount);
}

TEST_F(SidechainMultipleCertsTestSuite, InsertionOfTwoIncreasingQualitiesCertsInSubsequentEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    uint256 scId = storeSidechain(initialScState);

    //Insert low quality Certificate
    CMutableScCertificate lowQualityCert = txCreationUtils::createCertificate(scId, /*epochNum*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/1, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    lowQualityCert.quality = 100;
    lowQualityCert.epochNumber = 0; // NEEDED IN CURRENT IMPLENTATION

    //test
    ASSERT_TRUE(sidechainsView->UpdateScInfo(lowQualityCert, dummyUndo));

    //check
    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.topCommittedCertHash == lowQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == lowQualityCert.quality);
    EXPECT_TRUE(sidechain.topCommittedCertBwtAmount == CScCertificate(lowQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.balance == initialScState.balance - sidechain.topCommittedCertBwtAmount);

    //Insert high quality Certificate
    CMutableScCertificate highQualityCert = lowQualityCert;
    highQualityCert.addBwt(CTxOut(CAmount(2), dummyScriptPubKey));
    highQualityCert.quality = lowQualityCert.quality * 2;
    highQualityCert.epochNumber = lowQualityCert.epochNumber +1;
    //test
    ASSERT_TRUE(sidechainsView->UpdateScInfo(highQualityCert, dummyUndo));

    //check
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.topCommittedCertHash == highQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == highQualityCert.quality);
    EXPECT_TRUE(sidechain.topCommittedCertBwtAmount == CScCertificate(highQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.balance == initialScState.balance - sidechain.topCommittedCertBwtAmount);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// CheckQuality ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainMultipleCertsTestSuite, CheckQualityRejectsLowerQualityCertsInSameEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.topCommittedCertHash = uint256S("ddd");
    initialScState.topCommittedCertQuality = 100;
    initialScState.topCommittedCertReferencedEpoch = 12;
    uint256 scId = storeSidechain(initialScState);

    CMutableScCertificate lowQualityCert;
    lowQualityCert.scId = scId;
    lowQualityCert.quality = initialScState.topCommittedCertQuality / 2;
    lowQualityCert.epochNumber = initialScState.topCommittedCertReferencedEpoch;

    EXPECT_FALSE(sidechainsView->CheckQuality(lowQualityCert));
}

TEST_F(SidechainMultipleCertsTestSuite, CheckQualityRejectsEqualQualityCertsInSameEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.topCommittedCertHash = uint256S("ddd");
    initialScState.topCommittedCertQuality = 100;
    initialScState.topCommittedCertReferencedEpoch = 12;
    uint256 scId = storeSidechain(initialScState);

    CMutableScCertificate equalQualityCert;
    equalQualityCert.scId = scId;
    equalQualityCert.quality = initialScState.topCommittedCertQuality;
    equalQualityCert.epochNumber = initialScState.topCommittedCertReferencedEpoch;

    EXPECT_FALSE(sidechainsView->CheckQuality(equalQualityCert));
}

TEST_F(SidechainMultipleCertsTestSuite, CheckQualityAcceptsHigherQualityCertsInSameEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.topCommittedCertHash = uint256S("ddd");
    initialScState.topCommittedCertQuality = 100;
    initialScState.topCommittedCertReferencedEpoch = 12;
    uint256 scId = storeSidechain(initialScState);

    CMutableScCertificate highQualityCert;
    highQualityCert.scId = scId;
    highQualityCert.quality = initialScState.topCommittedCertQuality*2;
    highQualityCert.epochNumber = initialScState.topCommittedCertReferencedEpoch;

    EXPECT_TRUE(sidechainsView->CheckQuality(highQualityCert));
}

TEST_F(SidechainMultipleCertsTestSuite, CheckAcceptsLowerQualityCertsInDifferentEpoch) {
    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.topCommittedCertHash = uint256S("ddd");
    initialScState.topCommittedCertQuality = 100;
    initialScState.topCommittedCertReferencedEpoch = 12;
    uint256 scId = storeSidechain(initialScState);

    CMutableScCertificate highQualityCert;
    highQualityCert.scId = scId;
    highQualityCert.quality = initialScState.topCommittedCertQuality / 2;
    highQualityCert.epochNumber = initialScState.topCommittedCertReferencedEpoch + 1;

    EXPECT_TRUE(sidechainsView->CheckQuality(highQualityCert));
}

TEST_F(SidechainMultipleCertsTestSuite, CheckInMempoolDelegateToBackingView) {
    CTxMemPool aMempool(::minRelayTxFee);
    CCoinsViewMemPool viewMempool(sidechainsView, aMempool);

    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.topCommittedCertHash = uint256S("ddd");
    initialScState.topCommittedCertQuality = 100;
    initialScState.topCommittedCertReferencedEpoch = 12;
    uint256 scId = storeSidechain(initialScState);

    CMutableScCertificate cert;
    cert.scId = scId;

    //Lower quality, same epoch
    cert.quality = initialScState.topCommittedCertQuality -1;
    cert.epochNumber = initialScState.topCommittedCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(cert));

    //Equal quality, same epoch
    cert.quality = initialScState.topCommittedCertQuality;
    cert.epochNumber = initialScState.topCommittedCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(cert));

    //Higher quality, same epoch
    cert.quality = initialScState.topCommittedCertQuality +1;
    cert.epochNumber = initialScState.topCommittedCertReferencedEpoch;
    EXPECT_TRUE(viewMempool.CheckQuality(cert));

    //Lower quality, different epoch
    cert.quality = initialScState.topCommittedCertQuality - 1;
    cert.epochNumber = initialScState.topCommittedCertReferencedEpoch + 1;
    EXPECT_TRUE(viewMempool.CheckQuality(cert));
}

TEST_F(SidechainMultipleCertsTestSuite, CertsInMempoolDoNotAffectCheckQuality) {
    CTxMemPool aMempool(::minRelayTxFee);
    CCoinsViewMemPool viewMempool(sidechainsView, aMempool);

    CSidechain initialScState;
    initialScState.balance = CAmount(10);
    initialScState.creationBlockHeight = 1987;
    initialScState.topCommittedCertHash = uint256S("ddd");
    initialScState.topCommittedCertQuality = 100;
    initialScState.topCommittedCertReferencedEpoch = 12;
    uint256 scId = storeSidechain(initialScState);

    // add certificate to mempool
    CMutableScCertificate mempoolCert;
    mempoolCert.scId = scId;
    mempoolCert.quality = initialScState.topCommittedCertQuality * 2;
    mempoolCert.epochNumber = initialScState.topCommittedCertReferencedEpoch + 1 ;
    CCertificateMemPoolEntry certEntry(mempoolCert, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(mempoolCert.GetHash(), certEntry));

    CMutableScCertificate trialCert;
    trialCert.scId = scId;

    //Lower quality, same epoch
    trialCert.quality = initialScState.topCommittedCertQuality -1;
    trialCert.epochNumber = initialScState.topCommittedCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(trialCert));

    //Equal quality, same epoch
    trialCert.quality = initialScState.topCommittedCertQuality;
    trialCert.epochNumber = initialScState.topCommittedCertReferencedEpoch;
    EXPECT_FALSE(viewMempool.CheckQuality(trialCert));

    //Higher quality, same epoch
    trialCert.quality = initialScState.topCommittedCertQuality +1;
    trialCert.epochNumber = initialScState.topCommittedCertReferencedEpoch;
    EXPECT_TRUE(viewMempool.CheckQuality(trialCert));

    //Lower quality, different epoch
    trialCert.quality = initialScState.topCommittedCertQuality - 1;
    trialCert.epochNumber = initialScState.topCommittedCertReferencedEpoch + 1;
    EXPECT_TRUE(viewMempool.CheckQuality(trialCert));
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////// CheckCertificatesOrdering //////////////////////////
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
/////////////////////////////////// HELPERS ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
uint256 SidechainMultipleCertsTestSuite::storeSidechain(const CSidechain& sidechain)
{
    uint256 scId = uint256S("aaa");

    CSidechainsMap mapSidechain;
    mapSidechain[scId] = CSidechainsCacheEntry(sidechain,CSidechainsCacheEntry::Flags::FRESH);

    sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyHash, dummyAnchors,
                               dummyNullifiers, mapSidechain, dummyCeasedScs);

    return scId;
}
