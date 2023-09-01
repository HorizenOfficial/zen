#include <gtest/gtest.h>

//includes for helpers
#include <txdb.h>
#include <boost/filesystem.hpp>
#include <util.h>
#include <primitives/block.h>
#include <pubkey.h>
#include <consensus/validation.h>
#include <pow.h>
#include <miner.h>
#include <sync.h>

//includes for sut
#include <main.h>

//NOTES: LoadBlocksFromExternalFile invoke fclose on file via CBufferedFile dtor

class CFakeCoinDB : public CCoinsViewDB
{
public:
    CFakeCoinDB(size_t nCacheSize, bool fWipe = false)
        : CCoinsViewDB(nCacheSize, DEFAULT_DB_MAX_OPEN_FILES, false, fWipe) {}

    bool BatchWrite(CCoinsMap &mapCoins) { return true; }
};

class ReindexTestSuite: public ::testing::Test {
public:
    ReindexTestSuite():
        dataDirLocation(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path()),
        chainStateDbSize(2 * 1024 * 1024),
        pChainStateDb(nullptr),
        csMainLock(cs_main, "cs_main", __FILE__, __LINE__)
    {
        // only in regtest we can compute easily a proper equihash solution
        // for the blocks we will produce
        SelectParams(CBaseChainParams::REGTEST);
        boost::filesystem::create_directories(dataDirLocation);
        mapArgs["-datadir"] = dataDirLocation.string();

        pChainStateDb = new CFakeCoinDB(chainStateDbSize,/*fWipe*/true);
        pcoinsTip     = new CCoinsViewCache(pChainStateDb);
    };

    void SetUp() override {
        mempool.reset(new CTxMemPool(::minRelayTxFee, DEFAULT_MAX_MEMPOOL_SIZE_MB * 1000000));
        connman.reset(new CConnman());

        UnloadBlockIndex();
    };

    void TearDown() override { UnloadBlockIndex(); };

    ~ReindexTestSuite()
    {
        delete pcoinsTip;
        pcoinsTip = nullptr;

        delete pChainStateDb;
        pChainStateDb = nullptr;

        ClearDatadirCache();
        boost::system::error_code ec;
        boost::filesystem::remove_all(dataDirLocation.string(), ec);
    };

protected:
    CBlock createCoinBaseOnlyBlock(const uint256& prevBlockHash, unsigned int blockHeight);
    bool storeToFile(CBlock& blkToStore, CDiskBlockPos& diskBlkPos);

private:
    CBlockHeader createCoinBaseOnlyBlockHeader(const uint256& prevBlockHash);

    boost::filesystem::path  dataDirLocation;
    const unsigned int       chainStateDbSize;
    CFakeCoinDB*             pChainStateDb;
    CCriticalBlock           csMainLock;        /**< Critical section needed when compiled with --enable-debug, which activates ASSERT_HELD */
};

TEST_F(ReindexTestSuite, BlocksAreNotLoadedFromEmptyBlkFile) {
    //prerequisites
    int blkFileSuffix = {1};

    CDiskBlockPos diskBlkPos(blkFileSuffix, 0);
    ASSERT_TRUE(!boost::filesystem::exists(GetBlockPosFilename(diskBlkPos, "blk")));

    //opening not read-only creates the file
    FILE* filePtr = OpenBlockFile(diskBlkPos, /*fReadOnly*/false);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskBlkPos, /*loadHeadersOnly*/false);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(ReindexTestSuite, OrphanBlocksAreNotLoadedFromFileIntoMapBlockIndex) {
    // prerequisites
    CBlock aBlock = createCoinBaseOnlyBlock(uint256(), /*height*/19);
    EXPECT_TRUE(aBlock.hashPrevBlock.IsNull());
    EXPECT_TRUE(aBlock.GetHash() != Params().GenesisBlock().GetHash());

    CDiskBlockPos diskPos(12345, 0);
    ASSERT_TRUE(storeToFile(aBlock, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/false);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(mapBlockIndex.count(aBlock.GetHash()));
    EXPECT_TRUE(chainActive.Height() == -1);
}

TEST_F(ReindexTestSuite, GenesisIsLoadedFromFileIntoMapBlockIndex) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/false);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(mapBlockIndex.count(Params().GenesisBlock().GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(Params().GenesisBlock().GetHash())->nStatus
            == BLOCK_HAVE_MASK || BlockStatus::BLOCK_VALID_SCRIPTS);
}

TEST_F(ReindexTestSuite, GenesisIsLoadedFromFileIntoChainActive) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/false);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(chainActive.Height() == 0);
    EXPECT_TRUE(*(chainActive.Genesis()->phashBlock) == Params().GenesisBlock().GetHash());
}

TEST_F(ReindexTestSuite, BlockIsLoadedFromFileIntoMapBlockIndex) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));

    CBlock aBlock = createCoinBaseOnlyBlock(genesisCpy.GetHash(), /*height*/1);
    ASSERT_TRUE(storeToFile(aBlock, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/false);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(mapBlockIndex.count(Params().GenesisBlock().GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(Params().GenesisBlock().GetHash())->nStatus
            == BLOCK_HAVE_MASK || BlockStatus::BLOCK_VALID_SCRIPTS);
    ASSERT_TRUE(mapBlockIndex.count(aBlock.GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(aBlock.GetHash())->nStatus
            == BLOCK_HAVE_MASK || BlockStatus::BLOCK_VALID_SCRIPTS);
}

TEST_F(ReindexTestSuite, BlockIsLoadedFromFileIntoChainActive) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));

    CBlock aBlock = createCoinBaseOnlyBlock(genesisCpy.GetHash(), /*height*/1);
    ASSERT_TRUE(storeToFile(aBlock, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/false);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(chainActive.Height() == 1);
    EXPECT_TRUE(*(chainActive.Genesis()->phashBlock) == Params().GenesisBlock().GetHash());
    EXPECT_TRUE(*(chainActive.Tip()->phashBlock) == aBlock.GetHash());
}

TEST_F(ReindexTestSuite, OutOfOrderBlocksAreLoadedFromFileIntoMapBlockIndex) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    CBlock aBlock = createCoinBaseOnlyBlock(genesisCpy.GetHash(), /*height*/1);
    CBlock anotherBlock = createCoinBaseOnlyBlock(aBlock.GetHash(), /*height*/2);

    ASSERT_TRUE(storeToFile(aBlock, diskPos));
    ASSERT_TRUE(storeToFile(anotherBlock, diskPos));
    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/false);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(mapBlockIndex.count(Params().GenesisBlock().GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(Params().GenesisBlock().GetHash())->nStatus
            == BLOCK_HAVE_MASK || BlockStatus::BLOCK_VALID_SCRIPTS);
    ASSERT_TRUE(mapBlockIndex.count(aBlock.GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(aBlock.GetHash())->nStatus
            == BLOCK_HAVE_MASK || BlockStatus::BLOCK_VALID_SCRIPTS);
    ASSERT_TRUE(mapBlockIndex.count(anotherBlock.GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(anotherBlock.GetHash())->nStatus
            == BLOCK_HAVE_MASK || BlockStatus::BLOCK_VALID_SCRIPTS);
}

TEST_F(ReindexTestSuite, OutOfOrderBlocksAreLoadedFromFileIntoChainActive) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    CBlock aBlock = createCoinBaseOnlyBlock(genesisCpy.GetHash(), /*height*/1);
    CBlock anotherBlock = createCoinBaseOnlyBlock(aBlock.GetHash(), /*height*/2);

    ASSERT_TRUE(storeToFile(aBlock, diskPos));
    ASSERT_TRUE(storeToFile(anotherBlock, diskPos));
    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/false);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(chainActive.Height() == 2);
    EXPECT_TRUE(*(chainActive.Genesis()->phashBlock) == Params().GenesisBlock().GetHash());
    EXPECT_TRUE(*(chainActive.Tip()->phashBlock) == anotherBlock.GetHash());
    EXPECT_TRUE(*(chainActive.Tip()->pprev->phashBlock) == aBlock.GetHash());
}

TEST_F(ReindexTestSuite, DuplicatedBlocksAreHandledInMapBlockIndex) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    CBlock aBlock = createCoinBaseOnlyBlock(genesisCpy.GetHash(), /*height*/1);

    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));
    ASSERT_TRUE(storeToFile(aBlock, diskPos));
    ASSERT_TRUE(storeToFile(aBlock, diskPos));
    ASSERT_TRUE(storeToFile(aBlock, diskPos)); //store it thrice

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/false);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(mapBlockIndex.count(Params().GenesisBlock().GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(Params().GenesisBlock().GetHash())->nStatus
            == BLOCK_HAVE_MASK || BlockStatus::BLOCK_VALID_SCRIPTS);
    ASSERT_TRUE(mapBlockIndex.count(aBlock.GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(aBlock.GetHash())->nStatus
            == BLOCK_HAVE_MASK || BlockStatus::BLOCK_VALID_SCRIPTS);
}

TEST_F(ReindexTestSuite, DuplicatedBlocksAreHandledInChainActive) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    CBlock aBlock = createCoinBaseOnlyBlock(genesisCpy.GetHash(), /*height*/1);

    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));
    ASSERT_TRUE(storeToFile(aBlock, diskPos));
    ASSERT_TRUE(storeToFile(aBlock, diskPos));
    ASSERT_TRUE(storeToFile(aBlock, diskPos)); //store it thrice

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/false);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(chainActive.Height() == 1);
    EXPECT_TRUE(*(chainActive.Genesis()->phashBlock) == Params().GenesisBlock().GetHash());
    EXPECT_TRUE(*(chainActive.Tip()->phashBlock) == aBlock.GetHash());
}

TEST_F(ReindexTestSuite, BlocksHeadersAreNotLoadedFromEmptyBlkFile) {
    //prerequisites
    int blkFileSuffix = {1};

    CDiskBlockPos diskBlkPos(blkFileSuffix, 0);
    ASSERT_TRUE(!boost::filesystem::exists(GetBlockPosFilename(diskBlkPos, "blk")));

    //opening not read-only creates the file
    FILE* filePtr = OpenBlockFile(diskBlkPos, /*fReadOnly*/false);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskBlkPos, /*loadHeadersOnly*/true);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(ReindexTestSuite, HeadersFromOrphanBlocksAreNotLoadedFromFileIntoMapBlockIndex) {
    // prerequisites
    CBlock aBlock = createCoinBaseOnlyBlock(uint256(), /*height*/19);
    EXPECT_TRUE(aBlock.hashPrevBlock.IsNull());
    EXPECT_TRUE(aBlock.GetHash() != Params().GenesisBlock().GetHash());

    CDiskBlockPos diskPos(12345, 0);
    ASSERT_TRUE(storeToFile(aBlock, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/true);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(mapBlockIndex.count(aBlock.GetHash()));
    EXPECT_TRUE(chainActive.Height() == -1);
}

TEST_F(ReindexTestSuite, GenesisHeaderIsLoadedFromFileIntoMapBlockIndex) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/true);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(mapBlockIndex.count(Params().GenesisBlock().GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(Params().GenesisBlock().GetHash())->nStatus == BLOCK_VALID_TREE);
    EXPECT_TRUE(chainActive.Height() == -1);
}

TEST_F(ReindexTestSuite, BlockHeaderIsLoadedFromFileIntoMapBlockIndex) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));

    CBlock aBlock = createCoinBaseOnlyBlock(genesisCpy.GetHash(), /*height*/1);
    ASSERT_TRUE(storeToFile(aBlock, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/true);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(mapBlockIndex.count(Params().GenesisBlock().GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(Params().GenesisBlock().GetHash())->nStatus == BLOCK_VALID_TREE);
    ASSERT_TRUE(mapBlockIndex.count(aBlock.GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(aBlock.GetHash())->nStatus == BLOCK_VALID_TREE);
    EXPECT_TRUE(chainActive.Height() == -1);
}

TEST_F(ReindexTestSuite, OutOfOrderBlockHeadersAreLoadedFromFileIntoMapBlockIndex) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    CBlock aBlock = createCoinBaseOnlyBlock(genesisCpy.GetHash(), /*height*/1);
    CBlock anotherBlock = createCoinBaseOnlyBlock(aBlock.GetHash(), /*height*/2);

    ASSERT_TRUE(storeToFile(aBlock, diskPos));
    ASSERT_TRUE(storeToFile(anotherBlock, diskPos));
    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/true);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(mapBlockIndex.count(Params().GenesisBlock().GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(Params().GenesisBlock().GetHash())->nStatus
            == BlockStatus::BLOCK_VALID_TREE);
    ASSERT_TRUE(mapBlockIndex.count(aBlock.GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(aBlock.GetHash())->nStatus
            == BlockStatus::BLOCK_VALID_TREE);
    ASSERT_TRUE(mapBlockIndex.count(anotherBlock.GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(anotherBlock.GetHash())->nStatus
            == BlockStatus::BLOCK_VALID_TREE);
}

TEST_F(ReindexTestSuite, DuplicatedBlocksHeadersAreHandledInMapBlockIndex) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    CBlock aBlock = createCoinBaseOnlyBlock(genesisCpy.GetHash(), /*height*/1);

    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));
    ASSERT_TRUE(storeToFile(aBlock, diskPos));
    ASSERT_TRUE(storeToFile(aBlock, diskPos));
    ASSERT_TRUE(storeToFile(aBlock, diskPos)); //store it thrice

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/true);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(mapBlockIndex.count(Params().GenesisBlock().GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(Params().GenesisBlock().GetHash())->nStatus
            == BlockStatus::BLOCK_VALID_TREE);
    ASSERT_TRUE(mapBlockIndex.count(aBlock.GetHash()));
    EXPECT_TRUE(mapBlockIndex.at(aBlock.GetHash())->nStatus
    		== BlockStatus::BLOCK_VALID_TREE);
}

TEST_F(ReindexTestSuite, HeaderAndBlockLoadWorkBackToBack) {
    // prerequisites
    CDiskBlockPos diskPos(12345, 0);

    CBlock genesisCpy = Params().GenesisBlock();
    CBlock aBlock = createCoinBaseOnlyBlock(genesisCpy.GetHash(), /*height*/1);
    CBlock anotherBlock = createCoinBaseOnlyBlock(aBlock.GetHash(), /*height*/2);

    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));
    ASSERT_TRUE(storeToFile(aBlock, diskPos));
    ASSERT_TRUE(storeToFile(anotherBlock, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //test headers load first...
    LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/true);

    //checks
    EXPECT_TRUE(mapBlockIndex.at(Params().GenesisBlock().GetHash())->nStatus
            == BlockStatus::BLOCK_VALID_TREE);
    EXPECT_TRUE(mapBlockIndex.at(aBlock.GetHash())->nStatus
            == BlockStatus::BLOCK_VALID_TREE);
    EXPECT_TRUE(mapBlockIndex.at(anotherBlock.GetHash())->nStatus
            == BlockStatus::BLOCK_VALID_TREE);

    //... then test blocks load first
    diskPosReopened.nPos = 0;
    filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    LoadBlocksFromExternalFile(filePtr, &diskPosReopened, /*loadHeadersOnly*/false);

    //checks
    EXPECT_TRUE(mapBlockIndex.at(Params().GenesisBlock().GetHash())->nStatus
            == BLOCK_HAVE_MASK || BlockStatus::BLOCK_VALID_SCRIPTS);
    EXPECT_TRUE(mapBlockIndex.at(aBlock.GetHash())->nStatus
    		== BLOCK_HAVE_MASK || BlockStatus::BLOCK_VALID_SCRIPTS);
    EXPECT_TRUE(mapBlockIndex.at(anotherBlock.GetHash())->nStatus
    		== BLOCK_HAVE_MASK || BlockStatus::BLOCK_VALID_SCRIPTS);
    EXPECT_TRUE(chainActive.Height() == 2) << chainActive.Height();
}

/**
 * Test that when a blk?????.dat file is full a new one is created
 * and that the related variables are correctly initialized.
 */
TEST_F(ReindexTestSuite, CreateNewBlockFile)
{
    CValidationState state;
    CDiskBlockPos pos;
    unsigned int nAddSize = MAX_BLOCKFILE_SIZE -1;
    unsigned int nHeight = 0;
    uint64_t nTime = 0;
    bool fKnown = false;
    ASSERT_TRUE(FindBlockPos(state, pos, nAddSize, nHeight++, nTime, fKnown));
    ASSERT_TRUE(FindBlockPos(state, pos, nAddSize, nHeight++, nTime, fKnown));
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
CBlockHeader ReindexTestSuite::createCoinBaseOnlyBlockHeader(const uint256& prevBlockHash)
{
    CBlockHeader res;
    res.nVersion = BLOCK_VERSION_ORIGINAL;
    res.hashPrevBlock = prevBlockHash;
    res.hashMerkleRoot = uint256();
    res.hashScTxsCommitment.SetNull();

    static unsigned int runCounter = 0;
    SetMockTime(time(nullptr) + ++runCounter);
    CBlockIndex fakePrevBlockIdx(Params().GenesisBlock());
    UpdateTime(&res, Params().GetConsensus(), &fakePrevBlockIdx);

    res.nBits = UintToArith256(Params().GetConsensus().powLimit).GetCompact();
    res.nNonce = Params().GenesisBlock().nNonce;
    return res;
}

CBlock ReindexTestSuite::createCoinBaseOnlyBlock(const uint256& prevBlockHash, unsigned int blockHeight)
{
    CBlock res;
    res.SetBlockHeader(createCoinBaseOnlyBlockHeader(prevBlockHash));

    CScript coinbaseScript = CScript() << OP_DUP << OP_HASH160
            << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG;
    res.vtx.push_back(createCoinbase(coinbaseScript, /*fees*/CAmount(), blockHeight));

    bool fDummy = false;
    res.hashMerkleRoot = res.BuildMerkleTree(&fDummy);

    generateEquihash(res);

    return res;
}

bool ReindexTestSuite::storeToFile(CBlock& blkToStore, CDiskBlockPos& diskBlkPos)
{
    // AcceptBlock flushes block to file by first FindBlockPos and then WriteBlockToDisk
    // and maybe FlushStateToDisk too.
    // We emulate that flow in this function
    bool res =  WriteBlockToDisk(blkToStore, diskBlkPos, Params().MessageStart());
    diskBlkPos.nPos += ::GetSerializeSize(blkToStore, SER_DISK, CLIENT_VERSION);

    return res;
}
