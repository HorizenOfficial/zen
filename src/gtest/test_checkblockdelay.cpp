#include <gtest/gtest.h>
#include <zen/delay.h> 
using namespace zen;

TEST(delay_tests, get_block_delay) {
    CBlockIndex *block = new CBlockIndex();
    CBlockIndex *pblock = new CBlockIndex();

    CBlock newblock;
    CBlockIndex indexNew {newblock};
    CBlock prevblock;
    CBlockIndex indexPrev {prevblock};


    indexNew.nHeight = 100;
    indexPrev.nChainDelay = 0;
    int activeChainHeight = 100; 
    ASSERT_EQ (GetBlockDelay(indexNew, indexPrev, activeChainHeight, false), 0);

    
    indexNew.nHeight = 5;
    indexPrev.nChainDelay = 0;
    activeChainHeight = 16;
    ASSERT_EQ (GetBlockDelay(indexNew, indexPrev, activeChainHeight, false), 11);


    // some delay in the current chain
    indexNew.nHeight = 100 ;
    indexPrev.nChainDelay = 20 ;
    activeChainHeight = 500; 
    ASSERT_EQ (GetBlockDelay(indexNew, indexPrev, activeChainHeight, false), 400);


    indexNew.nHeight = 6;
    indexPrev.nChainDelay = 11;
    activeChainHeight = 16;
    ASSERT_EQ (GetBlockDelay(indexNew, indexPrev, activeChainHeight, false), 10);

}
