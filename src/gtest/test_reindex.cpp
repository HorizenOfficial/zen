#include <gtest/gtest.h>

//includes for helpers
#include <txdb.h>
#include <boost/filesystem.hpp>
#include <util.h>
#include <primitives/block.h>
#include <pubkey.h>

//includes for sut
#include <main.h>

//NOTES: LoadBlocksFromExternalFile invoke fclose on file via CBufferedFile dtor

class CCoinsOnlyViewDB : public CCoinsViewDB
{
public:
    CCoinsOnlyViewDB(size_t nCacheSize, bool fWipe = false)
        : CCoinsViewDB(nCacheSize, false, fWipe) {}

    bool BatchWrite(CCoinsMap &mapCoins) { return true; }
};

class ReindexTestSuite: public ::testing::Test {
public:
    ReindexTestSuite():
        dataDirLocation(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path()),
        chainStateDbSize(2 * 1024 * 1024),
        pChainStateDb(nullptr)
    {
        SelectParams(CBaseChainParams::REGTEST);
        boost::filesystem::create_directories(dataDirLocation);
        mapArgs["-datadir"] = dataDirLocation.string();

        pChainStateDb = new CCoinsOnlyViewDB(chainStateDbSize,/*fWipe*/true);
        pcoinsTip     = new CCoinsViewCache(pChainStateDb);
    };

    void SetUp() override
    {
        chainActive.SetTip(nullptr);
        mapBlockIndex.clear();
    };

    void TearDown() override
    {
        chainActive.SetTip(nullptr);
        mapBlockIndex.clear();
    };

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
    CBlock generateABlock();
    bool storeToFile(CBlock& blkToStore, CDiskBlockPos& diskBlkPos);

private:
    CBlockHeader generateABlockHeader();
    CTransaction generateATransparentTx();
    void signTx(CMutableTransaction& mtx);

    boost::filesystem::path  dataDirLocation;
    const unsigned int       chainStateDbSize;
    CCoinsOnlyViewDB*        pChainStateDb;
};

TEST_F(ReindexTestSuite, LoadingBlocksFromNullFilePtrWillAbort) {
    //prerequisites
    int blkFileSuffix = {1987};

    CDiskBlockPos diskBlkPos(blkFileSuffix, 0);
    ASSERT_TRUE(!boost::filesystem::exists(GetBlockPosFilename(diskBlkPos, "blk")));

    FILE* filePtr = OpenBlockFile(diskBlkPos, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr == nullptr);

    //test && checks
    EXPECT_EXIT(LoadBlocksFromExternalFile(filePtr, &diskBlkPos), ::testing::KilledBySignal(SIGSEGV),".*");
}

TEST_F(ReindexTestSuite, BlocksAreNotLoadedFromEmptyBlkFile) {
    //prerequisites
    int blkFileSuffix = {1};

    CDiskBlockPos diskBlkPos(blkFileSuffix, 0);
    ASSERT_TRUE(!boost::filesystem::exists(GetBlockPosFilename(diskBlkPos, "blk")));

    //opening not read-only creates the file
    FILE* filePtr = OpenBlockFile(diskBlkPos, /*fReadOnly*/false);
    ASSERT_TRUE(filePtr != nullptr);

    //test
    bool res = LoadBlocksFromExternalFile(filePtr, &diskBlkPos);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(ReindexTestSuite, OrphanBlocksAreNotLoadedFromFileIntoMapBlockIndex) {
    // prerequisites
    CBlock aBlock = generateABlock();
    EXPECT_TRUE(aBlock.hashPrevBlock.IsNull());
    EXPECT_TRUE(aBlock.GetHash() != Params().GenesisBlock().GetHash());

    CDiskBlockPos diskPos(12345, 0);
    ASSERT_TRUE(storeToFile(aBlock, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //check
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened);

    //test
    EXPECT_FALSE(res);
    EXPECT_FALSE(mapBlockIndex.count(aBlock.GetHash()));
}

TEST_F(ReindexTestSuite, GenesisIsLoadedFromFileIntoMapBlockIndex) {
    // prerequisites
	CBlock genesisCpy = Params().GenesisBlock();
    CDiskBlockPos diskPos(12345, 0);
    ASSERT_TRUE(storeToFile(genesisCpy, diskPos));

    //Note: read from start of file, not from pos returned by storeToFile
    CDiskBlockPos diskPosReopened(12345, 0);
    FILE* filePtr = OpenBlockFile(diskPosReopened, /*fReadOnly*/true);
    ASSERT_TRUE(filePtr != nullptr);

    //check
    bool res = LoadBlocksFromExternalFile(filePtr, &diskPosReopened);

    //test
    EXPECT_TRUE(res);
    EXPECT_TRUE(mapBlockIndex.count(Params().GenesisBlock().GetHash()));
    ASSERT_TRUE(chainActive.Tip()->phashBlock != nullptr);
    EXPECT_TRUE(*(chainActive.Tip()->phashBlock) == Params().GenesisBlock().GetHash());
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
CBlockHeader ReindexTestSuite::generateABlockHeader()
{
    CBlockHeader res;
    res.nVersion = MIN_BLOCK_VERSION;
    res.hashPrevBlock = uint256();
    res.hashMerkleRoot = uint256S("aaa");
    res.hashReserved.SetNull();
    res.nTime = Params().GenesisBlock().nTime;
    res.nBits = Params().GenesisBlock().nBits;
    res.nNonce = Params().GenesisBlock().nNonce;
    return res;
}

CBlock ReindexTestSuite::generateABlock()
{
    CBlock res = generateABlockHeader();
    res.vtx.push_back(generateATransparentTx());
    return res;
}

CTransaction ReindexTestSuite::generateATransparentTx()
{
    CMutableTransaction mtx;
    mtx.nVersion = TRANSPARENT_TX_VERSION;

    mtx.vin.resize(2);
    mtx.vin[0].prevout.hash = uint256S("1");
    mtx.vin[0].prevout.n = 0;
    mtx.vin[1].prevout.hash = uint256S("2");
    mtx.vin[1].prevout.n = 0;

    mtx.vout.resize(2);
    mtx.vout.at(0).nValue = 0;
    mtx.vout.at(1).nValue = 0;

    signTx(mtx);

    return CTransaction(mtx);
}

void ReindexTestSuite::signTx(CMutableTransaction& mtx)
{
    // Generate an ephemeral keypair.
    uint256 joinSplitPubKey;
    unsigned char joinSplitPrivKey[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(joinSplitPubKey.begin(), joinSplitPrivKey);
    mtx.joinSplitPubKey = joinSplitPubKey;
    // Compute the correct hSig.
    // TODO: #966.
    static const uint256 one(uint256S("1"));
    // Empty output script.
    CScript scriptCode;
    CTransaction signTx(mtx);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);
    if (dataToBeSigned == one) {
        throw std::runtime_error("SignatureHash failed");
    }
    // Add the signature
    assert(crypto_sign_detached(&mtx.joinSplitSig[0], NULL, dataToBeSigned.begin(), 32, joinSplitPrivKey ) == 0);
}

bool ReindexTestSuite::storeToFile(CBlock& blkToStore, CDiskBlockPos& diskBlkPos)
{
    // AcceptBlock flushes block to file by first FindBlockPos and then WriteBlockToDisk
    // and maybe FlushStateToDisk too.
    // For this test purpose we skip FindBlockPos since it's not really necessary

    bool res =  WriteBlockToDisk(blkToStore, diskBlkPos, Params().MessageStart());
//    if (res) {
//        FILE *pFile = OpenBlockFile(diskBlkPos);
//        FileCommit(pFile);
//        fclose(pFile);
//    }
    return res;
}
