#include <gtest/gtest.h>
#include "gtest/tx_creation_utils.h"
#include <boost/filesystem.hpp>

#include <chainparams.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>


class CertInWalletTest : public ::testing::Test {
public:
    CertInWalletTest(): walletDbLocation(), pWallet(nullptr), pWalletDb(nullptr) {}
    ~CertInWalletTest() = default;

    void SetUp() override {
        //Setup environment
        SelectParams(CBaseChainParams::TESTNET);
        boost::filesystem::path walletDbLocation = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories(walletDbLocation);
        mapArgs["-datadir"] = walletDbLocation.string();

        //create wallet db
        pWallet = new CWallet("wallet.dat");
        try {  pWalletDb = new CWalletDB(pWallet->strWalletFile, "cr+"); }
        catch(std::exception& e) {
            ASSERT_TRUE(false)<<"Could not create tmp wallet db for reason "<<e.what();
        }
    };

    void TearDown() override {
        std::vector<std::shared_ptr<CWalletTransactionBase> > vWtx;
        DBErrors nZapWalletRet = pWallet->ZapWalletTx(vWtx);

        delete pWalletDb;
        pWalletDb = nullptr;

        delete pWallet;
        pWallet = nullptr;

        ClearDatadirCache();
        boost::system::error_code ec;
        boost::filesystem::remove_all(walletDbLocation.string(), ec);
    };

protected:
    boost::filesystem::path walletDbLocation;
    CWallet* pWallet;
    CWalletDB* pWalletDb;
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
    mutTx.vin.push_back(CTxIn(COutPoint(uint256S("aaa"), 0), CScript(), 10));
    mutTx.addOut(CTxOut(CAmount(5), CScript()));
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

TEST_F(CertInWalletTest, LoadWalletTxFromDb) {
    //Create wallet transaction to be stored
    CMutableTransaction mutTx;
    mutTx.nVersion = TRANSPARENT_TX_VERSION;
    mutTx.vin.push_back(CTxIn(COutPoint(uint256S("aaa"), 0), CScript(), 1));
    mutTx.addOut(CTxOut(CAmount(10), CScript()));
    CTransaction tx(mutTx);
    CWalletTx walletTx(pWallet, tx);

    // Test
    EXPECT_TRUE(pWalletDb->WriteWalletTxBase(tx.GetHash(), walletTx));

    DBErrors ret  = pWalletDb->LoadWallet(pWallet);
    ASSERT_TRUE(DB_LOAD_OK == ret) << "Error: "<<ret;
    //EXPECT_TRUE(pWallet->getMapWallet().size() == 1);
    ASSERT_TRUE(pWallet->getMapWallet().count(tx.GetHash()));
    CWalletTx retrievedWalletTx = *dynamic_cast<const CWalletTx*>(pWallet->getMapWallet().at(tx.GetHash()).get());
    EXPECT_TRUE(retrievedWalletTx == walletTx);
}

TEST_F(CertInWalletTest, LoadWalletCertFromDb) {
    //Create wallet cert to be stored
    CScCertificate cert =
            txCreationUtils::createCertificate(uint256S("aaa"), /*epochNum*/0, uint256S("bbb"), /*numChangeOut*/2, /*bwTotaltAmount*/CAmount(10), /*numBwt*/4);
    CWalletCert walletCert(pWallet, cert);

    // Test
    EXPECT_TRUE(pWalletDb->WriteWalletTxBase(cert.GetHash(), walletCert));

    DBErrors ret  = pWalletDb->LoadWallet(pWallet);
    ASSERT_TRUE(DB_LOAD_OK == ret) << "Error: "<<ret;
    //EXPECT_TRUE(pWallet->getMapWallet().size() == 1) << pWallet->getMapWallet().size();
    ASSERT_TRUE(pWallet->getMapWallet().count(cert.GetHash()));
    CWalletCert retrievedWalletCert = *dynamic_cast<const CWalletCert*>(pWallet->getMapWallet().at(cert.GetHash()).get());
    EXPECT_TRUE(retrievedWalletCert == walletCert);
}
