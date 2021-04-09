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

#include <miner.h>
#include <gtest/libzendoo_test_files.h>

class CNakedCCoinsViewCache : public CCoinsViewCache
{
public:
    CNakedCCoinsViewCache(CCoinsView* pWrappedView): CCoinsViewCache(pWrappedView)
    {
        uint256 dummyAnchor = uint256S("59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7"); //anchor for empty block!?
        this->hashAnchor = dummyAnchor;

        CAnchorsCacheEntry dummyAnchorsEntry;
        dummyAnchorsEntry.entered = true;
        dummyAnchorsEntry.flags = CAnchorsCacheEntry::DIRTY;
        this->cacheAnchors[dummyAnchor] = dummyAnchorsEntry;

    };
    CSidechainsMap& getSidechainMap() {return this->cacheSidechains; };
    CSidechainEventsMap& getScEventsMap() {return this->cacheSidechainEvents; };
};

class CInMemorySidechainDb final: public CCoinsView {
public:
    CInMemorySidechainDb()  = default;
    virtual ~CInMemorySidechainDb() = default;

    bool HaveSidechain(const uint256& scId) const override {
        return sidechainsInMemoryMap.count(scId) && sidechainsInMemoryMap.at(scId).flag != CSidechainsCacheEntry::Flags::ERASED;
    }
    bool GetSidechain(const uint256& scId, CSidechain& info) const override {
        if(!HaveSidechain(scId))
            return false;
        info = sidechainsInMemoryMap.at(scId).sidechain;
        return true;
    }

    bool HaveSidechainEvents(int height)  const override {
        return eventsInMemoryMap.count(height) && eventsInMemoryMap.at(height).flag != CSidechainEventsCacheEntry::Flags::ERASED;
    }
    bool GetSidechainEvents(int height, CSidechainEvents& scEvents) const override {
        if(!HaveSidechainEvents(height))
            return false;
        scEvents = eventsInMemoryMap.at(height).scEvents;
        return true;
    }

    virtual void GetScIds(std::set<uint256>& scIdsList) const override {
        for (auto& entry : sidechainsInMemoryMap)
            scIdsList.insert(entry.first);
        return;
    }

    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap,
                    CSidechainEventsMap& mapSidechainEvents, CCswNullifiersMap& cswNullifiers) override
    {
        for (auto& entryToWrite : sidechainMap)
            WriteMutableEntry(entryToWrite.first, entryToWrite.second, sidechainsInMemoryMap);

        for (auto& entryToWrite : mapSidechainEvents)
            WriteMutableEntry(entryToWrite.first, entryToWrite.second, eventsInMemoryMap);

        sidechainMap.clear();
        mapSidechainEvents.clear();
        return true;
    }

private:
    mutable boost::unordered_map<uint256, CSidechainsCacheEntry, CCoinsKeyHasher> sidechainsInMemoryMap;
    mutable boost::unordered_map<int, CSidechainEventsCacheEntry> eventsInMemoryMap;
};

class SidechainsConnectCertsBlockTestSuite : public ::testing::Test {
public:
    SidechainsConnectCertsBlockTestSuite():
        fakeChainStateDb(nullptr), sidechainsView(nullptr),
        dummyBlock(), dummyHash(), dummyCertStatusUpdateInfo(), dummyScriptPubKey(),
        dummyState(), dummyChain(), dummyScEvents(), dummyFeeAmount(), dummyCoinbaseScript(),
        csMainLock(cs_main, "cs_main", __FILE__, __LINE__)
    {
        dummyScriptPubKey = GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/false);
    }

    ~SidechainsConnectCertsBlockTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        // clear globals
        UnloadBlockIndex();
        mGlobalForkTips.clear();

        fakeChainStateDb   = new CInMemorySidechainDb();
        sidechainsView     = new CNakedCCoinsViewCache(fakeChainStateDb);

        dummyHash = dummyBlock.GetHash();
        dummyCoinbaseScript = CScript() << OP_DUP << OP_HASH160
                << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG;
    };

    void TearDown() override {
        delete sidechainsView;
        sidechainsView = nullptr;

        delete fakeChainStateDb;
        fakeChainStateDb = nullptr;

        // clear globals
        UnloadBlockIndex();
        mGlobalForkTips.clear();
    };

protected:
    CInMemorySidechainDb  *fakeChainStateDb;
    CNakedCCoinsViewCache *sidechainsView;

    //helpers
    CBlock                                    dummyBlock;
    uint256                                   dummyHash;
    std::vector<CScCertificateStatusUpdateInfo>  dummyCertStatusUpdateInfo;
    CScript                                   dummyScriptPubKey;

    CValidationState    dummyState;
    CChain              dummyChain;

    CSidechainEventsMap dummyScEvents;

    void storeSidechain(const uint256& scId, const CSidechain& sidechain);
    void storeSidechainEvent(int eventHeight, const CSidechainEvents& scEvent);

    void fillBlockHeader(CBlock& blockToFill, const uint256& prevBlockHash);

    CAmount dummyFeeAmount;
    CScript dummyCoinbaseScript;
    void    CreateCheckpointAfter(CBlockIndex* blkIdx);

private:
    //Critical sections below needed when compiled with --enable-debug, which activates ASSERT_HELD
    CCriticalBlock csMainLock;
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// ConnectBlock ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_SingleCert_SameEpoch_CertCoinHasBwt)
{
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 100;
    initialScState.creationData.withdrawalEpochLength = 20;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 7;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    storeSidechain(scId, initialScState);

    //... and initial ceasing event too
    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    storeSidechainEvent(initialScState.GetScheduledCeasingHeight(), event);

    // set relevant heights
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch;
    int certBlockHeight = initialScState.GetCertSubmissionWindowStart(certEpoch)+1;

    // create coinbase to finance certificate submission (just in view)
    ASSERT_TRUE(certBlockHeight <= initialScState.GetCertSubmissionWindowEnd(certEpoch));
    uint256 inputTxHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // create block with certificate ...
    CMutableScCertificate singleCert;
    singleCert.vin.push_back(CTxIn(inputTxHash, 0, CScript(), 0));
    singleCert.nVersion    = SC_CERT_VERSION;
    singleCert.scProof     = CScProof{ParseHex(SAMPLE_PROOF)};
    singleCert.scId        = scId;
    singleCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    singleCert.quality     = initialScState.lastTopQualityCertQuality * 2;
    singleCert.endEpochBlockHash = *(chainActive.Tip()->pprev->phashBlock);
    singleCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));

    CBlock certBlock;
    fillBlockHeader(certBlock, uint256S("aaa"));
    certBlock.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));
    certBlock.vcert.push_back(singleCert);

    // ... and corresponding block index
    CBlockIndex* certBlockIndex = AddToBlockIndex(certBlock);
    certBlockIndex->nHeight = certBlockHeight;
    certBlockIndex->pprev = chainActive.Tip();
    certBlockIndex->pprev->phashBlock = &dummyHash;
    certBlockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(certBlockIndex);

    bool fJustCheck = true;
    bool fScRelatedChecks = false;

    // test
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain, fJustCheck, fScRelatedChecks, &dummyCertStatusUpdateInfo);

    //checks
    ASSERT_TRUE(res);
    ASSERT_TRUE(sidechainsView->HaveCoins(singleCert.GetHash()));
    CCoins certCoin;
    sidechainsView->GetCoins(singleCert.GetHash(), certCoin);
    EXPECT_TRUE(certCoin.IsFromCert());
    EXPECT_TRUE(certCoin.vout.size() == 1);
    EXPECT_TRUE(certCoin.nFirstBwtPos == 0);
    EXPECT_TRUE(certCoin.IsAvailable(0));
}

TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_SingleCert_DifferentEpoch_CertCoinHasBwt)
{
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 100;
    initialScState.creationData.withdrawalEpochLength = 20;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 7;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    storeSidechain(scId, initialScState);

    //... and initial ceasing event too
    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    storeSidechainEvent(initialScState.GetScheduledCeasingHeight(), event);

    // set relevant heights
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch+1;
    int certBlockHeight = initialScState.GetCertSubmissionWindowStart(certEpoch)+1;

    // create coinbase to finance certificate submission (just in view)
    ASSERT_TRUE(certBlockHeight <= initialScState.GetCertSubmissionWindowEnd(certEpoch));
    uint256 inputTxHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // create block with certificate ...
    CMutableScCertificate singleCert;
    singleCert.vin.push_back(CTxIn(inputTxHash, 0, CScript(), 0));
    singleCert.nVersion    = SC_CERT_VERSION;
    singleCert.scProof     = CScProof{ParseHex(SAMPLE_PROOF)};
    singleCert.scId        = scId;
    singleCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch + 1;
    singleCert.quality     = 1;
    singleCert.endEpochBlockHash = *(chainActive.Tip()->pprev->phashBlock);
    singleCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));

    CBlock certBlock;
    fillBlockHeader(certBlock, uint256S("aaa"));
    certBlock.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));
    certBlock.vcert.push_back(singleCert);

    // ... and corresponding block index
    CBlockIndex* certBlockIndex = AddToBlockIndex(certBlock);
    certBlockIndex->nHeight = certBlockHeight;
    certBlockIndex->pprev = chainActive.Tip();
    certBlockIndex->pprev->phashBlock = &dummyHash;
    certBlockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(certBlockIndex);

    bool fJustCheck = true;
    bool fScRelatedChecks = false;

    // test
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain, fJustCheck, fScRelatedChecks, &dummyCertStatusUpdateInfo);

    //checks
    ASSERT_TRUE(res);
    ASSERT_TRUE(sidechainsView->HaveCoins(singleCert.GetHash()));
    CCoins certCoin;
    sidechainsView->GetCoins(singleCert.GetHash(), certCoin);
    EXPECT_TRUE(certCoin.IsFromCert());
    EXPECT_TRUE(certCoin.vout.size() == 1);
    EXPECT_TRUE(certCoin.nFirstBwtPos == 0);
    EXPECT_TRUE(certCoin.IsAvailable(0));
}

TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_MultipleCerts_SameEpoch_LowQualityCertCoinHasNotBwt)
{
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 100;
    initialScState.creationData.withdrawalEpochLength = 20;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 7;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    storeSidechain(scId, initialScState);

    //... and initial ceasing event too
    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    storeSidechainEvent(initialScState.GetScheduledCeasingHeight(), event);

    // set relevant heights
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch;
    int certBlockHeight = initialScState.GetCertSubmissionWindowStart(certEpoch)+1;
    ASSERT_TRUE(certBlockHeight <= initialScState.GetCertSubmissionWindowEnd(certEpoch));

    // create coinbase to finance certificate submission (just in view)
    uint256 inputLowQCertHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);
    uint256 inputHighQCertHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY-1);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // create block with certificates ...
    CMutableScCertificate lowQualityCert;
    lowQualityCert.vin.push_back(CTxIn(inputLowQCertHash, 0, CScript(), 0));
    lowQualityCert.nVersion    = SC_CERT_VERSION;
    lowQualityCert.scProof     = CScProof{ParseHex(SAMPLE_PROOF)};
    lowQualityCert.scId        = scId;
    lowQualityCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    lowQualityCert.quality     = initialScState.lastTopQualityCertQuality * 2;
    lowQualityCert.endEpochBlockHash = *(chainActive.Tip()->pprev->phashBlock);
    lowQualityCert.addBwt(CTxOut(CAmount(40), dummyScriptPubKey));

    CMutableScCertificate highQualityCert;
    highQualityCert.vin.push_back(CTxIn(inputHighQCertHash, 0, CScript(), 0));
    highQualityCert.nVersion    = lowQualityCert.nVersion;
    highQualityCert.scProof     = lowQualityCert.scProof;
    highQualityCert.scId        = lowQualityCert.scId;
    highQualityCert.epochNumber = lowQualityCert.epochNumber;
    highQualityCert.quality     = lowQualityCert.quality * 2;
    highQualityCert.endEpochBlockHash = lowQualityCert.endEpochBlockHash;
    highQualityCert.addBwt(CTxOut(CAmount(50), dummyScriptPubKey));

    CBlock certBlock;
    fillBlockHeader(certBlock, uint256S("aaa"));
    certBlock.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));
    certBlock.vcert.push_back(lowQualityCert);
    certBlock.vcert.push_back(highQualityCert);

    // ... and corresponding block index
    CBlockIndex* certBlockIndex = AddToBlockIndex(certBlock);
    certBlockIndex->nHeight = certBlockHeight;
    certBlockIndex->pprev = chainActive.Tip();
    certBlockIndex->pprev->phashBlock = &dummyHash;
    certBlockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(certBlockIndex);

    bool fJustCheck = true;
    bool fScRelatedChecks = false;

    // test
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain, fJustCheck, fScRelatedChecks, &dummyCertStatusUpdateInfo);

    //checks
    ASSERT_TRUE(res);
    CCoins lowQualityCertCoin;
    EXPECT_FALSE(sidechainsView->GetCoins(lowQualityCert.GetHash(), lowQualityCertCoin));

    CCoins highQualityCertCoin;
    sidechainsView->GetCoins(highQualityCert.GetHash(), highQualityCertCoin);
    EXPECT_TRUE(highQualityCertCoin.IsFromCert());
    EXPECT_TRUE(highQualityCertCoin.vout.size() == 1);
    EXPECT_TRUE(highQualityCertCoin.nFirstBwtPos == 0);
    EXPECT_TRUE(highQualityCertCoin.IsAvailable(0));
}

TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_MultipleCerts_DifferentEpoch_LowQualityCertCoinHasNotBwt)
{
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 100;
    initialScState.creationData.withdrawalEpochLength = 20;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 7;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    storeSidechain(scId, initialScState);

    //... and initial ceasing event too
    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    storeSidechainEvent(initialScState.GetScheduledCeasingHeight(), event);

    // set relevant heights
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch + 1;
    int certBlockHeight = initialScState.GetCertSubmissionWindowStart(certEpoch)+1;
    ASSERT_TRUE(certBlockHeight <= initialScState.GetCertSubmissionWindowEnd(certEpoch));

    // create coinbase to finance certificate submission (just in view)
    uint256 inputLowQCertHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);
    uint256 inputHighQCertHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY-1);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // create block with certificates ...
    CMutableScCertificate lowQualityCert;
    lowQualityCert.vin.push_back(CTxIn(inputLowQCertHash, 0, CScript(), 0));
    lowQualityCert.nVersion    = SC_CERT_VERSION;
    lowQualityCert.scProof     = CScProof{ParseHex(SAMPLE_PROOF)};
    lowQualityCert.scId        = scId;
    lowQualityCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch +1;
    lowQualityCert.quality     = 1;
    lowQualityCert.endEpochBlockHash = *(chainActive.Tip()->pprev->phashBlock);
    lowQualityCert.addBwt(CTxOut(CAmount(40), dummyScriptPubKey));

    CMutableScCertificate highQualityCert;
    highQualityCert.vin.push_back(CTxIn(inputHighQCertHash, 0, CScript(), 0));
    highQualityCert.nVersion    = lowQualityCert.nVersion;
    highQualityCert.scProof     = lowQualityCert.scProof ;
    highQualityCert.scId        = lowQualityCert.scId;
    highQualityCert.epochNumber = lowQualityCert.epochNumber;
    highQualityCert.quality     = lowQualityCert.quality * 2;
    highQualityCert.endEpochBlockHash = lowQualityCert.endEpochBlockHash;
    highQualityCert.addBwt(CTxOut(CAmount(50), dummyScriptPubKey));

    CBlock certBlock;
    fillBlockHeader(certBlock, uint256S("aaa"));
    certBlock.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));
    certBlock.vcert.push_back(lowQualityCert);
    certBlock.vcert.push_back(highQualityCert);

    // ... and corresponding block index
    CBlockIndex* certBlockIndex = AddToBlockIndex(certBlock);
    certBlockIndex->nHeight = certBlockHeight;
    certBlockIndex->pprev = chainActive.Tip();
    certBlockIndex->pprev->phashBlock = &dummyHash;
    certBlockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(certBlockIndex);

    bool fJustCheck = true;
    bool fScRelatedChecks = false;

    // test
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain, fJustCheck, fScRelatedChecks, &dummyCertStatusUpdateInfo);

    //checks
    ASSERT_TRUE(res);
    CCoins lowQualityCertCoin;
    EXPECT_FALSE(sidechainsView->GetCoins(lowQualityCert.GetHash(), lowQualityCertCoin));

    CCoins highQualityCertCoin;
    sidechainsView->GetCoins(highQualityCert.GetHash(), highQualityCertCoin);
    EXPECT_TRUE(highQualityCertCoin.IsFromCert());
    EXPECT_TRUE(highQualityCertCoin.vout.size() == 1);
    EXPECT_TRUE(highQualityCertCoin.nFirstBwtPos == 0);
    EXPECT_TRUE(highQualityCertCoin.IsAvailable(0));
}

TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_ScCreation_then_Mbtr_InSameBlock)
{
    // create coinbase to finance certificate submission (just in view)
    int certBlockHeight {201};
    uint256 inputScCreationHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);
    uint256 inputMbtrHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY-1);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // setup sidechain initial state
    CSidechain dummyScState;
    uint256 dummyScId = uint256();
    storeSidechain(dummyScId, dummyScState); //Setup bestBlock

    // create block with scCreation and mbtr ...
    CBlock block;
    fillBlockHeader(block, uint256S("aaa"));
    block.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));

    CMutableTransaction scCreation;
    scCreation.vin.push_back(CTxIn(inputScCreationHash, 0, CScript(), 0));
    scCreation.nVersion    = SC_TX_VERSION;
    scCreation.vsc_ccout.resize(1);
    scCreation.vsc_ccout[0].nValue = CAmount(1);
    scCreation.vsc_ccout[0].withdrawalEpochLength = 15;
    scCreation.vsc_ccout[0].wCertVk = CScVKey(ParseHex(SAMPLE_VK));
    scCreation.vsc_ccout[0].wMbtrVk = CScVKey(ParseHex(SAMPLE_VK));

    CMutableTransaction mbtrTx;
    mbtrTx.vin.push_back(CTxIn(inputMbtrHash, 0, CScript(), 0));
    CBwtRequestOut mcBwtReq;
    mcBwtReq.scId = CTransaction(scCreation).GetScIdFromScCcOut(0);
    mcBwtReq.scFee = CAmount(0);
    mcBwtReq.scProof = CScProof{ParseHex(SAMPLE_PROOF)};
    mcBwtReq.scRequestData = CFieldElement{SAMPLE_FIELD};
    mbtrTx.nVersion = SC_TX_VERSION;
    mbtrTx.vmbtr_out.push_back(mcBwtReq);

    block.vtx.push_back(scCreation);
    block.vtx.push_back(mbtrTx);

    // ... and corresponding block index
    CBlockIndex* blockIndex = AddToBlockIndex(block);
    blockIndex->nHeight = certBlockHeight;
    blockIndex->pprev = chainActive.Tip();
    blockIndex->pprev->phashBlock = &dummyHash;
    blockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(blockIndex);

    bool fJustCheck = true;
    bool fScRelatedChecks = false;

    // test
    bool res = ConnectBlock(block, dummyState, blockIndex, *sidechainsView, dummyChain, fJustCheck, fScRelatedChecks, &dummyCertStatusUpdateInfo);

    //checks
    ASSERT_TRUE(res);
    ASSERT_TRUE(sidechainsView->HaveSidechain(CTransaction(scCreation).GetScIdFromScCcOut(0)));
}

TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_Mbtr_then_ScCreation_InSameBlock)
{
    // create coinbase to finance certificate submission (just in view)
    int certBlockHeight {201};
    uint256 inputScCreationHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);
    uint256 inputMbtrHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY-1);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // setup sidechain initial state
    CSidechain dummyScState;
    uint256 dummyScId = uint256();
    storeSidechain(dummyScId, dummyScState); //Setup bestBlock

    // create faulty block with mbtr before scCreation ...
    CBlock block;
    fillBlockHeader(block, uint256S("aaa"));
    block.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));

    CMutableTransaction scCreation;
    scCreation.vin.push_back(CTxIn(inputScCreationHash, 0, CScript(), 0));
    scCreation.nVersion    = SC_TX_VERSION;
    scCreation.vsc_ccout.resize(1);
    scCreation.vsc_ccout[0].nValue = CAmount(1);
    scCreation.vsc_ccout[0].withdrawalEpochLength = 15;
    scCreation.vsc_ccout[0].wCertVk = CScVKey(ParseHex(SAMPLE_VK));
    scCreation.vsc_ccout[0].wMbtrVk = CScVKey(ParseHex(SAMPLE_VK));

    CMutableTransaction mbtrTx;
    mbtrTx.vin.push_back(CTxIn(inputMbtrHash, 0, CScript(), 0));
    CBwtRequestOut mcBwtReq;
    mcBwtReq.scId = CTransaction(scCreation).GetScIdFromScCcOut(0);
    mcBwtReq.scFee = CAmount(0);
    mbtrTx.nVersion = SC_TX_VERSION;
    mbtrTx.vmbtr_out.push_back(mcBwtReq);

    block.vtx.push_back(mbtrTx);
    block.vtx.push_back(scCreation);

    // ... and corresponding block index
    CBlockIndex* blockIndex = AddToBlockIndex(block);
    blockIndex->nHeight = certBlockHeight;
    blockIndex->pprev = chainActive.Tip();
    blockIndex->pprev->phashBlock = &dummyHash;
    blockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(blockIndex);

    bool fJustCheck = true;
    bool fScRelatedChecks = false;

    // test
    bool res = ConnectBlock(block, dummyState, blockIndex, *sidechainsView, dummyChain, fJustCheck, fScRelatedChecks, &dummyCertStatusUpdateInfo);

    //checks
    EXPECT_FALSE(res);
}
///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// HELPERS ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void SidechainsConnectCertsBlockTestSuite::storeSidechain(const uint256& scId, const CSidechain& sidechain)
{
    txCreationUtils::storeSidechain(sidechainsView->getSidechainMap(), scId, sidechain);

    CSidechainsMap      dummySidechains;
    CSidechainEventsMap dummySidechainsEvents;
    CCoinsMap           dummyCoins;
    uint256             dummyAnchor = uint256S("59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7"); //anchor for empty block!?
    CNullifiersMap      dummyNullifiers;

    CAnchorsCacheEntry dummyAnchorsEntry;
    dummyAnchorsEntry.entered = true;
    dummyAnchorsEntry.flags = CAnchorsCacheEntry::DIRTY;

    CAnchorsMap dummyAnchors;
    dummyAnchors[dummyAnchor] = dummyAnchorsEntry;

    CCswNullifiersMap dummyCswNullifiers;

    sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyAnchor, dummyAnchors,
                               dummyNullifiers, dummySidechains, dummySidechainsEvents, dummyCswNullifiers);

    return;
}

void SidechainsConnectCertsBlockTestSuite::storeSidechainEvent(int eventHeight, const CSidechainEvents& scEvent)
{
    txCreationUtils::storeSidechainEvent(sidechainsView->getScEventsMap(), eventHeight, scEvent);

    CSidechainsMap      dummySidechains;
    CSidechainEventsMap dummySidechainsEvents;
    CCoinsMap           dummyCoins;
    uint256             dummyAnchor = uint256S("59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7"); //anchor for empty block!?
    CNullifiersMap      dummyNullifiers;

    CAnchorsCacheEntry dummyAnchorsEntry;
    dummyAnchorsEntry.entered = true;
    dummyAnchorsEntry.flags = CAnchorsCacheEntry::DIRTY;

    CAnchorsMap dummyAnchors;
    dummyAnchors[dummyAnchor] = dummyAnchorsEntry;

    CCswNullifiersMap dummyCswNullifiers;

    sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyAnchor, dummyAnchors,
                               dummyNullifiers, dummySidechains, dummySidechainsEvents, dummyCswNullifiers);

    return;
}

void SidechainsConnectCertsBlockTestSuite::fillBlockHeader(CBlock& blockToFill, const uint256& prevBlockHash)
{
    blockToFill.nVersion = MIN_BLOCK_VERSION;
    blockToFill.hashPrevBlock = prevBlockHash;
    blockToFill.hashMerkleRoot = uint256();
    blockToFill.hashScTxsCommitment.SetNull();

    static unsigned int runCounter = 0;
    SetMockTime(time(nullptr) + ++runCounter);
    CBlockIndex fakePrevBlockIdx(Params().GenesisBlock());
    UpdateTime(&blockToFill, Params().GetConsensus(), &fakePrevBlockIdx);

    blockToFill.nBits = UintToArith256(Params().GetConsensus().powLimit).GetCompact();
    blockToFill.nNonce = Params().GenesisBlock().nNonce;
    return;
}

void SidechainsConnectCertsBlockTestSuite::CreateCheckpointAfter(CBlockIndex* blkIdx)
{
    assert(blkIdx != nullptr);

    CBlock dummyCheckpointBlock;
    CBlockIndex* dummyCheckPoint = AddToBlockIndex(dummyCheckpointBlock);
    dummyCheckPoint->nHeight = blkIdx->nHeight + 1;
    dummyCheckPoint->pprev = blkIdx;
    Checkpoints::CCheckpointData& checkpoints = const_cast<Checkpoints::CCheckpointData&>(Params().Checkpoints());
    checkpoints.mapCheckpoints[dummyCheckPoint->nHeight] = dummyCheckpointBlock.GetHash();
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// BLOCK_FORMATION ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
#include <algorithm>

class SidechainsBlockFormationTestSuite : public ::testing::Test {
public:
    SidechainsBlockFormationTestSuite():
        fakeChainStateDb(nullptr), blockchainView(nullptr),
        vecPriority(), orphanList(), mapDependers(),
        dummyHeight(1987), dummyLockTimeCutoff(0),
        dummyAmount(10), dummyScript(), dummyOut(dummyAmount, dummyScript) {}

    ~SidechainsBlockFormationTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        UnloadBlockIndex();

        fakeChainStateDb   = new CInMemorySidechainDb();
        blockchainView     = new CCoinsViewCache(fakeChainStateDb);
    };

    void TearDown() override {
        delete blockchainView;
        blockchainView = nullptr;

        delete fakeChainStateDb;
        fakeChainStateDb = nullptr;

        UnloadBlockIndex();
    };

protected:
    CInMemorySidechainDb *fakeChainStateDb;
    CCoinsViewCache      *blockchainView;

    std::vector<TxPriority> vecPriority;
    std::list<COrphan> orphanList;
    std::map<uint256, std::vector<COrphan*> > mapDependers;

    int dummyHeight;
    int64_t dummyLockTimeCutoff;

    CAmount dummyAmount;
    CScript dummyScript;
    CTxOut dummyOut;
};

TEST_F(SidechainsBlockFormationTestSuite, EmptyMempoolOrdering)
{
    ASSERT_TRUE(mempool.size() == 0);

    GetBlockTxPriorityData(*blockchainView, dummyHeight, dummyLockTimeCutoff, vecPriority, orphanList, mapDependers);
    GetBlockCertPriorityData(*blockchainView, dummyHeight, vecPriority, orphanList, mapDependers);

    EXPECT_TRUE(vecPriority.size() == 0);
    EXPECT_TRUE(orphanList.size() == 0);
    EXPECT_TRUE(mapDependers.size() == 0);
}

TEST_F(SidechainsBlockFormationTestSuite, SingleTxes_MempoolOrdering)
{
    uint256 inputCoinHash_1 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight);
    uint256 inputCoinHash_2 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight-1);

    CMutableTransaction tx_highFee;
    tx_highFee.vin.push_back(CTxIn(inputCoinHash_1, 0, dummyScript));
    tx_highFee.addOut(dummyOut);
    CTxMemPoolEntry tx_highFee_entry(tx_highFee, /*fee*/CAmount(100), /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(tx_highFee.GetHash(), tx_highFee_entry));

    CMutableTransaction tx_highPriority;
    tx_highPriority.vin.push_back(CTxIn(inputCoinHash_2, 0, dummyScript));
    tx_highPriority.addOut(dummyOut);
    CTxMemPoolEntry tx_highPriority_entry(tx_highPriority, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/100.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(tx_highPriority.GetHash(), tx_highPriority_entry));

    //test
    GetBlockTxPriorityData(*blockchainView, dummyHeight, dummyLockTimeCutoff, vecPriority, orphanList, mapDependers);

    //checks
    EXPECT_TRUE(vecPriority.size() == 2);
    EXPECT_TRUE(orphanList.size() == 0);

    TxPriorityCompare sortByFee(/*sort-by-fee*/true);
    std::make_heap(vecPriority.begin(), vecPriority.end(), sortByFee);
    EXPECT_TRUE(vecPriority.front().get<2>()->GetHash() == tx_highFee.GetHash());
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == tx_highPriority.GetHash());

    TxPriorityCompare sortByPriority(/*sort-by-fee*/false);
    std::make_heap(vecPriority.begin(), vecPriority.end(), sortByPriority);
    EXPECT_TRUE(vecPriority.front().get<2>()->GetHash() == tx_highPriority.GetHash());
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == tx_highFee.GetHash());
}

TEST_F(SidechainsBlockFormationTestSuite, DifferentScIdCerts_FeesAndPriorityOnlyContributeToMempoolOrdering)
{
    LOCK(mempool.cs); //needed when compiled with --enable-debug, which activates ASSERT_HELD
    uint256 inputCoinHash_1 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight);
    uint256 inputCoinHash_2 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight-1);

    CMutableScCertificate cert_highFee;
    cert_highFee.scId = uint256S("aaa");
    cert_highFee.vin.push_back(CTxIn(inputCoinHash_1, 0, dummyScript));
    cert_highFee.addOut(dummyOut);
    CCertificateMemPoolEntry cert_highFee_entry(cert_highFee, /*fee*/CAmount(100), /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_highFee.GetHash(), cert_highFee_entry));

    CMutableScCertificate cert_highPriority;
    cert_highPriority.scId = uint256S("bbb");
    cert_highPriority.vin.push_back(CTxIn(inputCoinHash_2, 0, dummyScript));
    cert_highPriority.addOut(dummyOut);
    CCertificateMemPoolEntry cert_highPriority_entry(cert_highPriority, /*fee*/CAmount(1),   /*time*/ 1000, /*priority*/100.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_highPriority.GetHash(), cert_highPriority_entry));

    //test
    GetBlockCertPriorityData(*blockchainView, dummyHeight, vecPriority, orphanList, mapDependers);

    //checks
    EXPECT_TRUE(vecPriority.size() == 2);
    EXPECT_TRUE(orphanList.size() == 0);

    TxPriorityCompare sortByFee(/*sort-by-fee*/true);
    std::make_heap(vecPriority.begin(), vecPriority.end(), sortByFee);
    EXPECT_TRUE(vecPriority.front().get<2>()->GetHash() == cert_highFee.GetHash());
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == cert_highPriority.GetHash());

    TxPriorityCompare sortByPriority(/*sort-by-fee*/false);
    std::make_heap(vecPriority.begin(), vecPriority.end(), sortByPriority);
    EXPECT_TRUE(vecPriority.front().get<2>()->GetHash() == cert_highPriority.GetHash());
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == cert_highFee.GetHash());
}

TEST_F(SidechainsBlockFormationTestSuite, SameScIdCerts_HighwQualityCertsSpedingLowQualityOnesAreAccepted)
{
    LOCK(mempool.cs); //needed when compiled with --enable-debug, which activates ASSERT_HELD
    uint256 inputCoinHash_1 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight);

    CMutableScCertificate cert_lowQuality;
    cert_lowQuality.scId = uint256S("aaa");
    cert_lowQuality.quality = 100;
    cert_lowQuality.vin.push_back(CTxIn(inputCoinHash_1, 0, dummyScript));
    cert_lowQuality.addOut(dummyOut);
    CCertificateMemPoolEntry cert_lowQuality_entry(cert_lowQuality, /*fee*/CAmount(1),   /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_lowQuality.GetHash(), cert_lowQuality_entry));

    CMutableScCertificate cert_highQuality;
    cert_highQuality.scId = cert_lowQuality.scId;
    cert_highQuality.quality = cert_lowQuality.quality * 2;
    cert_highQuality.vin.push_back(CTxIn(cert_lowQuality.GetHash(), 0, dummyScript));
    cert_highQuality.addOut(dummyOut);
    CCertificateMemPoolEntry cert_highQuality_entry(cert_highQuality, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_highQuality.GetHash(), cert_highQuality_entry));

    //test
    GetBlockCertPriorityData(*blockchainView, dummyHeight, vecPriority, orphanList, mapDependers);

    //checks
    EXPECT_TRUE(vecPriority.size() == 1);
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == cert_lowQuality.GetHash());
    EXPECT_TRUE(orphanList.size() == 1);
    EXPECT_TRUE(*dynamic_cast<const CScCertificate*>(orphanList.back().ptx) == CScCertificate(cert_highQuality));
}

TEST_F(SidechainsBlockFormationTestSuite, SameScIdCerts_LowQualityCertsSpedingHighQualityOnesAreRejected)
{
    LOCK(mempool.cs); //needed when compiled with --enable-debug, which activates ASSERT_HELD
    uint256 inputCoinHash_1 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight);

    CMutableScCertificate cert_highQuality;
    cert_highQuality.scId = uint256S("aaa");
    cert_highQuality.quality = 100;
    cert_highQuality.vin.push_back(CTxIn(inputCoinHash_1, 0, dummyScript));
    cert_highQuality.addOut(dummyOut);
    CCertificateMemPoolEntry cert_highQuality_entry(cert_highQuality, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_highQuality.GetHash(), cert_highQuality_entry));

    CMutableScCertificate cert_lowQuality;
    cert_lowQuality.scId = cert_highQuality.scId;
    cert_lowQuality.quality = cert_highQuality.quality / 2;
    cert_lowQuality.vin.push_back(CTxIn(cert_highQuality.GetHash(), 0, dummyScript));
    cert_lowQuality.addOut(dummyOut);
    CCertificateMemPoolEntry cert_lowQuality_entry(cert_lowQuality, /*fee*/CAmount(1),   /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_lowQuality.GetHash(), cert_lowQuality_entry));

    //test
    GetBlockCertPriorityData(*blockchainView, dummyHeight, vecPriority, orphanList, mapDependers);

    //checks
    EXPECT_TRUE(vecPriority.size() == 1);
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == cert_highQuality.GetHash());
    EXPECT_TRUE(orphanList.size() == 0) << "cert_lowQuality should not be counted since it's wrong dependency";
}

TEST_F(SidechainsBlockFormationTestSuite, Unconfirmed_Mbtr_scCreation_DulyOrdered)
{
    uint256 inputCoinHash_1 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight);

    CMutableTransaction mutScCreation = txCreationUtils::createNewSidechainTxWith(dummyAmount, dummyHeight);
    mutScCreation.vin.at(0) = CTxIn(inputCoinHash_1, 0, dummyScript);
    CTransaction scCreation(mutScCreation);
    const uint256& scId = scCreation.GetScIdFromScCcOut(0);
    CTxMemPoolEntry scCreation_entry(scCreation, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(scCreation.GetHash(), scCreation_entry));

    CMutableTransaction mbtrTx;
    CBwtRequestOut mcBwtReq;
    mcBwtReq.scId = scId;
    mbtrTx.nVersion = SC_TX_VERSION;
    mbtrTx.vmbtr_out.push_back(mcBwtReq);
    CTxMemPoolEntry mbtr_entry(mbtrTx, /*fee*/CAmount(1000),   /*time*/ 1000, /*priority*/1000.0, /*height*/dummyHeight);
    std::map<uint256, CFieldElement> dummyCertDataHashInfo;
    dummyCertDataHashInfo[scId] = CFieldElement{};
    ASSERT_TRUE(mempool.addUnchecked(mbtrTx.GetHash(), mbtr_entry, /*fCurrentEstimate*/true, dummyCertDataHashInfo));

    //test
    int64_t dummyLockTimeCutoff{0};
    GetBlockTxPriorityData(*blockchainView, dummyHeight, dummyLockTimeCutoff, vecPriority, orphanList, mapDependers);

    //checks
    EXPECT_TRUE(vecPriority.size() == 1);
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == scCreation.GetHash());
    EXPECT_TRUE(orphanList.size() == 1);
    EXPECT_TRUE(orphanList.front().ptx->GetHash() == CTransaction(mbtrTx).GetHash());
}
