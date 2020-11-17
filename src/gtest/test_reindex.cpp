#include <gtest/gtest.h>

//includes for helpers
#include <boost/filesystem.hpp>
#include <util.h>

//includes for sut
#include <main.h>

class ReindexTestSuite: public ::testing::Test {
public:
    ReindexTestSuite():
        dataDirLocation(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path())
    {
        SelectParams(CBaseChainParams::REGTEST);
        boost::filesystem::create_directories(dataDirLocation);
        mapArgs["-datadir"] = dataDirLocation.string();
    };

    ~ReindexTestSuite()
    {
        ClearDatadirCache();
        boost::system::error_code ec;
        boost::filesystem::remove_all(dataDirLocation.string(), ec);
    };

    void SetUp() override {};
    void TearDown() override {};

private:
    boost::filesystem::path  dataDirLocation;
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
