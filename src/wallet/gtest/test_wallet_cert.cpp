#include <gtest/gtest.h>
#include "gtest/tx_creation_utils.h"
#include <boost/filesystem.hpp>

#include <chainparams.h>
#include <main.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>


class CertInWalletTest : public ::testing::Test {
public:
    CertInWalletTest(): walletDbLocation(), pWallet(nullptr), pWalletDb(nullptr) {}
    ~CertInWalletTest() = default;

    void SetUp() override {
        //Setup environment
        SelectParams(CBaseChainParams::TESTNET);
        walletDbLocation = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
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
        mempool.clear();

        std::vector<std::shared_ptr<CWalletTransactionBase> > vWtx;
        DBErrors nZapWalletRet = pWallet->ZapWalletTx(vWtx);
        EXPECT_TRUE(DB_LOAD_OK == nZapWalletRet)
            <<"Failed cleaning-up the wallet with return code" << nZapWalletRet
            <<". Isolation and independence of subsequent UTs cannot be guaranteed";

        delete pWalletDb;
        pWalletDb = nullptr;

        delete pWallet;
        pWallet = nullptr;

        ClearDatadirCache();
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

///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// IsOutputMature ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CertInWalletTest, IsOutputMature_TransparentTx_InBlockChain) {
    //Create a transparent tx
    CTransaction transparentTx = txCreationUtils::createTransparentTx();

    chainSettingUtils::ExtendChainActiveToHeight(/*startHeight*/100);

    //Add block information
    CWalletTx walletTx(pWallet, transparentTx);
    CBlock txBlock;
    txBlock.vtx.push_back(transparentTx);
    walletTx.hashBlock = txBlock.GetHash();
    walletTx.SetMerkleBranch(txBlock);
    walletTx.fMerkleVerified = true; //shortcut

    chainSettingUtils::ExtendChainActiveWithBlock(txBlock);
    int txCreationHeight = chainActive.Height();

    CCoins::outputMaturity txMaturity = CCoins::outputMaturity::NOT_APPLICABLE;

    //Test
    txMaturity = walletTx.IsOutputMature(0);
    EXPECT_TRUE(txMaturity == CCoins::outputMaturity::MATURE)
        <<"txMaturity is "<<int(txMaturity);
}

TEST_F(CertInWalletTest, IsOutputMature_CoinBase_InBlockChain) {
    //Create coinbase
    CAmount coinBaseAmount = 10;
    CTransaction coinBase = txCreationUtils::createCoinBase(coinBaseAmount);

    //Add block information
    chainSettingUtils::ExtendChainActiveToHeight(/*startHeight*/100);
    CWalletTx walletCoinBase(pWallet, coinBase);
    CBlock coinBaseBlock;
    coinBaseBlock.vtx.push_back(coinBase);
    walletCoinBase.hashBlock = coinBaseBlock.GetHash();
    walletCoinBase.SetMerkleBranch(coinBaseBlock);
    walletCoinBase.fMerkleVerified = true; //shortcut

    chainSettingUtils::ExtendChainActiveWithBlock(coinBaseBlock);
    int coinBaseCreationHeight = chainActive.Height();

    CCoins::outputMaturity coinBaseMaturity = CCoins::outputMaturity::NOT_APPLICABLE;

    //Test
    for(int height = coinBaseCreationHeight; height < coinBaseCreationHeight + COINBASE_MATURITY; ++height)
    {
        chainSettingUtils::ExtendChainActiveToHeight(height);
        coinBaseMaturity = walletCoinBase.IsOutputMature(0);
        EXPECT_TRUE(coinBaseMaturity == CCoins::outputMaturity::IMMATURE)
            <<"coinBaseMaturity at height "<< height << " is "<<int(coinBaseMaturity);
    }

    //Test
    chainSettingUtils::ExtendChainActiveToHeight(coinBaseCreationHeight + COINBASE_MATURITY);
    coinBaseMaturity = walletCoinBase.IsOutputMature(0);
    EXPECT_TRUE(coinBaseMaturity == CCoins::outputMaturity::MATURE)
        <<"coinBaseMaturity is "<<int(coinBaseMaturity);

    //Test no hysteresis
    chainSettingUtils::ExtendChainActiveToHeight(coinBaseCreationHeight + COINBASE_MATURITY - 1);
    coinBaseMaturity = walletCoinBase.IsOutputMature(0);
    EXPECT_TRUE(coinBaseMaturity == CCoins::outputMaturity::IMMATURE)
        <<"coinBaseMaturity is "<<int(coinBaseMaturity);
}

TEST_F(CertInWalletTest, IsOutputMature_Certificate_InBlockChain) {
    //Create certificate
    CScCertificate cert = txCreationUtils::createCertificate(uint256S("aaa"), /*epochNum*/12,
            /*endEpochBlockHash*/uint256S("ccc"), /*numChangeOut*/2, /*bwTotaltAmount*/CAmount(10), /*numBwt*/4);

    //Add block information
    chainSettingUtils::ExtendChainActiveToHeight(/*startHeight*/100);
    CWalletCert walletCert(pWallet, cert);
    CBlock certBlock;
    certBlock.vcert.push_back(cert);
    walletCert.hashBlock = certBlock.GetHash();
    walletCert.bwtMaturityDepth = 25;
    walletCert.SetMerkleBranch(certBlock);
    walletCert.fMerkleVerified = true; //shortcut

    chainSettingUtils::ExtendChainActiveWithBlock(certBlock);
    int certCreationHeight = chainActive.Height();

    CCoins::outputMaturity changeOutputMaturity = CCoins::outputMaturity::NOT_APPLICABLE;
    CCoins::outputMaturity bwtOutputMaturity    = CCoins::outputMaturity::NOT_APPLICABLE;

    //Test
    for(int height = certCreationHeight; height < certCreationHeight + walletCert.bwtMaturityDepth; ++height)
    {
        chainSettingUtils::ExtendChainActiveToHeight(height);
        changeOutputMaturity = walletCert.IsOutputMature(0);
        EXPECT_TRUE(changeOutputMaturity == CCoins::outputMaturity::MATURE)
            <<"changeOutputMaturity for output 0 at height "<< height << " is "<<int(changeOutputMaturity);

        bwtOutputMaturity = walletCert.IsOutputMature(cert.GetVout().size()-1);
        EXPECT_TRUE(bwtOutputMaturity == CCoins::outputMaturity::IMMATURE)
            <<"bwtOutputMaturity for output"<< cert.GetVout().size()-1 <<" at height "<< height << " is "<<int(bwtOutputMaturity);
    }

    //Test
    chainSettingUtils::ExtendChainActiveToHeight(certCreationHeight + walletCert.bwtMaturityDepth);
    changeOutputMaturity = walletCert.IsOutputMature(0);
    EXPECT_TRUE(changeOutputMaturity == CCoins::outputMaturity::MATURE)
        <<"changeOutputMaturity for output 0 at height "<< certCreationHeight + walletCert.bwtMaturityDepth
        << " is "<<int(changeOutputMaturity);

    bwtOutputMaturity = walletCert.IsOutputMature(cert.GetVout().size()-1);
    EXPECT_TRUE(bwtOutputMaturity == CCoins::outputMaturity::MATURE)
        <<"bwtOutputMaturity for output"<< cert.GetVout().size()-1 <<" at height "<< certCreationHeight + walletCert.bwtMaturityDepth << " is "<<int(bwtOutputMaturity);

    //Test no hysteresis
    chainSettingUtils::ExtendChainActiveToHeight(certCreationHeight + walletCert.bwtMaturityDepth - 1);
    changeOutputMaturity = walletCert.IsOutputMature(0);
    EXPECT_TRUE(changeOutputMaturity == CCoins::outputMaturity::MATURE)
        <<"changeOutputMaturity for output 0 at height "<< certCreationHeight + walletCert.bwtMaturityDepth
        << " is "<<int(changeOutputMaturity);

    bwtOutputMaturity = walletCert.IsOutputMature(cert.GetVout().size()-1);
    EXPECT_TRUE(bwtOutputMaturity == CCoins::outputMaturity::IMMATURE)
        <<"bwtOutputMaturity for output"<< cert.GetVout().size()-1 <<" at height "<< certCreationHeight + walletCert.bwtMaturityDepth << " is "<<int(bwtOutputMaturity);
}

TEST_F(CertInWalletTest, IsOutputMature_TransparentTx_InMemoryPool) {
    //Create a transparent tx
    CTransaction transparentTx = txCreationUtils::createTransparentTx();

    //Add block information
    int txCreationHeight = 100;
    chainSettingUtils::ExtendChainActiveToHeight(txCreationHeight);

    CTxMemPoolEntry mempoolEntry(transparentTx, /*fee*/CAmount(0), int64_t(0), double(0.0), int(0), false);
    mempool.addUnchecked(transparentTx.GetHash(), mempoolEntry , /*fCurrentEstimate*/false);

    CWalletTx walletTx(pWallet, transparentTx);
    walletTx.hashBlock.SetNull();
    walletTx.nIndex = -1;

    CCoins::outputMaturity txMaturity = CCoins::outputMaturity::NOT_APPLICABLE;

    //Test
    txMaturity = walletTx.IsOutputMature(0);
    EXPECT_TRUE(txMaturity == CCoins::outputMaturity::MATURE)
        <<"txMaturity is "<<int(txMaturity);
}

TEST_F(CertInWalletTest, IsOutputMature_Certificate_InMemoryPool) {
    //Create certificate
    CScCertificate cert = txCreationUtils::createCertificate(uint256S("aaa"), /*epochNum*/12,
            /*endEpochBlockHash*/uint256S("ccc"), /*numChangeOut*/2, /*bwTotaltAmount*/CAmount(10), /*numBwt*/4);

    //Add block information
    int txCreationHeight = 100;
    chainSettingUtils::ExtendChainActiveToHeight(txCreationHeight);

    CCertificateMemPoolEntry mempoolEntry(cert, /*fee*/CAmount(0), int64_t(0), double(0.0), int(0));
    mempool.addUnchecked(cert.GetHash(), mempoolEntry , /*fCurrentEstimate*/false);

    CWalletCert walletCert(pWallet, cert);
    walletCert.hashBlock.SetNull();
    walletCert.nIndex = -1;

    CCoins::outputMaturity changeOutputMaturity = CCoins::outputMaturity::NOT_APPLICABLE;
    CCoins::outputMaturity bwtOutputMaturity    = CCoins::outputMaturity::NOT_APPLICABLE;

    //Test
    changeOutputMaturity = walletCert.IsOutputMature(0);
    EXPECT_TRUE(changeOutputMaturity == CCoins::outputMaturity::MATURE)
        <<"txMaturity is "<<int(changeOutputMaturity);

    bwtOutputMaturity = walletCert.IsOutputMature(cert.GetVout().size()-1);
    EXPECT_TRUE(bwtOutputMaturity == CCoins::outputMaturity::IMMATURE)
        <<"txMaturity is "<<int(bwtOutputMaturity);
}

TEST_F(CertInWalletTest, IsOutputMature_TransparentTx_Conflicted) {
    //Create a transparent tx
    CTransaction transparentTx = txCreationUtils::createTransparentTx();

    //Add block information
    int txCreationHeight = 100;
    chainSettingUtils::ExtendChainActiveToHeight(txCreationHeight);

    CWalletTx walletTx(pWallet, transparentTx);
    walletTx.hashBlock.SetNull();
    walletTx.nIndex = -1;

    CCoins::outputMaturity txMaturity = CCoins::outputMaturity::NOT_APPLICABLE;

    //Test
    txMaturity = walletTx.IsOutputMature(0);
    EXPECT_TRUE(txMaturity == CCoins::outputMaturity::NOT_APPLICABLE)
        <<"txMaturity is "<<int(txMaturity);
}

TEST_F(CertInWalletTest, IsOutputMature_CoinBase_Conflicted) {
    //Create coinbase
    CAmount coinBaseAmount = 10;
    CTransaction coinBase = txCreationUtils::createCoinBase(coinBaseAmount);

    //Add block information
    int coinBaseCreationHeight = 100;
    chainSettingUtils::ExtendChainActiveToHeight(coinBaseCreationHeight);
    CWalletTx walletCoinBase(pWallet, coinBase);
    walletCoinBase.hashBlock.SetNull();
    walletCoinBase.nIndex = -1;

    //Test
    CCoins::outputMaturity coinBaseMaturity = CCoins::outputMaturity::NOT_APPLICABLE;
    coinBaseMaturity = walletCoinBase.IsOutputMature(0);
    EXPECT_TRUE(coinBaseMaturity == CCoins::outputMaturity::NOT_APPLICABLE)
        <<"coinBaseMaturity is "<<int(coinBaseMaturity);
}

TEST_F(CertInWalletTest, IsOutputMature_Certificate_Conflicted) {
    //Create certificate
    CScCertificate cert = txCreationUtils::createCertificate(uint256S("aaa"), /*epochNum*/12,
            /*endEpochBlockHash*/uint256S("ccc"), /*numChangeOut*/2, /*bwTotaltAmount*/CAmount(10), /*numBwt*/4);

    //Add block information
    int txCreationHeight = 100;
    chainSettingUtils::ExtendChainActiveToHeight(txCreationHeight);

    CWalletCert walletCert(pWallet, cert);
    walletCert.hashBlock.SetNull();
    walletCert.nIndex = -1;

    CCoins::outputMaturity changeOutputMaturity = CCoins::outputMaturity::NOT_APPLICABLE;
    CCoins::outputMaturity bwtOutputMaturity    = CCoins::outputMaturity::NOT_APPLICABLE;

    //Test
    changeOutputMaturity = walletCert.IsOutputMature(0);
    EXPECT_TRUE(changeOutputMaturity == CCoins::outputMaturity::NOT_APPLICABLE)
        <<"txMaturity is "<<int(changeOutputMaturity);

    bwtOutputMaturity = walletCert.IsOutputMature(cert.GetVout().size()-1);
    EXPECT_TRUE(bwtOutputMaturity == CCoins::outputMaturity::NOT_APPLICABLE)
        <<"txMaturity is "<<int(bwtOutputMaturity);
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////////////// GetCredit //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CertInWalletTest, GetCredit_CoinBase)
{
    //Create coinbase
    CAmount coinBaseAmount = 10;
    CMutableTransaction mutCoinBase = txCreationUtils::createCoinBase(coinBaseAmount);

    CKey coinsKey;
    coinsKey.MakeNewKey(true);
    pWallet->AddKey(coinsKey);
    CScript lockingScript = CScript() << OP_DUP << OP_HASH160 << ToByteVector(coinsKey.GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    mutCoinBase.getOut(0).scriptPubKey = lockingScript;
    CTransaction coinBase = mutCoinBase;

    //Add block information
    chainSettingUtils::ExtendChainActiveToHeight(/*startHeight*/100);
    CWalletTx walletCoinBase(pWallet, coinBase);
    CBlock coinBaseBlock;
    coinBaseBlock.vtx.push_back(coinBase);
    walletCoinBase.hashBlock = coinBaseBlock.GetHash();
    walletCoinBase.SetMerkleBranch(coinBaseBlock);
    walletCoinBase.fMerkleVerified = true; //shortcut

    chainSettingUtils::ExtendChainActiveWithBlock(coinBaseBlock);
    int coinBaseCreationHeight = chainActive.Height();

    CAmount coinBaseCredit =-1;

    //Test
    for(int height = coinBaseCreationHeight+1; height < coinBaseCreationHeight + COINBASE_MATURITY; ++height)
    {
        chainSettingUtils::ExtendChainActiveToHeight(height);
        coinBaseCredit = walletCoinBase.GetCredit(ISMINE_SPENDABLE);
        EXPECT_TRUE(CAmount(0) == coinBaseCredit)
            <<"coinBaseCredit at height "<< height << " is "<<coinBaseCredit;
    }

    //Test
    chainSettingUtils::ExtendChainActiveToHeight(coinBaseCreationHeight + COINBASE_MATURITY);
    coinBaseCredit = walletCoinBase.GetCredit(ISMINE_SPENDABLE);
    EXPECT_TRUE(coinBaseAmount == coinBaseCredit)
        <<"coinBaseCredit is "<<int(coinBaseCredit);

    //Test no hysteresis
    for(int height = coinBaseCreationHeight + COINBASE_MATURITY-1; height >= coinBaseCreationHeight; --height)
    {
        chainSettingUtils::ExtendChainActiveToHeight(height);
        coinBaseCredit = walletCoinBase.GetCredit(ISMINE_SPENDABLE);
        EXPECT_TRUE(CAmount(0) == coinBaseCredit)
            <<"coinBaseCredit at height "<< height <<" is "<<int(coinBaseCredit);
    }
}
