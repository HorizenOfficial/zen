#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <chainparams.h>
#include <util.h>

#include <txdb.h>
#include <main.h>
#include <zen/forks/fork8_sidechainfork.h>

#include <key.h>
#include <keystore.h>
#include <script/sign.h>

#include "tx_creation_utils.h"
#include <gtest/libzendoo_test_files.h>
#include <consensus/validation.h>

#include <sc/sidechain.h>
#include <txmempool.h>
#include <init.h>
#include <undo.h>
#include <gtest/libzendoo_test_files.h>

using namespace txCreationUtils;

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
        CCswNullifiersMap cswNullifiers;

        return CCoinsViewDB::BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapSidechains, mapSidechainEvents, cswNullifiers);
    }
};

class SidechainsInMempoolTestSuite: public ::testing::Test {
public:
    SidechainsInMempoolTestSuite():
        aMempool(::minRelayTxFee),
        pathTemp(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path()),
        chainStateDbSize(2 * 1024 * 1024),
        pChainStateDb(nullptr),
        minimalHeightForSidechains(SidechainFork().getHeight(CBaseChainParams::REGTEST)),
        csMainLock(cs_main, "cs_main", __FILE__, __LINE__),
        csAMempool(aMempool.cs, "cs_AMempool", __FILE__, __LINE__)
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
    CTxMemPool aMempool;
    CTransaction GenerateScTx(const CAmount & creationTxAmount, int epochLenght = -1, bool ceasedVkDefined = true);
    CTransaction GenerateFwdTransferTx(const uint256 & newScId, const CAmount & fwdTxAmount);
    CTransaction GenerateBtrTx(const uint256 & scId, const CAmount & mbtrFee = CAmount(1));
    CTxCeasedSidechainWithdrawalInput GenerateCSWInput(const uint256& scId,
        const std::string& nullifierHex, const std::string& actCertDataHex, const std::string& ceasingCumScTxCommTreeHex,
        CAmount amount, CFieldElement* nullifierPtrIn = nullptr);
    CTransaction GenerateCSWTx(const std::vector<CTxCeasedSidechainWithdrawalInput>& csws);
    CTransaction GenerateCSWTx(const CTxCeasedSidechainWithdrawalInput& csw);

    CScCertificate GenerateCertificate(
        const uint256 & scId, int epochNum,
        const CFieldElement& endEpochCumScTxCommTreeRoot, CAmount inputAmount, CAmount changeTotalAmount/* = 0*/, unsigned int numChangeOut/* = 0*/,
        CAmount bwtTotalAmount/* = 1*/, unsigned int numBwt/* = 1*/,
        CAmount ftScFee, CAmount mbtrScFee, int64_t quality,
        const CTransactionBase* inputTxBase = nullptr);

    void storeSidechainWithCurrentHeight(CNakedCCoinsViewCache& view, const uint256& scId, const CSidechain& sidechain, int chainActiveHeight);
    uint256 createAndStoreSidechain(CAmount ftScFee = CAmount(0), CAmount mbtrScFee = CAmount(0), size_t mbtrScDataLength = 0, int epochLength = 2);
    void moveSidechainToNextEpoch(uint256 scId, CCoinsViewCache& sidechainView);

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
    bool StoreCoins(std::pair<uint256, CCoinsCacheEntry> entryToStore);

    //Critical sections below needed when compiled with --enable-debug, which activates ASSERT_HELD
    CCriticalBlock csMainLock;
    CCriticalBlock csAMempool;
};

TEST_F(SidechainsInMempoolTestSuite, NewSidechainIsAcceptedToMempool) {
    CTransaction scTx = GenerateScTx(CAmount(1));
    CValidationState txState;

    MempoolReturnValue res = AcceptTxToMemoryPool(mempool, txState, scTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                                  MempoolProofVerificationFlag::SYNC);
    EXPECT_TRUE(res == MempoolReturnValue::VALID);
}

TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToUnknownSidechainAreNotAllowed) {
    uint256 scId = uint256S("dddd");
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;

    MempoolReturnValue res = AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                                  MempoolProofVerificationFlag::SYNC);
    EXPECT_TRUE(res == MempoolReturnValue::INVALID);
}

//A proof that https://github.com/HorizenOfficial/zen/issues/215 is solved
TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToUnconfirmedSidechainsAreAllowed) {
    CTransaction scTx = GenerateScTx(CAmount(1));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CValidationState scTxState;

    MempoolReturnValue res = AcceptTxToMemoryPool(mempool, scTxState, scTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                                  MempoolProofVerificationFlag::SYNC);
    EXPECT_TRUE(res == MempoolReturnValue::VALID);
    ASSERT_TRUE(mempool.hasSidechainCreationTx(scId));

    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;
    res = AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                               MempoolProofVerificationFlag::SYNC);
    EXPECT_TRUE(res == MempoolReturnValue::VALID);
}

TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToConfirmedSidechainsAreAllowed) {
    int creationHeight = 1789;
    chainSettingUtils::ExtendChainActiveToHeight(creationHeight);

    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);

    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateSidechain(scTx, aBlock, creationHeight);
    sidechainsView.SetBestBlock(chainActive.Tip()->GetBlockHash());
    sidechainsView.Flush();

    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;

    MempoolReturnValue res = AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                                  MempoolProofVerificationFlag::SYNC);
    EXPECT_TRUE(res == MempoolReturnValue::VALID);
}

TEST_F(SidechainsInMempoolTestSuite, BtrToUnknownSidechainAreNotAllowed) {
    uint256 scId = uint256S("dddd");
    CTransaction btrTx = GenerateBtrTx(scId);
    CValidationState btrTxState;

    MempoolReturnValue res = AcceptTxToMemoryPool(mempool, btrTxState, btrTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                                  MempoolProofVerificationFlag::SYNC);
    EXPECT_TRUE(res == MempoolReturnValue::INVALID);
}

TEST_F(SidechainsInMempoolTestSuite, BtrToUnconfirmedSidechainsAreAllowed) {
    CTransaction scTx = GenerateScTx(CAmount(1));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CValidationState scTxState;

    MempoolReturnValue res = AcceptTxToMemoryPool(mempool, scTxState, scTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                                  MempoolProofVerificationFlag::SYNC);
    ASSERT_TRUE(res == MempoolReturnValue::VALID);
    ASSERT_TRUE(mempool.hasSidechainCreationTx(scId));

    CTransaction btrTx = GenerateBtrTx(scId);
    CValidationState btrTxState;
    res = AcceptTxToMemoryPool(mempool, btrTxState, btrTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                               MempoolProofVerificationFlag::SYNC);
    ASSERT_TRUE(res == MempoolReturnValue::VALID);
}

TEST_F(SidechainsInMempoolTestSuite, BtrToConfirmedSidechainsAreAllowed) {
    int creationHeight = 1789;
    chainSettingUtils::ExtendChainActiveToHeight(creationHeight);

    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);

    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateSidechain(scTx, aBlock, creationHeight);
    sidechainsView.SetBestBlock(chainActive.Tip()->GetBlockHash());
    sidechainsView.Flush();

    CTransaction btrTx = GenerateBtrTx(scId);
    CValidationState btrTxState;

    MempoolReturnValue res = AcceptTxToMemoryPool(mempool, btrTxState, btrTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                                  MempoolProofVerificationFlag::SYNC);
    EXPECT_TRUE(res == MempoolReturnValue::VALID);
}

TEST_F(SidechainsInMempoolTestSuite, hasSidechainCreationTxTest) {
    uint256 scId = uint256S("1492");

    //Case 1: no sidechain related tx in mempool
    bool res = aMempool.hasSidechainCreationTx(scId);
    EXPECT_FALSE(res);

    //Case 2: fwd transfer tx only in mempool
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdPoolEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdPoolEntry.GetTx().GetHash(), fwdPoolEntry);
    res = aMempool.hasSidechainCreationTx(scId);
    EXPECT_FALSE(res);

    //Case 3: btr tx only in mempool
    CTransaction btrTx = GenerateBtrTx(scId);
    CTxMemPoolEntry btrTxEntry(btrTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(btrTxEntry.GetTx().GetHash(), btrTxEntry, /*fCurrentEstimate*/true);
    res = aMempool.hasSidechainCreationTx(scId);
    EXPECT_FALSE(res);

    //Case 4: sc creation tx in mempool
    CTransaction scTx  = GenerateScTx(CAmount(10));
    const uint256& scIdOk = scTx.GetScIdFromScCcOut(0);
    CTxMemPoolEntry scPoolEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scPoolEntry.GetTx().GetHash(), scPoolEntry);
    res = aMempool.hasSidechainCreationTx(scIdOk);
    EXPECT_TRUE(res);
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsAndBtrInMempool_ScNonRecursiveRemoval) {
    // Associated scenario: Sidechain creation and some fwds and btr are in mempool.
    // Sc Creation is confirmed, hence it has to be removed from mempool, while fwds stay.

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

    CTransaction btrTx = GenerateBtrTx(scId);
    CTxMemPoolEntry btrEntry(btrTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);

    aMempool.addUnchecked(btrTx.GetHash(), btrEntry, /*fCurrentEstimate*/true);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(scTx, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), scTx));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), btrTx));
}

TEST_F(SidechainsInMempoolTestSuite, FwdsAndBtrsOnlyInMempool_FwdNonRecursiveRemoval) {
    // Associated scenario: fwts and btr are in mempool, hence scCreation must be already confirmed
    // A fwd is confirmed hence it, and only it, is removed from mempool
    uint256 scId = uint256S("ababab");

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    CTransaction btrTx = GenerateBtrTx(scId);
    CTxMemPoolEntry btrEntry(btrTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(btrTx.GetHash(), btrEntry, /*fCurrentEstimate*/true);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(fwdTx1, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), btrTx));
}

TEST_F(SidechainsInMempoolTestSuite, FwdsAndBtrsOnlyInMempool_BtrNonRecursiveRemoval) {
    // Associated scenario: fws and btr are in mempool, hence scCreation must be already confirmed
    // A fwd is confirmed hence it, and only it, is removed from mempool

    uint256 scId = uint256S("ababab");

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    CTransaction btrTx = GenerateBtrTx(scId);
    CTxMemPoolEntry btrEntry(btrTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(btrTx.GetHash(), btrEntry, /*fCurrentEstimate*/true);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(btrTx, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), btrTx));
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsAndBtrInMempool_ScRecursiveRemoval) {
    // Associated scenario: Sidechain creation and some fwds/btr are in mempool, e.g. as a result of previous blocks disconnections
    // One of the new blocks about to me mounted double spends the original scTx, hence scCreation is marked for recursive removal by removeForConflicts
    // both scCreation and fwds must be cleared from mempool

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

    CTransaction btrTx = GenerateBtrTx(scId);
    CTxMemPoolEntry btrEntry(btrTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(btrTx.GetHash(), btrEntry, /*fCurrentEstimate*/true);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(scTx, removedTxs, removedCerts, /*fRecursive*/true);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), scTx));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), btrTx));
}

TEST_F(SidechainsInMempoolTestSuite, FwdsAndBtrOnlyInMempool_ScRecursiveRemoval) {
    // Associated scenario: upon block disconnections fwds and btr have entered into mempool.
    // While unmounting block containing scCreation, scCreation cannot make to mempool. fwds and btr must me purged
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    CTransaction btrTx = GenerateBtrTx(scId);
    CTxMemPoolEntry btrEntry(btrTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(btrTx.GetHash(), btrEntry, /*fCurrentEstimate*/true);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(scTx, removedTxs, removedCerts, /*fRecursive*/true);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), btrTx));
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsAndBtrInMempool_FwdRecursiveRemoval) {
    // Associated scenario: upon block disconnections a fwd cannot make to mempool.
    // Recursive removal for refused fwd is called, but other fwds are unaffected

    uint256 scId = uint256S("1492");

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    CTransaction btrTx = GenerateBtrTx(scId);
    CTxMemPoolEntry btrEntry(btrTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(btrTx.GetHash(), btrEntry, /*fCurrentEstimate*/true);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(fwdTx2, removedTxs, removedCerts, /*fRecursive*/true);

    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), btrTx));
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsAndBtrInMempool_BtrRecursiveRemoval) {
    // Associated scenario: upon block disconnections a btr cannot make to mempool.
    // Recursive removal for refused btr is called, but other fwds are unaffected

    uint256 scId = uint256S("1492");

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    CTransaction btrTx = GenerateBtrTx(scId);
    CTxMemPoolEntry btrEntry(btrTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(btrTx.GetHash(), btrEntry, /*fCurrentEstimate*/true);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(btrTx, removedTxs, removedCerts, /*fRecursive*/true);

    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), btrTx));
}

TEST_F(SidechainsInMempoolTestSuite, SimpleCertRemovalFromMempool) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateSidechain(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load certificate in mempool
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNum*/0,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(6), /*numBwt*/2,
        /*ftScFee*/0, /*mbtrScFee*/0);
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
    sidechainsView.UpdateSidechain(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load a certificate in mempool
    CScCertificate cert1 = txCreationUtils::createCertificate(scId, /*epochNum*/0,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(6), /*numBwt*/2,
        /*ftScFee*/0, /*mbtrScFee*/0);
    CCertificateMemPoolEntry certEntry1(cert1, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cert1.GetHash(), certEntry1);

    //Remove the certificate
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    CScCertificate cert2 = txCreationUtils::createCertificate(scId, /*epochNum*/0,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(0), /*numBwt*/2,
        /*ftScFee*/0, /*mbtrScFee*/0);
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
    sidechainsView.UpdateSidechain(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load a fwt in mempool
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(fwdTx.GetHash(), fwdEntry);

    //load a certificate in mempool
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNum*/0,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(2), /*numBwt*/2,
        /*ftScFee*/0, /*mbtrScFee*/0);
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
    EXPECT_TRUE(mempool.mapSidechains.at(scId).fwdTxHashes.count(fwdTx.GetHash()));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).mBackwardCertificates.empty());
}

TEST_F(SidechainsInMempoolTestSuite, FwdsAndCertInMempool_FwtRemovalDoesNotAffectCert) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateSidechain(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load a fwd in mempool
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(fwdTx.GetHash(), fwdEntry);

    //load a certificate in mempool
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNum*/0,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(2), /*numBwt*/2,
        /*ftScFee*/0, /*mbtrScFee*/0);
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
    EXPECT_FALSE(mempool.mapSidechains.at(scId).fwdTxHashes.count(fwdTx.GetHash()));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).HasCert(cert.GetHash()));
}

TEST_F(SidechainsInMempoolTestSuite, CertCannotSpendSameQualityCertOutput)
{
    CNakedCCoinsViewCache sidechainsView(pcoinsTip);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    txCreationUtils::storeSidechain(sidechainsView.getSidechainMap(), scId, initialScState);

    int64_t certQuality = 10;
    CFieldElement dummyCumTree {SAMPLE_FIELD};
    CAmount dummyInputAmount{20};
    CAmount dummyNonZeroFee {10};
    CAmount dummyNonZeroChange = dummyInputAmount - dummyNonZeroFee;
    CAmount dummyBwtAmount {0};

    CScCertificate parentCert = GenerateCertificate(scId, /*epochNum*/0, dummyCumTree, /*inputAmount*/dummyInputAmount,
        /*changeTotalAmount*/dummyNonZeroChange,/*numChangeOut*/1, /*bwtAmount*/dummyBwtAmount, /*numBwt*/2,
        /*ftScFee*/0, /*mbtrScFee*/0, /*quality*/certQuality);

    CCertificateMemPoolEntry mempoolParentCertCertEntry(parentCert, /*fee*/dummyNonZeroFee, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(parentCert.GetHash(), mempoolParentCertCertEntry);
    ASSERT_TRUE(mempool.exists(parentCert.GetHash()));

    CScCertificate sameQualityChildCert = GenerateCertificate(scId, /*epochNum*/0, dummyCumTree, /*inputAmount*/dummyInputAmount,
        /*changeTotalAmount*/dummyNonZeroChange,/*numChangeOut*/1, /*bwtAmount*/dummyBwtAmount, /*numBwt*/2,
        /*ftScFee*/0, /*mbtrScFee*/0, /*quality*/certQuality, &parentCert);
    ASSERT_TRUE(sameQualityChildCert.GetHash() != parentCert.GetHash());

    //test
    EXPECT_FALSE(mempool.checkIncomingCertConflicts(sameQualityChildCert));
}

TEST_F(SidechainsInMempoolTestSuite, CertCannotSpendHigherQualityCertOutput)
{
    CNakedCCoinsViewCache sidechainsView(pcoinsTip);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    txCreationUtils::storeSidechain(sidechainsView.getSidechainMap(), scId, initialScState);

    int64_t topQuality = 10;
    CFieldElement dummyCumTree {SAMPLE_FIELD};
    CAmount dummyInputAmount{20};
    CAmount dummyNonZeroFee {10};
    CAmount dummyNonZeroChange = dummyInputAmount - dummyNonZeroFee;
    CAmount dummyBwtAmount {0};

    CScCertificate parentCert = GenerateCertificate(scId, /*epochNum*/0, dummyCumTree, /*inputAmount*/dummyInputAmount,
        /*changeTotalAmount*/dummyNonZeroChange,/*numChangeOut*/1, /*bwtAmount*/dummyBwtAmount, /*numBwt*/2,
        /*ftScFee*/CAmount(0), /*mbtrScFee*/CAmount(0), /*quality*/topQuality);

    CCertificateMemPoolEntry mempoolParentCertCertEntry(parentCert, /*fee*/dummyNonZeroFee, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(parentCert.GetHash(), mempoolParentCertCertEntry);
    ASSERT_TRUE(mempool.exists(parentCert.GetHash()));

    CScCertificate lowerQualityChildCert = GenerateCertificate(scId, /*epochNum*/0, dummyCumTree, /*inputAmount*/dummyInputAmount,
        /*changeTotalAmount*/dummyNonZeroChange,/*numChangeOut*/1, /*bwtAmount*/dummyBwtAmount, /*numBwt*/2,
        /*ftScFee*/CAmount(0), /*mbtrScFee*/CAmount(0), /*quality*/topQuality/2, &parentCert);

    //test
    EXPECT_FALSE(mempool.checkIncomingCertConflicts(lowerQualityChildCert));
}

TEST_F(SidechainsInMempoolTestSuite, CswInputsPerScLimit) {
    static const int SC_MAX_NUM_OF_CSW_INPUTS_IN_MEMPOOL = Params().ScMaxNumberOfCswInputsInMempool(); 

    uint256 scId1 = uint256S("aaa");

    uint256 lastTxHash;
    for (int i = 0; i < SC_MAX_NUM_OF_CSW_INPUTS_IN_MEMPOOL; i++)
    {
        // get a random nullifier fe
        CFieldElement nullifier{};
        blockchain_test_utils::RandomSidechainField(nullifier);

        CAmount cswTxCoins = 10 + i;
        CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId1, "aabb", "ccdd", "eeff", cswTxCoins, &nullifier);
        CTransaction cswTx1 = GenerateCSWTx(cswInput);
 
        CValidationState dummyState;
        EXPECT_TRUE(mempool.checkCswInputsPerScLimit(cswTx1));

        CTxMemPoolEntry cswEntry(cswTx1, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
        EXPECT_TRUE(mempool.addUnchecked(cswTx1.GetHash(), cswEntry));

        lastTxHash = cswTx1.GetHash();
    }

    EXPECT_TRUE(mempool.getNumOfCswInputs(scId1) == SC_MAX_NUM_OF_CSW_INPUTS_IN_MEMPOOL);

    CFieldElement nullifier{};
    blockchain_test_utils::RandomSidechainField(nullifier);

    CAmount cswTxCoins = 10;
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId1, "aabb", "ccdd", "eeff", cswTxCoins, &nullifier);
    CTransaction cswTx1 = GenerateCSWTx(cswInput);
 
    CValidationState dummyState;
    EXPECT_FALSE(mempool.checkCswInputsPerScLimit(cswTx1));


    // check that a csw for a different scid would be accepted instead
    uint256 scId2 = uint256S("bbb");
    CTxCeasedSidechainWithdrawalInput cswInput2 = GenerateCSWInput(scId2, "aabb", "ccdd", "eeff", cswTxCoins, &nullifier);
    CTransaction cswTx2 = GenerateCSWTx(cswInput2);
    EXPECT_TRUE(mempool.checkCswInputsPerScLimit(cswTx2));
    CTxMemPoolEntry cswEntry2(cswTx2, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(cswTx2.GetHash(), cswEntry2));
    EXPECT_TRUE(mempool.getNumOfCswInputs(scId2) == 1);

    //Remove one csw tx related to scid1 from mempool

    CTransaction lastTxSc1;
    EXPECT_TRUE(mempool.lookup(lastTxHash, lastTxSc1));

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.remove(lastTxSc1, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_FALSE(mempool.existsTx(lastTxHash));

    // check that the counter for scid1 is back in the allowd range
    EXPECT_TRUE(mempool.getNumOfCswInputs(scId1) == (SC_MAX_NUM_OF_CSW_INPUTS_IN_MEMPOOL - 1));

    // check that now the tx would be ok
    EXPECT_TRUE(mempool.checkCswInputsPerScLimit(cswTx1));

    CTxMemPoolEntry cswEntry(cswTx1, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(cswTx1.GetHash(), cswEntry));

    EXPECT_TRUE(mempool.getNumOfCswInputs(scId1)  == SC_MAX_NUM_OF_CSW_INPUTS_IN_MEMPOOL);

    // now use a tx with multiple csw input for scid2 and check that the tx exceed the limit
    CMutableTransaction mutTx;

    for (int i = 0; i < SC_MAX_NUM_OF_CSW_INPUTS_IN_MEMPOOL; i++)
    {
        // get a random nullifier fe
        CFieldElement nullifier{};
        blockchain_test_utils::RandomSidechainField(nullifier);

        CAmount cswTxCoins = 10 + i;
        CTxCeasedSidechainWithdrawalInput cswInput2 = GenerateCSWInput(scId2, "aabb", "ccdd", "eeff", cswTxCoins, &nullifier);

        mutTx.vcsw_ccin.push_back(cswInput2);
    }
    CTransaction cswTx(mutTx);

    EXPECT_FALSE(mempool.checkCswInputsPerScLimit(cswTx));

    EXPECT_TRUE(mempool.getNumOfCswInputs(scId2) == 1);
}

TEST_F(SidechainsInMempoolTestSuite, DuplicatedCSWsToCeasedSidechainAreRejected) {
    uint256 scId = uint256S("aaa");
    CAmount cswTxCoins = 10;
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId, "aabb", "ccdd", "eeff", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CTxMemPoolEntry cswEntry(cswTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(cswTx.GetHash(), cswEntry));

    CMutableTransaction duplicatedCswTx = cswTx;
    duplicatedCswTx.addOut(CTxOut(CAmount(5), CScript()));
    ASSERT_TRUE(cswTx.GetHash() != duplicatedCswTx.GetHash());

    CValidationState dummyState;
    EXPECT_FALSE(mempool.checkIncomingTxConflicts(duplicatedCswTx));
}

TEST_F(SidechainsInMempoolTestSuite, UnconfirmedFwtTxToCeasedSidechainsAreRemovedFromMempool) {
    CNakedCCoinsViewCache sidechainsView(pcoinsTip);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1492;
    initialScState.fixedParams.withdrawalEpochLength = 14;
    initialScState.balance = CAmount{1000};
    initialScState.InitScFees();
    int heightWhereAlive = initialScState.GetScheduledCeasingHeight() -1;

    storeSidechainWithCurrentHeight(sidechainsView, scId, initialScState, heightWhereAlive);
    sidechainsView.Flush();
    ASSERT_TRUE(sidechainsView.GetSidechainState(scId) == CSidechain::State::ALIVE);

    CTransaction fwtTx = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwtEntry(fwtTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(fwtTx.GetHash(), fwtEntry));

    // Sidechain State is Active. No removed Txs and Certs expected.
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    int dummyHeight{1815};
    mempool.removeStaleTransactions(&sidechainsView, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 0);
    EXPECT_TRUE(removedCerts.size() == 0);

    // Cease sidechains
    chainSettingUtils::ExtendChainActiveToHeight(initialScState.GetScheduledCeasingHeight());
    sidechainsView.SetBestBlock(chainActive.Tip()->GetBlockHash());
    sidechainsView.Flush();
    ASSERT_TRUE(sidechainsView.GetSidechainState(scId) == CSidechain::State::CEASED);

    // Sidechain State is Ceased. FT expected to be removed.
    removedTxs.clear();
    removedCerts.clear();
    mempool.removeStaleTransactions(&sidechainsView, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 1);
    EXPECT_TRUE(std::find(removedTxs.begin(), removedTxs.end(), fwtTx) != removedTxs.end());
    EXPECT_TRUE(removedCerts.size() == 0);
}

TEST_F(SidechainsInMempoolTestSuite, UnconfirmedCsw_LargerThanSidechainBalanceAreRemovedFromMempool) {
    // This can happen upon faulty/malicious circuits

    CNakedCCoinsViewCache sidechainsView(pcoinsTip);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1492;
    initialScState.fixedParams.withdrawalEpochLength = 14;
    initialScState.balance = CAmount{1000};
    initialScState.InitScFees();
    int heightWhereCeased = initialScState.GetScheduledCeasingHeight();

    storeSidechainWithCurrentHeight(sidechainsView, scId, initialScState, heightWhereCeased);
    sidechainsView.Flush();
    ASSERT_TRUE(sidechainsView.GetSidechainState(scId) == CSidechain::State::CEASED);

    // Create and add CSW Tx
    CAmount cswTxCoins = initialScState.balance; // csw coins = total sc mature coins
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId, "aabb", "ccdd", "eeff", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CTxMemPoolEntry cswEntry(cswTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(cswTx.GetHash(), cswEntry));

    // Sidechain State is Ceased and there is no Sidechain balance conflicts in the mempool. No removed Txs and Certs expected.
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.removeOutOfScBalanceCsw(&sidechainsView, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 0);
    EXPECT_TRUE(removedCerts.size() == 0);

    // Add without checks another CSW tx to the same sidechain
    CAmount cswTxCoins2 = 1;
    CTxCeasedSidechainWithdrawalInput cswInput2 = GenerateCSWInput(scId, "ddcc", "aabb", "eeff", cswTxCoins2);
    CTransaction cswTx2 = GenerateCSWTx(cswInput2);
    CTxMemPoolEntry cswEntry2(cswTx2, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(cswTx2.GetHash(), cswEntry2));

    // Mempool CSW Txs total withdrawal amount is greater than Sidechain mature balance -> both Txs expected to be removed.
    removedTxs.clear();
    removedCerts.clear();
    mempool.removeOutOfScBalanceCsw(&sidechainsView, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 2);
    EXPECT_TRUE(std::find(removedTxs.begin(), removedTxs.end(), cswTx) != removedTxs.end());
    EXPECT_TRUE(std::find(removedTxs.begin(), removedTxs.end(), cswTx2) != removedTxs.end());
    EXPECT_TRUE(removedCerts.size() == 0);
}

TEST_F(SidechainsInMempoolTestSuite, UnconfirmedCswForAliveSidechainsAreRemovedFromMempool) {
    //This can happen upon reverting end-of-epoch block

    CNakedCCoinsViewCache sidechainsView(pcoinsTip);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1492;
    initialScState.fixedParams.withdrawalEpochLength = 14;
    initialScState.balance = CAmount{1000};
    initialScState.InitScFees();
    int heightWhereCeased = initialScState.GetScheduledCeasingHeight();

    storeSidechainWithCurrentHeight(sidechainsView, scId, initialScState, heightWhereCeased);
    sidechainsView.Flush();
    ASSERT_TRUE(sidechainsView.GetSidechainState(scId) == CSidechain::State::CEASED);

    // Create and add CSW Tx
    CAmount cswTxCoins = initialScState.balance; // csw coins = total sc mature coins
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId, "aabb", "ccdd", "eeff", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CTxMemPoolEntry cswEntry(cswTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(cswTx.GetHash(), cswEntry));

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    int dummyHeight{1789};
    // Sidechain State is Ceased and there is no Sidechain balance conflicts in the mempool. No removed Txs and Certs expected.
    mempool.removeStaleTransactions(&sidechainsView, removedTxs, removedCerts);
    mempool.removeOutOfScBalanceCsw(&sidechainsView, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 0);
    EXPECT_TRUE(removedCerts.size() == 0);

    // revert sidechain state to ACTIVE
    chainSettingUtils::ExtendChainActiveToHeight(initialScState.GetScheduledCeasingHeight()-1);
    sidechainsView.SetBestBlock(chainActive.Tip()->GetBlockHash());
    sidechainsView.Flush();
    ASSERT_TRUE(sidechainsView.GetSidechainState(scId) == CSidechain::State::ALIVE);

    // Mempool CSW Txs total withdrawal amount is greater than Sidechain mature balance -> both Txs expected to be removed.
    removedTxs.clear();
    removedCerts.clear();
    mempool.removeStaleTransactions(&sidechainsView, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 1);
    EXPECT_TRUE(std::find(removedTxs.begin(), removedTxs.end(), cswTx) != removedTxs.end());
    EXPECT_TRUE(removedCerts.size() == 0);
}

TEST_F(SidechainsInMempoolTestSuite, SimpleCswRemovalFromMempool) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateSidechain(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load csw tx to mempool
    CAmount dummyAmount(1);
    uint160 dummyPubKeyHash;
    CScProof dummyScProof;
    CFieldElement dummyActCertData;
    CFieldElement dummyCeasingCumTree;
    CScript dummyRedeemScript;

    CMutableTransaction mutTx;
    CFieldElement nullfier_1{std::vector<unsigned char>(size_t(CFieldElement::ByteSize()), 'a')};
    CFieldElement nullfier_2{std::vector<unsigned char>(size_t(CFieldElement::ByteSize()), 'b')};
    mutTx.vcsw_ccin.push_back(CTxCeasedSidechainWithdrawalInput(
        dummyAmount, scId, nullfier_1, dummyPubKeyHash, dummyScProof, dummyActCertData, dummyCeasingCumTree, dummyRedeemScript));
    mutTx.vcsw_ccin.push_back(CTxCeasedSidechainWithdrawalInput(
        dummyAmount, scId, nullfier_2, dummyPubKeyHash, dummyScProof, dummyActCertData, dummyCeasingCumTree, dummyRedeemScript));

    CTransaction cswTx(mutTx);
    CTxMemPoolEntry cswEntry(cswTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cswTx.GetHash(), cswEntry);

    //Remove the csw tx
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.remove(cswTx, removedTxs, removedCerts, /*fRecursive*/false);

    //checks
    EXPECT_TRUE(removedCerts.size() == 0);
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), cswTx));
    EXPECT_FALSE(mempool.existsTx(cswTx.GetHash()));
}

TEST_F(SidechainsInMempoolTestSuite, CSWsToCeasedSidechainWithoutVK) {
    // Create and persist sidechain
    int creationHeight = 1789;
    int epochLength = 10;
    CAmount scCoins = 1000;
    chainSettingUtils::ExtendChainActiveToHeight(creationHeight);
    // NOTE: no Ceased VK in SC creation output
    CTransaction scTx = GenerateScTx(scCoins, epochLength, /*ceasedVkDefined*/false);
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateSidechain(scTx, aBlock, creationHeight);
    sidechainsView.Flush();
//    for(const CTxScCreationOut& scCreationOut: scTx.GetVscCcOut())
//        ASSERT_TRUE(sidechainsView.ScheduleSidechainEvent(scCreationOut, creationHeight));
//    sidechainsView.Flush();

    // Make coins mature
    CBlockUndo dummyBlockUndo(IncludeScAttributes::ON);
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    int coinsMatureHeight = creationHeight + Params().ScCoinsMaturity();
    ASSERT_TRUE(sidechainsView.HandleSidechainEvents(coinsMatureHeight, dummyBlockUndo, &dummy));


    // Cease sidechain
    int safeguardMargin = epochLength/5;
    int ceasingHeight = creationHeight + epochLength + safeguardMargin;
    ASSERT_TRUE(sidechainsView.HandleSidechainEvents(ceasingHeight, dummyBlockUndo, &dummy));
    sidechainsView.Flush();

    chainSettingUtils::ExtendChainActiveToHeight(ceasingHeight);

    // Create and add CSW Tx
    CAmount cswTxCoins = scCoins / 4;
    assert(cswTxCoins > 0);
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId, "aabb", "ccdd", "eeff", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CValidationState cswTxState;

    MempoolReturnValue res = AcceptTxToMemoryPool(mempool, cswTxState, cswTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                                  MempoolProofVerificationFlag::SYNC);
    EXPECT_TRUE(res == MempoolReturnValue::INVALID);
}

TEST_F(SidechainsInMempoolTestSuite, ConflictingCswRemovalFromMempool) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateSidechain(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load csw tx to mempool
    CAmount dummyAmount(1);
    uint160 dummyPubKeyHash;
    CScProof dummyScProof;
    CFieldElement dummyActCertData;
    CFieldElement dummyCeasingCumTree;
    CScript dummyRedeemScript;

    CMutableTransaction mutTx;
    mutTx.nVersion = SC_TX_VERSION;
    CFieldElement nullfier_1{std::vector<unsigned char>(size_t(CFieldElement::ByteSize()), 'a')};
    CFieldElement nullfier_2{std::vector<unsigned char>(size_t(CFieldElement::ByteSize()), 'b')};
    mutTx.vcsw_ccin.push_back(CTxCeasedSidechainWithdrawalInput(
        dummyAmount, scId, nullfier_1, dummyPubKeyHash, dummyScProof, dummyActCertData, dummyCeasingCumTree, dummyRedeemScript));
    mutTx.vcsw_ccin.push_back(CTxCeasedSidechainWithdrawalInput(
        dummyAmount, scId, nullfier_2, dummyPubKeyHash, dummyScProof, dummyActCertData, dummyCeasingCumTree, dummyRedeemScript));

    CTransaction cswTx(mutTx);
    CTxMemPoolEntry cswEntry(cswTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cswTx.GetHash(), cswEntry);

    //Remove the csw tx due to nullifier conflict with cswConfirmedTx
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;

    CMutableTransaction mutConfirmedTx;
    mutConfirmedTx.vcsw_ccin.push_back(CTxCeasedSidechainWithdrawalInput(
        dummyAmount, scId, nullfier_1, dummyPubKeyHash, dummyScProof, dummyActCertData, dummyCeasingCumTree, dummyRedeemScript));

    CTransaction cswConfirmedTx(mutConfirmedTx);
    ASSERT_TRUE(cswTx.GetHash() != cswConfirmedTx.GetHash());
    mempool.removeConflicts(cswConfirmedTx, removedTxs, removedCerts);

    //checks
    EXPECT_TRUE(removedCerts.size() == 0);
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), cswTx));
    EXPECT_FALSE(mempool.existsTx(cswTx.GetHash()));
}


TEST_F(SidechainsInMempoolTestSuite, UnconfirmedTxSpendingImmatureCoinbaseIsDropped) {
    //This may happen in block disconnection, for instance

    //Create a coinbase
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
    std::list<CTransaction> outdatedTxs;
    std::list<CScCertificate> outdatedCerts;
    mempool.removeStaleTransactions(pcoinsTip, outdatedTxs, outdatedCerts);

    //Check
    EXPECT_FALSE(mempool.exists(mempoolTx1.GetHash()));
    EXPECT_TRUE(std::find(outdatedTxs.begin(), outdatedTxs.end(), mempoolTx1) != outdatedTxs.end());

    EXPECT_FALSE(mempool.exists(mempoolTx2.GetHash()));
    EXPECT_TRUE(std::find(outdatedTxs.begin(), outdatedTxs.end(), mempoolTx2) != outdatedTxs.end());
}

TEST_F(SidechainsInMempoolTestSuite, UnconfirmedFwdsTowardUnconfirmedSidechainsAreNotDropped)
{
    CNakedCCoinsViewCache sidechainsView(pcoinsTip);

    // setup sidechain initial state
    int scCreationHeight {200};
    uint256 inputScCreationTxHash = txCreationUtils::CreateSpendableCoinAtHeight(sidechainsView, scCreationHeight-COINBASE_MATURITY);

    CMutableTransaction mutScCreationTx = txCreationUtils::createNewSidechainTxWith(/*creationTxAmount*/CAmount(10), /*epochLength*/5);
    mutScCreationTx.vin.clear();
    mutScCreationTx.vin.push_back(CTxIn(inputScCreationTxHash, 0, CScript()));
    CTransaction scCreationTx(mutScCreationTx);
    uint256 scId = scCreationTx.GetScIdFromScCcOut(0);
    CTxMemPoolEntry scPoolEntry(scCreationTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(scCreationTx.GetHash(), scPoolEntry);
    ASSERT_TRUE(mempool.hasSidechainCreationTx(scId));

    // create coinbase to finance fwt
    int fwtHeight {201};
    uint256 inputTxHash = txCreationUtils::CreateSpendableCoinAtHeight(sidechainsView, fwtHeight-COINBASE_MATURITY);

    //Add fwt to mempool
    CMutableTransaction mutFwdTx = txCreationUtils::createFwdTransferTxWith(scId, /*fwdTxAmount*/CAmount(10));
    mutFwdTx.vin.clear();
    mutFwdTx.vin.push_back(CTxIn(inputTxHash, 0, CScript()));
    CTransaction fwdTx(mutFwdTx);
    CTxMemPoolEntry mempoolEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/fwtHeight);
    mempool.addUnchecked(fwdTx.GetHash(), mempoolEntry);

    //test
    std::list<CTransaction> outdatedTxs;
    std::list<CScCertificate> outdatedCerts;
    mempool.removeStaleTransactions(&sidechainsView, outdatedTxs, outdatedCerts);

    //checks
    EXPECT_TRUE(mempool.exists(fwdTx.GetHash()));
    EXPECT_FALSE(std::find(outdatedTxs.begin(), outdatedTxs.end(), fwdTx) != outdatedTxs.end());
}

TEST_F(SidechainsInMempoolTestSuite,UnconfirmedFwdsTowardAliveSidechainsAreNotDropped)
{
    CNakedCCoinsViewCache sidechainsView(pcoinsTip);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1492;
    initialScState.fixedParams.withdrawalEpochLength = 14;
    initialScState.InitScFees();
    int heightWhereAlive = initialScState.GetScheduledCeasingHeight() -1;

    storeSidechainWithCurrentHeight(sidechainsView, scId, initialScState, heightWhereAlive);
    sidechainsView.Flush();
    ASSERT_TRUE(sidechainsView.GetSidechainState(scId) == CSidechain::State::ALIVE);

    // create coinbase to finance fwt
    int fwtHeight = heightWhereAlive;
    uint256 inputTxHash = txCreationUtils::CreateSpendableCoinAtHeight(sidechainsView, fwtHeight-COINBASE_MATURITY);

    //Add fwt to mempool
    CMutableTransaction mutFwdTx = txCreationUtils::createFwdTransferTxWith(scId, /*fwdTxAmount*/CAmount(10));
    mutFwdTx.vin.clear();
    mutFwdTx.vin.push_back(CTxIn(inputTxHash, 0, CScript()));
    CTransaction fwdTx(mutFwdTx);
    CTxMemPoolEntry mempoolEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/fwtHeight);
    mempool.addUnchecked(fwdTx.GetHash(), mempoolEntry);

    //test
    std::list<CTransaction> outdatedTxs;
    std::list<CScCertificate> outdatedCerts;
    mempool.removeStaleTransactions(&sidechainsView, outdatedTxs, outdatedCerts);

    //checks
    EXPECT_TRUE(mempool.exists(fwdTx.GetHash()));
    EXPECT_FALSE(std::find(outdatedTxs.begin(), outdatedTxs.end(), fwdTx) != outdatedTxs.end());
}

TEST_F(SidechainsInMempoolTestSuite,UnconfirmedFwdsTowardCeasedSidechainsAreDropped)
{
    CNakedCCoinsViewCache sidechainsView(pcoinsTip);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1492;
    initialScState.fixedParams.withdrawalEpochLength = 14;
    initialScState.InitScFees();
    int heightWhereCeased = initialScState.GetScheduledCeasingHeight();

    storeSidechainWithCurrentHeight(sidechainsView, scId, initialScState, heightWhereCeased);
    sidechainsView.Flush();
    ASSERT_TRUE(sidechainsView.GetSidechainState(scId) == CSidechain::State::CEASED);

    // create coinbase to finance fwt
    int fwtHeight = heightWhereCeased + 2;
    uint256 inputTxHash = txCreationUtils::CreateSpendableCoinAtHeight(sidechainsView, fwtHeight-COINBASE_MATURITY);

    //Add fwt to mempool
    CMutableTransaction mutFwdTx = txCreationUtils::createFwdTransferTxWith(scId, /*fwdTxAmount*/CAmount(10));
    mutFwdTx.vin.clear();
    mutFwdTx.vin.push_back(CTxIn(inputTxHash, 0, CScript()));
    CTransaction fwdTx(mutFwdTx);
    CTxMemPoolEntry mempoolEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/fwtHeight);
    mempool.addUnchecked(fwdTx.GetHash(), mempoolEntry);

    //test
    std::list<CTransaction> outdatedTxs;
    std::list<CScCertificate> outdatedCerts;
    mempool.removeStaleTransactions(&sidechainsView, outdatedTxs, outdatedCerts);

    //checks
    EXPECT_FALSE(mempool.exists(fwdTx.GetHash()));
    EXPECT_TRUE(std::find(outdatedTxs.begin(), outdatedTxs.end(), fwdTx) != outdatedTxs.end());
}

TEST_F(SidechainsInMempoolTestSuite,UnconfirmedMbtrTowardCeasedSidechainIsDropped)
{
    CNakedCCoinsViewCache sidechainsView(pcoinsTip);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1492;
    initialScState.fixedParams.withdrawalEpochLength = 14;
    initialScState.InitScFees();
    int heightWhereCeased = initialScState.GetScheduledCeasingHeight();

    storeSidechainWithCurrentHeight(sidechainsView, scId, initialScState, heightWhereCeased);
    sidechainsView.Flush();
    ASSERT_TRUE(sidechainsView.GetSidechainState(scId) == CSidechain::State::CEASED);

    // create coinbase to finance mbtr
    int mbtrHeight = heightWhereCeased +1;
    uint256 inputTxHash = txCreationUtils::CreateSpendableCoinAtHeight(sidechainsView, mbtrHeight-COINBASE_MATURITY);

    //Add mbtr to mempool
    CBwtRequestOut mcBwtReq;
    mcBwtReq.scId = scId;
    CMutableTransaction mutMbtrTx;
    mutMbtrTx.nVersion = SC_TX_VERSION;
    mutMbtrTx.vin.clear();
    mutMbtrTx.vin.push_back(CTxIn(inputTxHash, 0, CScript()));
    mutMbtrTx.vmbtr_out.push_back(mcBwtReq);
    CTransaction mbtrTx(mutMbtrTx);
    CTxMemPoolEntry mempoolEntry(mbtrTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/mbtrHeight);
    mempool.addUnchecked(mbtrTx.GetHash(), mempoolEntry, /*fCurrentEstimate*/true);

    //test
    std::list<CTransaction> outdatedTxs;
    std::list<CScCertificate> outdatedCerts;
    mempool.removeStaleTransactions(&sidechainsView, outdatedTxs, outdatedCerts);

    //checks
    EXPECT_FALSE(mempool.exists(mbtrTx.GetHash()));
    EXPECT_TRUE(std::find(outdatedTxs.begin(), outdatedTxs.end(), mbtrTx) != outdatedTxs.end());
}

TEST_F(SidechainsInMempoolTestSuite,UnconfirmedCertTowardAliveSidechainIsNotDropped)
{
    CNakedCCoinsViewCache sidechainsView(pcoinsTip);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 201;
    initialScState.fixedParams.withdrawalEpochLength = 9;
    initialScState.lastTopQualityCertReferencedEpoch = 19;
    initialScState.InitScFees();
    int heightWhereAlive = initialScState.GetScheduledCeasingHeight()-1;
    storeSidechainWithCurrentHeight(sidechainsView, scId, initialScState, heightWhereAlive);
    sidechainsView.Flush();
    ASSERT_TRUE(sidechainsView.GetSidechainState(scId) == CSidechain::State::ALIVE);

    // set relevant heights
    int epochReferredByCert = initialScState.lastTopQualityCertReferencedEpoch + 1;
    int certHeight = initialScState.GetCertSubmissionWindowStart(epochReferredByCert) + 1;
    ASSERT_TRUE(certHeight <= initialScState.GetCertSubmissionWindowEnd(epochReferredByCert));

    // create coinbase to finance cert
    uint256 inputTxHash = txCreationUtils::CreateSpendableCoinAtHeight(sidechainsView, certHeight-COINBASE_MATURITY);

    //Add mbtr to mempool
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, epochReferredByCert,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(0), /*numBwt*/2,
        /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputTxHash, 0, CScript()));
    CScCertificate cert(mutCert);
    CCertificateMemPoolEntry mempoolEntry(cert, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/certHeight);
    mempool.addUnchecked(cert.GetHash(), mempoolEntry);

    //test
    std::list<CScCertificate> outdatedCerts;
    mempool.removeStaleCertificates(&sidechainsView, outdatedCerts);

    //checks
    EXPECT_TRUE(mempool.exists(cert.GetHash()));
    EXPECT_FALSE(std::find(outdatedCerts.begin(), outdatedCerts.end(), cert) != outdatedCerts.end());
}

TEST_F(SidechainsInMempoolTestSuite,UnconfirmedCertTowardCeasedSidechainIsDropped)
{
    CNakedCCoinsViewCache sidechainsView(pcoinsTip);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 1492;
    initialScState.fixedParams.withdrawalEpochLength = 14;
    initialScState.InitScFees();
    int heightWhereCeased = initialScState.GetScheduledCeasingHeight();

    storeSidechainWithCurrentHeight(sidechainsView, scId, initialScState, heightWhereCeased);
    sidechainsView.Flush();
    ASSERT_TRUE(sidechainsView.GetSidechainState(scId) == CSidechain::State::CEASED);

    // create coinbase to finance cert
    int certHeight = heightWhereCeased  +1 ;
    uint256 inputTxHash = txCreationUtils::CreateSpendableCoinAtHeight(sidechainsView, certHeight-COINBASE_MATURITY);

    //Add mbtr to mempool
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*currentEpoch*/0,
        CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(0), /*numBwt*/2,
        /*ftScFee*/0, /*mbtrScFee*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputTxHash, 0, CScript()));
    CScCertificate cert(mutCert);
    CCertificateMemPoolEntry mempoolEntry(cert, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/certHeight);
    mempool.addUnchecked(cert.GetHash(), mempoolEntry);

    //test
    std::list<CScCertificate> outdatedCerts;
    mempool.removeStaleCertificates(&sidechainsView, outdatedCerts);

    //checks
    EXPECT_FALSE(mempool.exists(cert.GetHash()));
    EXPECT_TRUE(std::find(outdatedCerts.begin(), outdatedCerts.end(), cert) != outdatedCerts.end());
}

TEST_F(SidechainsInMempoolTestSuite, DependenciesInEmptyMempool) {
    // prerequisites
    CAmount dummyAmount(10);
    CScript dummyScript;
    CTxOut dummyOut(dummyAmount, dummyScript);

    CMutableTransaction tx_1;
    tx_1.vin.push_back(CTxIn(uint256(), 0, dummyScript));
    tx_1.addOut(dummyOut);

    //test and checks
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_1).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_1).empty());
}

TEST_F(SidechainsInMempoolTestSuite, DependenciesOfSingleTransaction) {
    // prerequisites
    CAmount dummyAmount(10);
    CScript dummyScript;
    CTxOut dummyOut(dummyAmount, dummyScript);

    CMutableTransaction tx_1;
    tx_1.vin.push_back(CTxIn(uint256(), 0, dummyScript));
    tx_1.addOut(dummyOut);
    CTxMemPoolEntry tx_1_entry(tx_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);

    //test
    aMempool.addUnchecked(tx_1.GetHash(), tx_1_entry);

    //checks
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_1).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_1).empty());
}


TEST_F(SidechainsInMempoolTestSuite, DependenciesOfSimpleChain) {
    // prerequisites
    CAmount dummyAmount(10);
    CScript dummyScript;
    CTxOut dummyOut(dummyAmount, dummyScript);

    // Create chain tx_1 -> tx_2 -> tx_3
    CMutableTransaction tx_1;
    tx_1.vin.push_back(CTxIn(uint256(), 0, dummyScript));
    tx_1.addOut(dummyOut);
    CTxMemPoolEntry tx_1_entry(tx_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_1.GetHash(), tx_1_entry));

    CMutableTransaction tx_2;
    tx_2.vin.push_back(CTxIn(tx_1.GetHash(), 0, dummyScript));
    tx_2.addOut(dummyOut);
    CTxMemPoolEntry tx_2_entry(tx_2, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_2.GetHash(), tx_2_entry));

    CMutableTransaction tx_3;
    tx_3.vin.push_back(CTxIn(tx_2.GetHash(), 0, dummyScript));
    CTxMemPoolEntry tx_3_entry(tx_3, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_3.GetHash(), tx_3_entry));

    //checks
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_1).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_2) == std::vector<uint256>({tx_1.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_3) == std::vector<uint256>({tx_2.GetHash(),tx_1.GetHash()}));

    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_1) == std::vector<uint256>({tx_2.GetHash(),tx_3.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_2) == std::vector<uint256>({tx_3.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_3).empty());
}

TEST_F(SidechainsInMempoolTestSuite, DependenciesOfTree) {
    // prerequisites
    CAmount dummyAmount(10);
    CScript dummyScript;
    CTxOut dummyOut_1(dummyAmount, dummyScript);
    CTxOut dummyOut_2(++dummyAmount, dummyScript);

    CMutableTransaction tx_root;
    tx_root.vin.push_back(CTxIn(uint256(), 0, dummyScript));
    tx_root.addOut(dummyOut_1);
    tx_root.addOut(dummyOut_2);
    CTxMemPoolEntry tx_root_entry(tx_root, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_root.GetHash(), tx_root_entry));

    CMutableTransaction tx_child_1;
    tx_child_1.vin.push_back(CTxIn(tx_root.GetHash(), 0, dummyScript));
    tx_child_1.addOut(dummyOut_1);
    tx_child_1.addOut(dummyOut_2);
    CTxMemPoolEntry tx_child_1_entry(tx_child_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_child_1.GetHash(), tx_child_1_entry));

    CMutableTransaction tx_child_2;
    tx_child_2.vin.push_back(CTxIn(tx_root.GetHash(), 1, dummyScript));
    tx_child_2.addOut(dummyOut_1);
    tx_child_2.addOut(dummyOut_2);
    CTxMemPoolEntry tx_child_2_entry(tx_child_2, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_child_2.GetHash(), tx_child_2_entry));

    CMutableTransaction tx_grandchild_1;
    tx_grandchild_1.vin.push_back(CTxIn(tx_child_1.GetHash(), 0, dummyScript));
    CTxMemPoolEntry tx_grandchild_1_entry(tx_grandchild_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_grandchild_1.GetHash(), tx_grandchild_1_entry));

    CMutableTransaction tx_grandchild_2;
    tx_grandchild_2.vin.push_back(CTxIn(tx_child_1.GetHash(), 1, dummyScript));
    CTxMemPoolEntry tx_grandchild_2_entry(tx_grandchild_2, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_grandchild_2.GetHash(), tx_grandchild_2_entry));

    CMutableTransaction tx_grandchild_3;
    tx_grandchild_3.vin.push_back(CTxIn(tx_child_2.GetHash(), 0, dummyScript));
    CTxMemPoolEntry tx_grandchild_3_entry(tx_grandchild_3, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_grandchild_3.GetHash(), tx_grandchild_3_entry));

    //checks
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_root).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_child_1) == std::vector<uint256>({tx_root.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_child_2) == std::vector<uint256>({tx_root.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_grandchild_1) == std::vector<uint256>({tx_child_1.GetHash(), tx_root.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_grandchild_2) == std::vector<uint256>({tx_child_1.GetHash(), tx_root.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_grandchild_3) == std::vector<uint256>({tx_child_2.GetHash(), tx_root.GetHash()}));

    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_root)
        == std::vector<uint256>({tx_child_1.GetHash(), tx_grandchild_2.GetHash(), tx_grandchild_1.GetHash(),
                                 tx_child_2.GetHash(), tx_grandchild_3.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_child_1) == std::vector<uint256>({tx_grandchild_1.GetHash(), tx_grandchild_2.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_child_2) == std::vector<uint256>({tx_grandchild_3.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_grandchild_1).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_grandchild_2).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_grandchild_3).empty());
}

TEST_F(SidechainsInMempoolTestSuite, DependenciesOfTDAG) {
    // prerequisites
    CAmount dummyAmount(10);
    CScript dummyScript;
    CTxOut dummyOut_1(dummyAmount, dummyScript);
    CTxOut dummyOut_2(++dummyAmount, dummyScript);

    CMutableTransaction tx_root;
    tx_root.vin.push_back(CTxIn(uint256(), 0, dummyScript));
    tx_root.addOut(dummyOut_1);
    tx_root.addOut(dummyOut_2);
    CTxMemPoolEntry tx_root_entry(tx_root, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_root.GetHash(), tx_root_entry));

    CMutableTransaction tx_child_1;
    tx_child_1.vin.push_back(CTxIn(tx_root.GetHash(), 0, dummyScript));
    tx_child_1.addOut(dummyOut_1);
    CTxMemPoolEntry tx_child_1_entry(tx_child_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_child_1.GetHash(), tx_child_1_entry));

    CMutableTransaction tx_grandchild_1;
    tx_grandchild_1.vin.push_back(CTxIn(tx_root.GetHash(), 1, dummyScript));
    tx_grandchild_1.vin.push_back(CTxIn(tx_child_1.GetHash(), 0, dummyScript));
    CTxMemPoolEntry tx_grandchild_1_entry(tx_grandchild_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_grandchild_1.GetHash(), tx_grandchild_1_entry));

    //checks
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_root).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_child_1) == std::vector<uint256>({tx_root.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_grandchild_1) == std::vector<uint256>({tx_child_1.GetHash(),tx_root.GetHash()}));

    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_root) == std::vector<uint256>({tx_child_1.GetHash(), tx_grandchild_1.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_child_1) == std::vector<uint256>({tx_grandchild_1.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_grandchild_1).empty());
}


//////////////////////////////////////////////////////////
//////////////////// Fee validations /////////////////////
//////////////////////////////////////////////////////////
TEST_F(SidechainsInMempoolTestSuite, CheckFtFeeValidationOnMempool)
{
    CAmount ftScFee(7);
    uint256 scId = createAndStoreSidechain(/*FT fee*/ftScFee, /*MBTR fee*/0, /*MBTR data length*/0);

    CTransaction fwdTx = GenerateFwdTransferTx(scId, ftScFee);
    CValidationState fwdTxState;

    // Check that a FT with the fee equal to the minimum specified in the sidechain is rejected
    EXPECT_FALSE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                      MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);

    // Check that a FT with the fee greater than the minimum specified in the sidechain is accepted
    fwdTx = GenerateFwdTransferTx(scId, ftScFee + 1);
    EXPECT_TRUE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                     MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);

    // Check that a FT with the fee less than the minimum specified in the sidechain is rejected
    fwdTx = GenerateFwdTransferTx(scId, ftScFee - 1);
    EXPECT_FALSE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                      MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);
}

TEST_F(SidechainsInMempoolTestSuite, CheckMbtrFeeValidationOnMempool)
{
    CAmount mbtrScFee(7);
    uint256 scId = createAndStoreSidechain(/*FT fee*/0, /*MBTR fee*/mbtrScFee, /*MBTR data length*/1);

    CMutableTransaction mbtrTx = GenerateBtrTx(scId, mbtrScFee);
    CValidationState mbtrTxState;

    // Check that a MBTR with the fee equal to the minimum specified in the sidechain is accepted
    EXPECT_TRUE(AcceptTxToMemoryPool(mempool, mbtrTxState, mbtrTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                     MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);

    // Check that a MBTR with the fee greater than the minimum specified in the sidechain is accepted
    mbtrTx = GenerateBtrTx(scId, mbtrScFee + 1);
    EXPECT_TRUE(AcceptTxToMemoryPool(mempool, mbtrTxState, mbtrTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                     MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);

    // Check that a MBTR with the fee less than the minimum specified in the sidechain is rejected
    mbtrTx = GenerateBtrTx(scId, mbtrScFee - 1);
    EXPECT_FALSE(AcceptTxToMemoryPool(mempool, mbtrTxState, mbtrTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                      MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);
}


//////////////////////////////////////////////////////////
//////////////////// MBTR data length ////////////////////
//////////////////////////////////////////////////////////
TEST_F(SidechainsInMempoolTestSuite, MbtrDataLengthGreaterThanZeroEnablesMbtr)
{
    CAmount mbtrScFee(7);
    uint256 scId = createAndStoreSidechain(/*FT fee*/0, /*MBTR fee*/mbtrScFee, /*MBTR data length*/1);

    CMutableTransaction mbtrTx = GenerateBtrTx(scId, mbtrScFee);
    CValidationState mbtrTxState;

    EXPECT_TRUE(AcceptTxToMemoryPool(mempool, mbtrTxState, mbtrTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                     MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);
}

TEST_F(SidechainsInMempoolTestSuite, MbtrDataLengthZeroDisablesMbtr)
{
    CAmount mbtrScFee(7);
    uint256 scId = createAndStoreSidechain(/*FT fee*/0, /*MBTR fee*/mbtrScFee, /*MBTR data length*/0);

    CMutableTransaction mbtrTx = GenerateBtrTx(scId, mbtrScFee);
    CValidationState mbtrTxState;

    EXPECT_FALSE(AcceptTxToMemoryPool(mempool, mbtrTxState, mbtrTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                      MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);
}


//////////////////////////////////////////////////////////
/////////////////// Certificate update ///////////////////
//////////////////////////////////////////////////////////
TEST_F(SidechainsInMempoolTestSuite, NewFtFeeDoesNotRemoveTxFromMempoolOnFirstCert)
{
    SelectParams(CBaseChainParams::REGTEST);
    mapArgs["-blocksforscfeecheck"] = "0";

    CAmount ftScFee(7);
    uint256 scId = createAndStoreSidechain(/*FT fee*/ftScFee, /*MBTR fee*/0, /*MBTR data length*/0);

    CAmount ftScFeeForwardTx = ftScFee + 1;
    CTransaction fwdTx = GenerateFwdTransferTx(scId, ftScFee + 1);
    CValidationState fwdTxState;

    // Check that a FT with an amount greater than the Forward Transfer sidechain fee is accepted
    ASSERT_TRUE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                     MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);

    int64_t certQuality = 10;
    CFieldElement dummyCumTree {SAMPLE_FIELD};
    CAmount dummyInputAmount{20};
    CAmount dummyNonZeroFee {10};
    CAmount dummyNonZeroChange = dummyInputAmount - dummyNonZeroFee;
    CAmount dummyBwtAmount {0};

    CScCertificate certificate = GenerateCertificate(scId, /*epochNum*/0, dummyCumTree, /*inputAmount*/dummyInputAmount,
        /*changeTotalAmount*/dummyNonZeroChange,/*numChangeOut*/1, /*bwtAmount*/dummyBwtAmount, /*numBwt*/2,
        /*ftScFee*/ftScFee + 1, /*mbtrScFee*/CAmount(0), /*quality*/certQuality);
    CCoinsViewCache sidechainsView(pcoinsTip);
    CBlockUndo aBlock(IncludeScAttributes::ON);
    sidechainsView.UpdateSidechain(certificate, aBlock, sidechainsView.GetHeight()+1);
    moveSidechainToNextEpoch(scId, sidechainsView);

    // No transactions should be be removed.
    CSidechain sidechain;
    sidechainsView.GetSidechain(scId, sidechain);

    ASSERT_TRUE(sidechain.GetMinFtScFee() < ftScFeeForwardTx);
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.removeStaleTransactions(&sidechainsView, removedTxs, removedCerts);
    EXPECT_EQ(removedTxs.size(), 0);
}

TEST_F(SidechainsInMempoolTestSuite, NewFtFeeRemovesTxFromMempool)
{
    seed_insecure_rand(false);

    SelectParams(CBaseChainParams::REGTEST);
    static const int blocksForScFeeCheck = 100;
    mapArgs["-blocksforscfeecheck"] = std::to_string(blocksForScFeeCheck);
 
    static const int NUM_OF_LOOPS = 30;
    int loops = NUM_OF_LOOPS;
    while (loops-- > 0)
    {
        int epLen = insecure_rand()%100 + 5;
 
        int targetEpoch = blocksForScFeeCheck / epLen;
 
        if (targetEpoch == 0 || blocksForScFeeCheck % epLen)
            targetEpoch++;
 
#ifdef DEBUG_TRACE
        std::cout << std::setw(2) << (NUM_OF_LOOPS - loops) << ") " << "blocksforscfeecheck = " << blocksForScFeeCheck << ", epochLength=" << epLen << ", targetEpoch=" << targetEpoch << std::endl;
#endif
 
        CAmount ftScFee(7);
        uint256 scId = createAndStoreSidechain(/*FT fee*/ftScFee, /*MBTR fee*/0, /*MBTR data length*/0, /*epochLength*/ epLen);
 
        CTransaction fwdTx = GenerateFwdTransferTx(scId, ++ftScFee);
        CValidationState fwdTxState;
 
        // Check that a FT with an amount greater than the Forward Transfer sidechain fee is accepted
        ASSERT_TRUE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                         MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);
 
        int64_t certQuality = 10;
        CFieldElement dummyCumTree{SAMPLE_FIELD};
        CAmount dummyInputAmount{20};
        CAmount dummyNonZeroFee {10};
        CAmount dummyNonZeroChange = dummyInputAmount - dummyNonZeroFee;
        CAmount dummyBwtAmount {0};
 
        CCoinsViewCache sidechainsView(pcoinsTip);
#ifdef DEBUG_TRACE
        CSidechain sidechain;
        sidechainsView.GetSidechain(scId, sidechain);
        sidechain.DumpScFees();
#endif

        int epNum = 0;
 
        moveSidechainToNextEpoch(scId, sidechainsView);
 
        while (true)
        {
            if ( epNum > targetEpoch)
                break;
 
            CScCertificate certificate = GenerateCertificate(scId, /*epochNum*/epNum, dummyCumTree, /*inputAmount*/dummyInputAmount,
                /*changeTotalAmount*/dummyNonZeroChange,/*numChangeOut*/1, /*bwtAmount*/dummyBwtAmount, /*numBwt*/2,
                /*ftScFee*/++ftScFee, /*mbtrScFee*/CAmount(0), /*quality*/certQuality++);
 
            CBlockUndo aBlock(IncludeScAttributes::ON);
 
            sidechainsView.UpdateSidechain(certificate, aBlock, sidechainsView.GetHeight()+1);
            moveSidechainToNextEpoch(scId, sidechainsView);
 
            // the FT transaction must be removed as soon as the cert for target epoch arrives
            std::list<CTransaction> removedTxs;
            std::list<CScCertificate> removedCerts;
            mempool.removeStaleTransactions(&sidechainsView, removedTxs, removedCerts);
            EXPECT_EQ(removedTxs.size(), (epNum == targetEpoch) ? 1:0);
#ifdef DEBUG_TRACE
            CSidechain sidechain;
            sidechainsView.GetSidechain(scId, sidechain);
            sidechain.DumpScFees();
#endif
 
            epNum++;
        }
    }
}

TEST_F(SidechainsInMempoolTestSuite, NewFtFeeDoesNotRemoveTxFromMempool)
{
    CAmount ftScFee(7);
    uint256 scId = createAndStoreSidechain(/*FT fee*/ftScFee, /*MBTR fee*/0, /*MBTR data length*/0);

    CTransaction fwdTx = GenerateFwdTransferTx(scId, ftScFee + 1);
    CValidationState fwdTxState;

    // Check that a FT with an amount greater than the Forward Transfer sidechain fee is accepted
    ASSERT_TRUE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                     MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);

    int64_t certQuality = 10;
    CFieldElement dummyCumTree {SAMPLE_FIELD};
    CAmount dummyInputAmount{20};
    CAmount dummyNonZeroFee {10};
    CAmount dummyNonZeroChange = dummyInputAmount - dummyNonZeroFee;
    CAmount dummyBwtAmount {0};

    CScCertificate certificate = GenerateCertificate(scId, /*epochNum*/0, CFieldElement{SAMPLE_FIELD},
        /*inputAmount*/dummyInputAmount, /*changeTotalAmount*/dummyNonZeroChange,/*numChangeOut*/1, /*bwtAmount*/dummyBwtAmount,
        /*numBwt*/2, /*ftScFee*/ftScFee - 1, /*mbtrScFee*/CAmount(0), /*quality*/certQuality);
    CCoinsViewCache sidechainsView(pcoinsTip);
    CBlockUndo aBlock(IncludeScAttributes::ON);
    sidechainsView.UpdateSidechain(certificate, aBlock, sidechainsView.GetHeight()+1);
    moveSidechainToNextEpoch(scId, sidechainsView);

    // No transaction must be removed.
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.removeStaleTransactions(&sidechainsView, removedTxs, removedCerts);
    EXPECT_EQ(removedTxs.size(), 0);
}

TEST_F(SidechainsInMempoolTestSuite, NewMbtrFeeDoesNotRemoveTxFromMempoolOnFirstCert)
{
    SelectParams(CBaseChainParams::REGTEST);
    mapArgs["-blocksforscfeecheck"] = "0";

    CAmount mbtrScFee(7);
    uint256 scId = createAndStoreSidechain(/*FT fee*/0, /*MBTR fee*/mbtrScFee, /*MBTR data length*/1);

    CTransaction mbtrTx = GenerateBtrTx(scId, mbtrScFee);
    CValidationState mbtrTxState;

    // Check that a FT with the fee equal to the minimum specified in the sidechain is accepted
    ASSERT_TRUE(AcceptTxToMemoryPool(mempool, mbtrTxState, mbtrTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                     MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);

    int64_t certQuality = 10;
    CFieldElement dummyCumTree {SAMPLE_FIELD};
    CAmount dummyInputAmount{20};
    CAmount dummyNonZeroFee {10};
    CAmount dummyNonZeroChange = dummyInputAmount - dummyNonZeroFee;
    CAmount dummyBwtAmount {0};

    CScCertificate certificate = GenerateCertificate(scId, /*epochNum*/0, dummyCumTree, /*inputAmount*/dummyInputAmount,
        /*changeTotalAmount*/dummyNonZeroChange,/*numChangeOut*/1, /*bwtAmount*/dummyBwtAmount, /*numBwt*/2,
        /*ftScFee*/CAmount(0), /*mbtrScFee*/mbtrScFee + 1, /*quality*/certQuality);
    CCoinsViewCache sidechainsView(pcoinsTip);
    CBlockUndo aBlock(IncludeScAttributes::ON);
    sidechainsView.UpdateSidechain(certificate, aBlock, sidechainsView.GetHeight()+1);
    moveSidechainToNextEpoch(scId, sidechainsView);

    // No transactions should be be removed.
    CSidechain sidechain;
    sidechainsView.GetSidechain(scId, sidechain);

    ASSERT_TRUE(sidechain.GetMinMbtrScFee() <= mbtrScFee);
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.removeStaleTransactions(&sidechainsView, removedTxs, removedCerts);
    EXPECT_EQ(removedTxs.size(), 0);
}

TEST_F(SidechainsInMempoolTestSuite, NewMbtrFeeRemovesTxFromMempool)
{
    seed_insecure_rand(false);

    SelectParams(CBaseChainParams::REGTEST);
    static const int blocksForScFeeCheck = 100;
    mapArgs["-blocksforscfeecheck"] = std::to_string(blocksForScFeeCheck);
 
    static const int NUM_OF_LOOPS = 30;
    int loops = NUM_OF_LOOPS;
    while (loops-- > 0)
    {
        int epLen = insecure_rand()%100 + 5;
 
        int targetEpoch = blocksForScFeeCheck / epLen;
 
        if (targetEpoch == 0 || blocksForScFeeCheck % epLen)
            targetEpoch++;
 
#ifdef DEBUG_TRACE
        std::cout << std::setw(2) << (NUM_OF_LOOPS - loops) << ") " << "blocksforscfeecheck = " << blocksForScFeeCheck << ", epochLength=" << epLen << ", targetEpoch=" << targetEpoch << std::endl;
#endif
 
        CAmount mbtrScFee(10);
        uint256 scId = createAndStoreSidechain(/*FT fee*/0, /*MBTR fee*/mbtrScFee, /*MBTR data length*/1, /*epochLength*/ epLen);
 
        CTransaction mbtrTx = GenerateBtrTx(scId, mbtrScFee);
        CValidationState mbtrTxState;
 
        // Check that a FT with an amount greater than the Forward Transfer sidechain fee is accepted
        ASSERT_TRUE(AcceptTxToMemoryPool(mempool, mbtrTxState, mbtrTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                         MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);
 
        int64_t certQuality = 10;
        CFieldElement dummyCumTree {SAMPLE_FIELD};
        CAmount dummyInputAmount{20};
        CAmount dummyNonZeroFee {10};
        CAmount dummyNonZeroChange = dummyInputAmount - dummyNonZeroFee;
        CAmount dummyBwtAmount {0};
 
        CCoinsViewCache sidechainsView(pcoinsTip);
#ifdef DEBUG_TRACE
        CSidechain sidechain;
        sidechainsView.GetSidechain(scId, sidechain);
        sidechain.DumpScFees();
#endif

        int epNum = 0;
 
        moveSidechainToNextEpoch(scId, sidechainsView);
 
        while (true)
        {
            if ( epNum > targetEpoch)
                break;
 
            CScCertificate certificate = GenerateCertificate(scId, /*epochNum*/epNum, dummyCumTree, /*inputAmount*/dummyInputAmount,
                /*changeTotalAmount*/dummyNonZeroChange,/*numChangeOut*/1, /*bwtAmount*/dummyBwtAmount, /*numBwt*/2,
                /*ftScFee*/CAmount(0), /*mbtrScFee*/++mbtrScFee, /*quality*/certQuality++);
 
            CBlockUndo aBlock(IncludeScAttributes::ON);
 
            sidechainsView.UpdateSidechain(certificate, aBlock, sidechainsView.GetHeight()+1);
            moveSidechainToNextEpoch(scId, sidechainsView);
 
            // the FT transaction must be removed as soon as the cert for target epoch arrives
            std::list<CTransaction> removedTxs;
            std::list<CScCertificate> removedCerts;
            mempool.removeStaleTransactions(&sidechainsView, removedTxs, removedCerts);
            EXPECT_EQ(removedTxs.size(), (epNum == targetEpoch) ? 1:0);
#ifdef DEBUG_TRACE
            CSidechain sidechain;
            sidechainsView.GetSidechain(scId, sidechain);
            sidechain.DumpScFees();
#endif
 
            epNum++;
        }
    }
}

TEST_F(SidechainsInMempoolTestSuite, NewMbtrFeeDoesNotRemoveTxFromMempool)
{
    CAmount mbtrScFee(7);
    uint256 scId = createAndStoreSidechain(/*FT fee*/0, /*MBTR fee*/mbtrScFee, /*MBTR data length*/1);

    CTransaction mbtrTx = GenerateBtrTx(scId, mbtrScFee);
    CValidationState mbtrTxState;

    // Check that a FT with the fee equal to the minimum specified in the sidechain is accepted
    ASSERT_TRUE(AcceptTxToMemoryPool(mempool, mbtrTxState, mbtrTx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF,
                                     MempoolProofVerificationFlag::SYNC) == MempoolReturnValue::VALID);

    int64_t certQuality = 10;
    CFieldElement dummyCumTree {SAMPLE_FIELD};
    CAmount dummyInputAmount{20};
    CAmount dummyNonZeroFee {10};
    CAmount dummyNonZeroChange = dummyInputAmount - dummyNonZeroFee;
    CAmount dummyBwtAmount {0};

    CScCertificate certificate = GenerateCertificate(scId, /*epochNum*/0, dummyCumTree, /*inputAmount*/dummyInputAmount,
        /*changeTotalAmount*/dummyNonZeroChange,/*numChangeOut*/1, /*bwtAmount*/dummyBwtAmount, /*numBwt*/2,
        /*ftScFee*/CAmount(0), /*mbtrScFee*/mbtrScFee - 1, /*quality*/certQuality);
    CCoinsViewCache sidechainsView(pcoinsTip);
    CBlockUndo aBlock(IncludeScAttributes::ON);
    sidechainsView.UpdateSidechain(certificate, aBlock, sidechainsView.GetHeight()+1);
    moveSidechainToNextEpoch(scId, sidechainsView);

    // No transaction must be removed.
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.removeStaleTransactions(&sidechainsView, removedTxs, removedCerts);
    EXPECT_EQ(removedTxs.size(), 0);
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

bool SidechainsInMempoolTestSuite::StoreCoins(std::pair<uint256, CCoinsCacheEntry> entryToStore)
{
    pcoinsTip->WriteCoins(entryToStore.first, entryToStore.second);
    
    CCoinsViewCache view(pcoinsTip);
    return view.HaveCoins(entryToStore.first) == true;
}

CTransaction SidechainsInMempoolTestSuite::GenerateScTx(const CAmount & creationTxAmount, int epochLenght, bool ceasedVkDefined) {
    std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(1000);
    StoreCoins(coinData);

    CMutableTransaction scTx;
    scTx.nVersion = SC_TX_VERSION;
    scTx.vin.resize(1);
    scTx.vin[0].prevout = COutPoint(coinData.first, 0);

    scTx.vsc_ccout.resize(1);
    scTx.vsc_ccout[0].version = 0;
    scTx.vsc_ccout[0].nValue = creationTxAmount;
    // do not check min/max range for epoch, for negative tests
    scTx.vsc_ccout[0].withdrawalEpochLength = (epochLenght < 0)?getScMinWithdrawalEpochLength(): epochLenght;
    scTx.vsc_ccout[0].forwardTransferScFee = CAmount(1); // Dummy amount
    scTx.vsc_ccout[0].mainchainBackwardTransferRequestScFee = CAmount(1); // Dummy amount
    scTx.vsc_ccout[0].mainchainBackwardTransferRequestDataLength = 1;

    scTx.vsc_ccout[0].wCertVk = CScVKey{SAMPLE_CERT_DARLIN_VK};
    if(ceasedVkDefined) scTx.vsc_ccout[0].wCeasedVk = CScVKey{SAMPLE_CSW_DARLIN_VK};

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

    scTx.vft_ccout.resize(2); //testing double deletes
    scTx.vft_ccout[1].scId   = newScId;
    scTx.vft_ccout[1].nValue = fwdTxAmount;

    SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, scTx, 0);

    return scTx;
}

CTransaction SidechainsInMempoolTestSuite::GenerateBtrTx(const uint256 & scId, const CAmount& mbtrFee) {
    std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(1000);
    StoreCoins(coinData);

    CMutableTransaction scTx;
    scTx.nVersion = SC_TX_VERSION;
    scTx.vin.resize(1);
    scTx.vin[0].prevout = COutPoint(coinData.first, 0);

    scTx.vmbtr_out.resize(1);
    scTx.vmbtr_out[0].scId   = scId;
    scTx.vmbtr_out[0].scFee = mbtrFee;
    scTx.vmbtr_out[0].vScRequestData = std::vector<CFieldElement> { CFieldElement{ SAMPLE_FIELD } };

    scTx.vmbtr_out.resize(2); //testing double deletes
    scTx.vmbtr_out[1].scId   = scId;
    scTx.vmbtr_out[1].scFee = mbtrFee;
    scTx.vmbtr_out[1].vScRequestData = std::vector<CFieldElement> { CFieldElement{ SAMPLE_FIELD } };

    SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, scTx, 0);

    return scTx;
}

CTxCeasedSidechainWithdrawalInput SidechainsInMempoolTestSuite::GenerateCSWInput(
    const uint256& scId, const std::string& nullifierHex,
    const std::string& actCertDataHex, const std::string& ceasingCumScTxCommTreeHex, CAmount amount, CFieldElement* nullifierPtrIn)
{
    CFieldElement nullifier{};
    if (nullifierPtrIn != nullptr)
    {
        nullifier = *nullifierPtrIn;
    }
    else
    {
        std::vector<unsigned char> tmp1 = ParseHex(nullifierHex);
        tmp1.resize(CFieldElement::ByteSize(), 0x0);
        nullifier.SetByteArray(tmp1);
    }

    std::vector<unsigned char> tmp2 = ParseHex(actCertDataHex);
    tmp2.resize(CFieldElement::ByteSize());
    CFieldElement actCertDataHash{tmp2};

    std::vector<unsigned char> tmp3 = ParseHex(ceasingCumScTxCommTreeHex);
    tmp3.resize(CFieldElement::ByteSize());
    CFieldElement ceasingCumScTxCommTree{tmp3};

    uint160 dummyPubKeyHash = coinsKey.GetPubKey().GetID();
    CScProof dummyScProof;
    CFieldElement dummyActCertData;
    CFieldElement dummyCeasingCumTree;
    CScript dummyRedeemScript;

    return CTxCeasedSidechainWithdrawalInput(
        amount, scId, nullifier, dummyPubKeyHash, dummyScProof, actCertDataHash, ceasingCumScTxCommTree, dummyRedeemScript);
}

CTransaction SidechainsInMempoolTestSuite::GenerateCSWTx(const std::vector<CTxCeasedSidechainWithdrawalInput>& csws)
{
    CMutableTransaction mutTx;
    mutTx.nVersion = SC_TX_VERSION;
    mutTx.vcsw_ccin.insert(mutTx.vcsw_ccin.end(), csws.begin(), csws.end());

    CScript dummyScriptPubKey =
            GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/true);

    CAmount totalValue = 0;
    for(const CTxCeasedSidechainWithdrawalInput& csw: csws)
        totalValue += csw.nValue;
    mutTx.addOut(CTxOut(totalValue - 1, dummyScriptPubKey));

    // Sign CSW input
    for(const CTxCeasedSidechainWithdrawalInput& csw: csws)
    {
        SignSignature(keystore, csw.scriptPubKey(), mutTx, 0);
    }

    return mutTx;
}

CTransaction SidechainsInMempoolTestSuite::GenerateCSWTx(const CTxCeasedSidechainWithdrawalInput& csw)
{
    std::vector<CTxCeasedSidechainWithdrawalInput> csws;
    csws.push_back(csw);
    return GenerateCSWTx(csws);
}

CScCertificate SidechainsInMempoolTestSuite::GenerateCertificate(
    const uint256 & scId, int epochNum,
    const CFieldElement& endEpochCumScTxCommTreeRoot, CAmount inputAmount, CAmount changeTotalAmount, unsigned int numChangeOut,
    CAmount bwtTotalAmount, unsigned int numBwt, CAmount ftScFee, CAmount mbtrScFee,
    int64_t quality, const CTransactionBase* inputTxBase)
{
    CMutableScCertificate res;
    res.nVersion = SC_CERT_VERSION;
    res.scId = scId;
    res.epochNumber = epochNum;
    res.endEpochCumScTxCommTreeRoot = endEpochCumScTxCommTreeRoot;
    res.quality = quality;
    res.scProof = CScProof{SAMPLE_CERT_DARLIN_PROOF};
    res.forwardTransferScFee = ftScFee;
    res.mainchainBackwardTransferRequestScFee = mbtrScFee;

    CScript dummyScriptPubKey =
            GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/true);
    for(unsigned int idx = 0; idx < numChangeOut; ++idx)
        res.addOut(CTxOut(changeTotalAmount/numChangeOut,dummyScriptPubKey));

    for(unsigned int idx = 0; idx < numBwt; ++idx)
        res.addBwt(CTxOut(bwtTotalAmount/numBwt, dummyScriptPubKey));

    if (inputTxBase)
    {
        res.vin.push_back(CTxIn(COutPoint(inputTxBase->GetHash(), 0), CScript(), -1));
        SignSignature(keystore, inputTxBase->GetVout()[0].scriptPubKey, res, 0);
    }
    else if (inputAmount > 0)
    {
        std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(inputAmount);
        StoreCoins(coinData);
    
        res.vin.push_back(CTxIn(COutPoint(coinData.first, 0), CScript(), -1));
        SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, res, 0);
    }

    return res;
}

void SidechainsInMempoolTestSuite::storeSidechainWithCurrentHeight(txCreationUtils::CNakedCCoinsViewCache& view,
                                                                   const uint256& scId,
                                                                   const CSidechain& sidechain,
                                                                   int chainActiveHeight)
{
    chainSettingUtils::ExtendChainActiveToHeight(chainActiveHeight);
    view.SetBestBlock(chainActive.Tip()->GetBlockHash());
    txCreationUtils::storeSidechain(view.getSidechainMap(), scId, sidechain);
}

uint256 SidechainsInMempoolTestSuite::createAndStoreSidechain(CAmount ftScFee, CAmount mbtrScFee, size_t mbtrScDataLength, int epochLength)
{
    int creationHeight = 1789;
    chainSettingUtils::ExtendChainActiveToHeight(creationHeight);

    CMutableTransaction scTx = GenerateScTx(CAmount(10), epochLength);
    scTx.vsc_ccout[0].forwardTransferScFee = ftScFee;
    scTx.vsc_ccout[0].mainchainBackwardTransferRequestScFee = mbtrScFee;
    scTx.vsc_ccout[0].mainchainBackwardTransferRequestDataLength = mbtrScDataLength;
    uint256 scId = CTransaction(scTx).GetScIdFromScCcOut(0);


    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateSidechain(scTx, aBlock, creationHeight);
    sidechainsView.SetBestBlock(chainActive.Tip()->GetBlockHash());
    sidechainsView.Flush();

    return scId;
}

/**
 * Helper function that "triggers" the movement of a sidechain from the current epoch to the next one.
 * The main purpose of this function is to be called after updating the sidechain with a certificate;
 * in such a case, the new parameters set by the certificate (e.g. fees) are not currently active. To
 * make them active, this function should be called.
 **/
void SidechainsInMempoolTestSuite::moveSidechainToNextEpoch(uint256 scId, CCoinsViewCache& sidechainView)
{
    CSidechain sidechain;
    sidechainView.GetSidechain(scId, sidechain);

    int32_t currentEpoch = sidechain.lastTopQualityCertReferencedEpoch;
    int nextEpochHeight = sidechain.GetEndHeightForEpoch(currentEpoch) + sidechain.fixedParams.withdrawalEpochLength;

    // Move sidechain
    chainSettingUtils::ExtendChainActiveToHeight(nextEpochHeight);
    sidechainView.SetBestBlock(chainActive.Tip()->GetBlockHash());
}
