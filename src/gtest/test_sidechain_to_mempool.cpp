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
        pcoinsTip->SetBestBlock(blocks.back().GetBlockHash());
        pindexBestHeader = chainActive.Tip();

        InitCoinGeneration();
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

    void InitCoinGeneration();
    std::pair<uint256, CCoinsCacheEntry> GenerateCoinsAmount(const CAmount & amountToGenerate);
    bool StoreCoins(const std::pair<uint256, CCoinsCacheEntry>& entryToStore);
};

TEST_F(SidechainsInMempoolTestSuite, NewSidechainsAreAcceptedToMempool) {
    CTransaction scTx = GenerateScTx(uint256S("ababababab"), CAmount(1));
    CValidationState txState;
    bool missingInputs = false;

    EXPECT_TRUE(AcceptToMemoryPool(mempool, txState, scTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, DuplicatedSidechainsAreNotAcceptedToMempool) {
    uint256 scId = uint256S("bbbb");
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

//A proof that https://github.com/ZencashOfficial/zen/issues/215 is solved
TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToSideChainsInMempoolAreAllowed) {
    uint256 scId = uint256S("cccc");
    CTransaction scTx = GenerateScTx(scId, CAmount(1));
    CValidationState scTxState;
    bool missingInputs = false;
    AcceptToMemoryPool(mempool, scTxState, scTx, false, &missingInputs);
    ASSERT_TRUE(mempool.sidechainExists(scId));

    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;
    EXPECT_TRUE(AcceptToMemoryPool(mempool, fwdTxState, fwdTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToUnknownSideChainAreNotAllowed) {
    uint256 scId = uint256S("dddd");
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;
    bool missingInputs = false;

    EXPECT_FALSE(AcceptToMemoryPool(mempool, fwdTxState, fwdTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, sidechainExistsTest) {
    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("1492");

    //Case 1: no sidechain related tx in mempool
    bool res = aMempool.sidechainExists(scId);
    EXPECT_FALSE(res);

    bool loopRes = false;
    for(const auto& tx : aMempool.mapTx)
        for(const auto& sc: tx.second.GetTx().vsc_ccout)
            if(sc.scId == scId) {
                loopRes = true;
                break;
            }
    EXPECT_TRUE(loopRes == res);

    //Case 2: fwd transfer tx only in mempool
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdPoolEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdPoolEntry.GetTx().GetHash(), fwdPoolEntry);
    res = aMempool.sidechainExists(scId);
    EXPECT_FALSE(res);

    loopRes = false;
    for(const auto& tx : aMempool.mapTx)
        for(const auto& sc: tx.second.GetTx().vsc_ccout)
            if(sc.scId == scId) {
                loopRes = true;
                break;
            }
    EXPECT_TRUE(loopRes == res);

    //Case 3: sc creation tx in mempool
    CTransaction scTx  = GenerateScTx(scId, CAmount(10));
    CTxMemPoolEntry scPoolEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scPoolEntry.GetTx().GetHash(), scPoolEntry);
    res = aMempool.sidechainExists(scId);
    EXPECT_TRUE(res);

    loopRes = false;
    for(const auto& tx : aMempool.mapTx)
        for(const auto& sc: tx.second.GetTx().vsc_ccout)
            if(sc.scId == scId) {
                loopRes = true;
                break;
            }
    EXPECT_TRUE(loopRes == res);
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsInMempool_ScNonRecursiveRemoval) {
    // Associated scenario: Sidechain creation and some fwds are in mempool.
    // Sc Creation is confirmed, hence it has to be removed from mempool, while fwds stay.

    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("aaaa");
    CTransaction scTx = GenerateScTx(scId, CAmount(10));
    CTxMemPoolEntry scEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scTx.GetHash(), scEntry);
    ASSERT_TRUE(aMempool.sidechainExists(scId));

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    aMempool.remove(scTx, removedTxs, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), scTx));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsInMempool_ScNonRecursiveRemoval_DifferentScCreationTx) {
    // Associated scenario: Sidechain creation and some fwds are in mempool.
    // Sc Creation is confirmed, but ScCreation txs in mempool is different from confirmed one.
    // Tx in mempool is NOT removed by simple remove. removeConflicts will take care of it.

    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("aaaa");
    CTransaction scTxInMempool = GenerateScTx(scId, CAmount(10));
    CTxMemPoolEntry scEntry(scTxInMempool, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scTxInMempool.GetHash(), scEntry);
    ASSERT_TRUE(aMempool.sidechainExists(scId));

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    CTransaction scTxConfirmed = GenerateScTx(scId, CAmount(10));
    aMempool.remove(scTxConfirmed, removedTxs, /*fRecursive*/false);

    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), scTxInMempool));
    EXPECT_TRUE(aMempool.sidechainExists(scId));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, RemoveConflictsHandlesConflictingSidechainDeclarations) {
    // Associated scenario: Sidechain creation and some fwds are in mempool.
    // Sc Creation is confirmed, but ScCreation tx in mempool is different from confirmed one.
    // removeConflicts takes care of the duplicated sc creation, while keeping fwds in mempool

    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("aaaa");
    CTransaction scTxInMempool = GenerateScTx(scId, CAmount(10));
    CTxMemPoolEntry scEntry(scTxInMempool, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scTxInMempool.GetHash(), scEntry);
    ASSERT_TRUE(aMempool.sidechainExists(scId));

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    CTransaction scTxInBlock = GenerateScTx(scId, CAmount(20));
    aMempool.removeConflicts(scTxInBlock, removedTxs);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), scTxInMempool));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, FwdsOnlyInMempool_FwdNonRecursiveRemoval) {
    // Associated scenario: fws are in mempool, hence scCreation must be already confirmed
    // A fwd is confirmed hence it, and only it, is removed from mempool

    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("ababab");

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    aMempool.remove(fwdTx1, removedTxs, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsInMempool_ScRecursiveRemoval) {
    // Associated scenario: Sidechain creation and some fwds are in mempool, e.g. as a result of previous blocks disconnections
    // One of the new blocks about to me mounted double spends the original fwdTx, hence scCreation is marked for recursive removal by removeForConflicts
    // both scCreation and fwds must be cleared from mempool

    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("aaaa");
    CTransaction scTx = GenerateScTx(scId, CAmount(10));
    CTxMemPoolEntry scEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scTx.GetHash(), scEntry);
    ASSERT_TRUE(aMempool.sidechainExists(scId));

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
}

TEST_F(SidechainsInMempoolTestSuite, FwdsOnlyInMempool_ScRecursiveRemoval) {
    // Associated scenario: upon block disconnections fwds have entered into mempool.
    // While unmounting block containing scCreation, scCreation cannot make to mempool. fwds must me purged

    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("cccc");

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    CTransaction scTx = GenerateScTx(scId, CAmount(10));
    aMempool.remove(scTx, removedTxs, /*fRecursive*/true);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsInMempool_FwdRecursiveRemoval) {
    // Associated scenario: upon block disconnections a fwd cannot make to mempool.
    // Recursive removal for refused fwd is called, but other fwds are unaffected

    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("1492");

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    aMempool.remove(fwdTx2, removedTxs, /*fRecursive*/true);

    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
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

std::pair<uint256, CCoinsCacheEntry> SidechainsInMempoolTestSuite::GenerateCoinsAmount(const CAmount & amountToGenerate) {
    static unsigned int hashSeed = 1987;
    CCoinsCacheEntry entry;
    entry.flags = CCoinsCacheEntry::FRESH | CCoinsCacheEntry::DIRTY;

    entry.coins.fCoinBase = false;
    entry.coins.nVersion = TRANSPARENT_TX_VERSION;
    entry.coins.nHeight = minimalHeightForSidechains;

    entry.coins.vout.resize(1);
    entry.coins.vout[0].nValue = amountToGenerate;
    entry.coins.vout[0].scriptPubKey = coinsScript;

    std::stringstream num;
    num << std::hex << ++hashSeed;

    return std::pair<uint256, CCoinsCacheEntry>(uint256S(num.str()), entry);
}

bool SidechainsInMempoolTestSuite::StoreCoins(const std::pair<uint256, CCoinsCacheEntry>& entryToStore) {
    CCoinsViewCache view(pcoinsTip);
    CCoinsMap tmpCoinsMap;
    tmpCoinsMap[entryToStore.first] = entryToStore.second;

    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;
    CSidechainsMap mapSidechains;

    pcoinsTip->BatchWrite(tmpCoinsMap, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapSidechains);

    return view.HaveCoins(entryToStore.first) == true;
}

CTransaction SidechainsInMempoolTestSuite::GenerateScTx(const uint256 & newScId, const CAmount & fwdTxAmount) {
    std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(1000);
    StoreCoins(coinData);

    CMutableTransaction scTx;
    scTx.nVersion = SC_TX_VERSION;
    scTx.vin.resize(1);
    scTx.vin[0].prevout = COutPoint(coinData.first, 0);

    scTx.vsc_ccout.resize(1);
    scTx.vsc_ccout[0].scId = newScId;

    scTx.vft_ccout.resize(1);
    scTx.vft_ccout[0].scId   = newScId;
    scTx.vft_ccout[0].nValue = fwdTxAmount;

    SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, scTx, 0);

    return scTx;
}

CTransaction SidechainsInMempoolTestSuite::GenerateFwdTransferTx(const uint256 & newScId, const CAmount & fwdTxAmount) {
    std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(1000);
    StoreCoins(coinData);

    CMutableTransaction scTx;
    scTx.nVersion = SC_TX_VERSION;
    scTx.vin.resize(1);
    scTx.vin[0].prevout = COutPoint(coinData.first, 0);

    scTx.vft_ccout.resize(1);
    scTx.vft_ccout[0].scId   = newScId;
    scTx.vft_ccout[0].nValue = fwdTxAmount;

    //scTx.vft_ccout.resize(2); //testing double deletes 
    //scTx.vft_ccout[1].scId   = newScId;
    //scTx.vft_ccout[1].nValue = fwdTxAmount;

    SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, scTx, 0);

    return scTx;
}
