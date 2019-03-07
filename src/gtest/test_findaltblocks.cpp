#include <gtest/gtest.h>
#include "main.h"

#include <algorithm>
#include <random>

//#define TEST_ALT_DEBUG

#if !defined(TEST_ALT_DEBUG)

static const int TRUNK_01_SZ = 1500;
static const int FORK_01_POS  = 10;

static const int TRUNK_02_SZ = 1500;
static const int FORK_02_POS  = 500;

static const int TRUNK_03_SZ = 2;

static const int FORK_03_POS  = 3000;
static const int TRUNK_04_SZ = 1;

static const int FORK_04_POS  = 1990;
static const int TRUNK_05_SZ = 100;

static const int MULTI_BLOCK_HEIGHT = 503;

//                            [503]- .. -[602]           (Trunk 5)
//                            /
//               [12]- .. - [502] - .. - .. -[1511]      (Trunk 2)
//               /
//    [0]- .. -[11]- .. -[501]- .. - .. - [1500]         (Trunk 1)
//                          \
//                          [502]-[503]                  (Trunk 3)
//                            \
//                            [503]                      (Trunk 4)

static const int  MAIN_CHAIN_TEST_LEN = 10000;

#else
static const int TRUNK_01_SZ = 5;
static const int FORK_01_POS  = 1;

static const int TRUNK_02_SZ = 5;
static const int FORK_02_POS  = 2;

static const int TRUNK_03_SZ = 2;

static const int FORK_03_POS  = 10;
static const int TRUNK_04_SZ = 1;

static const int FORK_04_POS  = 6;
static const int TRUNK_05_SZ = 2;

static const int MULTI_BLOCK_HEIGHT = 5;

//                    [5]-[6]            (Trunk 5)
//                    /
//              [3]-[4]-[5]-[6]-[7]      (Trunk 2)
//              /
//    [0]-[1]-[2]-[3]-[4]-[5]            (Trunk 1)
//                  \
//                  [4]-[5]              (Trunk 3)
//                    \
//                    [5]                (Trunk 4)

static const int  MAIN_CHAIN_TEST_LEN = 10;

#endif

std::vector<CBlockIndex*> vBlocks;

void makeFork(int start_pos, int trunk_size)
{
    assert(start_pos < vBlocks.size() );
    assert(trunk_size > 0);

    CBlockIndex* forkStart = vBlocks[start_pos];
    int baseH = forkStart->nHeight + 1;

    std::cout << " Fork from block at h(" << forkStart->nHeight << ") of length(" << trunk_size << ")" << std::endl;

    // add a fork
    for (int i = baseH; i < baseH + trunk_size; i++)
    {
        CBlock b;
        CBlockIndex* bi  = new CBlockIndex(b);
        bi->phashBlock = new uint256(GetRandHash());
        bi->nHeight = i;
        if (i == baseH)
        {
            bi->pprev = forkStart;
        }
        else
        {
            bi->pprev = vBlocks.back();
        }
        bi->nChainTx = 33;
        vBlocks.push_back(bi);
        addToLatestBlocks(bi);
    }
}

CBlockIndex* makeDummyOne(int h)
{
    CBlock b;
    CBlockIndex* bi = new CBlockIndex(b);
    bi->phashBlock = new uint256(GetRandHash());
    bi->nHeight = h;
    // no link to any previous block
    return bi;
}

void makeMain(int trunk_size)
{
    // Create a fake genesis block
    CBlock b;
    CBlockIndex* genesis = new CBlockIndex(b);
    genesis->phashBlock = new uint256(GetRandHash());
    genesis->nHeight = 0;
    chainActive.SetTip(genesis);

    CBlockIndex* pindex = NULL;

    // create the main trunk, from which some forks will possibly stem
    for (int i = 1; i < trunk_size + 1; i++)
    {
        CBlock b;

        CBlockIndex* bi = new CBlockIndex(b);
        bi->phashBlock = new uint256(GetRandHash());
        bi->nHeight = i;
        if (i == 1)
        {
            bi->pprev = genesis;
            pindex = bi;
        }
        else
        {
            bi->pprev = vBlocks.back();
        }
        bi->nChainTx = 33;
        bi->BuildSkip();
        chainActive.SetTip(bi);
        vBlocks.push_back(bi);
        addToLatestBlocks(bi);
    }

    std::cout << " main chain built: length(" << trunk_size << ")" << std::endl;
}

TEST(findaltblocks_test, findaltblocks) {

#if defined(TEST_ALT_DEBUG)
    // print logs to console
    fDebug = true;
    fPrintToConsole = true;
    mapArgs["-debug"] = "forks";
    mapMultiArgs["-debug"].push_back("forks");
#endif

    // Create an actual MAIN_CHAIN_TEST_LEN-long block chain (without valid blocks).
    auto vResult = std::vector<CBlockIndex*>();

    std::cout << "Building main chain...";
    makeMain(TRUNK_01_SZ);

    std::cout << "Forking from main chain...";
    makeFork(FORK_01_POS, TRUNK_02_SZ);

    std::cout << "Forking from main chain again...";
    makeFork(FORK_02_POS, TRUNK_03_SZ);

    std::cout << "Forking from latest fork...";
    makeFork(FORK_03_POS, TRUNK_04_SZ);

    std::cout << "Forking from first fork...";
    makeFork(FORK_04_POS, TRUNK_05_SZ);

    LatestBlocksContainer& latestBlocks = LatestBlocks::getInstance().latestBlocks;
    std::cout << "Container capacity: " << latestBlocks.capacity() << std::endl;
    latestBlocks.set_capacity(1000);
    std::cout << "Container capacity: " << latestBlocks.capacity() << std::endl;

    std::cout << "Checking we have a vector of size 5 for heigth " << MULTI_BLOCK_HEIGHT << "... expecting true" << std::endl;
    int hmin = latestBlocks.front()[0]->nHeight;
    int sz = latestBlocks[MULTI_BLOCK_HEIGHT-hmin].size();
    ASSERT_EQ(sz, 5);

#if defined(TEST_ALT_DEBUG)
    std::cout << "Blocks: " << vBlocks.size() <<std::endl;
    std::cout << "------------" << std::endl;

    BOOST_FOREACH(CBlockIndex* block, vBlocks)
    {
        std::cout << "h(" << block->nHeight << ") " <<
            block->GetBlockHash().ToString() << " <-- ";
        if (block->pprev)
        {
            std::cout << block->pprev->GetBlockHash().ToString() << std::endl;
        }
        else
        {
            std::cout << "???" << std::endl;
        }
    }

    dump_latest_blocks(NULL, true);
#endif

    std::cout << "Testing API: looking for tips, there should be 5 of them... " <<std::endl;
    findAltBlocks(chainActive[1], vResult);
    ASSERT_EQ (vResult.size(), 5);

    std::cout << "Results: " << vResult.size() << std::endl;
    BOOST_FOREACH(CBlockIndex* block, vResult)
    {
        std::cout << "   " << block->GetBlockHash().ToString() << "   h(" << block->nHeight << ") " << std::endl;
    }

    // create a 'hole' : should never happen in rela code, but just check reliability of algorithm
    vBlocks[FORK_03_POS]->nChainTx=0;
    vResult.clear();

#if defined(TEST_ALT_DEBUG)
    std::cout << "Blocks: " << vBlocks.size() <<std::endl;
    std::cout << "------------" << std::endl;

    BOOST_FOREACH(CBlockIndex* block, vBlocks)
    {
        std::cout << "h(" << block->nHeight << ") " <<
            block->GetBlockHash().ToString() << " <-- ";
        if (block->pprev)
        {
            std::cout << block->pprev->GetBlockHash().ToString() << std::endl;
        }
        else
        {
            std::cout << "???" << std::endl;
        }
    }

    dump_latest_blocks(NULL, true);
#endif

    std::cout << "Testing API: looking for tips after creating hole, there should be 3 of them now... " <<std::endl;
    findAltBlocks(chainActive[1], vResult);
    ASSERT_EQ (vResult.size(), 3);

    std::cout << "Results: " << vResult.size() << std::endl;
    BOOST_FOREACH(CBlockIndex* block, vResult)
    {
        std::cout << "   " << block->GetBlockHash().ToString() << "   h(" << block->nHeight << ") " << std::endl;
    }

    latestBlocks.clear();
    vBlocks.clear();
}

TEST(findaltblocks_test, addtolatestblocks)
{
    int hmin = -1;
    CBlockIndex* dum = NULL;

#if defined(TEST_ALT_DEBUG)
    // print logs to console
    fDebug = true;
    fPrintToConsole = true;
    mapArgs["-debug"] = "forks";
    mapMultiArgs["-debug"].push_back("forks");
#endif

    // Create an actual MAIN_CHAIN_TEST_LEN-long block chain (without valid blocks).
    makeMain(MAIN_CHAIN_TEST_LEN);

    std::cout << " Passing a null ptr... expecting false" << std::endl;
    ASSERT_EQ (addToLatestBlocks(NULL), false);

    dum = makeDummyOne(-123);
    std::cout << " Passing bad block (invalid height) ... expecting false" << std::endl;
    ASSERT_EQ (addToLatestBlocks(dum), false);

    dum = makeDummyOne(MAIN_CHAIN_TEST_LEN + 5);
    std::cout << " Passing bad block (living in the future) ... expecting false" << std::endl;
    ASSERT_EQ (addToLatestBlocks(dum), false);

    dum = makeDummyOne(MAIN_CHAIN_TEST_LEN + 1);
    std::cout << " Passing future contiguous block ... expecting true" << std::endl;
    ASSERT_EQ (addToLatestBlocks(dum), true);

    dum = makeDummyOne(MAIN_CHAIN_TEST_LEN - 5);
    std::cout << " Passing a good block... expecting true" << std::endl;
    ASSERT_EQ (addToLatestBlocks(dum), true);

    int h = MAIN_CHAIN_TEST_LEN - 2;
    std::cout << " Passing another good block... expecting true" << std::endl;
    dum = makeDummyOne(h);
    ASSERT_EQ (addToLatestBlocks(dum), true);

    std::cout << " Passing another block with same height as before... expecting true" << std::endl;
    dum = makeDummyOne(h);
    ASSERT_EQ (addToLatestBlocks(dum), true);

    LatestBlocksContainer& latestBlocks = LatestBlocks::getInstance().latestBlocks;
    hmin = latestBlocks.front()[0]->nHeight;
    int sz = latestBlocks[h-hmin].size();
    std::cout << " Checking we have a vector of size 3 for that heigth... expecting true" << std::endl;
    ASSERT_EQ(sz, 3);

    std::cout << " Now passing a block older than minimum ... expecting false" << std::endl;
    dum = chainActive[hmin - 1];
    ASSERT_EQ (addToLatestBlocks(dum), false);

    chainActive.SetTip(NULL);
    std::cout << " Passing the same block but with hMain =-1 ... expecting false" << std::endl;
    ASSERT_EQ (addToLatestBlocks(dum), false);

    latestBlocks.clear();
}
