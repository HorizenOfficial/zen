#include <gtest/gtest.h>
#include "gtest/tx_creation_utils.h"

#include <wallet/wallet.h>

class CertInWalletTest : public ::testing::Test {
public:
    CertInWalletTest() {}
    ~CertInWalletTest() = default;

    void SetUp() override {};
    void TearDown() override {};
};

TEST_F(CertInWalletTest, WalletCertSerializationOps) {
    CWallet dummyWallet;
    CScCertificate cert =
            txCreationUtils::createCertificate(uint256S("aaa"), /*epochNum*/0, uint256S("bbb"), /*numChangeOut*/2, /*bwTotaltAmount*/CAmount(10), /*numBwt*/4);
    CWalletCert walletCert(&dummyWallet, cert);

    //test
    CDataStream certStream(SER_DISK, CLIENT_VERSION);
    certStream << walletCert;
    CWalletCert retrievedWalletCert;
    certStream >> retrievedWalletCert;

    //Checks
    EXPECT_TRUE(walletCert == retrievedWalletCert);
}

TEST_F(CertInWalletTest, WalletTxSerializationOps) {
    CWallet dummyWallet;

    CMutableTransaction mutTx;
    mutTx.nVersion = TRANSPARENT_TX_VERSION;
    mutTx.vin.resize(1);
    mutTx.vin.push_back(CTxIn(COutPoint(uint256S("aaa"), 0), CScript(), 1));
    mutTx.addOut(CTxOut(CAmount(10), CScript()));
    CTransaction tx(mutTx);

    CWalletTx walletTx(&dummyWallet, tx);

    //test
    CDataStream txStream(SER_DISK, CLIENT_VERSION);
    txStream << walletTx;
    CWalletTx retrievedWalletTx;
    txStream >> retrievedWalletTx;

    //Checks
    EXPECT_TRUE(walletTx == retrievedWalletTx);
}


