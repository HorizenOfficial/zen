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


CWalletTx GetValidTx(const libzcash::SpendingKey &sk, CAmount value,
		bool randomInputs) {
	return GetValidReceive(*params, sk, value, randomInputs);
}


TEST(deadlock_test, setup_datadir_location_run_as_first_test) {
	// Get temporary and unique path for file.
	boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path()
			/ boost::filesystem::unique_path();
	boost::filesystem::create_directories(pathTemp);
	mapArgs["-datadir"] = pathTemp.string();
}

void write_db(CWallet &wallet, CBlock &block, SpendingKey &sk,
		std::atomic_int &finish) {

	for (int i = 0; i < 1000; i++) {
		auto wtx = GetValidTx(sk, 10, true);

		wallet.SyncTransaction(wtx, &block);
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

	//create wallet
	SelectParams(CBaseChainParams::TESTNET);
	bool fFirstRun = true;
	CWallet pwalletMain("wallet.dat");
	DBErrors nLoadWalletRet = pwalletMain.LoadWallet(fFirstRun);

	auto sk = libzcash::SpendingKey::random();
	pwalletMain.AddSpendingKey(sk);

	//create block
	CBlock block;

	//number of concurrent SyncTransaction Thread -1
	int size = 2;
	std::atomic_int finished(0);
	std::thread myThreads[size];

	//launch #size -1 thread to process transaction
	for (int i = 0; i < size; i++) {

		if (i == 0) {
			myThreads[i] = std::thread(write_block, std::ref(pwalletMain),
					std::ref(finished));

		}
		//otherwise it process txes
		else {
			myThreads[i] = std::thread(write_db, std::ref(pwalletMain),
					std::ref(block), std::ref(sk), std::ref(finished));

		}

	}

	//wait all threads to finish
	for (int i = 0; i < size; i++) {
		myThreads[i].join();
	}

	EXPECT_EQ(finished, size);

}
