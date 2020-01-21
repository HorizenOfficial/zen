#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <util.h>

#include <chainparams.h>

#include <txdb.h>
#include <main.h>

class SidechainsInMempoolTestSuite: public ::testing::Test {
public:
    SidechainsInMempoolTestSuite():
        pathTemp(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path()),
        chainStateDbSize(2 * 1024 * 1024), //bytes
        pChainStateDb(nullptr)
    {
        SelectParams(CBaseChainParams::REGTEST);

        boost::filesystem::create_directories(pathTemp);
        mapArgs["-datadir"] = pathTemp.string();

        pChainStateDb = new CCoinsViewDB(chainStateDbSize, /*fMemory*/false, /*fWipe*/true);
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

    void SetUp() override {}

    void TearDown() override {}

private:
    boost::filesystem::path pathTemp;
    const unsigned int chainStateDbSize;
    CCoinsViewDB* pChainStateDb;
};

TEST_F(SidechainsInMempoolTestSuite, SAMPLE_1) {
    EXPECT_TRUE(true);
}

TEST_F(SidechainsInMempoolTestSuite, SAMPLE_2) {
    EXPECT_TRUE(true);
}

TEST_F(SidechainsInMempoolTestSuite, SAMPLE_3) {
    EXPECT_TRUE(true);
}
