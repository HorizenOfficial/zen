// Copyright (c) 2018-2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include "zcash/Address.hpp"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "util.h"

#include <boost/filesystem.hpp>


class MockCWallet : public CWallet {
public:
    MockCWallet() : CWallet(),
                    csWalletLock(cs_wallet, "cs_wallet", __FILE__, __LINE__) {}

    MockCWallet(const std::string& strWalletFileIn) : CWallet(strWalletFileIn),
                                                      csWalletLock(cs_wallet, "cs_wallet", __FILE__, __LINE__) {}

private:
    CCriticalBlock csWalletLock;
};

class WalletZkeysTest: public ::testing::Test {
protected:
    WalletZkeysTest() {
        SelectParams(CBaseChainParams::REGTEST);
    }
};

/**
 * This test covers methods on CWallet
 * GenerateNewZKey()
 * AddZKey()
 * LoadZKey()
 * LoadZKeyMetadata()
 */
TEST_F(WalletZkeysTest, store_and_load_zkeys) {
    SelectParams(CBaseChainParams::MAIN);

    MockCWallet wallet;

    // wallet should be empty
    std::set<libzcash::PaymentAddress> addrs;
    wallet.GetPaymentAddresses(addrs);
    ASSERT_EQ(0, addrs.size());

    // wallet should have one key
    CZCPaymentAddress paymentAddress = wallet.GenerateNewZKey();
    wallet.GetPaymentAddresses(addrs);
    ASSERT_EQ(1, addrs.size());

    // verify wallet has spending key for the address
    auto addr = paymentAddress.Get();
    ASSERT_TRUE(wallet.HaveSpendingKey(addr));

    // manually add new spending key to wallet
    auto sk = libzcash::SpendingKey::random();
    ASSERT_TRUE(wallet.AddZKey(sk));

    // verify wallet did add it
    addr = sk.address();
    ASSERT_TRUE(wallet.HaveSpendingKey(addr));

    // verify spending key stored correctly
    libzcash::SpendingKey keyOut;
    wallet.GetSpendingKey(addr, keyOut);
    ASSERT_EQ(sk, keyOut);

    // verify there are two keys
    wallet.GetPaymentAddresses(addrs);
    ASSERT_EQ(2, addrs.size());
    ASSERT_EQ(1, addrs.count(addr));

    // Load a third key into the wallet
    sk = libzcash::SpendingKey::random();
    ASSERT_TRUE(wallet.LoadZKey(sk));

    // attach metadata to this third key
    addr = sk.address();
    int64_t now = GetTime();
    CKeyMetadata meta(now);
    ASSERT_TRUE(wallet.LoadZKeyMetadata(addr, meta));

    // check metadata is the same
    CKeyMetadata m= wallet.mapZKeyMetadata[addr];
    ASSERT_EQ(m.nCreateTime, now);
}

/**
 * This test covers methods on CWallet
 * AddViewingKey()
 * RemoveViewingKey()
 * LoadViewingKey()
 */
TEST_F(WalletZkeysTest, StoreAndLoadViewingKeys) {
    SelectParams(CBaseChainParams::MAIN);

    MockCWallet wallet;

    // wallet should be empty
    std::set<libzcash::PaymentAddress> addrs;
    wallet.GetPaymentAddresses(addrs);
    ASSERT_EQ(0, addrs.size());

    // manually add new viewing key to wallet
    auto sk = libzcash::SpendingKey::random();
    auto vk = sk.viewing_key();
    ASSERT_TRUE(wallet.AddViewingKey(vk));

    // verify wallet did add it
    auto addr = sk.address();
    ASSERT_TRUE(wallet.HaveViewingKey(addr));
    // and that we don't have the corresponding spending key
    ASSERT_FALSE(wallet.HaveSpendingKey(addr));

    // verify viewing key stored correctly
    libzcash::ViewingKey vkOut;
    wallet.GetViewingKey(addr, vkOut);
    ASSERT_EQ(vk, vkOut);

    // Load a second viewing key into the wallet
    auto sk2 = libzcash::SpendingKey::random();
    ASSERT_TRUE(wallet.LoadViewingKey(sk2.viewing_key()));

    // verify wallet did add it
    auto addr2 = sk2.address();
    ASSERT_TRUE(wallet.HaveViewingKey(addr2));
    ASSERT_FALSE(wallet.HaveSpendingKey(addr2));

    // Remove the first viewing key
    ASSERT_TRUE(wallet.RemoveViewingKey(vk));
    ASSERT_FALSE(wallet.HaveViewingKey(addr));
    ASSERT_TRUE(wallet.HaveViewingKey(addr2));
}

/**
 * This test covers methods on CWalletDB
 * WriteZKey()
 */
TEST_F(WalletZkeysTest, write_zkey_direct_to_db) {
    SelectParams(CBaseChainParams::TESTNET);

    // Get temporary and unique path for file.
    // Note: / operator to append paths
    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();

    bool fFirstRun;
    MockCWallet wallet("wallet.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));

    // No default CPubKey set
    ASSERT_TRUE(fFirstRun);

    // wallet should be empty
    std::set<libzcash::PaymentAddress> addrs;
    wallet.GetPaymentAddresses(addrs);
    ASSERT_EQ(0, addrs.size());

    // Add random key to the wallet
    auto paymentAddress = wallet.GenerateNewZKey();

    // wallet should have one key
    wallet.GetPaymentAddresses(addrs);
    ASSERT_EQ(1, addrs.size());

    // create random key and add it to database directly, bypassing wallet
    auto sk = libzcash::SpendingKey::random();
    auto addr = sk.address();
    int64_t now = GetTime();
    CKeyMetadata meta(now);
    CWalletDB db("wallet.dat");
    db.WriteZKey(addr, sk, meta);

    // wallet should not be aware of key
    ASSERT_FALSE(wallet.HaveSpendingKey(addr));

    // wallet sees one key
    wallet.GetPaymentAddresses(addrs);
    ASSERT_EQ(1, addrs.size());

    // wallet should have default metadata for addr with null createtime
    CKeyMetadata m = wallet.mapZKeyMetadata[addr];
    ASSERT_EQ(m.nCreateTime, 0);
    ASSERT_NE(m.nCreateTime, now);

    // load the wallet again
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));

    // wallet can now see the spending key
    ASSERT_TRUE(wallet.HaveSpendingKey(addr));

    // check key is the same
    libzcash::SpendingKey keyOut;
    wallet.GetSpendingKey(addr, keyOut);
    ASSERT_EQ(sk, keyOut);

    // wallet should have two keys
    wallet.GetPaymentAddresses(addrs);
    ASSERT_EQ(2, addrs.size());

    // check metadata is now the same
    m = wallet.mapZKeyMetadata[addr];
    ASSERT_EQ(m.nCreateTime, now);
}

/**
 * This test covers methods on CWalletDB
 * WriteViewingKey()
 */
TEST_F(WalletZkeysTest, WriteViewingKeyDirectToDB) {
    SelectParams(CBaseChainParams::TESTNET);

    // Get temporary and unique path for file.
    // Note: / operator to append paths
    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();

    bool fFirstRun;
    MockCWallet wallet("wallet-vkey.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));

    // No default CPubKey set
    ASSERT_TRUE(fFirstRun);

    // create random viewing key and add it to database directly, bypassing wallet
    auto sk = libzcash::SpendingKey::random();
    auto vk = sk.viewing_key();
    auto addr = sk.address();
    int64_t now = GetTime();
    CKeyMetadata meta(now);
    CWalletDB db("wallet-vkey.dat");
    db.WriteViewingKey(vk);

    // wallet should not be aware of viewing key
    ASSERT_FALSE(wallet.HaveViewingKey(addr));

    // load the wallet again
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));

    // wallet can now see the viewing key
    ASSERT_TRUE(wallet.HaveViewingKey(addr));

    // check key is the same
    libzcash::ViewingKey vkOut;
    wallet.GetViewingKey(addr, vkOut);
    ASSERT_EQ(vk, vkOut);
}



/**
 * This test covers methods on CWalletDB to load/save crypted z keys.
 */
TEST_F(WalletZkeysTest, write_cryptedzkey_direct_to_db) {
    SelectParams(CBaseChainParams::TESTNET);

    // Get temporary and unique path for file.
    // Note: / operator to append paths
    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();

    bool fFirstRun;
    MockCWallet wallet("wallet_crypted.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));

    // No default CPubKey set
    ASSERT_TRUE(fFirstRun);

    // wallet should be empty
    std::set<libzcash::PaymentAddress> addrs;
    wallet.GetPaymentAddresses(addrs);
    ASSERT_EQ(0, addrs.size());

    // Add random key to the wallet
    auto paymentAddress = wallet.GenerateNewZKey();

    // wallet should have one key
    wallet.GetPaymentAddresses(addrs);
    ASSERT_EQ(1, addrs.size());

    // encrypt wallet
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";
    ASSERT_TRUE(wallet.EncryptWallet(strWalletPass));
    
    // adding a new key will fail as the wallet is locked
    EXPECT_ANY_THROW(wallet.GenerateNewZKey());
    
    // unlock wallet and then add
    wallet.Unlock(strWalletPass);
    auto paymentAddress2 = wallet.GenerateNewZKey();

    // Create a new wallet from the existing wallet path
    CWallet wallet2("wallet_crypted.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet2.LoadWallet(fFirstRun));

    // Confirm it's not the same as the other wallet
    ASSERT_TRUE(&wallet != &wallet2);
    
    // wallet should have two keys
    wallet2.GetPaymentAddresses(addrs);
    ASSERT_EQ(2, addrs.size());
    
    // check we have entries for our payment addresses
    ASSERT_TRUE(addrs.count(paymentAddress.Get()));
    ASSERT_TRUE(addrs.count(paymentAddress2.Get()));

    // spending key is crypted, so we can't extract valid payment address
    libzcash::SpendingKey keyOut;
    wallet2.GetSpendingKey(paymentAddress.Get(), keyOut);
    ASSERT_FALSE(paymentAddress.Get() == keyOut.address());
    
    // unlock wallet to get spending keys and verify payment addresses
    wallet2.Unlock(strWalletPass);

    wallet2.GetSpendingKey(paymentAddress.Get(), keyOut);
    ASSERT_EQ(paymentAddress.Get(), keyOut.address());
    
    wallet2.GetSpendingKey(paymentAddress2.Get(), keyOut);
    ASSERT_EQ(paymentAddress2.Get(), keyOut.address());

}

