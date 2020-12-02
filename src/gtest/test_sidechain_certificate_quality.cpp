#include <gtest/gtest.h>
#include "tx_creation_utils.h"
#include <coins.h>
#include <chainparams.h>
#include <undo.h>
#include <pubkey.h>
#include <main.h>
#include <txdb.h>

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

    uint256 createSidechain(CAmount balance, int creationHeight);
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// UpdateScInfo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainMultipleCertsTestSuite, InsertionOfTwoIncreasingQualitiesCertsInSameEpoch) {
    // Create sidechain
    CAmount balance = CAmount(10);
    int scCreationHeight = 1987;
    uint256 scId = createSidechain(balance, scCreationHeight);

    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.balance == balance);
    EXPECT_TRUE(sidechain.topCommittedCertHash.IsNull());

    //Insert low quality Certificate
    CMutableScCertificate lowQualityCert = txCreationUtils::createCertificate(scId, /*epochNum*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/1, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    lowQualityCert.quality = 100;
    lowQualityCert.epochNumber = 0; // NEEDED IN CURRENT IMPLENTATION

    // NEEDED IN CURRENT IMPLENTATION
    UpdateCoins(lowQualityCert, *sidechainsView, dummyUndo, scCreationHeight + Params().ScCoinsMaturity());

    //test
    ASSERT_TRUE(sidechainsView->UpdateScInfo(lowQualityCert, dummyUndo));

    //check
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.balance == balance - CScCertificate(lowQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.topCommittedCertHash == lowQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == lowQualityCert.quality);

    //Insert high quality Certificate
    CMutableScCertificate highQualityCert = lowQualityCert;
    highQualityCert.addBwt(CTxOut(CAmount(2), dummyScriptPubKey));
    highQualityCert.quality = lowQualityCert.quality * 2;
    highQualityCert.epochNumber = lowQualityCert.epochNumber;

    //test
    ASSERT_TRUE(sidechainsView->UpdateScInfo(highQualityCert, dummyUndo));

    //check
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.balance == balance - CScCertificate(highQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.topCommittedCertHash == highQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == highQualityCert.quality);
}

TEST_F(SidechainMultipleCertsTestSuite, InsertionOfTwoDecreasingQualitiesCertsInSameEpoch) {
    // Create sidechain
    CAmount balance = CAmount(10);
    int scCreationHeight = 1987;
    uint256 scId = createSidechain(balance, scCreationHeight);

    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.balance == balance);
    EXPECT_TRUE(sidechain.topCommittedCertHash.IsNull());

    //Insert high quality Certificate
    CMutableScCertificate highQualityCert = txCreationUtils::createCertificate(scId, /*epochNum*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/1, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    highQualityCert.addBwt(CTxOut(CAmount(2), dummyScriptPubKey));
    highQualityCert.quality = 200;
    highQualityCert.epochNumber = 0; // NEEDED IN CURRENT IMPLENTATION

    // NEEDED IN CURRENT IMPLENTATION
    UpdateCoins(highQualityCert, *sidechainsView, dummyUndo, scCreationHeight + Params().ScCoinsMaturity());

    //test
    ASSERT_TRUE(sidechainsView->UpdateScInfo(highQualityCert, dummyUndo));

    //check
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.balance == balance - CScCertificate(highQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.topCommittedCertHash == highQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == highQualityCert.quality);

    //Insert low quality Certificate
    CMutableScCertificate lowQualityCert = txCreationUtils::createCertificate(scId, /*epochNum*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(2), /*numBwt*/2);
    lowQualityCert.quality = highQualityCert.quality /2;
    lowQualityCert.epochNumber = highQualityCert.epochNumber;

    //test
    ASSERT_FALSE(sidechainsView->UpdateScInfo(lowQualityCert, dummyUndo));

    //check
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.balance == balance - CScCertificate(highQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.topCommittedCertHash == highQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == highQualityCert.quality);
}

TEST_F(SidechainMultipleCertsTestSuite, InsertionOfTwoIncreasingQualitiesCertsInSubsequentEpoch) {
    // Create sidechain
    CAmount balance = CAmount(10);
    int scCreationHeight = 1987;
    uint256 scId = createSidechain(balance, scCreationHeight);

    CSidechain sidechain;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.balance == balance);
    EXPECT_TRUE(sidechain.topCommittedCertHash.IsNull());

    //Insert low quality Certificate
    CMutableScCertificate lowQualityCert = txCreationUtils::createCertificate(scId, /*epochNum*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/1, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    lowQualityCert.quality = 100;
    lowQualityCert.epochNumber = 0; // NEEDED IN CURRENT IMPLENTATION

    // NEEDED IN CURRENT IMPLENTATION
    UpdateCoins(lowQualityCert, *sidechainsView, dummyUndo, scCreationHeight + Params().ScCoinsMaturity());

    //test
    ASSERT_TRUE(sidechainsView->UpdateScInfo(lowQualityCert, dummyUndo));

    //check
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.balance == balance - CScCertificate(lowQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.topCommittedCertHash == lowQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == lowQualityCert.quality);

    //Insert high quality Certificate
    CMutableScCertificate highQualityCert = lowQualityCert;
    highQualityCert.addBwt(CTxOut(CAmount(2), dummyScriptPubKey));
    highQualityCert.quality = lowQualityCert.quality * 2;
    highQualityCert.epochNumber = lowQualityCert.epochNumber +1;
    //test
    ASSERT_TRUE(sidechainsView->UpdateScInfo(highQualityCert, dummyUndo));

    //check
    ASSERT_TRUE(sidechainsView->GetSidechain(scId,sidechain));
    EXPECT_TRUE(sidechain.balance == balance - CScCertificate(highQualityCert).GetValueOfBackwardTransfers());
    EXPECT_TRUE(sidechain.topCommittedCertHash == highQualityCert.GetHash());
    EXPECT_TRUE(sidechain.topCommittedCertQuality == highQualityCert.quality);
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// GetTopQualityCert /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainMultipleCertsTestSuite, GetTopQualityCertFromDbIsNull) {
    boost::filesystem::path  pathTemp(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path());
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();
    CCoinsViewDB aChainStateDb(1024, false, false);
    CCoinsViewCache cache(&aChainStateDb);

    const uint256& scId = uint256S("aaabbbccc");

    CSidechainsMap mapSidechain;
    CSidechain sidechain;
    sidechain.topCommittedCertQuality = 100;
    sidechain.topCommittedCertHash = uint256S("aaa");
    sidechain.topCommittedCertReferencedEpoch = 15;
    mapSidechain[scId] = CSidechainsCacheEntry(sidechain,CSidechainsCacheEntry::Flags::FRESH);

    // Null quality without persisted sidechain
    uint256 retrievedCertHash;
    EXPECT_TRUE(aChainStateDb.GetTopQualityCert(scId, sidechain.topCommittedCertReferencedEpoch, retrievedCertHash)
                    == CScCertificate::QUALITY_NULL);

    ASSERT_TRUE(cache.BatchWrite(dummyCoins, dummyHash, dummyHash, dummyAnchors,
                                 dummyNullifiers, mapSidechain, dummyCeasedScs));

    // CURRENTLY still null quality without persisted sidechain
    EXPECT_TRUE(aChainStateDb.GetTopQualityCert(scId, sidechain.topCommittedCertReferencedEpoch, retrievedCertHash)
                    == CScCertificate::QUALITY_NULL);
    EXPECT_TRUE(retrievedCertHash.IsNull());
}

TEST_F(SidechainMultipleCertsTestSuite, GetTopQualityCertFromViewSameEpoch) {
    const uint256& scId = uint256S("aaabbbccc");

    CSidechainsMap mapSidechain;
    CSidechain sidechain;
    sidechain.topCommittedCertQuality = 100;
    sidechain.topCommittedCertHash = uint256S("aaa");
    sidechain.topCommittedCertReferencedEpoch = 15;
    mapSidechain[scId] = CSidechainsCacheEntry(sidechain,CSidechainsCacheEntry::Flags::FRESH);

    // Null quality before flushing the sidechain
    uint256 retrievedCertHash;
    EXPECT_TRUE(sidechainsView->GetTopQualityCert(scId, sidechain.topCommittedCertReferencedEpoch, retrievedCertHash)
                    == CScCertificate::QUALITY_NULL);

    ASSERT_TRUE(sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyHash, dummyAnchors,
                                 dummyNullifiers, mapSidechain, dummyCeasedScs));

    // Non-null quality after flushing the sidechain
    EXPECT_TRUE(sidechainsView->GetTopQualityCert(scId, sidechain.topCommittedCertReferencedEpoch, retrievedCertHash)
                    == sidechain.topCommittedCertQuality);
    EXPECT_TRUE(retrievedCertHash == sidechain.topCommittedCertHash);
}

TEST_F(SidechainMultipleCertsTestSuite, GetTopQualityCertFromViewDifferentEpochAreNotReturned) {
    const uint256& scId = uint256S("aaabbbccc");

    CSidechainsMap mapSidechain;
    CSidechain sidechain;
    sidechain.topCommittedCertQuality = 100;
    sidechain.topCommittedCertHash = uint256S("aaa");
    sidechain.topCommittedCertReferencedEpoch = 15;
    mapSidechain[scId] = CSidechainsCacheEntry(sidechain,CSidechainsCacheEntry::Flags::FRESH);

    uint256 retrievedCertHash;
    ASSERT_TRUE(sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyHash, dummyAnchors,
                                 dummyNullifiers, mapSidechain, dummyCeasedScs));

    // Currently top quality cert is not returned for other epochs
    EXPECT_TRUE(sidechainsView->GetTopQualityCert(scId, sidechain.topCommittedCertReferencedEpoch +1, retrievedCertHash)
                    == CScCertificate::QUALITY_NULL);
    EXPECT_TRUE(retrievedCertHash.IsNull());

    // Currently top quality cert is not returned for other epochs
    EXPECT_TRUE(sidechainsView->GetTopQualityCert(scId, sidechain.topCommittedCertReferencedEpoch -1, retrievedCertHash)
                    == CScCertificate::QUALITY_NULL);
    EXPECT_TRUE(retrievedCertHash.IsNull());
}

TEST_F(SidechainMultipleCertsTestSuite, GetTopQualityCertFromViewMempool_CertInBackingViewOnly_SameEpoch) {
    CTxMemPool aMempool(::minRelayTxFee);
    CCoinsViewMemPool viewMempool(sidechainsView, aMempool);

    const uint256& scId = uint256S("aaabbbccc");

    CSidechainsMap mapSidechain;
    CSidechain sidechain;
    sidechain.topCommittedCertQuality = 100;
    sidechain.topCommittedCertHash = uint256S("aaa");
    sidechain.topCommittedCertReferencedEpoch = 15;
    mapSidechain[scId] = CSidechainsCacheEntry(sidechain,CSidechainsCacheEntry::Flags::FRESH);

    uint256 retrievedCertHash;
    ASSERT_TRUE(sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyHash, dummyAnchors,
                                 dummyNullifiers, mapSidechain, dummyCeasedScs));

    // Non-null quality after flushing the sidechain
    EXPECT_TRUE(viewMempool.GetTopQualityCert(scId, sidechain.topCommittedCertReferencedEpoch, retrievedCertHash)
                    == sidechain.topCommittedCertQuality);
    EXPECT_TRUE(retrievedCertHash == sidechain.topCommittedCertHash);
}

TEST_F(SidechainMultipleCertsTestSuite, GetTopQualityCertFromViewMempool_CertsInBackingViewAndMempool_SameEpoch) {
    CTxMemPool aMempool(::minRelayTxFee);
    CCoinsViewMemPool viewMempool(sidechainsView, aMempool);

    const uint256& scId = uint256S("aaabbbccc");

    CSidechainsMap mapSidechain;
    CSidechain sidechain;
    sidechain.topCommittedCertQuality = 100;
    sidechain.topCommittedCertHash = uint256S("aaa");
    sidechain.topCommittedCertReferencedEpoch = 15;
    mapSidechain[scId] = CSidechainsCacheEntry(sidechain,CSidechainsCacheEntry::Flags::FRESH);

    uint256 retrievedCertHash;
    ASSERT_TRUE(sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyHash, dummyAnchors,
                                 dummyNullifiers, mapSidechain, dummyCeasedScs));

    // add certificate to mempool
    CMutableScCertificate cert;
    cert.scId = scId;
    cert.quality = sidechain.topCommittedCertQuality * 2;
    cert.epochNumber = sidechain.topCommittedCertReferencedEpoch;
    CCertificateMemPoolEntry certEntry(cert, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(cert.GetHash(), certEntry));

    EXPECT_TRUE(viewMempool.GetTopQualityCert(scId, sidechain.topCommittedCertReferencedEpoch, retrievedCertHash)
                    == cert.quality);
    EXPECT_TRUE(retrievedCertHash == cert.GetHash());
}

TEST_F(SidechainMultipleCertsTestSuite, GetTopQualityCertFromViewMempool_CertsInBackingViewAndMempool_DifferentEpoch) {
    CTxMemPool aMempool(::minRelayTxFee);
    CCoinsViewMemPool viewMempool(sidechainsView, aMempool);

    const uint256& scId = uint256S("aaabbbccc");

    CSidechainsMap mapSidechain;
    CSidechain sidechain;
    sidechain.topCommittedCertQuality = 100;
    sidechain.topCommittedCertHash = uint256S("aaa");
    sidechain.topCommittedCertReferencedEpoch = 15;
    mapSidechain[scId] = CSidechainsCacheEntry(sidechain,CSidechainsCacheEntry::Flags::FRESH);

    uint256 retrievedCertHash;
    ASSERT_TRUE(sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyHash, dummyAnchors,
                                 dummyNullifiers, mapSidechain, dummyCeasedScs));

    // add certificate to mempool
    CMutableScCertificate cert;
    cert.scId = scId;
    cert.quality = sidechain.topCommittedCertQuality * 2;
    cert.epochNumber = sidechain.topCommittedCertReferencedEpoch;
    CCertificateMemPoolEntry certEntry(cert, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(cert.GetHash(), certEntry));

    EXPECT_TRUE(viewMempool.GetTopQualityCert(scId, sidechain.topCommittedCertReferencedEpoch+1, retrievedCertHash)
                == CScCertificate::QUALITY_NULL);
    EXPECT_TRUE(retrievedCertHash.IsNull());

    EXPECT_TRUE(viewMempool.GetTopQualityCert(scId, sidechain.topCommittedCertReferencedEpoch-1, retrievedCertHash)
                == CScCertificate::QUALITY_NULL);
    EXPECT_TRUE(retrievedCertHash.IsNull());
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// HELPERS ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
uint256 SidechainMultipleCertsTestSuite::createSidechain(CAmount balance, int creationHeight)
{
    CSidechain sidechain;
    sidechain.creationBlockHeight = creationHeight;
    sidechain.balance = balance;
    sidechain.topCommittedCertHash.SetNull();
    uint256 scId = uint256S("aaa");

    CSidechainsMap mapSidechain;
    mapSidechain[scId] = CSidechainsCacheEntry(sidechain,CSidechainsCacheEntry::Flags::FRESH);

    sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyHash, dummyAnchors,
                               dummyNullifiers, mapSidechain, dummyCeasedScs);

    return scId;
}
