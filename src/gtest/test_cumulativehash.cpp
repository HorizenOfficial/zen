// Copyright (c) 2021-2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>
#include "main.h"
#include <gtest/libzendoo_test_files.h>

class SidechainsTxCumulativeHashTestSuite: public ::testing::Test
{
public:
    SidechainsTxCumulativeHashTestSuite() {
        mempool.reset(new CTxMemPool(::minRelayTxFee, DEFAULT_MAX_MEMPOOL_SIZE_MB * 1000000));
    };
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
    std::vector<unsigned char> prevCumHashByteArray(32,0x1d);
    prevCumHashByteArray.resize(CFieldElement::ByteSize(),0x0);
    CFieldElement prevCumulativeHash{prevCumHashByteArray};

    CBlock prevBlock;
    prevBlock.nVersion = BLOCK_VERSION_SC_SUPPORT;
    prevBlock.hashScTxsCommitment = prevCumulativeHash.GetLegacyHash();

    CBlockIndex* prevPindex = AddToBlockIndex(prevBlock);
    prevPindex->scCumTreeHash = prevCumulativeHash;
    EXPECT_TRUE(prevCumulativeHash.GetLegacyHash() == prevPindex->hashScTxsCommitment)
    <<prevCumulativeHash.GetLegacyHash().ToString()<<"\n"
    <<prevPindex->hashScTxsCommitment.ToString();

    // Current block
    std::vector<unsigned char> currentHashByteArray(32, 0x1e);
    currentHashByteArray.resize(CFieldElement::ByteSize(),0x0);
    CFieldElement currentHash{currentHashByteArray};

    CBlock block;
    block.nVersion = BLOCK_VERSION_SC_SUPPORT;
    block.hashScTxsCommitment = currentHash.GetLegacyHash();
    block.hashPrevBlock = prevBlock.GetHash();

    CBlockIndex* pindex = AddToBlockIndex(block);
    EXPECT_TRUE(currentHash.GetLegacyHash() == pindex->hashScTxsCommitment)
    <<currentHash.GetLegacyHash().ToString()<<"\n"
    <<pindex->hashScTxsCommitment.ToString();

    EXPECT_TRUE(pindex->pprev == prevPindex);

    CFieldElement expectedHash = CFieldElement::ComputeHash(prevCumulativeHash, currentHash);
    EXPECT_TRUE(expectedHash.GetLegacyHash() == pindex->scCumTreeHash.GetLegacyHash())
    <<expectedHash.GetLegacyHash().ToString()<<"\n"
    <<pindex->scCumTreeHash.GetLegacyHash().ToString();

    UnloadBlockIndex();
}
