#include <gtest/gtest.h>
#include "main.h"

#include <algorithm>
#include <random>

extern bool fDebug;

static const int TRUNK_01_SZ = 1500;
static const int FORK_01_POS  = 10;

static const int TRUNK_02_SZ = 1500;
static const int FORK_02_POS  = 500;

static const int TRUNK_03_SZ = 2;

static const int FORK_03_POS  = 3000;
static const int TRUNK_04_SZ = 1;

//               [12]- .. -[1511]
//               /
//    [0]- .. -[11]- .. -[501]- .. -[1500]
//                          \
//                          [502]-[503]
//                            \
//                            [503]

std::set<uint64_t> sHashes;
std::vector<CBlockIndex*> vBlocks;

void makeFork(std::vector<CBlockIndex*>& vBlocks, int start_pos, int trunk_size)
{
    assert(start_pos < vBlocks.size() );
    assert(trunk_size > 0);

    CBlockIndex* forkStart = vBlocks[start_pos];
    int baseH = forkStart->nHeight + 1;

    std::cout << " Fork1 at h(" << forkStart->nHeight << ")" << std::endl;

    // add a fork
    for (int i = baseH; i < baseH + trunk_size; i++)
    {
        CBlock b;
        CBlockIndex* bi  = new CBlockIndex(b);
        while (true)
        {
            b.nTime = rand();
            if (sHashes.count(b.GetHash().GetCheapHash()) == 0 )
            {     
                sHashes.insert(b.GetHash().GetCheapHash());
                break;
            }
        }
        uint256* hash = new uint256(b.GetHash());
        bi->phashBlock = hash;
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
        addToLatestBlocks(bi, i);
    }
}

CBlockIndex* makeOne(int h)
{
    CBlock b;
    b.nTime = (int)rand()*rand()*time(NULL);
    CBlockIndex* bi = new CBlockIndex(b);
    uint256* hash = new uint256(b.GetHash());
    bi->phashBlock = hash;
    bi->nHeight = h;
    sHashes.insert(hash->GetCheapHash());
    return bi;
}

CBlockIndex* makeMain(std::vector<CBlockIndex*>& vBlocks, int trunk_size, bool addToLatestBlocksCall)
{
    // Create a fake genesis block
    CBlock block1;
    block1.nTime = (int)rand();
    CBlockIndex genesis {block1};
    uint256* hash = new uint256(block1.GetHash());
    genesis.phashBlock = hash;
    genesis.nHeight = 0;
//    std::cout << "Genesis: " << hash->ToString() << std::endl;
    sHashes.insert(hash->GetCheapHash());

    CBlockIndex* pindex = NULL;

    // create the main trunk, from which some forks will stem
    for (int i = 1; i < trunk_size + 1; i++)
    {
        CBlock b;
        b.nTime = vBlocks.size();

        CBlockIndex* bi = new CBlockIndex(b);
        while (true)
        {
            b.nTime = rand()*time(NULL);
            if (sHashes.count(b.GetHash().GetCheapHash()) == 0 )
            {     
                sHashes.insert(b.GetHash().GetCheapHash());
                break;
            }
        }
        uint256* hash = new uint256(b.GetHash());
        bi->phashBlock = hash;
        bi->nHeight = i;
        if (i == 1)
        {
            bi->pprev = &genesis;
            pindex = bi;
        }
        else
        {
            bi->pprev = vBlocks.back();
        }
        bi->nChainTx = 33;
        vBlocks.push_back(bi);
        if (addToLatestBlocksCall)
        {
            addToLatestBlocks(bi, i);
        }
    }

    return pindex;
}

TEST(findaltblocks_test, findaltblocks) {

    auto vResult = std::vector<CBlockIndex*>();

    CBlockIndex* pindex = makeMain(vBlocks, TRUNK_01_SZ, true);
//    std::cout << " Total Blocks: " << vBlocks.size() <<std::endl;
//    std::cout << " Total Hashes: " << sHashes.size() <<std::endl;

    makeFork(vBlocks, FORK_01_POS, TRUNK_02_SZ);
//    std::cout << " Total Blocks: " << vBlocks.size() <<std::endl;
//    std::cout << " Total Hashes: " << sHashes.size() <<std::endl;

    makeFork(vBlocks, FORK_02_POS, TRUNK_03_SZ);
//    std::cout << " Total Blocks: " << vBlocks.size() <<std::endl;
//    std::cout << " Total Hashes: " << sHashes.size() <<std::endl;

    makeFork(vBlocks, FORK_03_POS, TRUNK_04_SZ);
//    std::cout << " Total Blocks: " << vBlocks.size() <<std::endl;
//    std::cout << " Total Hashes: " << sHashes.size() <<std::endl;

#if 0
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

    std::cout << " Testing API... " <<std::endl;
    ASSERT_EQ (findAltBlocks(pindex, vResult), 4);

    std::cout << "Results: " << vResult.size() << std::endl;
    std::cout << "------------" << std::endl;
    BOOST_FOREACH(CBlockIndex* block, vResult)
    {
        std::cout << block->GetBlockHash().ToString() << "   h(" << block->nHeight << ") " << std::endl;
    }

    latestBlocks.clear();
}

TEST(findaltblocks_test, qqq)
{
    int hmin = -1;
    int hmax = -1;

    std::cout << " Passing a null ptr... " << std::endl;
    ASSERT_EQ (addToLatestBlocks(NULL, 0), false);

    vBlocks.push_back( makeOne(33) );

    std::cout << " Passing too old a block compared to main chain heigth... " << std::endl;
    ASSERT_EQ (addToLatestBlocks(vBlocks.back(), 10000), false);

    std::cout << " Passing a good block... " << std::endl;
    ASSERT_EQ (addToLatestBlocks(vBlocks.back(), 100), true);

    std::cout << " Passing a block 3 heigth ahead... " << std::endl;
    vBlocks.push_back(makeOne(36));
    ASSERT_EQ (addToLatestBlocks(vBlocks.back(), 0), true);

    hmin = latestBlocks.front()[0]->nHeight;
    hmax = latestBlocks.back()[0]->nHeight;
    std::cout << " Delta: min=" << hmin << ", hmax=" << hmax << std::endl;

    std::cout << " Passing a chain 3000 long ... " << std::endl;
    makeMain(vBlocks, 3000, true);

    hmin = latestBlocks.front()[0]->nHeight;
    hmax = latestBlocks.back()[0]->nHeight;
    std::cout << " Delta: min=" << hmin << ", hmax=" << hmax << std::endl;

    std::cout << " Now passing a block older than min ... " << std::endl;
    vBlocks.push_back(makeOne(hmin-1));
    ASSERT_EQ (addToLatestBlocks(vBlocks.back(), 0), false);

    std::cout << " Passing the same block but with hMain =-1 ... " << std::endl;
    ASSERT_EQ (addToLatestBlocks(vBlocks.back(), -1), true);

    latestBlocks.clear();
}
