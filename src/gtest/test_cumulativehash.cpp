#include <gtest/gtest.h>
#include "main.h"
#include <gtest/libzendoo_test_files.h>

class SidechainsTxCumulativeHashTestSuite: public ::testing::Test
{
public:
    SidechainsTxCumulativeHashTestSuite() = default;
    ~SidechainsTxCumulativeHashTestSuite() = default;
};

TEST_F(SidechainsTxCumulativeHashTestSuite, CBlockIndexSerialization)
{
    CBlockIndex originalpindex;
    originalpindex.nVersion = BLOCK_VERSION_SC_SUPPORT;
    originalpindex.scCumTreeHash = CFieldElement{SAMPLE_FIELD};

    CDataStream ssValue(SER_DISK, PROTOCOL_VERSION);
    ssValue << CDiskBlockIndex(&originalpindex);
    CDiskBlockIndex diskpindex;
    ssValue >> diskpindex;

    EXPECT_TRUE(originalpindex.scCumTreeHash == diskpindex.scCumTreeHash)
    <<originalpindex.scCumTreeHash.GetHexRepr()<<"\n"
	<<diskpindex.scCumTreeHash.GetHexRepr();
}

TEST_F(SidechainsTxCumulativeHashTestSuite, CBlockIndexCumulativeHashCheck)
{
    UnloadBlockIndex();
    SelectParams(CBaseChainParams::MAIN);

    // Previous block
    std::vector<unsigned char> prevCumHashByteArray(32,'a');
    prevCumHashByteArray.resize(CFieldElement::ByteSize(),0x0);
    CFieldElement prevCumulativeHash{prevCumHashByteArray};

    CBlock prevBlock;
    prevBlock.nVersion = BLOCK_VERSION_SC_SUPPORT;
    prevBlock.hashScTxsCommitment = prevCumulativeHash.GetLegacyHashTO_BE_REMOVED();

    CBlockIndex* prevPindex = AddToBlockIndex(prevBlock);
    prevPindex->scCumTreeHash = prevCumulativeHash;
    EXPECT_TRUE(prevCumulativeHash.GetLegacyHashTO_BE_REMOVED() == prevPindex->hashScTxsCommitment)
    <<prevCumulativeHash.GetLegacyHashTO_BE_REMOVED().ToString()<<"\n"
	<<prevPindex->hashScTxsCommitment.ToString();

    // Current block
    std::vector<unsigned char> currentHashByteArray(32,'b');
    currentHashByteArray.resize(CFieldElement::ByteSize(),0x0);
    CFieldElement currentHash{currentHashByteArray};

    CBlock block;
    block.nVersion = BLOCK_VERSION_SC_SUPPORT;
    block.hashScTxsCommitment = currentHash.GetLegacyHashTO_BE_REMOVED();
    block.hashPrevBlock = prevBlock.GetHash();

    CBlockIndex* pindex = AddToBlockIndex(block);
    EXPECT_TRUE(currentHash.GetLegacyHashTO_BE_REMOVED() == pindex->hashScTxsCommitment)
    <<currentHash.GetLegacyHashTO_BE_REMOVED().ToString()<<"\n"
	<<pindex->hashScTxsCommitment.ToString();

    EXPECT_TRUE(pindex->pprev == prevPindex);

    CFieldElement expectedHash = CFieldElement::ComputeHash(prevCumulativeHash, currentHash);
    EXPECT_TRUE(expectedHash.GetLegacyHashTO_BE_REMOVED() == pindex->scCumTreeHash.GetLegacyHashTO_BE_REMOVED())
    <<expectedHash.GetLegacyHashTO_BE_REMOVED().ToString()<<"\n"
	<<pindex->scCumTreeHash.GetLegacyHashTO_BE_REMOVED().ToString();

    UnloadBlockIndex();
}
