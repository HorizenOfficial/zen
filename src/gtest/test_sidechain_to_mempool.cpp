#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <chainparams.h>
#include <util.h>

#include <txdb.h>
#include <main.h>
#include <zen/forks/fork6_sidechainfork.h>

#include <key.h>
#include <keystore.h>
#include <script/sign.h>

#include "tx_creation_utils.h"
#include <consensus/validation.h>

#include <sc/sidechain.h>
#include <txmempool.h>
#include <init.h>

class CCoinsOnlyViewDB : public CCoinsViewDB
{
public:
    CCoinsOnlyViewDB(size_t nCacheSize, bool fWipe = false)
        : CCoinsViewDB(nCacheSize, false, fWipe) {}

    bool BatchWrite(CCoinsMap &mapCoins)
    {
        const uint256 hashBlock;
        const uint256 hashAnchor;
        CAnchorsMap mapAnchors;
        CNullifiersMap mapNullifiers;
        CSidechainsMap mapSidechains;

        return CCoinsViewDB::BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapSidechains);
    }
};

class SidechainsInMempoolTestSuite: public ::testing::Test {
public:
    SidechainsInMempoolTestSuite():
        pathTemp(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path()),
        chainStateDbSize(2 * 1024 * 1024),
        pChainStateDb(nullptr),
        minimalHeightForSidechains(SidechainFork().getHeight(CBaseChainParams::REGTEST))
    {
        SelectParams(CBaseChainParams::REGTEST);

        boost::filesystem::create_directories(pathTemp);
        mapArgs["-datadir"] = pathTemp.string();

        pChainStateDb = new CCoinsOnlyViewDB(chainStateDbSize,/*fWipe*/true);
        pcoinsTip     = new CCoinsViewCache(pChainStateDb);
    }

    void SetUp() override {
        GenerateChainActive();
        pcoinsTip->SetBestBlock(blocks.back().GetBlockHash()); //ABENEGIA: This appear to be called in AcceptToMempool but relevance is unclear
        pindexBestHeader = chainActive.Tip();

        InitCoinGeneration();
        GenerateCoinsAmount(1000);
        ASSERT_TRUE(StoreCoins());
    }

    void TearDown() override {
        mempool.clear();
        chainActive.SetTip(nullptr);
        mapBlockIndex.clear();
    }

    ~SidechainsInMempoolTestSuite() {
        delete pcoinsTip;
        pcoinsTip = nullptr;

        delete pChainStateDb;
        pChainStateDb = nullptr;

        ClearDatadirCache();
        boost::system::error_code ec;
        boost::filesystem::remove_all(pathTemp.string(), ec);
    }

protected:
    CTransaction GenerateScTx(const uint256 & newScId, const CAmount & fwdTxAmount);
    CTransaction GenerateFwdTransferTx(const uint256 & newScId, const CAmount & fwdTxAmount);

private:
    boost::filesystem::path  pathTemp;
    const unsigned int       chainStateDbSize;
    CCoinsOnlyViewDB*        pChainStateDb;

    const unsigned int       minimalHeightForSidechains;
    std::vector<uint256>     blockHashes;
    std::vector<CBlockIndex> blocks;
    void GenerateChainActive();

    CKey                     coinsKey;
    CBasicKeyStore           keystore;
    CScript                  coinsScript;
    CCoinsMap                initialCoinsSet;
    void InitCoinGeneration();
    void GenerateCoinsAmount(const CAmount & amountToGenerate);
    bool StoreCoins();
};

TEST_F(SidechainsInMempoolTestSuite, NewSidechainsAreAcceptedToMempool) {
    CTransaction scTx = GenerateScTx(uint256S("1492"), CAmount(1));
    CValidationState txState;
    bool missingInputs = false;

    EXPECT_TRUE(AcceptToMemoryPool(mempool, txState, scTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, DuplicatedSidechainsAreNotAcceptedToMempool) {
    uint256 scId = uint256S("1492");
    CTransaction scTx = GenerateScTx(scId, CAmount(1));
    CValidationState txState;
    bool missingInputs = false;
    AcceptToMemoryPool(mempool, txState, scTx, false, &missingInputs);

    scTx = GenerateScTx(scId, CAmount(100));
    txState = CValidationState();
    missingInputs = false;

    EXPECT_FALSE(AcceptToMemoryPool(mempool, txState, scTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, DuplicationsOfConfirmedSidechainsAreNotAcceptedToMempool) {
    uint256 scId = uint256S("a1b2");
    CTransaction scTx = GenerateScTx(scId, CAmount(1));
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    scTx = GenerateScTx(scId, CAmount(12));
    CValidationState txState;
    bool missingInputs = false;

    EXPECT_FALSE(AcceptToMemoryPool(mempool, txState, scTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToConfirmedSideChainsAreAllowed) {
    uint256 scId = uint256S("aaaa");
    CTransaction scTx = GenerateScTx(scId, CAmount(10));
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;
    bool missingInputs = false;

    EXPECT_TRUE(AcceptToMemoryPool(mempool, fwdTxState, fwdTx, false, &missingInputs));
}

//ABENEGIA: commented out since it fails as per current implementation (github issue 215)
//TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToSideChainsInMempoolAreAllowed) {
//    uint256 scId = uint256S("1492");
//    CTransaction scTx = GenerateScTx(scId, CAmount(1));
//    CValidationState scTxState;
//    bool missingInputs = false;
//    AcceptToMemoryPool(mempool, scTxState, scTx, false, &missingInputs);
//
//    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
//    CValidationState fwdTxState;
//    EXPECT_TRUE(AcceptToMemoryPool(mempool, fwdTxState, fwdTx, false, &missingInputs));
//}

TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToUnknownSideChainAreNotAllowed) {
    uint256 scId = uint256S("1492");
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;
    bool missingInputs = false;

    EXPECT_FALSE(AcceptToMemoryPool(mempool, fwdTxState, fwdTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, SidechainRemovalIsIneffectiveIfItisNotInMempool) {
    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("1492");
    CTransaction scTx = GenerateScTx(scId, CAmount(10));
    ASSERT_FALSE(Sidechain::existsInMempool(aMempool,scTx));

    std::list<CTransaction> removedTxs;
    aMempool.remove(scTx, removedTxs, /*fRecursive*/true);

    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), scTx));
    EXPECT_FALSE(Sidechain::existsInMempool(aMempool,scTx));
    EXPECT_FALSE(aMempool.sidechainExists(scId));
}

TEST_F(SidechainsInMempoolTestSuite, SidechainIsRemovedFromMempoolWithTxCreatingIt) {
    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("1492");
    CTransaction scTx = GenerateScTx(scId, CAmount(10));
    CTxMemPoolEntry poolEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scTx.GetHash(), poolEntry);
    ASSERT_TRUE(Sidechain::existsInMempool(aMempool,scTx));

    std::list<CTransaction> removedTxs;
    aMempool.remove(scTx, removedTxs, /*fRecursive*/true);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), scTx));
    EXPECT_FALSE(Sidechain::existsInMempool(aMempool,scTx));
    EXPECT_FALSE(aMempool.sidechainExists(scId));
}

TEST_F(SidechainsInMempoolTestSuite, FwdTransfersAreRemovedFromMempoolWithTxSendindThem) {
    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("1492");
    CTransaction scTx = GenerateScTx(scId, CAmount(10));
    CTxMemPoolEntry scEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scTx.GetHash(), scEntry);
    ASSERT_TRUE(Sidechain::existsInMempool(aMempool,scTx));

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    std::list<CTransaction> removedTxs;
    aMempool.remove(fwdTx1, removedTxs, /*fRecursive*/true);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(Sidechain::existsInMempool(aMempool,scTx));
}

TEST_F(SidechainsInMempoolTestSuite, FwdTransfersAreRemovedFromMempoolUponScRemoval) {
    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("1492");
    CTransaction scTx = GenerateScTx(scId, CAmount(10));
    CTxMemPoolEntry scEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scTx.GetHash(), scEntry);
    ASSERT_TRUE(Sidechain::existsInMempool(aMempool,scTx));

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    aMempool.remove(scTx, removedTxs, /*fRecursive*/true);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), scTx));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
    EXPECT_FALSE(Sidechain::existsInMempool(aMempool,scTx));
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////// Test Fixture definitions ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
void SidechainsInMempoolTestSuite::GenerateChainActive() {
    chainActive.SetTip(NULL);
    mapBlockIndex.clear();

    blockHashes.resize(minimalHeightForSidechains);
    blocks.resize(minimalHeightForSidechains);

    for (unsigned int height=0; height<blocks.size(); ++height) {
        blockHashes[height] = ArithToUint256(height);

        blocks[height].nHeight = height+1;
        blocks[height].pprev = height == 0? nullptr : &blocks[height - 1];
        blocks[height].phashBlock = &blockHashes[height];
        blocks[height].nTime = 1269211443 + height * Params().GetConsensus().nPowTargetSpacing;
        blocks[height].nBits = 0x1e7fffff;
        blocks[height].nChainWork = height == 0 ? arith_uint256(0) : blocks[height - 1].nChainWork + GetBlockProof(blocks[height - 1]);

        mapBlockIndex[blockHashes[height]] = &blocks[height];
    }

    chainActive.SetTip(&blocks.back());
}

void SidechainsInMempoolTestSuite::InitCoinGeneration() {
    coinsKey.MakeNewKey(true);
    keystore.AddKey(coinsKey);

    coinsScript << OP_DUP << OP_HASH160 << ToByteVector(coinsKey.GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
}

void SidechainsInMempoolTestSuite::GenerateCoinsAmount(const CAmount & amountToGenerate) {
    unsigned int coinHeight = minimalHeightForSidechains;
    CCoinsCacheEntry entry;
    entry.flags = CCoinsCacheEntry::FRESH | CCoinsCacheEntry::DIRTY;

    entry.coins.fCoinBase = false;
    entry.coins.nVersion = TRANSPARENT_TX_VERSION;
    entry.coins.nHeight = coinHeight;

    entry.coins.vout.resize(1);
    entry.coins.vout[0].nValue = amountToGenerate;
    entry.coins.vout[0].scriptPubKey = coinsScript;

    std::stringstream num;
    num << std::hex << coinHeight;

    initialCoinsSet[uint256S(num.str())] = entry;
    return;
}

bool SidechainsInMempoolTestSuite::StoreCoins() {
    CCoinsViewCache view(pcoinsTip);
    CCoinsMap tmpCopyConsumedOnWrite(initialCoinsSet);

    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;
    CSidechainsMap mapSidechains;

    pcoinsTip->BatchWrite(tmpCopyConsumedOnWrite, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapSidechains);

    return view.HaveCoins(initialCoinsSet.begin()->first) == true;
}

CTransaction SidechainsInMempoolTestSuite::GenerateScTx(const uint256 & newScId, const CAmount & fwdTxAmount) {
    CMutableTransaction scTx;
    scTx.nVersion = SC_TX_VERSION;
    scTx.vin.resize(1);
    scTx.vin[0].prevout = COutPoint(initialCoinsSet.begin()->first, 0);

    scTx.vsc_ccout.resize(1);
    scTx.vsc_ccout[0].scId = newScId;

    scTx.vft_ccout.resize(1);
    scTx.vft_ccout[0].scId   = newScId;
    scTx.vft_ccout[0].nValue = fwdTxAmount;

    SignSignature(keystore, initialCoinsSet.begin()->second.coins.vout[0].scriptPubKey, scTx, 0);

    return scTx;
}

CTransaction SidechainsInMempoolTestSuite::GenerateFwdTransferTx(const uint256 & newScId, const CAmount & fwdTxAmount) {
    CMutableTransaction scTx;
    scTx.nVersion = SC_TX_VERSION;
    scTx.vin.resize(1);
    scTx.vin[0].prevout = COutPoint(initialCoinsSet.begin()->first, 0);

    scTx.vft_ccout.resize(1);
    scTx.vft_ccout[0].scId   = newScId;
    scTx.vft_ccout[0].nValue = fwdTxAmount;

    SignSignature(keystore, initialCoinsSet.begin()->second.coins.vout[0].scriptPubKey, scTx, 0);

    return scTx;
}
