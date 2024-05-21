// Copyright (c) 2022 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "main.h"

using namespace zen;

// Checks IsInitialBlockDownload() conditions and lockIBDState latching to false
TEST(initialblockdwnld, checkIBDState) {
    ////// Init
    auto originalTip = chainActive.Tip();
    SelectParams(CBaseChainParams::MAIN);
    const CChainParams& chainParams = Params();

    ///// Set initial conditions so that IsInitialBlockDownload() returns false
    fImporting = fReindex = fReindexFast = fCheckpointsEnabled = false;
    // Reset the active chain and add the genesis block
    chainActive.SetTip(nullptr);
    CBlock b = Params().GenesisBlock();
    CBlockIndex* genesis = AddToBlockIndex(b);
    chainActive.SetTip(genesis);
    // Create a new pindexBestHeader and set its relevant member variables
    CBlockIndex bIdx;
    pindexBestHeader = &bIdx;
    pindexBestHeader->nHeight = -1;
    pindexBestHeader->nTime = GetTime() - chainParams.MaxTipAge() + 1;

    ///// Check every condition, one at a time
    // 1.
    // fImporting, fReindex and fReindexFast supposedly initialized as false
    // Check that setting any of these variables allows IsInitialBlockDownload to return true
    fImporting   = true; EXPECT_TRUE(IsInitialBlockDownload()); fImporting   = false;
    fReindex     = true; EXPECT_TRUE(IsInitialBlockDownload()); fReindex     = false;
    fReindexFast = true; EXPECT_TRUE(IsInitialBlockDownload()); fReindexFast = false;

    // 2.
    // Check that if fCheckpointsEnabled is set and the tip of activeChain is lower than
    // Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()) allows 
    // IsInitialBlockDownload to return true
    fCheckpointsEnabled = true;
    CBlockIndex block1;
    for (size_t blockHeight = 0; blockHeight < Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()); ++blockHeight)
    {
        block1.nHeight = blockHeight;
        chainActive.SetTip(&block1);
        EXPECT_TRUE(IsInitialBlockDownload());
    }

    // 3a.
    // Checks that if the first time-related condition is met allows
    // IsInitialBlockDownload to return true
    block1.nHeight = Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints());
    chainActive.SetTip(&block1);
    // Set pindexBestHeader as current tip and increase height to trigger condition
    pindexBestHeader = chainActive.Tip();
    pindexBestHeader->nHeight += 24 * 6 + 1;
    pindexBestHeader->nTime = GetTime() + chainParams.MaxTipAge();
    EXPECT_TRUE(IsInitialBlockDownload());

    // 3b.
    // Checks that if the second time-related condition is met allows
    // IsInitialBlockDownload to return true
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
    fCheckpointsEnabled = false;
    auto tempTip = chainActive.Tip();
    chainActive.SetTip(nullptr);
    EXPECT_TRUE(IsInitialBlockDownload());
    chainActive.SetTip(tempTip);

    /////
    // Set conditions so that all checks fail, and the end of the function is reached.
    // In this case:
    // - lockIBDState is set to true
    // - function returns false
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
