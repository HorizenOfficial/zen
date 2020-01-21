#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <chainparams.h>
#include <util.h>

#include <txdb.h>
#include <main.h>
#include <zen/forks/fork6_sidechainfork.h>

#include <key.h>
#include <keystore.h>

#include "tx_creation_utils.h"
#include <consensus/validation.h>

#include <sc/sidechain.h>

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

        return CCoinsViewDB::BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers);
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
        //mapArgs["-allownonstandardtx"] = std::string("1"); //ABENEGIA: temp hack before fixing signature

        pChainStateDb = new CCoinsOnlyViewDB(chainStateDbSize,/*fWipe*/true);
        pcoinsTip = new CCoinsViewCache(pChainStateDb);

        assert(Sidechain::ScMgr::instance().initPersistence(/*cacheSize*/0, /*fWipe*/true));

        fPrintToConsole = true;
    }

    void SetUp() override {
        //ASSERT_TRUE(chainActive.Height() == -1)<<chainActive.Height();
        GenerateChainActive();
        pcoinsTip->SetBestBlock(blocks.back().GetBlockHash());
        //ASSERT_TRUE(chainActive.Height() == minimalHeightForSidechains)<<chainActive.Height();

        InitCoinGeneration();
        GenerateCoins(1);

        CCoinsViewCache view(pChainStateDb);
        CCoinsMap copyConsumedOnWrite(initialCoinsSet);
        ASSERT_TRUE(pChainStateDb->BatchWrite(copyConsumedOnWrite));
        //ASSERT_TRUE(view.HaveCoins(initialCoinsSet.begin()->first) == true);
    }

    void TearDown() override {
        chainActive.SetTip(NULL);
        mapBlockIndex.clear();
    }

    ~SidechainsInMempoolTestSuite() {
        Sidechain::ScMgr::instance().reset();

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

private:
    boost::filesystem::path pathTemp;
    const unsigned int      chainStateDbSize;
    CCoinsOnlyViewDB*       pChainStateDb;

    const unsigned int       minimalHeightForSidechains;
    std::vector<uint256>     blockHashes;
    std::vector<CBlockIndex> blocks;
    void GenerateChainActive();

    CKey coinsKey;
    CBasicKeyStore keystore;
    CScript coinsScript;
    CCoinsMap initialCoinsSet;
    void InitCoinGeneration();
    void GenerateCoins(unsigned int coinsCount);
};

void SidechainsInMempoolTestSuite::GenerateChainActive() {
    chainActive.SetTip(NULL);
    mapBlockIndex.clear();

    blockHashes.resize(minimalHeightForSidechains);
    blocks.resize(minimalHeightForSidechains);

    for (unsigned int height=0; height<blocks.size(); ++height) {
        blockHashes[height] = ArithToUint256(height);

        blocks[height].nHeight = height+1;
        blocks[height].pprev = height != 0? &blocks[height - 1] : nullptr;
        blocks[height].phashBlock = &blockHashes[height];
        blocks[height].nTime = 1269211443 + height * Params().GetConsensus().nPowTargetSpacing;
        blocks[height].nBits = 0x1e7fffff;
        blocks[height].nChainWork = height != 0 ? blocks[height - 1].nChainWork + GetBlockProof(blocks[height - 1]) : arith_uint256(0);

        mapBlockIndex[blockHashes[height]] = &blocks[height];
    }

    chainActive.SetTip(&blocks.back());
}

void SidechainsInMempoolTestSuite::InitCoinGeneration() {
    coinsKey.MakeNewKey(true);
    keystore.AddKey(coinsKey);

    coinsScript << OP_DUP << OP_HASH160 << ToByteVector(coinsKey.GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
}

void SidechainsInMempoolTestSuite::GenerateCoins(unsigned int coinsCount) {
    for (unsigned int coinHeight = 0; coinHeight < coinsCount; ++coinHeight) {

        CCoinsCacheEntry entry;
        entry.flags = CCoinsCacheEntry::DIRTY;

        entry.coins.fCoinBase = false;
        entry.coins.nVersion = 2;
        entry.coins.nHeight = coinHeight % minimalHeightForSidechains;

        entry.coins.vout.resize(1);
        entry.coins.vout[0].nValue = 1000000;
        entry.coins.vout[0].scriptPubKey = coinsScript;

        std::stringstream num;
        num << std::hex << coinHeight;

        initialCoinsSet[uint256S(num.str())] = entry;
    }

    return;
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

    txCreationUtils::signTx(scTx);

    return scTx;
}

//TEST_F(SidechainsInMempoolTestSuite, SAMPLE_1) {
//    EXPECT_TRUE(true);
//}
//
//TEST_F(SidechainsInMempoolTestSuite, SAMPLE_2) {
//    EXPECT_TRUE(true);
//}

TEST_F(SidechainsInMempoolTestSuite, AcceptSimpleSidechainTxToMempool) {
    CTransaction scTx = GenerateScTx(uint256S("1492"), CAmount(1));
    CValidationState txState;
    CTxMemPool pool(::minRelayTxFee);
    bool missingInputs;

    bool res = AcceptToMemoryPool(pool, txState, scTx, false, &missingInputs);

    EXPECT_TRUE(res)<<"Rejection reason in Validation State is ["<<txState.GetRejectReason()<<"]";
}
