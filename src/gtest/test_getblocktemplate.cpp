#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "coins.h"
#include "random.h"
#include "txdb.h"
#include "util.h"
#include "chainparams.h"
#include "script/script.h"
#include "script/sign.h"
#include "pubkey.h"
#include "key.h"
#include "keystore.h"
#include "txmempool.h"
#include "main.h"
#include "consensus/validation.h"
#include "miner.h"
#include <pow.h>
#include "wallet/wallet.h"
#include "rpc/server.h"
#include "pow.h"

#include <vector>
#include <map>
#include <utility>
#include <boost/chrono.hpp>
#include <boost/filesystem.hpp>

static const unsigned int NUM_BLOCKS = 2;
static const unsigned int NUM_FAKE_COINS = 20000;

static const unsigned int NUM_SPEND_TX = 40;
static const unsigned int INPUTS_IN_TX_0 = 40;
static const unsigned int INPUTS_IN_TX_1 = 10;

static const unsigned int COINS_DB_CACHE_SIZE_MB = 2;
static const unsigned int COINS_DB_CACHE_SIZE = COINS_DB_CACHE_SIZE_MB * 1024 * 1024;


class TestReserveKey : public CReserveKey
{
public:
    TestReserveKey()
        : CReserveKey(nullptr)
    {
        m_key.MakeNewKey(true);
    }
    virtual ~TestReserveKey()
    {
    }

    bool GetReservedKey(CPubKey &pubkey) override
    {
        pubkey = m_key.GetPubKey();
        return true;
    }

private:
    CKey m_key;
};

class TestCCoinsViewDB : public CCoinsViewDB
{
public:
    TestCCoinsViewDB(size_t nCacheSize, bool fWipe = false)
        : CCoinsViewDB(nCacheSize, DEFAULT_DB_MAX_OPEN_FILES, false, fWipe)
    {
    }

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

class TxFactory
{
public:
    TxFactory(unsigned int numFakeCoins, unsigned int numSpendTx, unsigned int numBlocks)
        : m_numFakeCoins(numFakeCoins)
        , m_numSpendTx(numSpendTx)
        , m_numBlocks(numBlocks)
    {
        InitKeys();
        InitScriptPubKey();
    }

    void Generate()
    {
        GenerateFakeCoinSet();
        GenerateSpendTxSet();
    }

private:
    void GenerateFakeCoinSet()
    {
        for (unsigned int i = 0; i < m_numFakeCoins; i++) {

            CCoinsCacheEntry entry;
            entry.flags = CCoinsCacheEntry::DIRTY;
            GenerateFakeCoin(entry.coins, i % 2);

            std::stringstream num;
            num << std::hex << i;

            const uint256 txid = uint256S(num.str());
            fakeCoins.emplace(txid, entry);
        }
    }
    
    void GenerateSpendTxSet()
    {
        LogPrintf("start GenerateSpendTxSet \n");
        ASSERT_LE((INPUTS_IN_TX_0/2 + INPUTS_IN_TX_1/2)*m_numSpendTx, m_numFakeCoins) << "Inputs number must be less than coins number.";


        CCoinsMap::const_iterator pos = fakeCoins.begin();
        CCoinsMap::const_iterator end = fakeCoins.end();

        spendTransactions.resize(m_numSpendTx);
        for(unsigned int txIndex = 0; txIndex < m_numSpendTx; ++txIndex)
        {
            CCoinsMap::const_iterator txPos = pos;
            CMutableTransaction& tx = spendTransactions[txIndex];

            tx.nVersion = 2;
            // split the transaction pool in 2 type of transactions different by number of input
            tx.vin.resize(txIndex%2 == 0 ? INPUTS_IN_TX_0 : INPUTS_IN_TX_1);

            CAmount amount(0);
            for(unsigned int inputIndex = 0; inputIndex < tx.vin.size(); ++inputIndex, ++pos)
            {
                tx.vin[inputIndex].prevout = COutPoint(pos->first, 0);
                amount += GetCacheEntryTxOut(pos->second, 0).nValue;
            }
            tx.resizeOut(1);
            tx.getOut(0).nValue = amount*0.9;
            tx.getOut(0).scriptPubKey = GetScriptPubKey();

            for(unsigned int inputNum = 0; inputNum < tx.vin.size(); ++inputNum, ++txPos)
            {
                EXPECT_TRUE(SignSignature(m_keystore, GetCacheEntryTxOut(txPos->second, 0).scriptPubKey, tx, inputNum));
            }
        }
        LogPrintf("end GenerateSpendTxSet \n");
    }

    const CTxOut& GetCacheEntryTxOut(const CCoinsCacheEntry& entry, unsigned int num)
    {
        return entry.coins.vout[num];
    }

    void InitKeys()
    {
        m_key.MakeNewKey(true);
        m_keystore.AddKey(m_key);
    }

    void InitScriptPubKey()
    {
        m_script << OP_DUP << OP_HASH160 << ToByteVector(m_key.GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    }

    CScript& GetScriptPubKey()
    {
        return m_script;
    }

    void GenerateFakeCoin(CCoins& coins, int height)
    {
        coins.fCoinBase = false;
        coins.nVersion = 2;
        coins.nHeight = height;

        coins.vout.resize(1);
        coins.vout[0].nValue = 1000000;
        coins.vout[0].scriptPubKey = GetScriptPubKey();
    }

public:
    CCoinsMap fakeCoins;
    std::vector<CMutableTransaction> spendTransactions;

private:
    const unsigned int m_numFakeCoins;
    const unsigned int m_numSpendTx;
    const unsigned int m_numBlocks;

    CKey m_key;
    CBasicKeyStore m_keystore;
    CScript m_script;
};

class GetBlockTemplateTest : public ::testing::Test {
protected:
    boost::filesystem::path pathTemp;
    GetBlockTemplateTest()
    {
        SetupParams();
        GenerateChainActive();
    }

    ~GetBlockTemplateTest() override
    {
        chainActive.SetTip(NULL);
        mapBlockIndex.clear();

        boost::system::error_code ec;
        boost::filesystem::remove_all(pathTemp.string()+"/regtest", ec);
    }

    void SetupParams()
    {
        pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories(pathTemp);
        mapArgs["-datadir"] = pathTemp.string();

        //fPrintToConsole = true;

        SelectParams(CBaseChainParams::REGTEST);
    }

    void GenerateChainActive()
    {
        // start from scratch
        chainActive.SetTip(NULL);
        mapBlockIndex.clear();

        blockHashes.resize(NUM_BLOCKS);
        blocks.resize(NUM_BLOCKS);

        for (unsigned int i=0; i<blocks.size(); i++) {
            InitBlock(i);
            mapBlockIndex.insert(std::make_pair(blocks[i].GetBlockHash(), &blocks[i]));
        }

        chainActive.SetTip(&lastBlock());
    }

    void InitBlock(size_t i)
    {
        blockHashes[i] = ArithToUint256(i);

        blocks[i].nHeight = i+1;
        blocks[i].pprev = i ? &blocks[i - 1] : NULL;
        blocks[i].phashBlock = &blockHashes[i];
        blocks[i].nTime = 1269211443 + i * Params().GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x1e7fffff;
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    CBlockIndex& lastBlock()
    {
        return blocks.back();
    }

    void InitBlockTreeDB()
    {
        pblocktree = new CBlockTreeDB(1 << 20, DEFAULT_DB_MAX_OPEN_FILES, true);
    }

    void InitSetupCoinsViewCache(CCoinsViewDB* dbCoins)
    {
        pcoinsTip = new CCoinsViewCache(dbCoins);

        ZCIncrementalMerkleTree tree;
        uint256 cm = GetRandHash();
        tree.append(cm);
        pcoinsTip->PushAnchor(tree);
        pcoinsTip->Flush();

        pcoinsTip->SetBestBlock(lastBlock().GetBlockHash());
    }

    void FillMempool(const std::vector<CMutableTransaction>& transactions)
    {
        for(const CMutableTransaction& tx : transactions)
        {
            //To be able to control the order of adding transactions in the blocktemplate based on the number of inputs we provide different fees.
            ASSERT_TRUE(mempool.addUnchecked(tx.GetHash(), CTxMemPoolEntry(tx, tx.getOut(0).nValue/0.9 *0.1, 0, 0.00, 1)));

        }

        ASSERT_EQ(mempool.size(), transactions.size());
    }

protected:
    std::vector<uint256> blockHashes;
    std::vector<CBlockIndex> blocks;
};

void TestMaxWeight(int maxWeight, std::map<int,int> expectedInputTx)
{
    TestReserveKey reserveKey;
    boost::chrono::steady_clock::time_point startTime = boost::chrono::steady_clock::now();

    boost::optional<CScript> scriptPubKey = GetMinerScriptPubKey(reserveKey);

    CBlockTemplate* pblocktemplate = CreateNewBlock(*scriptPubKey, maxWeight);
    boost::chrono::steady_clock::time_point endTime = boost::chrono::steady_clock::now();

    ASSERT_NE(pblocktemplate, nullptr);

    boost::chrono::duration<double> elapsedTime = endTime - startTime;


    for(const CTransaction& tx: pblocktemplate->block.vtx)
    {

        if(tx.IsCoinBase())
            continue;

        ASSERT_TRUE(expectedInputTx.count(tx.GetVin().size()) > 0);

        expectedInputTx.at(tx.GetVin().size()) --;

        if(expectedInputTx.at(tx.GetVin().size()) == 0) {
            expectedInputTx.erase(tx.GetVin().size());
        }

    }
    ASSERT_TRUE(expectedInputTx.empty());

    LogPrintf("Block transaction count = %d\n", pblocktemplate->block.vtx.size());
    LogPrintf("CreateNewBlock() takes %g seconds.\n", elapsedTime.count());
}

TEST_F(GetBlockTemplateTest, TxWith100Inputs)
{
    TxFactory txFactory(NUM_FAKE_COINS,  NUM_SPEND_TX, NUM_BLOCKS);
    txFactory.Generate();
    ASSERT_EQ(txFactory.fakeCoins.size(), NUM_FAKE_COINS);
    ASSERT_EQ(txFactory.spendTransactions.size(), NUM_SPEND_TX);

    TestCCoinsViewDB dbCoins(COINS_DB_CACHE_SIZE, true);
    ASSERT_TRUE(dbCoins.BatchWrite(txFactory.fakeCoins));

    InitBlockTreeDB();
    InitSetupCoinsViewCache(&dbCoins);
    FillMempool(txFactory.spendTransactions);

    try
    {
        //call getblocktemplate with different max weights and verify if the block contains the expected transactions.
        TestMaxWeight(INPUTS_IN_TX_0*INPUTS_IN_TX_0*5 + INPUTS_IN_TX_1*INPUTS_IN_TX_1*4 + 1, {{INPUTS_IN_TX_0, 5},{INPUTS_IN_TX_1, 4}});
        TestMaxWeight(INPUTS_IN_TX_0*INPUTS_IN_TX_0*10 + INPUTS_IN_TX_1*INPUTS_IN_TX_1*9 + 1,{{INPUTS_IN_TX_0, 10},{INPUTS_IN_TX_1, 9}});
        TestMaxWeight(INPUTS_IN_TX_0*INPUTS_IN_TX_0*NUM_SPEND_TX/2 + INPUTS_IN_TX_1*INPUTS_IN_TX_1*NUM_SPEND_TX/2 + 1,{{INPUTS_IN_TX_0, NUM_SPEND_TX/2},{INPUTS_IN_TX_1, NUM_SPEND_TX/2}});
        // test without max weight and verify if block contains all mempool transactions
        TestMaxWeight(0,{{INPUTS_IN_TX_0, NUM_SPEND_TX/2},{INPUTS_IN_TX_1, NUM_SPEND_TX/2}});

        ClearDatadirCache();
    }
    catch(std::exception& ex)
    {
        LogPrintf("Exeption: %s \n", ex.what());
        ClearDatadirCache();

        chainActive.SetTip(NULL);
        mapBlockIndex.clear();

        throw ex;
    }
}
