#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <chainparams.h>
#include <util.h>

#include <txdb.h>
#include <main.h>
#include <zen/forks/fork7_sidechainfork.h>

#include <key.h>
#include <keystore.h>
#include <script/sign.h>

#include "tx_creation_utils.h"
#include <consensus/validation.h>

#include <sc/sidechain.h>
#include <txmempool.h>
#include <init.h>
#include <undo.h>

class CCoinsOnlyViewDB : public CCoinsViewDB
{
public:
    CCoinsOnlyViewDB(size_t nCacheSize, bool fWipe = false)
        : CCoinsViewDB(nCacheSize, false, fWipe) {}

    bool BatchWrite(CCoinsMap &mapCoins)
    {
        const uint256 hashBlock;
        const uint256 hashAnchor;
        CAnchorsMap mapAnchors;
        CNullifiersMap mapNullifiers;
        CSidechainsMap mapSidechains;
        CSidechainEventsMap mapSidechainEvents;

        return CCoinsViewDB::BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapSidechains, mapSidechainEvents);
    }
};

class SidechainsInMempoolTestSuite: public ::testing::Test {
public:
    SidechainsInMempoolTestSuite():
        pathTemp(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path()),
        chainStateDbSize(2 * 1024 * 1024),
        pChainStateDb(nullptr),
        minimalHeightForSidechains(SidechainFork().getHeight(CBaseChainParams::REGTEST))
    {
        SelectParams(CBaseChainParams::REGTEST);

        boost::filesystem::create_directories(pathTemp);
        mapArgs["-datadir"] = pathTemp.string();

        pChainStateDb = new CCoinsOnlyViewDB(chainStateDbSize,/*fWipe*/true);
        pcoinsTip     = new CCoinsViewCache(pChainStateDb);
    }

    void SetUp() override {
        chainSettingUtils::ExtendChainActiveToHeight(minimalHeightForSidechains);
        pcoinsTip->SetBestBlock(chainActive.Tip()->GetBlockHash());
        pindexBestHeader = chainActive.Tip();

        InitCoinGeneration();
    }

    void TearDown() override {
        mempool.clear();
        chainActive.SetTip(nullptr);
        mapBlockIndex.clear();
    }

    ~SidechainsInMempoolTestSuite() {
        delete pcoinsTip;
        pcoinsTip = nullptr;

        delete pChainStateDb;
        pChainStateDb = nullptr;

        ClearDatadirCache();
        boost::system::error_code ec;
        boost::filesystem::remove_all(pathTemp.string(), ec);
    }

protected:
    CTransaction GenerateScTx(const CAmount & creationTxAmount);
    CTransaction GenerateFwdTransferTx(const uint256 & newScId, const CAmount & fwdTxAmount);

private:
    boost::filesystem::path  pathTemp;
    const unsigned int       chainStateDbSize;
    CCoinsOnlyViewDB*        pChainStateDb;

    const unsigned int       minimalHeightForSidechains;

    CKey                     coinsKey;
    CBasicKeyStore           keystore;
    CScript                  coinsScript;

    void InitCoinGeneration();
    std::pair<uint256, CCoinsCacheEntry> GenerateCoinsAmount(const CAmount & amountToGenerate);
    bool StoreCoins(const std::pair<uint256, CCoinsCacheEntry>& entryToStore);
};

TEST_F(SidechainsInMempoolTestSuite, NewSidechainsAreAcceptedToMempool) {
    CTransaction scTx = GenerateScTx(CAmount(1));
    CValidationState txState;
    bool missingInputs = false;

    EXPECT_TRUE(AcceptTxToMemoryPool(mempool, txState, scTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToConfirmedSideChainsAreAllowed) {
    int creationHeight = 1789;
    chainSettingUtils::ExtendChainActiveToHeight(creationHeight);

    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);

    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, creationHeight);
    sidechainsView.SetBestBlock(pcoinsTip->GetBestBlock()); //do not alter BestBlock, as set in test fixture
    sidechainsView.Flush();

    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;
    bool missingInputs = false;

    EXPECT_TRUE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, false, &missingInputs));
}

//A proof that https://github.com/ZencashOfficial/zen/issues/215 is solved
TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToSideChainsInMempoolAreAllowed) {
    CTransaction scTx = GenerateScTx(CAmount(1));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CValidationState scTxState;
    bool missingInputs = false;
    AcceptTxToMemoryPool(mempool, scTxState, scTx, false, &missingInputs);
    ASSERT_TRUE(mempool.hasSidechainCreationTx(scId));

    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;
    EXPECT_TRUE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToUnknownSideChainAreNotAllowed) {
    uint256 scId = uint256S("dddd");
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;
    bool missingInputs = false;

    EXPECT_FALSE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, hasSidechainCreationTxTest) {
    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("1492");

    //Case 1: no sidechain related tx in mempool
    bool res = aMempool.hasSidechainCreationTx(scId);
    EXPECT_FALSE(res);

    bool loopRes = false;
    for(const auto& tx : aMempool.mapTx)
        for(const auto& sc: tx.second.GetTx().GetVscCcOut())
            if(sc.GetScId() == scId) {
                loopRes = true;
                break;
        }
    EXPECT_TRUE(loopRes == res);

    //Case 2: fwd transfer tx only in mempool
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdPoolEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdPoolEntry.GetTx().GetHash(), fwdPoolEntry);
    res = aMempool.hasSidechainCreationTx(scId);
    EXPECT_FALSE(res);

    loopRes = false;
    for(const auto& tx : aMempool.mapTx)
        for(const auto& sc: tx.second.GetTx().GetVscCcOut())
            if(sc.GetScId() == scId) {
                loopRes = true;
                break;
        }
    EXPECT_TRUE(loopRes == res);

    //Case 3: sc creation tx in mempool
    CTransaction scTx  = GenerateScTx(CAmount(10));
    const uint256& scIdOk = scTx.GetScIdFromScCcOut(0);
    CTxMemPoolEntry scPoolEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scPoolEntry.GetTx().GetHash(), scPoolEntry);
    res = aMempool.hasSidechainCreationTx(scIdOk);
    EXPECT_TRUE(res);

    loopRes = false;
    for(const auto& tx : aMempool.mapTx)
        for(const auto& sc: tx.second.GetTx().GetVscCcOut())
            if(sc.GetScId() == scIdOk) {
                loopRes = true;
                break;
        }
    EXPECT_TRUE(loopRes == res);
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsInMempool_ScNonRecursiveRemoval) {
    // Associated scenario: Sidechain creation and some fwds are in mempool.
    // Sc Creation is confirmed, hence it has to be removed from mempool, while fwds stay.

    CTxMemPool aMempool(::minRelayTxFee);
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CTxMemPoolEntry scEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scTx.GetHash(), scEntry);
    ASSERT_TRUE(aMempool.hasSidechainCreationTx(scId));

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(scTx, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), scTx));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, FwdsOnlyInMempool_FwdNonRecursiveRemoval) {
    // Associated scenario: fws are in mempool, hence scCreation must be already confirmed
    // A fwd is confirmed hence it, and only it, is removed from mempool

    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("ababab");

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(fwdTx1, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsInMempool_ScRecursiveRemoval) {
    // Associated scenario: Sidechain creation and some fwds are in mempool, e.g. as a result of previous blocks disconnections
    // One of the new blocks about to me mounted double spends the original fwdTx, hence scCreation is marked for recursive removal by removeForConflicts
    // both scCreation and fwds must be cleared from mempool

    CTxMemPool aMempool(::minRelayTxFee);
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CTxMemPoolEntry scEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scTx.GetHash(), scEntry);
    ASSERT_TRUE(aMempool.hasSidechainCreationTx(scId));

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(scTx, removedTxs, removedCerts, /*fRecursive*/true);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), scTx));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, FwdsOnlyInMempool_ScRecursiveRemoval) {
    // Associated scenario: upon block disconnections fwds have entered into mempool.
    // While unmounting block containing scCreation, scCreation cannot make to mempool. fwds must me purged

    CTxMemPool aMempool(::minRelayTxFee);
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(scTx, removedTxs, removedCerts, /*fRecursive*/true);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsInMempool_FwdRecursiveRemoval) {
    // Associated scenario: upon block disconnections a fwd cannot make to mempool.
    // Recursive removal for refused fwd is called, but other fwds are unaffected

    CTxMemPool aMempool(::minRelayTxFee);
    uint256 scId = uint256S("1492");

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(fwdTx2, removedTxs, removedCerts, /*fRecursive*/true);

    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, SimpleCertRemovalFromMempool) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load certificate in mempool
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNum*/0, /*endEpochBlockHash*/ uint256(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(6), /*numBwt*/2);
    CCertificateMemPoolEntry certEntry(cert, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cert.GetHash(), certEntry);

    //Remove the certificate
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.remove(cert, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(removedTxs.size() == 0);
    EXPECT_TRUE(std::count(removedCerts.begin(), removedCerts.end(), cert));
    EXPECT_FALSE(mempool.existsCert(cert.GetHash()));
}

TEST_F(SidechainsInMempoolTestSuite, ConflictingCertRemovalFromMempool) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load a certificate in mempool
    CScCertificate cert1 = txCreationUtils::createCertificate(scId, /*epochNum*/0, /*endEpochBlockHash*/ uint256(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(6), /*numBwt*/2);
    CCertificateMemPoolEntry certEntry1(cert1, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cert1.GetHash(), certEntry1);

    //Remove the certificate
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    CScCertificate cert2 = txCreationUtils::createCertificate(scId, /*epochNum*/0, /*endEpochBlockHash*/ uint256(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(0), /*numBwt*/2);
    mempool.removeConflicts(cert2, removedTxs, removedCerts);

    EXPECT_TRUE(removedTxs.size() == 0);
    EXPECT_TRUE(std::count(removedCerts.begin(), removedCerts.end(), cert1));
    EXPECT_FALSE(mempool.existsCert(cert1.GetHash()));
}

TEST_F(SidechainsInMempoolTestSuite, FwdsAndCertInMempool_CertRemovalDoesNotAffectFwt) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load a fwt in mempool
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(fwdTx.GetHash(), fwdEntry);

    //load a certificate in mempool
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNum*/0, /*endEpochBlockHash*/ uint256(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(2), /*numBwt*/2);
    CCertificateMemPoolEntry certEntry1(cert, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cert.GetHash(), certEntry1);

    //Remove the certificate
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.remove(cert, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedCerts.begin(), removedCerts.end(), cert));
    EXPECT_FALSE(mempool.existsCert(cert.GetHash()));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx));
    EXPECT_TRUE(mempool.existsTx(fwdTx.GetHash()));
    ASSERT_TRUE(mempool.mapSidechains.count(scId));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).fwdTransfersSet.count(fwdTx.GetHash()));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).backwardCertificate.IsNull());
}

TEST_F(SidechainsInMempoolTestSuite, FwdsAndCertInMempool_FwtRemovalDoesNotAffectCert) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load a fwd in mempool
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(fwdTx.GetHash(), fwdEntry);

    //load a certificate in mempool
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNum*/0, /*endEpochBlockHash*/ uint256(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(2), /*numBwt*/2);
    CCertificateMemPoolEntry certEntry1(cert, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cert.GetHash(), certEntry1);

    //Remove the certificate
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.remove(fwdTx, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx));
    EXPECT_FALSE(mempool.existsTx(fwdTx.GetHash()));
    EXPECT_FALSE(std::count(removedCerts.begin(), removedCerts.end(), cert));
    EXPECT_TRUE(mempool.existsCert(cert.GetHash()));
    ASSERT_TRUE(mempool.mapSidechains.count(scId));
    EXPECT_FALSE(mempool.mapSidechains.at(scId).fwdTransfersSet.count(fwdTx.GetHash()));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).backwardCertificate == cert.GetHash());
}

TEST_F(SidechainsInMempoolTestSuite, ImmatureExpenditureRemoval) {
    //Create and mature coinbase
    CMutableTransaction mutCoinBase;
    mutCoinBase.vin.push_back(CTxIn(uint256(), -1));
    mutCoinBase.addOut(CTxOut(10,CScript()));
    mutCoinBase.addOut(CTxOut(20,CScript()));
    CTransaction coinBase(mutCoinBase);
    CTxUndo dummyUndo;
    UpdateCoins(coinBase, *pcoinsTip, dummyUndo, chainActive.Height());

    EXPECT_FALSE(pcoinsTip->AccessCoins(coinBase.GetHash())->isOutputMature(0, chainActive.Height()));
    // mature the coinbase
    chainSettingUtils::ExtendChainActiveToHeight(chainActive.Height() + COINBASE_MATURITY);
    EXPECT_TRUE(pcoinsTip->AccessCoins(coinBase.GetHash())->isOutputMature(0, chainActive.Height()));

    //add to mempool txes spending coinbase
    CMutableTransaction mutTx;
    mutTx.vin.push_back(CTxIn(COutPoint(coinBase.GetHash(), 0), CScript(), -1));
    CTransaction mempoolTx1(mutTx);
    CTxMemPoolEntry mempoolEntry1(mempoolTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(mempoolTx1.GetHash(), mempoolEntry1);
    EXPECT_TRUE(mempool.exists(mempoolTx1.GetHash()));

    mutTx.vin.clear();
    mutTx.vin.push_back(CTxIn(COutPoint(coinBase.GetHash(), 1), CScript(), -1));
    CTransaction mempoolTx2(mutTx);
    CTxMemPoolEntry mempoolEntry2(mempoolTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(mempoolTx2.GetHash(), mempoolEntry2);
    EXPECT_TRUE(mempool.exists(mempoolTx2.GetHash()));

    //Revert chain undoing coinbase maturity, and check mempool cleanup
    chainSettingUtils::ExtendChainActiveToHeight(chainActive.Height() -1);
    // check coinbase is not mature anymore
    EXPECT_FALSE(pcoinsTip->AccessCoins(coinBase.GetHash())->isOutputMature(0, chainActive.Height()));

    //test
    mempool.removeImmatureExpenditures(pcoinsTip, chainActive.Height());

    //Check
    EXPECT_FALSE(mempool.exists(mempoolTx1.GetHash()));
    EXPECT_FALSE(mempool.exists(mempoolTx2.GetHash()));
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////// Test Fixture definitions ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
void SidechainsInMempoolTestSuite::InitCoinGeneration() {
    coinsKey.MakeNewKey(true);
    keystore.AddKey(coinsKey);

    coinsScript << OP_DUP << OP_HASH160 << ToByteVector(coinsKey.GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
}

std::pair<uint256, CCoinsCacheEntry> SidechainsInMempoolTestSuite::GenerateCoinsAmount(const CAmount & amountToGenerate) {
    static unsigned int hashSeed = 1987;
    CCoinsCacheEntry entry;
    entry.flags = CCoinsCacheEntry::FRESH | CCoinsCacheEntry::DIRTY;

    entry.coins.fCoinBase = false;
    entry.coins.nVersion = TRANSPARENT_TX_VERSION;
    entry.coins.nHeight = minimalHeightForSidechains;

    entry.coins.vout.resize(1);
    entry.coins.vout[0].nValue = amountToGenerate;
    entry.coins.vout[0].scriptPubKey = coinsScript;

    std::stringstream num;
    num << std::hex << ++hashSeed;

    return std::pair<uint256, CCoinsCacheEntry>(uint256S(num.str()), entry);
}

bool SidechainsInMempoolTestSuite::StoreCoins(const std::pair<uint256, CCoinsCacheEntry>& entryToStore) {
    CCoinsViewCache view(pcoinsTip);
    CCoinsMap tmpCoinsMap;
    tmpCoinsMap[entryToStore.first] = entryToStore.second;

    const uint256 hashBlock = pcoinsTip->GetBestBlock(); //keep same best block as set in Fixture setup
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;
    CSidechainsMap mapSidechains;
    CSidechainEventsMap mapCeasingScs;

    pcoinsTip->BatchWrite(tmpCoinsMap, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapSidechains, mapCeasingScs);

    return view.HaveCoins(entryToStore.first) == true;
}

CTransaction SidechainsInMempoolTestSuite::GenerateScTx(const CAmount & creationTxAmount) {
    std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(1000);
    StoreCoins(coinData);

    CMutableTransaction scTx;
    scTx.nVersion = SC_TX_VERSION;
    scTx.vin.resize(1);
    scTx.vin[0].prevout = COutPoint(coinData.first, 0);

    scTx.vsc_ccout.resize(1);
    scTx.vsc_ccout[0].nValue = creationTxAmount;
    scTx.vsc_ccout[0].withdrawalEpochLength = getScMinWithdrawalEpochLength();

    SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, scTx, 0);

    return scTx;
}

CTransaction SidechainsInMempoolTestSuite::GenerateFwdTransferTx(const uint256 & newScId, const CAmount & fwdTxAmount) {
    std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(1000);
    StoreCoins(coinData);

    CMutableTransaction scTx;
    scTx.nVersion = SC_TX_VERSION;
    scTx.vin.resize(1);
    scTx.vin[0].prevout = COutPoint(coinData.first, 0);

    scTx.vft_ccout.resize(1);
    scTx.vft_ccout[0].scId   = newScId;
    scTx.vft_ccout[0].nValue = fwdTxAmount;

    //scTx.vft_ccout.resize(2); //testing double deletes 
    //scTx.vft_ccout[1].scId   = newScId;
    //scTx.vft_ccout[1].nValue = fwdTxAmount;

    SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, scTx, 0);

    return scTx;
}
