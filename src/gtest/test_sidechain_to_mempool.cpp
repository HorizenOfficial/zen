#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <chainparams.h>
#include <util.h>

#include <txdb.h>
#include <main.h>
#include <zen/forks/fork6_sidechainfork.h>

#include "tx_creation_utils.h"
#include <consensus/validation.h>

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

        pChainStateDb = new CCoinsOnlyViewDB(chainStateDbSize,/*fWipe*/true);
        pcoinsTip = new CCoinsViewCache(pChainStateDb);

        fPrintToConsole = true;
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

    void SetUp() override {
        //ASSERT_TRUE(chainActive.Height() == -1)<<chainActive.Height();
        GenerateChainActive();
        //ASSERT_TRUE(chainActive.Height() == minimalHeightForSidechains)<<chainActive.Height();
    }

    void TearDown() override {
        chainActive.SetTip(NULL);
        mapBlockIndex.clear();
    }

private:
    boost::filesystem::path pathTemp;
    const unsigned int      chainStateDbSize;
    CCoinsOnlyViewDB*       pChainStateDb;

    const unsigned int       minimalHeightForSidechains;
    std::vector<uint256>     blockHashes;
    std::vector<CBlockIndex> blocks;

    void GenerateChainActive();
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

TEST_F(SidechainsInMempoolTestSuite, SAMPLE_1) {
    EXPECT_TRUE(true);
}

TEST_F(SidechainsInMempoolTestSuite, SAMPLE_2) {
    EXPECT_TRUE(true);
}

TEST_F(SidechainsInMempoolTestSuite, AcceptSimpleSidechainTxToMempool) {
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(uint256S("aaa"), CAmount(100));
    CValidationState txState;
    CTxMemPool pool(::minRelayTxFee);
    bool missingInputs;

    bool res = AcceptToMemoryPool(pool, txState, scTx, false, &missingInputs);

    EXPECT_TRUE(res)<<txState.GetRejectReason();
}
