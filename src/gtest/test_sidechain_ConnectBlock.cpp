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

class SidechainConnectCertsBlockTestSuite : public ::testing::Test {
public:
    SidechainConnectCertsBlockTestSuite():
        fakeChainStateDb(nullptr), sidechainsView(nullptr),
        dummyBlock(), dummyHash(), dummyVoidedCertMap(), dummyScriptPubKey(),
        dummyState(), dummyChain(), dummyScEvents(), dummyFeeAmount(), dummyCoinbaseScript()
    {
        dummyScriptPubKey = GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/false);
    }

    ~SidechainConnectCertsBlockTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        UnloadBlockIndex();

        fakeChainStateDb   = new CInMemorySidechainDb();
        sidechainsView     = new CCoinsViewCache(fakeChainStateDb);

        dummyHash = dummyBlock.GetHash();
        dummyCoinbaseScript = CScript() << OP_DUP << OP_HASH160
                << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG;
    };

    void TearDown() override {
        delete sidechainsView;
        sidechainsView = nullptr;

        delete fakeChainStateDb;
        fakeChainStateDb = nullptr;

        UnloadBlockIndex();
    };

protected:
    CInMemorySidechainDb *fakeChainStateDb;
    CCoinsViewCache      *sidechainsView;

    //helpers
    CBlock                  dummyBlock;
    uint256                 dummyHash;
    std::map<uint256, bool> dummyVoidedCertMap;
    CScript                 dummyScriptPubKey;

    CValidationState    dummyState;
    CChain              dummyChain;

    CSidechainEventsMap dummyScEvents;

    uint256 storeSidechain(const uint256& scId, const CSidechain& sidechain, CSidechainEventsMap& sidechainEventsMap);
    void fillBlockHeader(CBlock& blockToFill, const uint256& prevBlockHash);

    CAmount dummyFeeAmount;
    CScript dummyCoinbaseScript;
    uint256 CreateSpendableTxAtHeight(unsigned int coinHeight);
    void    CreateCheckpointAfter(CBlockIndex* blkIdx);
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// ConnectBlock ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainConnectCertsBlockTestSuite, ConnectBlock_SingleCert_SameEpoch_CertCoinHasBwt)
{
    // create coinbase to finance certificate submission (just in view)
    int certBlockHeight {201};
    uint256 inputTxHash = CreateSpendableTxAtHeight(certBlockHeight-COINBASE_MATURITY);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 100;
    initialScState.creationData.withdrawalEpochLength = 20;
    initialScState.topCommittedCertHash = uint256S("cccc");
    initialScState.topCommittedCertQuality = 100;
    initialScState.topCommittedCertReferencedEpoch = initialScState.EpochFor(certBlockHeight)-1;
    initialScState.topCommittedCertBwtAmount = 50;
    initialScState.balance = CAmount(100);

    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    CSidechainEventsMap ceasingMap;
    ceasingMap[205] = CSidechainEventsCacheEntry(event, CSidechainEventsCacheEntry::Flags::FRESH);
    storeSidechain(scId, initialScState, ceasingMap);

    // create block with certificate ...
    CMutableScCertificate singleCert;
    singleCert.vin.push_back(CTxIn(inputTxHash, 0, CScript(), 0));
    singleCert.nVersion    = SC_CERT_VERSION;
    singleCert.scProof     = libzendoomc::ScProof(ParseHex(SAMPLE_PROOF));
    singleCert.scId        = scId;
    singleCert.epochNumber = initialScState.topCommittedCertReferencedEpoch;
    singleCert.quality     = initialScState.topCommittedCertQuality * 2;
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
    bool fCheckScTxesCommitment = false;

    // test
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain, fJustCheck, fCheckScTxesCommitment, &dummyVoidedCertMap);

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

TEST_F(SidechainConnectCertsBlockTestSuite, ConnectBlock_SingleCert_DifferentEpoch_CertCoinHasBwt)
{
    // create coinbase to finance certificate submission (just in view)
    int certBlockHeight {201};
    uint256 inputTxHash = CreateSpendableTxAtHeight(certBlockHeight-COINBASE_MATURITY);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 100;
    initialScState.creationData.withdrawalEpochLength = 20;
    initialScState.topCommittedCertHash = uint256S("cccc");
    initialScState.topCommittedCertQuality = 100;
    initialScState.topCommittedCertReferencedEpoch = initialScState.EpochFor(certBlockHeight)-2;
    initialScState.topCommittedCertBwtAmount = 50;
    initialScState.balance = CAmount(100);

    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    CSidechainEventsMap ceasingMap;
    ceasingMap[205] = CSidechainEventsCacheEntry(event, CSidechainEventsCacheEntry::Flags::FRESH);
    storeSidechain(scId, initialScState, ceasingMap);

    // create block with certificate ...
    CMutableScCertificate singleCert;
    singleCert.vin.push_back(CTxIn(inputTxHash, 0, CScript(), 0));
    singleCert.nVersion    = SC_CERT_VERSION;
    singleCert.scProof     = libzendoomc::ScProof(ParseHex(SAMPLE_PROOF));
    singleCert.scId        = scId;
    singleCert.epochNumber = initialScState.topCommittedCertReferencedEpoch + 1;
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
    bool fCheckScTxesCommitment = false;

    // test
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain, fJustCheck, fCheckScTxesCommitment, &dummyVoidedCertMap);

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

TEST_F(SidechainConnectCertsBlockTestSuite, ConnectBlock_MultipleCerts_SameEpoch_LowQualityCertCoinHasNotBwt)
{
    // create coinbase to finance certificate submission (just in view)
    int certBlockHeight {201};
    uint256 inputLowQCertHash = CreateSpendableTxAtHeight(certBlockHeight-COINBASE_MATURITY);
    uint256 inputHighQCertHash = CreateSpendableTxAtHeight(certBlockHeight-COINBASE_MATURITY-1);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 100;
    initialScState.creationData.withdrawalEpochLength = 20;
    initialScState.topCommittedCertHash = uint256S("cccc");
    initialScState.topCommittedCertQuality = 100;
    initialScState.topCommittedCertReferencedEpoch = initialScState.EpochFor(certBlockHeight)-1;
    initialScState.topCommittedCertBwtAmount = 50;
    initialScState.balance = CAmount(100);

    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    CSidechainEventsMap ceasingMap;
    ceasingMap[205] = CSidechainEventsCacheEntry(event, CSidechainEventsCacheEntry::Flags::FRESH);
    storeSidechain(scId, initialScState, ceasingMap);

    // create block with certificates ...
    CMutableScCertificate lowQualityCert;
    lowQualityCert.vin.push_back(CTxIn(inputLowQCertHash, 0, CScript(), 0));
    lowQualityCert.nVersion    = SC_CERT_VERSION;
    lowQualityCert.scProof     = libzendoomc::ScProof(ParseHex(SAMPLE_PROOF));
    lowQualityCert.scId        = scId;
    lowQualityCert.epochNumber = initialScState.topCommittedCertReferencedEpoch;
    lowQualityCert.quality     = initialScState.topCommittedCertQuality * 2;
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
    bool fCheckScTxesCommitment = false;

    // test
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain, fJustCheck, fCheckScTxesCommitment, &dummyVoidedCertMap);

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

TEST_F(SidechainConnectCertsBlockTestSuite, ConnectBlock_MultipleCerts_DifferentEpoch_LowQualityCertCoinHasNotBwt)
{
    // create coinbase to finance certificate submission (just in view)
    int certBlockHeight {201};
    uint256 inputLowQCertHash = CreateSpendableTxAtHeight(certBlockHeight-COINBASE_MATURITY);
    uint256 inputHighQCertHash = CreateSpendableTxAtHeight(certBlockHeight-COINBASE_MATURITY-1);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 100;
    initialScState.creationData.withdrawalEpochLength = 20;
    initialScState.topCommittedCertHash = uint256S("cccc");
    initialScState.topCommittedCertQuality = 100;
    initialScState.topCommittedCertReferencedEpoch = initialScState.EpochFor(certBlockHeight)-2;
    initialScState.topCommittedCertBwtAmount = 50;
    initialScState.balance = CAmount(100);

    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    CSidechainEventsMap ceasingMap;
    ceasingMap[205] = CSidechainEventsCacheEntry(event, CSidechainEventsCacheEntry::Flags::FRESH);
    storeSidechain(scId, initialScState, ceasingMap);

    // create block with certificates ...
    CMutableScCertificate lowQualityCert;
    lowQualityCert.vin.push_back(CTxIn(inputLowQCertHash, 0, CScript(), 0));
    lowQualityCert.nVersion    = SC_CERT_VERSION;
    lowQualityCert.scProof     = libzendoomc::ScProof(ParseHex(SAMPLE_PROOF));
    lowQualityCert.scId        = scId;
    lowQualityCert.epochNumber = initialScState.topCommittedCertReferencedEpoch +1;
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
    bool fCheckScTxesCommitment = false;

    // test
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain, fJustCheck, fCheckScTxesCommitment, &dummyVoidedCertMap);

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

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// HELPERS ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
uint256 SidechainConnectCertsBlockTestSuite::storeSidechain(const uint256& scId, const CSidechain& sidechain, CSidechainEventsMap& sidechainEventsMap)
{
    CSidechainsMap mapSidechain;
    mapSidechain[scId] = CSidechainsCacheEntry(sidechain,CSidechainsCacheEntry::Flags::FRESH);

    CCoinsMap           dummyCoins;
    uint256             dummyAnchor = uint256S("59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7"); //anchor for empty block!?
    CNullifiersMap      dummyNullifiers;

    CAnchorsCacheEntry dummyAnchorsEntry;
    dummyAnchorsEntry.entered = true;
    dummyAnchorsEntry.flags = CAnchorsCacheEntry::DIRTY;

    CAnchorsMap dummyAnchors;
    dummyAnchors[dummyAnchor] = dummyAnchorsEntry;

    sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyAnchor, dummyAnchors,
                               dummyNullifiers, mapSidechain, sidechainEventsMap);

    return scId;
}

void SidechainConnectCertsBlockTestSuite::fillBlockHeader(CBlock& blockToFill, const uint256& prevBlockHash)
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

uint256 SidechainConnectCertsBlockTestSuite::CreateSpendableTxAtHeight(unsigned int coinHeight)
{
    CTransaction inputTx = createCoinbase(dummyCoinbaseScript, dummyFeeAmount, coinHeight);
    CTxUndo dummyUndo;
    UpdateCoins(inputTx, *sidechainsView, dummyUndo, coinHeight);
    assert(sidechainsView->HaveCoins(inputTx.GetHash()));
    return inputTx.GetHash();
}

void SidechainConnectCertsBlockTestSuite::CreateCheckpointAfter(CBlockIndex* blkIdx)
{
    assert(blkIdx != nullptr);

    CBlock dummyCheckpointBlock;
    CBlockIndex* dummyCheckPoint = AddToBlockIndex(dummyCheckpointBlock);
    dummyCheckPoint->nHeight = blkIdx->nHeight + 1;
    dummyCheckPoint->pprev = blkIdx;
    Checkpoints::CCheckpointData& checkpoints = const_cast<Checkpoints::CCheckpointData&>(Params().Checkpoints());
    checkpoints.mapCheckpoints[dummyCheckPoint->nHeight] = dummyCheckpointBlock.GetHash();
}
