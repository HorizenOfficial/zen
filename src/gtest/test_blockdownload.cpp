#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "main.h"

using namespace zen;

void makeTemporaryMainChain(int trunk_size);

// Checks IsInitialBlockDownload() conditions and lockIBDState latching to false
TEST(initialblockdwnld, checkIBDState) {
    // Init
    auto originalTip = chainActive.Tip();
    chainActive.SetTip(NULL);
    SelectParams(CBaseChainParams::MAIN);
    const CChainParams& chainParams = Params();
    constexpr int MAIN_CHAIN_TEST_SIZE = 150000;
    makeTemporaryMainChain(MAIN_CHAIN_TEST_SIZE);

    // 1.
    // fImporting, fReindex and fReindexFast supposedly initialized as false
    // Check each individual variable and restore its state
    fImporting   = true; EXPECT_TRUE(IsInitialBlockDownload()); fImporting   = false;
    fReindex     = true; EXPECT_TRUE(IsInitialBlockDownload()); fReindex     = false;
    fReindexFast = true; EXPECT_TRUE(IsInitialBlockDownload()); fReindexFast = false;

    // 2.
    // Expecting that fImporting, fReindex and fReindexFast are false, check if:
    // chainActive.Height() < Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints())
    fCheckpointsEnabled = true;
    EXPECT_TRUE(IsInitialBlockDownload());

    // Setting tip of active chain lower than checkpoint
    CBlockIndex block1;
    for (size_t blockHeight = 0; blockHeight < Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()); ++blockHeight)
    {
        block1.nHeight = blockHeight;
        chainActive.SetTip(&block1);
        EXPECT_TRUE(IsInitialBlockDownload());
    }

    // 3a.
    // increase tip height
    block1.nHeight = Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()) + 1;
    chainActive.SetTip(&block1);
    // Set pindexBestHeader as current tip and increase height to trigger condition
    pindexBestHeader = chainActive.Tip();
    pindexBestHeader->nHeight += 24 * 6 + 1;
    pindexBestHeader->nTime = GetTime() + chainParams.MaxTipAge();
    EXPECT_TRUE(IsInitialBlockDownload());

    // 3b.
    // restore pindexBestHeader
    pindexBestHeader = chainActive.Tip();
    pindexBestHeader->nHeight -= 24 * 6 + 1;
    // and set pindexBestHeader time
    pindexBestHeader->nTime = GetTime() - chainParams.MaxTipAge() - 1;
    EXPECT_TRUE(IsInitialBlockDownload());

    // 4.
    // pindexBestHeader nullptr check
    auto tempPIndexPtr = pindexBestHeader;
    pindexBestHeader = nullptr;
    EXPECT_TRUE(IsInitialBlockDownload());
    pindexBestHeader = tempPIndexPtr;

    // 5.
    // chainTip nullptr check
    auto tempTip = chainActive.Tip();
    chainActive.SetTip(nullptr);
    EXPECT_TRUE(IsInitialBlockDownload());
    chainActive.SetTip(tempTip);

    //
    // Set conditions so that all checks fail, and the end of function is reached so that:
    // - lockIBDState set to true
    // - funciton returns false
    pindexBestHeader->nTime = GetTime();
    EXPECT_FALSE(IsInitialBlockDownload());

    // Setting back each condition should still return false
    fImporting = true;
    EXPECT_FALSE(IsInitialBlockDownload());
    fReindex = true;
    EXPECT_FALSE(IsInitialBlockDownload());
    fReindexFast = true;
    EXPECT_FALSE(IsInitialBlockDownload());
    fCheckpointsEnabled = true;
    block1.nHeight = 0;
    chainActive.SetTip(&block1);
    EXPECT_FALSE(IsInitialBlockDownload());
    pindexBestHeader->nTime = 0;
    EXPECT_FALSE(IsInitialBlockDownload());

    // Restore active chain tip
    chainActive.SetTip(originalTip);
}


void makeTemporaryMainChain(int trunk_size)
{
    // Create genesis block
    CBlock b = Params().GenesisBlock();
    CBlockIndex* genesis = AddToBlockIndex(b);
    auto hashOfPrevBlock = b.GetHash();

    // create the main trunk, from which some forks will possibly stem
    for (int i = 0; i < trunk_size; i++)
    {
        CBlock b;
        b.nVersion = MIN_BLOCK_VERSION;
        b.nNonce = uint256(GetRandHash());  
        b.nBits = arith_uint256(uint256(GetRandHash()).ToString() ).GetCompact();
        b.hashPrevBlock = hashOfPrevBlock;
        hashOfPrevBlock = b.GetHash();

        CBlockIndex *bi = AddToBlockIndex(b);

        chainActive.SetTip(bi);
    }

    return;
}