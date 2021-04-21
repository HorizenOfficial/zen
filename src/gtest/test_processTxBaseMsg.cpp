#include <gtest/gtest.h>
#include <gtest/tx_creation_utils.h>
#include <vector>
#include <boost/filesystem.hpp>

#include <txBaseMsgProcessor.h>
#include <main.h>
#include <protocol.h>
#include <txdb.h>
#include <undo.h>
#include <consensus/validation.h>

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

struct FakeMempoolProcessor
{
    FakeMempoolProcessor(): txHashToNumOfOutputs(), invalidTxes(), DosLevelIfInvalid(0) {}
    ~FakeMempoolProcessor() {mempool.clear();}
    std::map<uint256, unsigned int> txHashToNumOfOutputs;
    std::set<uint256> invalidTxes;
    int DosLevelIfInvalid;

    void markTxAsInvalid(const uint256& hash)
    {
        invalidTxes.insert(hash);
        txHashToNumOfOutputs.erase(hash);

        if (mempool.exists(hash)) //keep mempool coherent since it is used in AlredyHave, unfortunately
        {
            const CTransactionBase * toRm = nullptr;
            if (mempool.existsTx(hash))
                toRm = &mempool.mapTx.at(hash).GetTx();
            else
                toRm = &mempool.mapCertificate.at(hash).GetCertificate();

            std::list<CTransaction> removedTxs;
            std::list<CScCertificate> removedCerts;
            mempool.remove(*toRm, removedTxs, removedCerts, /*fRecursive*/true);
        }
    }

    MempoolReturnValue FakeAcceptToMempool(CTxMemPool& pool, CValidationState &state, const CTransactionBase &txBase, LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee)
    {
        // This fake declares:
        //                     Tx is VALID if it spends coinbase or spends txes in txHashToNumOfOutputs
        //                     Tx is MISSING_INPUT if at least one input is not coinbase nor spends txes in txHashToNumOfOutputs
        //                     Tx is INVALID if its hash is registered in invalidTxes set

        if (invalidTxes.count(txBase.GetHash()))
        {
            state.DoS(DosLevelIfInvalid, false, CValidationState::Code::INVALID);
            return MempoolReturnValue::INVALID;
        }

        for (const auto& in: txBase.GetVin())
        {
            if (in.prevout.hash.IsNull()) //coinbase input is always accepted
                continue;

            if (txHashToNumOfOutputs.count(in.prevout.hash) == 0 || in.prevout.n >= txHashToNumOfOutputs.at(in.prevout.hash))
                return MempoolReturnValue::MISSING_INPUT;
        }

        txHashToNumOfOutputs[txBase.GetHash()] = txBase.GetVout().size();
        if (txBase.IsCertificate()) // also add to mempool since this is checked HaveAlready
        {
            CCertificateMemPoolEntry mempoolEntry(dynamic_cast<const CScCertificate&>(txBase), /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
            mempool.addUnchecked(txBase.GetHash(), mempoolEntry);
        } else
        {
            CTxMemPoolEntry mempoolEntry(dynamic_cast<const CTransaction&>(txBase), /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
            mempool.addUnchecked(txBase.GetHash(), mempoolEntry);
        }
        return MempoolReturnValue::VALID;
    }
};

class FakeNode : public CNodeInterface
{
public:
    FakeNode(): fWhitelisted(false) {};
    ~FakeNode() = default;

    bool fWhitelisted;
    std::string commandInvoked;

    void AddInventoryKnown(const CInv& inv) override final  {}; //dummyImpl
    NodeId GetId() const override final {return 1987;};
    virtual bool IsWhiteListed() const override final {return fWhitelisted; };
    std::string GetCleanSubVer() const override final { return std::string{}; };
    void StopAskingFor(const CInv& inv) override final { return; }
    void PushMessage(const char* pszCommand, const std::string& param1, unsigned char param2,
                     const std::string& param3, const uint256& param4) override final
    {
        commandInvoked = std::string(pszCommand) + param1;
        return;
    }
};

class ProcessTxBaseMsgTestSuite : public ::testing::Test
{
public:
    ProcessTxBaseMsgTestSuite():
        fakedMemProcessor(),
        pathTemp(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path()),
        chainStateDbSize(0), pChainStateDb(nullptr), theFake()
    {
        fakedMemProcessor = std::bind(&FakeMempoolProcessor::FakeAcceptToMempool, &theFake,
                 std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5);
    };

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);
        boost::filesystem::create_directories(pathTemp);
        mapArgs["-datadir"] = pathTemp.string();

        UnloadBlockIndex();

        // trick to duly initialize recentRejects
        chainSettingUtils::ExtendChainActiveToHeight(0);
        TxBaseMsgProcessor::get().SetupRejectionFilter(120000, 0.000001);

        pChainStateDb = new CCoinsOnlyViewDB(chainStateDbSize,/*fWipe*/true);
        pcoinsTip     = new CCoinsViewCache(pChainStateDb);
    }

    void TearDown() override {
        UnloadBlockIndex();
        mapRelay.clear();

        delete pcoinsTip;
        pcoinsTip = nullptr;

        delete pChainStateDb;
        pChainStateDb = nullptr;
    }

    ~ProcessTxBaseMsgTestSuite() = default;

protected:
    processMempoolTx fakedMemProcessor;
    FakeMempoolProcessor     theFake;

private:
    boost::filesystem::path  pathTemp;
    const unsigned int       chainStateDbSize;
    CCoinsOnlyViewDB*        pChainStateDb;
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////// VALID TRANSACTIONS HANDLING /////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(ProcessTxBaseMsgTestSuite, ValidTxIsRelayed)
{
    CMutableTransaction mutValidTx;
    mutValidTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction validTx(mutValidTx);
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, validTx.GetHash())) == 0);

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(validTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, validTx.GetHash())) != 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, ValidTxIsRecordedAsKnown)
{
    CMutableTransaction mutValidTx;
    mutValidTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction validTx(mutValidTx);
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, validTx.GetHash())));

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(validTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(AlreadyHave(CInv(MSG_TX, validTx.GetHash())));
}

TEST_F(ProcessTxBaseMsgTestSuite, RetransmittedValidTxFromNonWhitelistedNodeIsNotRelayed)
{
    //Place a valid transaction in mempool, so that is marked as already known
    CMutableTransaction mutValidTx;
    mutValidTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction validTx(mutValidTx);
    CTxMemPoolEntry mempoolEntry(validTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(validTx.GetHash(), mempoolEntry);
    ASSERT_TRUE(AlreadyHave(CInv(MSG_TX, validTx.GetHash())));
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, validTx.GetHash())) == 0);

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(validTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, validTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, RetransmittedValidTxFromWhitelistedNodeIsRelayed)
{
    //Place a valid transaction in mempool, so that is marked as already known
    CMutableTransaction mutValidTx;
    mutValidTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction validTx(mutValidTx);
    CTxMemPoolEntry mempoolEntry(validTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(validTx.GetHash(), mempoolEntry);
    ASSERT_TRUE(AlreadyHave(CInv(MSG_TX, validTx.GetHash())));
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, validTx.GetHash())) == 0);

    FakeNode sourceNode{};
    sourceNode.fWhitelisted = true;
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(validTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, validTx.GetHash())) != 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, NoRejectMsgIsSentForValidATx)
{
    CMutableTransaction mutValidTx;
    mutValidTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction validTx(mutValidTx);

    FakeNode sourceNode{};
    ASSERT_TRUE(sourceNode.commandInvoked.empty());
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(validTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(sourceNode.commandInvoked.empty());
}

///////////////////////////////////////////////////////////////////////////////
////////////////// ORPHAN NON-JOINSPLIT TRANSACTIONS HANDLING /////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(ProcessTxBaseMsgTestSuite, MissingInputsNonJoinsplitTxIsNotRelayed)
{
    CMutableTransaction mutOrphanNonJoinSplitTx;
    mutOrphanNonJoinSplitTx.vin.push_back(CTxIn(uint256S("aaa"), 0));
    CTransaction orphanNonJoinsplitTx(mutOrphanNonJoinSplitTx);
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, orphanNonJoinsplitTx.GetHash())) == 0);

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanNonJoinsplitTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, orphanNonJoinsplitTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, MissingInputsNonJoinsplitTxIsRecordedAsKnown)
{
    CMutableTransaction mutOrphanNonJoinSplitTx;
    mutOrphanNonJoinSplitTx.vin.push_back(CTxIn(uint256S("aaa"), 0));
    CTransaction orphanNonJoinsplitTx(mutOrphanNonJoinSplitTx);
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, orphanNonJoinsplitTx.GetHash())));

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanNonJoinsplitTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(AlreadyHave(CInv(MSG_TX, orphanNonJoinsplitTx.GetHash())));
}

TEST_F(ProcessTxBaseMsgTestSuite, RetransmittedMissingInputsNonJoinsplitTxFromNonWhitelistedNodeIsNotRelayed)
{
    //Place an orphan non-joinsplit transaction in mempool, so that is marked as already known
    CMutableTransaction mutOrphanNonJoinSplitTx;
    mutOrphanNonJoinSplitTx.vin.push_back(CTxIn(uint256S("aaa"), 0));
    CTransaction orphanNonJoinsplitTx(mutOrphanNonJoinSplitTx);

    CTxMemPoolEntry mempoolEntry(orphanNonJoinsplitTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(orphanNonJoinsplitTx.GetHash(), mempoolEntry);
    ASSERT_TRUE(AlreadyHave(CInv(MSG_TX, orphanNonJoinsplitTx.GetHash())));
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, orphanNonJoinsplitTx.GetHash())) == 0);

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanNonJoinsplitTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, orphanNonJoinsplitTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, RetransmittedMissingInputsNonJoinsplitTxFromWhitelistedNodeIsRelayed)
{
    //Place an orphan non-joinsplit transaction in mempool, so that is marked as already known
    CMutableTransaction mutOrphanNonJoinSplitTx;
    mutOrphanNonJoinSplitTx.vin.push_back(CTxIn(uint256S("aaa"), 0));
    CTransaction orphanNonJoinsplitTx(mutOrphanNonJoinSplitTx);

    CTxMemPoolEntry mempoolEntry(orphanNonJoinsplitTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(orphanNonJoinsplitTx.GetHash(), mempoolEntry);
    ASSERT_TRUE(AlreadyHave(CInv(MSG_TX, orphanNonJoinsplitTx.GetHash())));
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, orphanNonJoinsplitTx.GetHash())) == 0);

    FakeNode sourceNode{};
    sourceNode.fWhitelisted = true;
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanNonJoinsplitTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, orphanNonJoinsplitTx.GetHash())) != 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, NoRejectMsgIsSentForMissingInputsNonJoinsplitTx)
{
    CMutableTransaction mutOrphanNonJoinSplitTx;
    mutOrphanNonJoinSplitTx.vin.push_back(CTxIn(uint256S("aaa"), 0));
    CTransaction orphanNonJoinsplitTx(mutOrphanNonJoinSplitTx);

    FakeNode sourceNode{};
    ASSERT_TRUE(sourceNode.commandInvoked.empty());
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanNonJoinsplitTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(sourceNode.commandInvoked.empty());
}
///////////////////////////////////////////////////////////////////////////////
//////////////////// ORPHAN JOINSPLIT TRANSACTIONS HANDLING ///////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(ProcessTxBaseMsgTestSuite, MissingInputsJoinsplitTxIsNotRelayed)
{
    CMutableTransaction mutOrphanJoinSplitTx;
    mutOrphanJoinSplitTx.vin.push_back(CTxIn(uint256S("aaa"), 0));
    mutOrphanJoinSplitTx.vjoinsplit.push_back(JSDescription::getNewInstance(GROTH_TX_VERSION));
    CTransaction orphanJoinsplitTx(mutOrphanJoinSplitTx);
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, orphanJoinsplitTx.GetHash())) == 0);

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanJoinsplitTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, orphanJoinsplitTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, MissingInputsJoinsplitTxFromWhitelistedPeerIsRelayed)
{
    CMutableTransaction mutOrphanJoinSplitTx;
    mutOrphanJoinSplitTx.vin.push_back(CTxIn(uint256S("aaa"), 0));
    mutOrphanJoinSplitTx.vjoinsplit.push_back(JSDescription::getNewInstance(GROTH_TX_VERSION));
    CTransaction orphanJoinsplitTx(mutOrphanJoinSplitTx);
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, orphanJoinsplitTx.GetHash())) == 0);

    FakeNode sourceNode{};
    sourceNode.fWhitelisted = true;
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanJoinsplitTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, orphanJoinsplitTx.GetHash())) != 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, MissingInputsJoinsplitTxIsRecordedAsKnown)
{
    CMutableTransaction mutOrphanJoinSplitTx;
    mutOrphanJoinSplitTx.vin.push_back(CTxIn(uint256S("aaa"), 0));
    mutOrphanJoinSplitTx.vjoinsplit.push_back(JSDescription::getNewInstance(GROTH_TX_VERSION));
    CTransaction orphanJoinsplitTx(mutOrphanJoinSplitTx);
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, orphanJoinsplitTx.GetHash())));

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanJoinsplitTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(AlreadyHave(CInv(MSG_TX, orphanJoinsplitTx.GetHash())));
}

TEST_F(ProcessTxBaseMsgTestSuite, RetransmittedMissingInputsJoinsplitTxFromNonWhitelistedNodeIsNotRelayed)
{
    //Place an orphan joinsplit transaction in mempool, so that is marked as already known
    CMutableTransaction mutOrphanJoinSplitTx;
    mutOrphanJoinSplitTx.vin.push_back(CTxIn(uint256S("aaa"), 0));
    mutOrphanJoinSplitTx.vjoinsplit.push_back(JSDescription::getNewInstance(GROTH_TX_VERSION));
    CTransaction orphanJoinsplitTx(mutOrphanJoinSplitTx);

    CTxMemPoolEntry mempoolEntry(orphanJoinsplitTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(orphanJoinsplitTx.GetHash(), mempoolEntry);
    ASSERT_TRUE(AlreadyHave(CInv(MSG_TX, orphanJoinsplitTx.GetHash())));
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, orphanJoinsplitTx.GetHash())) == 0);

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanJoinsplitTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, orphanJoinsplitTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, RetransmittedMissingInputsJoinsplitTxFromWhitelistedNodeIsRelayed)
{
    //Place an orphan joinsplit transaction in mempool, so that is marked as already known
    CMutableTransaction mutOrphanJoinSplitTx;
    mutOrphanJoinSplitTx.vin.push_back(CTxIn(uint256S("aaa"), 0));
    mutOrphanJoinSplitTx.vjoinsplit.push_back(JSDescription::getNewInstance(GROTH_TX_VERSION));
    CTransaction orphanJoinsplitTx(mutOrphanJoinSplitTx);

    CTxMemPoolEntry mempoolEntry(orphanJoinsplitTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(orphanJoinsplitTx.GetHash(), mempoolEntry);
    ASSERT_TRUE(AlreadyHave(CInv(MSG_TX, orphanJoinsplitTx.GetHash())));
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, orphanJoinsplitTx.GetHash())) == 0);

    FakeNode sourceNode{};
    sourceNode.fWhitelisted = true;
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanJoinsplitTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, orphanJoinsplitTx.GetHash())) != 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, NoRejectMsgIsSentForMissingInputsJoinsplitTx)
{
    CMutableTransaction mutOrphanJoinSplitTx;
    mutOrphanJoinSplitTx.vin.push_back(CTxIn(uint256S("aaa"), 0));
    mutOrphanJoinSplitTx.vjoinsplit.push_back(JSDescription::getNewInstance(GROTH_TX_VERSION));
    CTransaction orphanJoinsplitTx(mutOrphanJoinSplitTx);

    FakeNode sourceNode{};
    ASSERT_TRUE(sourceNode.commandInvoked.empty());
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanJoinsplitTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(sourceNode.commandInvoked.empty());
}

///////////////////////////////////////////////////////////////////////////////
/////////////////// INVALID ZERO-DOS TRANSACTIONS HANDLING ////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(ProcessTxBaseMsgTestSuite, InvalidZeroDosTxIsNotRelayed)
{
    CMutableTransaction mutInvalidZeroDoSTx;
    mutInvalidZeroDoSTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction invalidZeroDoSTx(mutInvalidZeroDoSTx);
    theFake.markTxAsInvalid(invalidZeroDoSTx.GetHash());
    theFake.DosLevelIfInvalid = 0;
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, invalidZeroDoSTx.GetHash())) == 0);

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidZeroDoSTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, invalidZeroDoSTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, InvalidZeroDosTxFromWhitelistedPeerIsRelayed)
{
    CMutableTransaction mutInvalidZeroDoSTx;
    mutInvalidZeroDoSTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction invalidZeroDoSTx(mutInvalidZeroDoSTx);
    theFake.markTxAsInvalid(invalidZeroDoSTx.GetHash());
    theFake.DosLevelIfInvalid = 0;
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, invalidZeroDoSTx.GetHash())) == 0);

    FakeNode sourceNode{};
    sourceNode.fWhitelisted = true;
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidZeroDoSTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, invalidZeroDoSTx.GetHash())) != 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, InvalidZeroDosTxIsRecordedAsKnown)
{
    CMutableTransaction mutInvalidZeroDoSTx;
    mutInvalidZeroDoSTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction invalidZeroDoSTx(mutInvalidZeroDoSTx);
    theFake.markTxAsInvalid(invalidZeroDoSTx.GetHash());
    theFake.DosLevelIfInvalid = 0;
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, invalidZeroDoSTx.GetHash())));

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidZeroDoSTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(AlreadyHave(CInv(MSG_TX, invalidZeroDoSTx.GetHash())));
}

TEST_F(ProcessTxBaseMsgTestSuite, RetransmittedInvalidZeroDosTxFromNonWhitelistedNodeIsNotRelayed)
{
    // Process the invalid zero-DoS tx once and then again, showing that
    // retransmission doesn't cause a relay
    CMutableTransaction mutInvalidZeroDoSTx;
    mutInvalidZeroDoSTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction invalidZeroDoSTx(mutInvalidZeroDoSTx);
    theFake.markTxAsInvalid(invalidZeroDoSTx.GetHash());
    theFake.DosLevelIfInvalid = 0;

    FakeNode sourceNode{};

    // process first time
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidZeroDoSTx, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);
    ASSERT_TRUE(AlreadyHave(CInv(MSG_TX, invalidZeroDoSTx.GetHash())));

    //test
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidZeroDoSTx, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, invalidZeroDoSTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, RetransmittedInvalidZeroDosTxFromWhitelistedNodeIsRelayed)
{
    // Process the invalid zero-DoS tx once and then again from whitelisted node,
    // showing that retransmission does cause a relay
    CMutableTransaction mutInvalidZeroDoSTx;
    mutInvalidZeroDoSTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction invalidZeroDoSTx(mutInvalidZeroDoSTx);
    theFake.markTxAsInvalid(invalidZeroDoSTx.GetHash());
    theFake.DosLevelIfInvalid = 0;

    FakeNode sourceNode{};

    // process first time
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidZeroDoSTx, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);
    ASSERT_TRUE(AlreadyHave(CInv(MSG_TX, invalidZeroDoSTx.GetHash())));

    // Set whitelist and retry
    sourceNode.fWhitelisted = true;

    //test
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidZeroDoSTx, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, invalidZeroDoSTx.GetHash())) != 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, RejectMsgIsSentForInvalidZeroDosTx)
{
    CMutableTransaction mutInvalidZeroDoSTx;
    mutInvalidZeroDoSTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction invalidZeroDoSTx(mutInvalidZeroDoSTx);
    theFake.markTxAsInvalid(invalidZeroDoSTx.GetHash());
    theFake.DosLevelIfInvalid = 0;

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidZeroDoSTx, &sourceNode);
    ASSERT_TRUE(sourceNode.commandInvoked.empty());

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(sourceNode.commandInvoked == std::string("rejecttx"));
}

///////////////////////////////////////////////////////////////////////////////
/////////////////// INVALID HIGH-DOS TRANSACTIONS HANDLING ////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(ProcessTxBaseMsgTestSuite, InvalidHighDosTxIsNotRelayed)
{
    CMutableTransaction mutInvalidHighDoSTx;
    mutInvalidHighDoSTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction invalidHighDoSTx(mutInvalidHighDoSTx);
    theFake.markTxAsInvalid(invalidHighDoSTx.GetHash());
    theFake.DosLevelIfInvalid = 100;
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, invalidHighDoSTx.GetHash())) == 0);

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidHighDoSTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, invalidHighDoSTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, InvalidHighDosTxFromWhitelistedPeerIsNotRelayed)
{
    CMutableTransaction mutInvalidHighDoSTx;
    mutInvalidHighDoSTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction invalidHighDoSTx(mutInvalidHighDoSTx);
    theFake.markTxAsInvalid(invalidHighDoSTx.GetHash());
    theFake.DosLevelIfInvalid = 100;
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, invalidHighDoSTx.GetHash())) == 0);

    FakeNode sourceNode{};
    sourceNode.fWhitelisted = true;
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidHighDoSTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, invalidHighDoSTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, InvalidHighDosTxIsRecordedAsKnown)
{
    CMutableTransaction mutInvalidHighDoSTx;
    mutInvalidHighDoSTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction invalidHighDoSTx(mutInvalidHighDoSTx);
    theFake.markTxAsInvalid(invalidHighDoSTx.GetHash());
    theFake.DosLevelIfInvalid = 100;
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, invalidHighDoSTx.GetHash())));

    FakeNode sourceNode{};
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidHighDoSTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(AlreadyHave(CInv(MSG_TX, invalidHighDoSTx.GetHash())));
}

TEST_F(ProcessTxBaseMsgTestSuite, RejectMsgIsSentForInvalidHighDosTx)
{
    CMutableTransaction mutInvalidHighDoSTx;
    mutInvalidHighDoSTx.vin.push_back(CTxIn(uint256{}, 0));
    CTransaction invalidHighDoSTx(mutInvalidHighDoSTx);
    theFake.markTxAsInvalid(invalidHighDoSTx.GetHash());
    theFake.DosLevelIfInvalid = 100;

    FakeNode sourceNode{};
    ASSERT_TRUE(sourceNode.commandInvoked.empty());
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidHighDoSTx, &sourceNode);

    //test
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(sourceNode.commandInvoked == std::string("rejecttx"));
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////// DEPENDENCIES HANDLING ////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(ProcessTxBaseMsgTestSuite, OrphanTxesTurningValidAreRelayed)
{
    //Generate a valid tx and two more txes zero-spending it
    CMutableTransaction mutValidTx;
    mutValidTx.vin.push_back(CTxIn(uint256{}, 0));
    mutValidTx.addOut(CTxOut(CAmount(10), CScript()));
    mutValidTx.addOut(CTxOut(CAmount(20), CScript()));
    CTransaction validTx(mutValidTx);

    CMutableTransaction mutOrphanTx_1;
    mutOrphanTx_1.vin.push_back(CTxIn(validTx.GetHash(), 0));
    CTransaction orphanTx_1(mutOrphanTx_1);

    CMutableTransaction mutOrphanTx_2;
    mutOrphanTx_2.vin.push_back(CTxIn(validTx.GetHash(), 1));
    CTransaction orphanTx_2(mutOrphanTx_2);

    FakeNode sourceNode{};

    // Orphan txes are inserted first and not relayed
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanTx_1, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, orphanTx_1.GetHash())) == 0);

    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanTx_2, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, orphanTx_2.GetHash())) == 0);

    //test finally the valid parent tx is processed
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(validTx, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, validTx.GetHash())) != 0);
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, orphanTx_1.GetHash())) != 0);
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, orphanTx_2.GetHash())) != 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, OrphanTxesStayingOrphanAreNotRelayed)
{
    //Generate a valid tx and an orphan tx NOT spending the valid one
    CMutableTransaction mutValidTx;
    mutValidTx.vin.push_back(CTxIn(uint256{}, 0)); //Valid since it spends "coinbase"
    mutValidTx.addOut(CTxOut(CAmount(10), CScript()));
    mutValidTx.addOut(CTxOut(CAmount(20), CScript()));
    CTransaction validTx(mutValidTx);

    CMutableTransaction mutOrphanTx;
    mutOrphanTx.vin.push_back(CTxIn(uint256S("aaa"), 0)); //Orphan since it spends an unknown input
    CTransaction orphanTx_1(mutOrphanTx);

    FakeNode sourceNode{};

    // Orphan txes are inserted first and not relayed
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(orphanTx_1, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, orphanTx_1.GetHash())) == 0);

    //test finally the valid parent tx is processed
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(validTx, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, validTx.GetHash())) != 0);
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, orphanTx_1.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, OrphanTxesTurningZeroDosInvalidAreNotRelayed)
{
    // Generate a valid tx and an orphan tx zero-spending it
    CMutableTransaction mutValidTx;
    mutValidTx.vin.push_back(CTxIn(uint256{}, 0));
    mutValidTx.addOut(CTxOut(CAmount(10), CScript()));
    CTransaction validTx(mutValidTx);

    CMutableTransaction mutInvalidMissingInputsTx;
    mutInvalidMissingInputsTx.vin.push_back(CTxIn(validTx.GetHash(), 0));
    CTransaction invalidMissingInputsTx(mutInvalidMissingInputsTx);

    FakeNode sourceNode{};

    // Orphan tx is inserted first and not relayed, classified as missing input
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidMissingInputsTx, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, invalidMissingInputsTx.GetHash())) == 0);

    // then orphan is marked as invalid
    theFake.DosLevelIfInvalid = 0;
    theFake.markTxAsInvalid(invalidMissingInputsTx.GetHash());

    //test finally the valid parent tx is processed
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(validTx, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, validTx.GetHash())) != 0);
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, invalidMissingInputsTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, OrphanTxesTurningHighDosInvalidAreNotRelayed)
{
    // Generate a valid tx and an orphan tx zero-spending it
    CMutableTransaction mutValidTx;
    mutValidTx.vin.push_back(CTxIn(uint256{}, 0));
    mutValidTx.addOut(CTxOut(CAmount(10), CScript()));
    CTransaction validTx(mutValidTx);

    CMutableTransaction mutInvalidMissingInputsTx;
    mutInvalidMissingInputsTx.vin.push_back(CTxIn(validTx.GetHash(), 0));
    CTransaction invalidMissingInputsTx(mutInvalidMissingInputsTx);

    FakeNode sourceNode{};

    // Orphan tx is inserted first and not relayed, classified as missing input
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(invalidMissingInputsTx, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);
    ASSERT_TRUE(mapRelay.count(CInv(MSG_TX, invalidMissingInputsTx.GetHash())) == 0);

    // then orphan is marked as invalid
    theFake.DosLevelIfInvalid = 100;
    theFake.markTxAsInvalid(invalidMissingInputsTx.GetHash());

    //test finally the valid parent tx is processed
    TxBaseMsgProcessor::get().addTxBaseMsgToProcess(validTx, &sourceNode);
    TxBaseMsgProcessor::get().ProcessTxBaseMsg(fakedMemProcessor);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, validTx.GetHash())) != 0);
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, invalidMissingInputsTx.GetHash())) == 0);
}
