#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "main.h"

using namespace zen;

// Checks IsInitialBlockDownload() conditions and lockIBDState latching to false
TEST(initialblockdwnld, checkIBDState) {
    SelectParams(CBaseChainParams::MAIN);
    const CChainParams& chainParams = Params();

    // 1.
    // fImporting, fReindex and fReindexFast initialized as false
    // Check each individual variable and restore its state
    fImporting   = true; EXPECT_TRUE(IsInitialBlockDownload()); fImporting   = false;
    fReindex     = true; EXPECT_TRUE(IsInitialBlockDownload()); fReindex     = false;
    fReindexFast = true; EXPECT_TRUE(IsInitialBlockDownload()); fReindexFast = false;

    // 2.
    // Expecting that fImporting, fReindex and fReindexFast are false, check if
    // chainActive.Height() < Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints())
    fCheckpointsEnabled = true;
    EXPECT_TRUE(IsInitialBlockDownload());

    // Setting tip of active chain lower than checkpoint
    auto block1 = CBlockIndex();
    block1.nHeight = Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()) - 1;
    chainActive.SetTip(&block1);
    EXPECT_TRUE(IsInitialBlockDownload());

    // 3a.
    // increase tip height
    block1.nHeight = Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()) + 1;
    chainActive.SetTip(&block1);
    // Set pindexBestHeader as current tip
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
}