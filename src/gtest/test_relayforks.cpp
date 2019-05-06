#include <gtest/gtest.h>
#include "main.h"
#include "consensus/validation.h"

#include <algorithm>
#include <random>


extern CBlockIndex* AddToBlockIndex(const CBlockHeader& block);

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

//                    [5]-[6]            (Trunk 5)    f4
//                    /
//              [3]-[4]-[5]-[6]-[7]      (Trunk 2)    f1
//              /
//    [0]-[1]-[2]-[3]-[4]-[5]            (Trunk 1)    main
//                  \
//                  [4]-[5]              (Trunk 3)    f2
//                    \
//                    [5]                (Trunk 4)    f3

static const int  MAIN_CHAIN_TEST_LEN = 10;

#endif

std::vector<CBlockIndex*> vBlocks;
std::vector<uint256> vOutput;

void CleanUpAll()
{
    chainActive.SetTip(NULL);
    vBlocks.clear();
    vOutput.clear();
    mapBlockIndex.clear();
    mGlobalForkTips.clear();
}

const CBlockIndex* makeFork(int start_pos, int trunk_size)
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
        addToGlobalForkTips(bi);
    }
    return vBlocks.back();
}

const CBlockIndex* makeMain(int trunk_size)
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
        b.nVersion = MIN_BLOCK_VERSION;

        CBlockIndex *bi = AddToBlockIndex(b);
//        CBlockIndex* bi = new CBlockIndex(b);
//        bi->phashBlock = new uint256(GetRandHash());
//        bi->nHeight = i;
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
        addToGlobalForkTips(bi);
    }

    std::cout << " main chain built: length(" << trunk_size << ")" << std::endl;
    return vBlocks.back();
}

TEST(relayforks_test, relayforks) {
    CleanUpAll();

#if defined(TEST_ALT_DEBUG)
    // print logs to console
    fDebug = true;
    fPrintToConsole = true;
    mapArgs["-debug"] = "forks";
    mapMultiArgs["-debug"].push_back("forks");
#endif

//    SelectParams(CBaseChainParams::REGTEST);

    std::cout << "Building main chain..." << std::endl;
    const CBlockIndex* fm = makeMain(TRUNK_01_SZ);
    MilliSleep(2000);

    std::cout << "Forking from main chain..." << std::endl;
    const CBlockIndex* f1 = makeFork(FORK_01_POS, TRUNK_02_SZ);
    MilliSleep(2000);

    std::cout << "Forking from main chain again..." << std::endl;
    const CBlockIndex* f2 = makeFork(FORK_02_POS, TRUNK_03_SZ);
    MilliSleep(2000);

    std::cout << "Forking from latest fork..." << std::endl;
    const CBlockIndex* f3 = makeFork(FORK_03_POS, TRUNK_04_SZ);
    MilliSleep(2000);

    std::cout << "Forking from first fork..." << std::endl;
    const CBlockIndex* f4 = makeFork(FORK_04_POS, TRUNK_05_SZ);

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
#endif

    dump_global_tips();

    // 1. check the heighest block is on top of the container which orders them
    const CBlockIndex* highest = (*(mGlobalForkTips.begin())).first;
    ASSERT_EQ ( highest->GetBlockHash(), f1->GetBlockHash() );

    // 2. check that the latest arrived tips are in the correct order
    std::cout << "f4: " << std::to_string((int)mGlobalForkTips[f4]) << std::endl;
    std::cout << "f3: " << std::to_string((int)mGlobalForkTips[f3]) << std::endl;
    std::cout << "f2: " << std::to_string((int)mGlobalForkTips[f2]) << std::endl;

    vOutput.clear();
    ASSERT_EQ ( getMostRecentGlobalForkTips(vOutput), 3);
    ASSERT_EQ ( vOutput[0], f4->GetBlockHash() );
    ASSERT_EQ ( vOutput[1], f3->GetBlockHash() );
    ASSERT_EQ ( vOutput[2], f2->GetBlockHash() );

    // 3. update the time of the tip on f1 and check it is the most recent now 
    MilliSleep(2000);
    ASSERT_EQ( updateGlobalForkTips(f1, false), true);
    vOutput.clear();
    ASSERT_EQ ( getMostRecentGlobalForkTips(vOutput), 3);
    ASSERT_EQ ( vOutput[0], f1->GetBlockHash() );

    // 4. take a block on the main chain, updating the concerned tip should fail
    MilliSleep(2000);
    const CBlockIndex* dum1 = vBlocks[FORK_01_POS + 1];
    ASSERT_EQ( updateGlobalForkTips(dum1, true), false);

    // 5. take a block on a fork placed behind a crossroads: updating of both its tips should
    // be succesful and they would be on top of ordered vector 
    MilliSleep(2000);
    const CBlockIndex* dum2 = vBlocks[FORK_04_POS - 1];
    ASSERT_EQ( updateGlobalForkTips(dum2, true), true);

    vOutput.clear();
    ASSERT_EQ ( getMostRecentGlobalForkTips(vOutput), 3);

    // same time, that means in either of the two first positions)
    bool b1 = ( (vOutput[0] == f1->GetBlockHash() ) && (vOutput[1] == f4->GetBlockHash() ) );
    bool b2 = ( (vOutput[0] == f4->GetBlockHash() ) && (vOutput[1] == f1->GetBlockHash() ) );

    ASSERT_EQ( (b1 || b2) , true);

    CleanUpAll();
}

TEST(relayforks_test, checkisonmain) {
    CleanUpAll();

#if defined(TEST_ALT_DEBUG)
    // print logs to console
    fDebug = true;
    fPrintToConsole = true;
    mapArgs["-debug"] = "forks";
    mapMultiArgs["-debug"].push_back("forks");
#endif

    std::cout << "Building main chain..." << std::endl;
    const CBlockIndex* fm = makeMain(TRUNK_01_SZ);
    MilliSleep(2000);

    std::cout << "Forking from main chain..." << std::endl;
    const CBlockIndex* f1 = makeFork(FORK_01_POS, TRUNK_02_SZ);
    MilliSleep(2000);

    std::cout << "Forking from main chain again..." << std::endl;
    const CBlockIndex* f2 = makeFork(FORK_02_POS, TRUNK_03_SZ);
    MilliSleep(2000);

    std::cout << "Forking from latest fork..." << std::endl;
    const CBlockIndex* f3 = makeFork(FORK_03_POS, TRUNK_04_SZ);
    MilliSleep(2000);

    std::cout << "Forking from first fork..." << std::endl;
    const CBlockIndex* f4 = makeFork(FORK_04_POS, TRUNK_05_SZ);

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
#endif

    dump_global_tips();

    CBlockLocator bl = chainActive.GetLocator(fm);
    uint256 hashStop = fm->GetBlockHash();
    CBlockIndex* ref = NULL;

    bool onMain = getHeadersIsOnMain(bl, hashStop, &ref);

    ASSERT_EQ( (onMain) , true);

    CleanUpAll();
}
