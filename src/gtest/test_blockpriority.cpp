#include <gtest/gtest.h>
#include "tx_creation_utils.h"
#include <coins.h>
#include <consensus/validation.h>
#include <main.h>
#include <undo.h>
#include <miner.h>

#include <librustzcash.h>

extern ZCJoinSplit *pzcashParams;

class BlockPriorityTestSuite: public ::testing::Test {
public:
    BlockPriorityTestSuite(): dummyBackingView() /*,view(&dummyBackingView)*/ ,dummyState(), dummyTxundo(),
                              k(libzcash::SpendingKey::random()),
                              addr(k.address()),
                              note(addr.a_pk, 100, uint256(), uint256())
                              {};

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        chainSettingUtils::GenerateChainActive(201);

        pcoinsTip = new CCoinsViewCache(&dummyBackingView);

        uint256 commitment = note.cm();
        merkleTree.append(commitment);
        pcoinsTip->PushAnchor(merkleTree);

        pcoinsTip->SetBestBlock(chainActive.Tip()->GetBlockHash());
        pindexBestHeader = chainActive.Tip();

        fDebug = true;
        fPrintToConsole = true;
        mapMultiArgs["-debug"].push_back("sc");
        mapMultiArgs["-debug"].push_back("mempool");
        mapArgs["-allownonstandardtx"] = "1";
        mapArgs["-deprecatedgetblocktemplate"] = "1";

        //Joinsplit
        boost::filesystem::path pk_path = ZC_GetParamsDir() / "sprout-proving.key";
        boost::filesystem::path vk_path = ZC_GetParamsDir() / "sprout-verifying.key";
        pzcashParams = ZCJoinSplit::Prepared(vk_path.string(), pk_path.string());
        boost::filesystem::path sapling_spend = ZC_GetParamsDir() / "sapling-spend.params";
        boost::filesystem::path sapling_output = ZC_GetParamsDir() / "sapling-output.params";
        boost::filesystem::path sprout_groth16 = ZC_GetParamsDir() / "sprout-groth16.params";

        std::string sapling_spend_str = sapling_spend.string();
        std::string sapling_output_str = sapling_output.string();
        std::string sprout_groth16_str = sprout_groth16.string();

        librustzcash_init_zksnark_params(
            sapling_spend_str.c_str(),
            "8270785a1a0d0bc77196f000ee6d221c9c9894f55307bd9357c3f0105d31ca63991ab91324160d8f53e2bbd3c2633a6eb8bdf5205d822e7f3f73edac51b2b70c",
            sapling_output_str.c_str(),
            "657e3d38dbb5cb5e7dd2970e8b03d69b4787dd907285b5a7f0790dcc8072f60bf593b32cc2d1c030e00ff5ae64bf84c5c3beb84ddc841d48264b4a171744d028",
            sprout_groth16_str.c_str(),
            "e9b238411bd6c0ec4791e9d04245ec350c9c5744f5610dfcce4365d5ca49dfefd5054e371842b3f88fa1b9d7e8e075249b3ebabd167fa8b0f3161292d36c180a"
        );
    };

    void TearDown() override {
        mempool.clear();

        delete pcoinsTip;
        pcoinsTip = nullptr;
    };

    ~BlockPriorityTestSuite() = default;

protected:
    CCoinsView      dummyBackingView;
//    CCoinsViewCache view;

    CValidationState dummyState;
    CTxUndo dummyTxundo;

    CTransaction makeTransparentTx(const CTxIn& input1, const CTxIn& input2, const CTxOut& output1, const CTxOut& output2);

    CTransaction makeJoinSplit(const uint256& jsPubKey);
    ZCIncrementalMerkleTree merkleTree;
    libzcash::SpendingKey k;
    libzcash::PaymentAddress addr;
    libzcash::Note note;
    uint256 commitment;
};

#include <key.h>
#include <zcash/Zcash.h>
#include <zcash/JoinSplit.hpp>
//#include <sodium/crypto_sign.h>
#include <sodium.h>
#include <script/interpreter.h>
#include <init.h>
TEST_F(BlockPriorityTestSuite, ShieldedTxFaultyPriorityInBlockFormation)
{
    //Generate coins in Mempool, enough to fill a block
    unsigned int txCounter = 0;
    unsigned int txTotalSize = 0;
    for(unsigned int round = 1; ; ++round)
    {
        //Generate input coin
        CTransaction inputTx = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(300000000), CScript()<<round<< OP_ADD<< round+1<< OP_EQUAL), CTxOut());
        UpdateCoins(inputTx, dummyState, *pcoinsTip, dummyTxundo, /*inputHeight*/100);
        ASSERT_TRUE(pcoinsTip->HaveCoins(inputTx.GetHash()));

        //Add Tx in mempool spending it
        CTransaction txForBlock = makeTransparentTx(CTxIn(inputTx.GetHash(), 0, CScript() << 1), CTxIn(), CTxOut(CAmount(100000000), CScript()<<OP_TRUE), CTxOut());
        if (txTotalSize + txForBlock.CalculateSize() > DEFAULT_BLOCK_MAX_SIZE -2000)
            break;

        ASSERT_TRUE(AcceptToMemoryPool(mempool, dummyState, txForBlock, false, nullptr));

        txTotalSize += txForBlock.CalculateSize();
        ++txCounter;
    }

    //Try push a max priority joinsplit
    unsigned char joinSplitPrivKey_[crypto_sign_SECRETKEYBYTES];
    uint256 joinSplitPubKey;
    crypto_sign_keypair(joinSplitPubKey.begin(), joinSplitPrivKey_);
    CMutableTransaction joinpsplitTx = makeJoinSplit(joinSplitPubKey);
    joinpsplitTx.joinSplitPubKey = joinSplitPubKey;

    CScript scriptCode;
    CTransaction signTx(joinpsplitTx);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);

    // Add the signature
    if (!(crypto_sign_detached(&joinpsplitTx.joinSplitSig[0], NULL,
           dataToBeSigned.begin(), 32,
           joinSplitPrivKey_
           ) == 0))
    {
       throw std::runtime_error("crypto_sign_detached failed");
    }

    // Sanity check
    if (!(crypto_sign_verify_detached(&joinpsplitTx.joinSplitSig[0],
           dataToBeSigned.begin(), 32,
           joinpsplitTx.joinSplitPubKey.begin()
           ) == 0))
    {
       throw std::runtime_error("crypto_sign_verify_detached failed");
    }

    ///////////////////////////////////////////////////
    /////////////////////////////////////////////////

    ASSERT_TRUE(AcceptToMemoryPool(mempool, dummyState, joinpsplitTx, false, nullptr));

    //Try to create the block and check if it is filled
    CBlockTemplate* res = CreateNewBlock(/*scriptPubKeyIn*/CScript());
    ASSERT_TRUE(res != nullptr);
    EXPECT_FALSE(res->block.vtx.size() == txCounter + 1)<<res->block.vtx.size() <<" at txCounter " << txCounter;
    //EXPECT_TRUE(false)<<"BLOCK VTX SIZE IS "<<res->block.vtx.size();
    EXPECT_TRUE(std::find(res->block.vtx.begin(),res->block.vtx.end(),CTransaction(joinpsplitTx)) != res->block.vtx.end());
    //EXPECT_TRUE(res->block.vtx[1] == joinpsplitTx)<<res->block.vtx[1].ToString();
    delete res;
    res = nullptr;
}

CTransaction BlockPriorityTestSuite::makeJoinSplit(const uint256& jsPubKey) {
    uint256 rt = merkleTree.root();
    auto witness = merkleTree.witness();

    // create JSDescription
    std::array<libzcash::JSInput, ZC_NUM_JS_INPUTS> inputs = {
        libzcash::JSInput(witness, note, k),
        libzcash::JSInput() // dummy input of zero value
    };
    std::array<libzcash::JSOutput, ZC_NUM_JS_OUTPUTS> outputs = {
        libzcash::JSOutput(addr, 50),
        libzcash::JSOutput(addr, 50)
    };

    auto verifier = libzcash::ProofVerifier::Strict();
    JSDescription jsdesc(/*isGroth*/true, *pzcashParams, jsPubKey, rt, inputs, outputs, 0, 0);
    jsdesc.Verify(*pzcashParams, verifier, jsPubKey);

    CMutableTransaction joinsplitTx;
    joinsplitTx.nVersion = GROTH_TX_VERSION;
    joinsplitTx.vjoinsplit.push_back(jsdesc);

    return joinsplitTx;
}

//TEST_F(BlockPriorityTestSuite, getPriority_TransparentTx_SingleInput) {
//    //Create input
//    CTransaction inputTx = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());
//
//    int inputHeight = 100;
//    UpdateCoins(inputTx, dummyState, view, dummyTxundo, inputHeight);
//    ASSERT_TRUE(view.HaveCoins(inputTx.GetHash()));
//
//    //Create tx spending input
//    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx.GetHash(), 0, CScript()), CTxIn(), CTxOut(), CTxOut());
//
//    //Test
//    int spendingHeight0 = inputHeight;
//    double priority0 = view.GetPriority(spendingTx,spendingHeight0);
//    EXPECT_TRUE(priority0 == 0.0)<<priority0;
//
//    //Test
//    int spendingHeight1 = spendingHeight0 + 1;
//    double priority1 = view.GetPriority(spendingTx,spendingHeight1);
//    EXPECT_TRUE(priority1 != 0.0);
//    EXPECT_TRUE(priority1 == inputTx.GetValueOut()*(spendingHeight1 - inputHeight)/spendingTx.CalculateModifiedSize())<<priority1;
//
//    //Test
//    int spendingHeight2 = spendingHeight0 + 2;
//    double priority2 = view.GetPriority(spendingTx,spendingHeight2);
//    EXPECT_TRUE(priority2 == 2*priority1)<<priority2;
//
//    //Test
//    int spendingHeight3 = spendingHeight0 + 7;
//    double priority3 = view.GetPriority(spendingTx,spendingHeight3);
//    EXPECT_TRUE(priority3 == 7*priority1)<<priority2;
//}
//
//TEST_F(BlockPriorityTestSuite, getPriority_TransparentTx_MultipleInput) {
//    //Create first Input
//    CTransaction inputTx1 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());
//
//    int inputHeight1 = 100;
//    UpdateCoins(inputTx1, dummyState, view, dummyTxundo, inputHeight1);
//    ASSERT_TRUE(view.HaveCoins(inputTx1.GetHash()));
//
//    //Create second Input
//    CTransaction inputTx2 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(40), CScript()), CTxOut());
//
//    int inputHeight2 = 110;
//    UpdateCoins(inputTx2, dummyState, view, dummyTxundo, inputHeight2);
//    ASSERT_TRUE(view.HaveCoins(inputTx2.GetHash()));
//
//    //Create tx spending input
//    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx1.GetHash(), 0, CScript()),CTxIn(inputTx2.GetHash(), 0, CScript()), CTxOut(), CTxOut());
//
//    //Test
//    int spendingHeight0 = std::max(inputHeight1,inputHeight2);
//    double priority0 = view.GetPriority(spendingTx,spendingHeight0);
//    EXPECT_TRUE(priority0 == 20.0)<<priority0;
//
//    //Test
//    int spendingHeight1 = spendingHeight0 + 1;
//    double priority1 = view.GetPriority(spendingTx,spendingHeight1);
//    EXPECT_TRUE(priority1 == 26.0)<<priority1;
//
//    //Test
//    int spendingHeight2 = spendingHeight0 + 2;
//    double priority2 = view.GetPriority(spendingTx,spendingHeight2);
//    EXPECT_TRUE(priority2 == 32)<<priority2;
//
//    //Test
//    int spendingHeight3 = spendingHeight0 + 7;
//    double priority3 = view.GetPriority(spendingTx,spendingHeight3);
//    EXPECT_TRUE(priority3 == 62)<<priority3;
//}
//
//TEST_F(BlockPriorityTestSuite, getPriority_JoinSplits) {
//    //Create tx spending input
//    CMutableTransaction spendingTx;
//    spendingTx.nVersion = PHGR_TX_VERSION;
//    spendingTx.vjoinsplit.resize(1);
//
//    //Test
//    int spendingHeight0 = 10;
//    double priority0 = view.GetPriority(spendingTx,spendingHeight0);
//    EXPECT_TRUE(priority0 == MAXIMUM_PRIORITY)<<priority0;
//
//    //Test
//    int spendingHeight1 = spendingHeight0 + 1;
//    double priority1 = view.GetPriority(spendingTx,spendingHeight1);
//    EXPECT_TRUE(priority1 == MAXIMUM_PRIORITY);
//
//    //Test
//    int spendingHeight2 = spendingHeight0 + 2;
//    double priority2 = view.GetPriority(spendingTx,spendingHeight2);
//    EXPECT_TRUE(priority2 == MAXIMUM_PRIORITY)<<priority2;
//
//    //Test
//    int spendingHeight3 = spendingHeight0 + 7;
//    double priority3 = view.GetPriority(spendingTx,spendingHeight3);
//    EXPECT_TRUE(priority3 == MAXIMUM_PRIORITY)<<priority2;
//}
//
//TEST_F(BlockPriorityTestSuite, getPriority_TransparentTx_SingleInput_InputInMempool) {
//    CTxMemPool mempool(CFeeRate(1));
//    CCoinsViewMemPool aMempool(&dummyBackingView, mempool);
//    view.SetBackend(aMempool);
//
//    //Create input and push to mempool
//    CTransaction inputTx = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());
//    CTxMemPoolEntry txPoolEntry(inputTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
//    mempool.addUnchecked(inputTx.GetHash(), txPoolEntry, true);
//    ASSERT_TRUE(view.HaveCoins(inputTx.GetHash()));
//
//    //Create tx spending input
//    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx.GetHash(), 0, CScript()),CTxIn(), CTxOut(), CTxOut());
//
//    //Test
//    int spendingHeight0 = 100;
//    double priority0 = view.GetPriority(spendingTx,spendingHeight0);
//    EXPECT_TRUE(priority0 == 0.0)<<priority0;
//
//    //Test
//    int spendingHeight1 = spendingHeight0 + 1;
//    double priority1 = view.GetPriority(spendingTx,spendingHeight1);
//    EXPECT_TRUE(priority1 == 0.0)<<priority1;
//
//    //Test
//    int spendingHeight2 = spendingHeight0 + 2;
//    double priority2 = view.GetPriority(spendingTx,spendingHeight2);
//    EXPECT_TRUE(priority2 == 0.0)<<priority2;
//
//    //Test
//    int spendingHeight3 = spendingHeight0 + 7;
//    double priority3 = view.GetPriority(spendingTx,spendingHeight3);
//    EXPECT_TRUE(priority3 == 0.0)<<priority3;
//}
//
////TEST_F(BlockPriorityTestSuite, getPriority_CTxMemPoolEntry) {
////    CTxMemPool mempool(CFeeRate(1));
////    CCoinsViewMemPool aMempool(&dummyBackingView, mempool);
////    view.SetBackend(aMempool);
////
////    //Create input and push to mempool
////    CTransaction inputTx = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(19), CScript()), CTxOut());
////
////    int poolEntryHeight = 100;
////    double poolEntryInitialPriority = 1.0;
////    CTxMemPoolEntry txPoolEntry(inputTx, /*fee*/CAmount(1), /*time*/ 1000, poolEntryInitialPriority, poolEntryHeight);
////    mempool.addUnchecked(inputTx.GetHash(), txPoolEntry, true);
////    ASSERT_TRUE(view.HaveCoins(inputTx.GetHash()));
////
////    //Test
////    int spendingHeight0 = poolEntryHeight;
////    double priority0 = txPoolEntry.GetPriority(spendingHeight0);
////    EXPECT_TRUE(priority0 == poolEntryInitialPriority)<<priority0;
////
////    //Test
////    int spendingHeight1 = spendingHeight0 + 1;
////    double priority1 = txPoolEntry.GetPriority(spendingHeight1);
////    EXPECT_TRUE(priority1 == 21)<<priority1;
////
////    //Test
////    int spendingHeight2 = spendingHeight0 + 2;
////    double priority2 = txPoolEntry.GetPriority(spendingHeight2);
////    EXPECT_TRUE(priority2 == 2*priority0 + poolEntryInitialPriority)<<priority2;
////
////    //Test
////    int spendingHeight3 = spendingHeight0 + 7;
////    double priority3 = txPoolEntry.GetPriority(spendingHeight3);
////    EXPECT_TRUE(priority3 == 7*priority0 + poolEntryInitialPriority)<<priority3;
////}
//
//TEST_F(BlockPriorityTestSuite, SimpleDependency)
//{
//    CBlock dummyBlock;
//    int64_t dummyMedianTimePast;
//
//    //Create input
//    CTransaction inputTx1 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());
//
//    int inputHeight = 100;
//    UpdateCoins(inputTx1, dummyState, view, dummyTxundo, inputHeight);
//    ASSERT_TRUE(view.HaveCoins(inputTx1.GetHash()));
//
//    //Create tx spending input
//    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx1.GetHash(), 0, CScript()), CTxIn(), CTxOut(CAmount(10), CScript()), CTxOut());
//    CTxMemPoolEntry txPoolEntry(spendingTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
//    mempool.addUnchecked(spendingTx.GetHash(), txPoolEntry, true);
//
//    //test
//    int nextTipHeight = 10;
//    std::vector<TxPriority> priorityVector;
//    std::list<COrphan> orphansList;
//    std::map<uint256, std::vector<COrphan*> > mapDependers;
//    GetBlockTxPriorityData(&dummyBlock, nextTipHeight, dummyMedianTimePast, view, priorityVector, orphansList, mapDependers);
//
//    //checks
//    EXPECT_TRUE(priorityVector.size() == 1)<<priorityVector.size();
//    EXPECT_TRUE(priorityVector.at(0).get<0>() == 2486558869.8947368)<<priorityVector.at(0).get<0>();
//    EXPECT_TRUE(priorityVector.at(0).get<1>() == CFeeRate(txPoolEntry.GetFee(), spendingTx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)));
//    EXPECT_TRUE(priorityVector.at(0).get<2>() == &mempool.mapTx[spendingTx.GetHash()].GetTx());
//
//    EXPECT_TRUE(orphansList.size() == 0)<<orphansList.size();
//    EXPECT_TRUE(mapDependers.size() == 0)<<mapDependers.size();
//}
//
//TEST_F(BlockPriorityTestSuite, SimpleOrphan)
//{
//    //Create input
//    CTransaction inputTx1 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());
//
//    int inputHeight = 100;
//    UpdateCoins(inputTx1, dummyState, view, dummyTxundo, inputHeight);
//    ASSERT_TRUE(view.HaveCoins(inputTx1.GetHash()));
//
//    //Create tx spending input
//    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx1.GetHash(), 0, CScript()), CTxIn(), CTxOut(CAmount(10), CScript()), CTxOut());
//    CTxMemPoolEntry txPoolEntry(spendingTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
//    mempool.addUnchecked(spendingTx.GetHash(), txPoolEntry, true);
//
//    //Create orphan tx
//    CTransaction orphanTx = makeTransparentTx(CTxIn(spendingTx.GetHash(), 0, CScript()), CTxIn(), CTxOut(), CTxOut());
//    CTxMemPoolEntry orphanPoolEntry(orphanTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
//    mempool.addUnchecked(orphanTx.GetHash(), orphanPoolEntry, true);
//
//    //test
//    int nextTipHeight = 10;
//    CBlock dummyBlock;
//    int64_t dummyMedianTimePast;
//    std::vector<TxPriority> priorityVector;
//    std::list<COrphan> orphansList;
//    std::map<uint256, std::vector<COrphan*> > mapDependers;
//    GetBlockTxPriorityData(&dummyBlock, nextTipHeight, dummyMedianTimePast, view, priorityVector, orphansList, mapDependers);
//
//    //checks
//    EXPECT_TRUE(priorityVector.size() == 1)<<priorityVector.size();
//    EXPECT_TRUE(priorityVector.at(0).get<0>() == 2486558869.8947368)<<priorityVector.at(0).get<0>();
//    EXPECT_TRUE(priorityVector.at(0).get<1>() == CFeeRate(txPoolEntry.GetFee(), spendingTx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)));
//    EXPECT_TRUE(priorityVector.at(0).get<2>() == &mempool.mapTx[spendingTx.GetHash()].GetTx());
//
//    EXPECT_TRUE(orphansList.size() == 1)<<orphansList.size();
//    EXPECT_TRUE(orphansList.back().ptx = &mempool.mapTx[orphanTx.GetHash()].GetTx());
//    EXPECT_TRUE(orphansList.back().setDependsOn.count(spendingTx.GetHash()));
//    EXPECT_TRUE(orphansList.back().dPriority == 0.0)<<orphansList.back().dPriority;
//
//    EXPECT_TRUE(mapDependers.size() == 1)<<mapDependers.size();
//    ASSERT_TRUE(mapDependers.count(spendingTx.GetHash()));
//    ASSERT_TRUE(mapDependers.at(spendingTx.GetHash()).size() == 1);
//    EXPECT_TRUE(mapDependers.at(spendingTx.GetHash())[0] == &orphansList.back());
//}
//
//TEST_F(BlockPriorityTestSuite, CompoundedOrphans)
//{
//    //Create input
//    CTransaction inputTx1 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());
//
//    int inputHeight = 100;
//    UpdateCoins(inputTx1, dummyState, view, dummyTxundo, inputHeight);
//    ASSERT_TRUE(view.HaveCoins(inputTx1.GetHash()));
//
//    //Create tx spending input
//    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx1.GetHash(), 0, CScript()), CTxIn(), CTxOut(CAmount(10), CScript()), CTxOut());
//    CTxMemPoolEntry txPoolEntry(spendingTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
//    mempool.addUnchecked(spendingTx.GetHash(), txPoolEntry, true);
//
//    //Create orphan tx
//    CTransaction orphanTx = makeTransparentTx(CTxIn(spendingTx.GetHash(), 0, CScript()), CTxIn(), CTxOut(CAmount(10), CScript()), CTxOut());
//    CTxMemPoolEntry orphanPoolEntry(orphanTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
//    mempool.addUnchecked(orphanTx.GetHash(), orphanPoolEntry, true);
//
//    //Create compuounded orphan tx
//    CTransaction orphan2Tx = makeTransparentTx(CTxIn(orphanTx.GetHash(), 0, CScript()), CTxIn(), CTxOut(), CTxOut());
//    CTxMemPoolEntry orphan2PoolEntry(orphan2Tx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
//    mempool.addUnchecked(orphan2Tx.GetHash(), orphan2PoolEntry, true);
//
//    //test
//    int nextTipHeight = 10;
//    CBlock dummyBlock;
//    int64_t dummyMedianTimePast;
//    std::vector<TxPriority> priorityVector;
//    std::list<COrphan> orphansList;
//    std::map<uint256, std::vector<COrphan*> > mapDependers;
//    GetBlockTxPriorityData(&dummyBlock, nextTipHeight, dummyMedianTimePast, view, priorityVector, orphansList, mapDependers);
//
//    //checks
//    EXPECT_TRUE(priorityVector.size() == 1)<<priorityVector.size();
//    EXPECT_TRUE(priorityVector.at(0).get<0>() == 2486558869.8947368)<<priorityVector.at(0).get<0>();
//    EXPECT_TRUE(priorityVector.at(0).get<1>() == CFeeRate(txPoolEntry.GetFee(), spendingTx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)));
//    EXPECT_TRUE(priorityVector.at(0).get<2>() == &mempool.mapTx[spendingTx.GetHash()].GetTx());
//
//    EXPECT_TRUE(orphansList.size() == 2)<<orphansList.size();
//    EXPECT_TRUE(orphansList.front().ptx = &mempool.mapTx[orphan2Tx.GetHash()].GetTx());
//    EXPECT_TRUE(orphansList.front().setDependsOn.count(orphanTx.GetHash()));
//    EXPECT_TRUE(orphansList.front().dPriority == 0.0)<<orphansList.front().dPriority;
//    EXPECT_TRUE(orphansList.back().ptx = &mempool.mapTx[orphanTx.GetHash()].GetTx());
//    EXPECT_TRUE(orphansList.back().setDependsOn.count(spendingTx.GetHash()));
//    EXPECT_TRUE(orphansList.back().dPriority == 0.0)<<orphansList.back().dPriority;
//
//    EXPECT_TRUE(mapDependers.size() == 2)<<mapDependers.size();
//    ASSERT_TRUE(mapDependers.count(spendingTx.GetHash()));
//    ASSERT_TRUE(mapDependers.at(spendingTx.GetHash()).size() == 1);
//    EXPECT_TRUE(mapDependers.at(spendingTx.GetHash())[0] == &orphansList.back());
//    ASSERT_TRUE(mapDependers.count(orphanTx.GetHash()));
//    ASSERT_TRUE(mapDependers.at(orphanTx.GetHash()).size() == 1);
//    EXPECT_TRUE(mapDependers.at(orphanTx.GetHash())[0] == &orphansList.front());
//}
//
//TEST_F(BlockPriorityTestSuite, MixedOrphans)
//{
//    //Create input
//    CTransaction inputTx1 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut(CAmount(30), CScript()));
//
//    int inputHeight = 100;
//    UpdateCoins(inputTx1, dummyState, view, dummyTxundo, inputHeight);
//    ASSERT_TRUE(view.HaveCoins(inputTx1.GetHash()));
//
//    //Create tx spending input
//    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx1.GetHash(), 0, CScript()), CTxIn(), CTxOut(CAmount(10), CScript()), CTxOut());
//    CTxMemPoolEntry txPoolEntry(spendingTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
//    mempool.addUnchecked(spendingTx.GetHash(), txPoolEntry, true);
//
//    //Create orphan tx
//    CTransaction orphanTx = makeTransparentTx(CTxIn(spendingTx.GetHash(), 0, CScript()), CTxIn(inputTx1.GetHash(), 1, CScript()), CTxOut(), CTxOut());
//    CTxMemPoolEntry orphanPoolEntry(orphanTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
//    mempool.addUnchecked(orphanTx.GetHash(), orphanPoolEntry, true);
//
//    //test
//    int nextTipHeight = 2000;
//    CBlock dummyBlock;
//    int64_t dummyMedianTimePast;
//    std::vector<TxPriority> priorityVector;
//    std::list<COrphan> orphansList;
//    std::map<uint256, std::vector<COrphan*> > mapDependers;
//    GetBlockTxPriorityData(&dummyBlock, nextTipHeight, dummyMedianTimePast, view, priorityVector, orphansList, mapDependers);
//
//    //checks
//    EXPECT_TRUE(priorityVector.size() == 1)<<priorityVector.size();
//    EXPECT_TRUE(priorityVector.at(0).get<0>() == 8.526315789473685)<<priorityVector.at(0).get<0>();
//    EXPECT_TRUE(priorityVector.at(0).get<1>() == CFeeRate(txPoolEntry.GetFee(), spendingTx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)));
//    EXPECT_TRUE(priorityVector.at(0).get<2>() == &mempool.mapTx[spendingTx.GetHash()].GetTx());
//
//    EXPECT_TRUE(orphansList.size() == 1)<<orphansList.size();
//    EXPECT_TRUE(orphansList.back().ptx = &mempool.mapTx[orphanTx.GetHash()].GetTx());
//    EXPECT_TRUE(orphansList.back().setDependsOn.count(spendingTx.GetHash()));
//    EXPECT_TRUE(orphansList.back().dPriority == 5700)<<orphansList.back().dPriority;
//
//    EXPECT_TRUE(mapDependers.size() == 1)<<mapDependers.size();
//    ASSERT_TRUE(mapDependers.count(spendingTx.GetHash()));
//    ASSERT_TRUE(mapDependers.at(spendingTx.GetHash()).size() == 1);
//    EXPECT_TRUE(mapDependers.at(spendingTx.GetHash())[0] == &orphansList.back());
//}

CTransaction BlockPriorityTestSuite::makeTransparentTx(const CTxIn& input1, const CTxIn& input2, const CTxOut& output1, const CTxOut& output2)
{
    static CTxIn dummyInput  = CTxIn();
    static CTxOut dummyOutput = CTxOut();

    CMutableTransaction res;
    res.nVersion = TRANSPARENT_TX_VERSION;

    if (input1 != dummyInput)
        res.vin.push_back(input1);

    if (input2 != dummyInput)
        res.vin.push_back(input2);

    if (output1 != dummyOutput)
        res.vout.push_back(output1);

    if (output2 != dummyOutput)
        res.vout.push_back(output2);

    return res;
}
