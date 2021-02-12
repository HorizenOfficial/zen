#include <gtest/gtest.h>

#include "main.h"

extern CBlockIndex* AddToBlockIndex(const CBlockHeader& block);
extern void CleanUpAll();

class ScTxCumulativeHashTestSuite: public ::testing::Test {

public:
    ScTxCumulativeHashTestSuite(){}

    ~ScTxCumulativeHashTestSuite(){}
};

TEST_F(ScTxCumulativeHashTestSuite, CBlockIndexSerialization)
{
    CBlockIndex originalpindex;
    originalpindex.scCumTreeHash = CPoseidonHash(libzendoomc::ScFieldElement({std::vector<unsigned char>(size_t(SC_FIELD_SIZE), 'a')}));

    CDataStream ssValue(SER_DISK, PROTOCOL_VERSION);
    ssValue << CDiskBlockIndex(&originalpindex);
    CDiskBlockIndex diskpindex;
    ssValue >> diskpindex;

    EXPECT_TRUE(originalpindex.scCumTreeHash == diskpindex.scCumTreeHash);
}

TEST_F(ScTxCumulativeHashTestSuite, CBlockIndexCumulativeHashCheck)
{
    CleanUpAll();
    SelectParams(CBaseChainParams::MAIN);

    // Previous block
    CPoseidonHash prevCumulativeHash(libzendoomc::ScFieldElement({std::vector<unsigned char>(size_t(SC_FIELD_SIZE), 'a')}));
    CBlock prevBlock;
    prevBlock.nVersion = BLOCK_VERSION_SC_SUPPORT;
    prevBlock.hashScTxsCommitment = prevCumulativeHash;

    CBlockIndex* prevPindex = AddToBlockIndex(prevBlock);
    prevPindex->scCumTreeHash = prevCumulativeHash;
    EXPECT_TRUE(prevCumulativeHash == prevPindex->hashScTxsCommitment);

    // Current block
    CPoseidonHash currentHash(libzendoomc::ScFieldElement({std::vector<unsigned char>(size_t(SC_FIELD_SIZE), 'b')}));
    CBlock block;
    block.nVersion = BLOCK_VERSION_SC_SUPPORT;
    block.hashScTxsCommitment = currentHash;
    block.hashPrevBlock = prevBlock.GetHash();

    CBlockIndex* pindex = AddToBlockIndex(block);
    EXPECT_TRUE(currentHash == pindex->hashScTxsCommitment);
    EXPECT_TRUE(pindex->pprev == prevPindex);

    CPoseidonHash expectedHash = CPoseidonHash::ComputeHash(prevCumulativeHash, currentHash);
    EXPECT_TRUE(expectedHash == pindex->scCumTreeHash);

    CleanUpAll();
}