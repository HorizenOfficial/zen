#include <gtest/gtest.h>
// ZEN_MOD_START
#include <zen/delay.h> 
using namespace zen;
// ZEN_MOD_END

TEST(delay_tests, get_block_delay) {
    CBlockIndex *block = new CBlockIndex();
    CBlockIndex *pblock = new CBlockIndex();

    // no delay in current chain.
    block->nHeight= 100 ;
    pblock->nChainDelay= 0 ;
    int activeChainHeight = 100; 
    ASSERT_EQ (GetBlockDelay(block, pblock, activeChainHeight), 0);

    
    block->nHeight= 980 ;
    pblock->nChainDelay= 0 ;
    activeChainHeight = 550; 
    ASSERT_EQ (GetBlockDelay(block, pblock, activeChainHeight), 0);


    // some delay in the current chain
    block->nHeight= 100 ;
    pblock->nChainDelay= 20 ;
    activeChainHeight = 500; 
    ASSERT_EQ (GetBlockDelay(block, pblock, activeChainHeight), 0);


    block->nHeight= 1000 ;
    pblock->nChainDelay= 300 ;
    activeChainHeight = 500; 
    ASSERT_EQ (GetBlockDelay(block, pblock, activeChainHeight), 0);

}
