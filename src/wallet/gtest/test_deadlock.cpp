#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sodium.h>

#include <boost/filesystem.hpp>
#include <thread>
#include <atomic>

#include "zcash/JoinSplit.hpp"
#include "wallet/wallet.h"
#include "utiltest.h"

using namespace libzcash;

extern ZCJoinSplit *params;

void write_db(CWallet &wallet, SpendingKey &sk, std::atomic_int &finish)
{
    CBlock block;
    for (int i = 0; i < 1000; i++) {
        auto wtx = GetValidReceive(*params, sk, 10, true);
        wallet.SyncTransaction(wtx.getWrappedTx(), &block);
    }

    finish++;
}

void write_block(CWallet &walletdb, std::atomic_int &finish) {
    for (int i = 0; i < 100; i++) {
        CBlockLocator loc;
        walletdb.SetBestChain(loc);
    }

    finish++;
}

TEST(deadlock_test, deadlock) {
    //set tmp db folder
    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path()
            / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();

    //create wallet
    SelectParams(CBaseChainParams::TESTNET);
    bool fFirstRun = true;
    CWallet walletMain("deadlock_ut_wallet.dat");
    walletMain.LoadWallet(fFirstRun);

    auto sk = libzcash::SpendingKey::random();
    walletMain.AddSpendingKey(sk);

    //number of concurrent SyncTransaction Thread -1
    enum { threadNum = 2 };
    std::atomic_int finished(0);
    std::thread myThreads[threadNum];

    //launch #size -1 thread to process transaction
    for (int i = 0; i < threadNum; i++) {
        if (i == 0) {
            myThreads[i] = std::thread(write_block, std::ref(walletMain),
                    std::ref(finished));

        }
        else { //otherwise it process txes
            myThreads[i] = std::thread(write_db, std::ref(walletMain),
                    std::ref(sk), std::ref(finished));
        }

    }

    //wait all threads to finish
    for (int i = 0; i < threadNum; i++) {
        myThreads[i].join();
    }

    EXPECT_EQ(finished, threadNum);
}
