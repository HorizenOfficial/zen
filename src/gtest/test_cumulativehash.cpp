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
    originalpindex.nVersion = BLOCK_VERSION_SC_SUPPORT;
    std::vector<unsigned char> hashByteArray(CSidechainField::ByteSize()-2,'a');
    hashByteArray.resize(CSidechainField::ByteSize(), 0x0);
    originalpindex.scCumTreeHash.SetByteArray(hashByteArray);

    CDataStream ssValue(SER_DISK, PROTOCOL_VERSION);
    ssValue << CDiskBlockIndex(&originalpindex);
    CDiskBlockIndex diskpindex;
    ssValue >> diskpindex;

    EXPECT_TRUE(originalpindex.scCumTreeHash == diskpindex.scCumTreeHash)
    <<originalpindex.scCumTreeHash.GetHexRepr()<<"\n"
    <<diskpindex.scCumTreeHash.GetHexRepr();
}

TEST_F(ScTxCumulativeHashTestSuite, CBlockIndexCumulativeHashCheck)
{
    CleanUpAll();
    SelectParams(CBaseChainParams::MAIN);

    // Previous block
    std::vector<unsigned char> prevCumulativeHashByteArray(CSidechainField::ByteSize()-2,'a');
    prevCumulativeHashByteArray.resize(CSidechainField::ByteSize(), 0x0);
    CSidechainField prevCumulativeHash{prevCumulativeHashByteArray};
    CBlock prevBlock;
    prevBlock.nVersion = BLOCK_VERSION_SC_SUPPORT;
    prevBlock.hashScTxsCommitment = prevCumulativeHash.GetLegacyHashTO_BE_REMOVED();

    CBlockIndex* prevPindex = AddToBlockIndex(prevBlock);
    prevPindex->scCumTreeHash = prevCumulativeHash;
    EXPECT_TRUE(prevCumulativeHash.GetLegacyHashTO_BE_REMOVED() == prevPindex->hashScTxsCommitment.GetLegacyHashTO_BE_REMOVED())
    <<prevCumulativeHash.GetHexRepr()<<"\n"
    <<prevPindex->hashScTxsCommitment.GetHexRepr();

    // Current block
    std::vector<unsigned char> currentHashByteArray(CSidechainField::ByteSize()-2,'b');
    currentHashByteArray.resize(CSidechainField::ByteSize(), 0x0);
    CSidechainField currentHash{currentHashByteArray};
    CBlock block;
    block.nVersion = BLOCK_VERSION_SC_SUPPORT;
    block.hashScTxsCommitment = currentHash.GetLegacyHashTO_BE_REMOVED();
    block.hashPrevBlock = prevBlock.GetHash();

    CBlockIndex* pindex = AddToBlockIndex(block);
    EXPECT_TRUE(currentHash.GetLegacyHashTO_BE_REMOVED() == pindex->hashScTxsCommitment.GetLegacyHashTO_BE_REMOVED())
    <<currentHash.GetHexRepr()<<"\n"
    <<pindex->hashScTxsCommitment.GetHexRepr();
    EXPECT_TRUE(pindex->pprev == prevPindex);

    //TEST BELOW BROKEN TILL NEW FIELD ELEMENT WITH RIGHT SIZE IS PULLED IN
    CSidechainField expectedHash = CSidechainField::ComputeHash(prevCumulativeHash, currentHash);
    EXPECT_TRUE(expectedHash.GetLegacyHashTO_BE_REMOVED() == pindex->scCumTreeHash.GetLegacyHashTO_BE_REMOVED())
    <<expectedHash.GetHexRepr()<<"\n"
    <<pindex->hashScTxsCommitment.GetHexRepr();

    CleanUpAll();
}
