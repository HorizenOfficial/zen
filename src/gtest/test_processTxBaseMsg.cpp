#include <gtest/gtest.h>
#include <gtest/tx_creation_utils.h>

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

MempoolReturnValue ValidTxInMempool(CTxMemPool& pool, CValidationState &state, const CTransactionBase &txBase,
                                    LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee)
    {
        if (txBase.IsCertificate())
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

MempoolReturnValue OrphanTxInMempool(CTxMemPool& pool, CValidationState &state, const CTransactionBase &txBase,
                                     LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee)
    {return MempoolReturnValue::MISSING_INPUT;}

MempoolReturnValue InvalidZeroDosTxInMempool(CTxMemPool& pool, CValidationState &state, const CTransactionBase &txBase,
                                      LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee)
    {
        state.DoS(0, false, REJECT_INVALID);
        return MempoolReturnValue::INVALID;
    }

MempoolReturnValue InvalidHighDosTxInMempool(CTxMemPool& pool, CValidationState &state, const CTransactionBase &txBase,
                                      LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee)
    {
        state.DoS(100, false, REJECT_INVALID);
        return MempoolReturnValue::INVALID;
    }

class ProcessTxBaseMsgTestSuite : public ::testing::Test
{
public:
    ProcessTxBaseMsgTestSuite():
        ValidTxMock(::ValidTxInMempool), OrphanTxMock(::OrphanTxInMempool),
        InvalidZeroDosTxMock(::InvalidZeroDosTxInMempool), InvalidHighDosTxMock(::InvalidHighDosTxInMempool),
        pathTemp(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path()),
        chainStateDbSize(0), pChainStateDb(nullptr) {};

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);
        boost::filesystem::create_directories(pathTemp);
        mapArgs["-datadir"] = pathTemp.string();

        UnloadBlockIndex();

        // trick to duly initialize recentRejects
        chainSettingUtils::ExtendChainActiveToHeight(0);
        ASSERT_TRUE(InitBlockIndex());

        pChainStateDb = new CCoinsOnlyViewDB(chainStateDbSize,/*fWipe*/true);
        pcoinsTip     = new CCoinsViewCache(pChainStateDb);
    }

    void TearDown() override {
        UnloadBlockIndex();
        mapRelay.clear();

        delete pChainStateDb;
        pChainStateDb = nullptr;
    }

    ~ProcessTxBaseMsgTestSuite() = default;

protected:
    processMempoolTx ValidTxMock;
    processMempoolTx OrphanTxMock;
    processMempoolTx InvalidZeroDosTxMock;
    processMempoolTx InvalidHighDosTxMock;

private:
    boost::filesystem::path  pathTemp;
    const unsigned int       chainStateDbSize;
    CCoinsOnlyViewDB*        pChainStateDb;
};

TEST_F(ProcessTxBaseMsgTestSuite, ValidUnknownTxIsRelayed)
{
    CTransaction aKnownTx = txCreationUtils::createTransparentTx();
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));

    //Prepare node from which tx originates
    CNode::ClearBanned();
    CAddress dummyAddr(CService(CNetAddr{}, Params().GetDefaultPort()));
    CNode sourceNode(INVALID_SOCKET, dummyAddr, "", true);

    //test
    ProcessTxBaseMsg(aKnownTx, &sourceNode, ValidTxMock);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, aKnownTx.GetHash())) != 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, ValidUnknownTxIsRecordedAsKnown)
{
    CTransaction aKnownTx = txCreationUtils::createTransparentTx();
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));

    //Prepare node from which tx originates
    CNode::ClearBanned();
    CAddress dummyAddr(CService(CNetAddr{}, Params().GetDefaultPort()));
    CNode sourceNode(INVALID_SOCKET, dummyAddr, "", true);

    //test
    ProcessTxBaseMsg(aKnownTx, &sourceNode, ValidTxMock);

    //checks
    EXPECT_TRUE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));
}

TEST_F(ProcessTxBaseMsgTestSuite, MissingInputsNonJoinsplitUnknownTxIsNotRelayed)
{
    CTransaction aKnownTx = txCreationUtils::createTransparentTx();
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));

    //Prepare node from which tx originates
    CNode::ClearBanned();
    CAddress dummyAddr(CService(CNetAddr{}, Params().GetDefaultPort()));
    CNode sourceNode(INVALID_SOCKET, dummyAddr, "", true);

    //test
    ProcessTxBaseMsg(aKnownTx, &sourceNode, OrphanTxMock);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, aKnownTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, MissingInputsNonJoinsplitUnknownTxIsRecordedAsKnown)
{
    CTransaction aKnownTx = txCreationUtils::createTransparentTx();
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));

    //Prepare node from which tx originates
    CNode::ClearBanned();
    CAddress dummyAddr(CService(CNetAddr{}, Params().GetDefaultPort()));
    CNode sourceNode(INVALID_SOCKET, dummyAddr, "", true);

    //test
    ProcessTxBaseMsg(aKnownTx, &sourceNode, OrphanTxMock);

    //checks
    EXPECT_TRUE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));
}

TEST_F(ProcessTxBaseMsgTestSuite, InvalidZeroDosUnknownTxIsNotRelayed)
{
    CTransaction aKnownTx = txCreationUtils::createTransparentTx();
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));

    //Prepare node from which tx originates
    CNode::ClearBanned();
    CAddress dummyAddr(CService(CNetAddr{}, Params().GetDefaultPort()));
    CNode sourceNode(INVALID_SOCKET, dummyAddr, "", true);

    //test
    ProcessTxBaseMsg(aKnownTx, &sourceNode, InvalidZeroDosTxMock);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, aKnownTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, InvalidZeroDosUnknownTxFromWhitelistedPeerIsRelayed)
{
    CTransaction aKnownTx = txCreationUtils::createTransparentTx();
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));

    //Prepare node from which tx originates
    CNode::ClearBanned();
    CAddress dummyAddr(CService(CNetAddr{}, Params().GetDefaultPort()));
    CNode sourceNode(INVALID_SOCKET, dummyAddr, "", true);
    sourceNode.fWhitelisted = true;

    //test
    ProcessTxBaseMsg(aKnownTx, &sourceNode, InvalidZeroDosTxMock);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, aKnownTx.GetHash())) != 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, InvalidZeroDosUnknownUnknownTxIsRecordedAsKnown)
{
    CTransaction aKnownTx = txCreationUtils::createTransparentTx();
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));

    //Prepare node from which tx originates
    CNode::ClearBanned();
    CAddress dummyAddr(CService(CNetAddr{}, Params().GetDefaultPort()));
    CNode sourceNode(INVALID_SOCKET, dummyAddr, "", true);

    //test
    ProcessTxBaseMsg(aKnownTx, &sourceNode, InvalidZeroDosTxMock);

    //checks
    EXPECT_TRUE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));
}

TEST_F(ProcessTxBaseMsgTestSuite, InvalidHighDosUnknownTxIsNotRelayed)
{
    CTransaction aKnownTx = txCreationUtils::createTransparentTx();
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));

    //Prepare node from which tx originates
    CNode::ClearBanned();
    CAddress dummyAddr(CService(CNetAddr{}, Params().GetDefaultPort()));
    CNode sourceNode(INVALID_SOCKET, dummyAddr, "", true);

    //test
    ProcessTxBaseMsg(aKnownTx, &sourceNode, InvalidHighDosTxMock);

    //checks
    EXPECT_TRUE(mapRelay.count(CInv(MSG_TX, aKnownTx.GetHash())) == 0);
}

TEST_F(ProcessTxBaseMsgTestSuite, InvalidHighDosUnknownUnknownTxIsRecordedAsKnown)
{
    CTransaction aKnownTx = txCreationUtils::createTransparentTx();
    ASSERT_FALSE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));

    //Prepare node from which tx originates
    CNode::ClearBanned();
    CAddress dummyAddr(CService(CNetAddr{}, Params().GetDefaultPort()));
    CNode sourceNode(INVALID_SOCKET, dummyAddr, "", true);

    //test
    ProcessTxBaseMsg(aKnownTx, &sourceNode, InvalidHighDosTxMock);

    //checks
    EXPECT_TRUE(AlreadyHave(CInv(MSG_TX, aKnownTx.GetHash())));
}
