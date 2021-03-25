#include <gtest/gtest.h>
#include <gtest/tx_creation_utils.h>

#include <main.h>
#include <protocol.h>
#include <txdb.h>
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

MempoolReturnValue InvalidTxInMempool(CTxMemPool& pool, CValidationState &state, const CTransactionBase &txBase,
                                 LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee)
    {return MempoolReturnValue::INVALID;}

class ProcessTxBaseMsgTestSuite : public ::testing::Test
{
public:
    ProcessTxBaseMsgTestSuite():
        ValidTxMock(::ValidTxInMempool), OrphanTxMock(::OrphanTxInMempool), InvalidTxMock(::InvalidTxInMempool),
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

        delete pChainStateDb;
        pChainStateDb = nullptr;
    }

    ~ProcessTxBaseMsgTestSuite() = default;

protected:
    processMempoolTx ValidTxMock;
    processMempoolTx OrphanTxMock;
    processMempoolTx InvalidTxMock;

private:
    boost::filesystem::path  pathTemp;
    const unsigned int       chainStateDbSize;
    CCoinsOnlyViewDB*        pChainStateDb;
};

TEST_F(ProcessTxBaseMsgTestSuite, ValidUnknownTxIsRelayed)
{
    //prepare an unknown, valid tx
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
    //prepare an unknown, valid tx
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
