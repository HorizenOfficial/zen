#include <gtest/gtest.h>
#include <coins.h>
#include <consensus/validation.h>
#include <main.h>
#include <undo.h>
#include <miner.h>

class BlockPriorityTestSuite: public ::testing::Test {
public:
    BlockPriorityTestSuite(): dummyBackingView(), view(&dummyBackingView),dummyState(), dummyTxundo() {};
    ~BlockPriorityTestSuite() {mempool.clear();}

protected:
    CCoinsView      dummyBackingView;
    CCoinsViewCache view;

    CValidationState dummyState;
    CTxUndo dummyTxundo;

    CTransaction makeTransparentTx(const CTxIn& input1, const CTxIn& input2, const CTxOut& output1, const CTxOut& output2);
};

TEST_F(BlockPriorityTestSuite, getPriority_TransparentTx_SingleInput) {
    //Create input
    CTransaction inputTx = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());

    int inputHeight = 100;
    UpdateCoins(inputTx, dummyState, view, dummyTxundo, inputHeight);
    ASSERT_TRUE(view.HaveCoins(inputTx.GetHash()));

    //Create tx spending input
    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx.GetHash(), 0, CScript()), CTxIn(), CTxOut(), CTxOut());

    //Test
    int spendingHeight0 = inputHeight;
    double priority0 = view.GetPriority(spendingTx,spendingHeight0);
    EXPECT_TRUE(priority0 == 0.0)<<priority0;

    //Test
    int spendingHeight1 = spendingHeight0 + 1;
    double priority1 = view.GetPriority(spendingTx,spendingHeight1);
    EXPECT_TRUE(priority1 != 0.0);
    EXPECT_TRUE(priority1 == inputTx.GetValueOut()*(spendingHeight1 - inputHeight)/spendingTx.CalculateModifiedSize())<<priority1;

    //Test
    int spendingHeight2 = spendingHeight0 + 2;
    double priority2 = view.GetPriority(spendingTx,spendingHeight2);
    EXPECT_TRUE(priority2 == 2*priority1)<<priority2;

    //Test
    int spendingHeight3 = spendingHeight0 + 7;
    double priority3 = view.GetPriority(spendingTx,spendingHeight3);
    EXPECT_TRUE(priority3 == 7*priority1)<<priority2;
}

TEST_F(BlockPriorityTestSuite, getPriority_TransparentTx_MultipleInput) {
    //Create first Input
    CTransaction inputTx1 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());

    int inputHeight1 = 100;
    UpdateCoins(inputTx1, dummyState, view, dummyTxundo, inputHeight1);
    ASSERT_TRUE(view.HaveCoins(inputTx1.GetHash()));

    //Create second Input
    CTransaction inputTx2 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(40), CScript()), CTxOut());

    int inputHeight2 = 110;
    UpdateCoins(inputTx2, dummyState, view, dummyTxundo, inputHeight2);
    ASSERT_TRUE(view.HaveCoins(inputTx2.GetHash()));

    //Create tx spending input
    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx1.GetHash(), 0, CScript()),CTxIn(inputTx2.GetHash(), 0, CScript()), CTxOut(), CTxOut());

    //Test
    int spendingHeight0 = std::max(inputHeight1,inputHeight2);
    double priority0 = view.GetPriority(spendingTx,spendingHeight0);
    EXPECT_TRUE(priority0 == 20.0)<<priority0;

    //Test
    int spendingHeight1 = spendingHeight0 + 1;
    double priority1 = view.GetPriority(spendingTx,spendingHeight1);
    EXPECT_TRUE(priority1 == 26.0)<<priority1;

    //Test
    int spendingHeight2 = spendingHeight0 + 2;
    double priority2 = view.GetPriority(spendingTx,spendingHeight2);
    EXPECT_TRUE(priority2 == 32)<<priority2;

    //Test
    int spendingHeight3 = spendingHeight0 + 7;
    double priority3 = view.GetPriority(spendingTx,spendingHeight3);
    EXPECT_TRUE(priority3 == 62)<<priority3;
}

TEST_F(BlockPriorityTestSuite, getPriority_JoinSplits) {
    //Create tx spending input
    CMutableTransaction spendingTx;
    spendingTx.nVersion = PHGR_TX_VERSION;
    spendingTx.vjoinsplit.resize(1);

    //Test
    int spendingHeight0 = 10;
    double priority0 = view.GetPriority(spendingTx,spendingHeight0);
    EXPECT_TRUE(priority0 == MAXIMUM_PRIORITY)<<priority0;

    //Test
    int spendingHeight1 = spendingHeight0 + 1;
    double priority1 = view.GetPriority(spendingTx,spendingHeight1);
    EXPECT_TRUE(priority1 == MAXIMUM_PRIORITY);

    //Test
    int spendingHeight2 = spendingHeight0 + 2;
    double priority2 = view.GetPriority(spendingTx,spendingHeight2);
    EXPECT_TRUE(priority2 == MAXIMUM_PRIORITY)<<priority2;

    //Test
    int spendingHeight3 = spendingHeight0 + 7;
    double priority3 = view.GetPriority(spendingTx,spendingHeight3);
    EXPECT_TRUE(priority3 == MAXIMUM_PRIORITY)<<priority2;
}

TEST_F(BlockPriorityTestSuite, getPriority_TransparentTx_SingleInput_InputInMempool) {
    CTxMemPool mempool(CFeeRate(1));
    CCoinsViewMemPool aMempool(&dummyBackingView, mempool);
    view.SetBackend(aMempool);

    //Create input and push to mempool
    CTransaction inputTx = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());
    CTxMemPoolEntry txPoolEntry(inputTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(inputTx.GetHash(), txPoolEntry, true);
    ASSERT_TRUE(view.HaveCoins(inputTx.GetHash()));

    //Create tx spending input
    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx.GetHash(), 0, CScript()),CTxIn(), CTxOut(), CTxOut());

    //Test
    int spendingHeight0 = MEMPOOL_HEIGHT;
    double priority0 = view.GetPriority(spendingTx,spendingHeight0);
    EXPECT_TRUE(priority0 == 0.0)<<priority0;

    //Test
    int spendingHeight1 = spendingHeight0 + 1;
    double priority1 = view.GetPriority(spendingTx,spendingHeight1);
    EXPECT_TRUE(priority1 == 0.0)<<priority1;

    //Test
    int spendingHeight2 = spendingHeight0 + 2;
    double priority2 = view.GetPriority(spendingTx,spendingHeight2);
    EXPECT_TRUE(priority2 == 0.0)<<priority2;

    //Test
    int spendingHeight3 = spendingHeight0 + 7;
    double priority3 = view.GetPriority(spendingTx,spendingHeight3);
    EXPECT_TRUE(priority3 == 0.0)<<priority3;
}

//TEST_F(BlockPriorityTestSuite, getPriority_CTxMemPoolEntry) {
//    CTxMemPool mempool(CFeeRate(1));
//    CCoinsViewMemPool aMempool(&dummyBackingView, mempool);
//    view.SetBackend(aMempool);
//
//    //Create input and push to mempool
//    CTransaction inputTx = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(19), CScript()), CTxOut());
//
//    int poolEntryHeight = 100;
//    double poolEntryInitialPriority = 1.0;
//    CTxMemPoolEntry txPoolEntry(inputTx, /*fee*/CAmount(1), /*time*/ 1000, poolEntryInitialPriority, poolEntryHeight);
//    mempool.addUnchecked(inputTx.GetHash(), txPoolEntry, true);
//    ASSERT_TRUE(view.HaveCoins(inputTx.GetHash()));
//
//    //Test
//    int spendingHeight0 = poolEntryHeight;
//    double priority0 = txPoolEntry.GetPriority(spendingHeight0);
//    EXPECT_TRUE(priority0 == poolEntryInitialPriority)<<priority0;
//
//    //Test
//    int spendingHeight1 = spendingHeight0 + 1;
//    double priority1 = txPoolEntry.GetPriority(spendingHeight1);
//    EXPECT_TRUE(priority1 == 21)<<priority1;
//
//    //Test
//    int spendingHeight2 = spendingHeight0 + 2;
//    double priority2 = txPoolEntry.GetPriority(spendingHeight2);
//    EXPECT_TRUE(priority2 == 2*priority0 + poolEntryInitialPriority)<<priority2;
//
//    //Test
//    int spendingHeight3 = spendingHeight0 + 7;
//    double priority3 = txPoolEntry.GetPriority(spendingHeight3);
//    EXPECT_TRUE(priority3 == 7*priority0 + poolEntryInitialPriority)<<priority3;
//}

TEST_F(BlockPriorityTestSuite, SimpleDependency)
{
    CBlock dummyBlock;
    int64_t dummyMedianTimePast;

    //Create input
    CTransaction inputTx1 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());

    int inputHeight = 100;
    UpdateCoins(inputTx1, dummyState, view, dummyTxundo, inputHeight);
    ASSERT_TRUE(view.HaveCoins(inputTx1.GetHash()));

    //Create tx spending input
    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx1.GetHash(), 0, CScript()), CTxIn(), CTxOut(CAmount(10), CScript()), CTxOut());
    CTxMemPoolEntry txPoolEntry(spendingTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(spendingTx.GetHash(), txPoolEntry, true);

    //test
    int nextTipHeight = 10;
    std::vector<TxPriority> priorityVector;
    std::list<COrphan> orphansList;
    std::map<uint256, std::vector<COrphan*> > mapDependers;
    GetBlockTxPriorityData(&dummyBlock, nextTipHeight, dummyMedianTimePast, view, priorityVector, orphansList, mapDependers);

    //checks
    EXPECT_TRUE(priorityVector.size() == 1)<<priorityVector.size();
    EXPECT_TRUE(priorityVector.at(0).get<0>() == 2486558869.8947368)<<priorityVector.at(0).get<0>();
    EXPECT_TRUE(priorityVector.at(0).get<1>() == CFeeRate(txPoolEntry.GetFee(), spendingTx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)));
    EXPECT_TRUE(priorityVector.at(0).get<2>() == &mempool.mapTx[spendingTx.GetHash()].GetTx());

    EXPECT_TRUE(orphansList.size() == 0)<<orphansList.size();
    EXPECT_TRUE(mapDependers.size() == 0)<<mapDependers.size();
}

TEST_F(BlockPriorityTestSuite, SimpleOrphan)
{
    //Create input
    CTransaction inputTx1 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());

    int inputHeight = 100;
    UpdateCoins(inputTx1, dummyState, view, dummyTxundo, inputHeight);
    ASSERT_TRUE(view.HaveCoins(inputTx1.GetHash()));

    //Create tx spending input
    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx1.GetHash(), 0, CScript()), CTxIn(), CTxOut(CAmount(10), CScript()), CTxOut());
    CTxMemPoolEntry txPoolEntry(spendingTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(spendingTx.GetHash(), txPoolEntry, true);

    //Create orphan tx
    CTransaction orphanTx = makeTransparentTx(CTxIn(spendingTx.GetHash(), 0, CScript()), CTxIn(), CTxOut(), CTxOut());
    CTxMemPoolEntry orphanPoolEntry(orphanTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(orphanTx.GetHash(), orphanPoolEntry, true);

    //test
    int nextTipHeight = 10;
    CBlock dummyBlock;
    int64_t dummyMedianTimePast;
    std::vector<TxPriority> priorityVector;
    std::list<COrphan> orphansList;
    std::map<uint256, std::vector<COrphan*> > mapDependers;
    GetBlockTxPriorityData(&dummyBlock, nextTipHeight, dummyMedianTimePast, view, priorityVector, orphansList, mapDependers);

    //checks
    EXPECT_TRUE(priorityVector.size() == 1)<<priorityVector.size();
    EXPECT_TRUE(priorityVector.at(0).get<0>() == 2486558869.8947368)<<priorityVector.at(0).get<0>();
    EXPECT_TRUE(priorityVector.at(0).get<1>() == CFeeRate(txPoolEntry.GetFee(), spendingTx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)));
    EXPECT_TRUE(priorityVector.at(0).get<2>() == &mempool.mapTx[spendingTx.GetHash()].GetTx());

    EXPECT_TRUE(orphansList.size() == 1)<<orphansList.size();
    EXPECT_TRUE(orphansList.back().ptx = &mempool.mapTx[orphanTx.GetHash()].GetTx());
    EXPECT_TRUE(orphansList.back().setDependsOn.count(spendingTx.GetHash()));
    EXPECT_TRUE(orphansList.back().dPriority == 0.0)<<orphansList.back().dPriority;

    EXPECT_TRUE(mapDependers.size() == 1)<<mapDependers.size();
    ASSERT_TRUE(mapDependers.count(spendingTx.GetHash()));
    ASSERT_TRUE(mapDependers.at(spendingTx.GetHash()).size() == 1);
    EXPECT_TRUE(mapDependers.at(spendingTx.GetHash())[0] == &orphansList.back());
}

TEST_F(BlockPriorityTestSuite, CompoundedOrphans)
{
    //Create input
    CTransaction inputTx1 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut());

    int inputHeight = 100;
    UpdateCoins(inputTx1, dummyState, view, dummyTxundo, inputHeight);
    ASSERT_TRUE(view.HaveCoins(inputTx1.GetHash()));

    //Create tx spending input
    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx1.GetHash(), 0, CScript()), CTxIn(), CTxOut(CAmount(10), CScript()), CTxOut());
    CTxMemPoolEntry txPoolEntry(spendingTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(spendingTx.GetHash(), txPoolEntry, true);

    //Create orphan tx
    CTransaction orphanTx = makeTransparentTx(CTxIn(spendingTx.GetHash(), 0, CScript()), CTxIn(), CTxOut(CAmount(10), CScript()), CTxOut());
    CTxMemPoolEntry orphanPoolEntry(orphanTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(orphanTx.GetHash(), orphanPoolEntry, true);

    //Create compuounded orphan tx
    CTransaction orphan2Tx = makeTransparentTx(CTxIn(orphanTx.GetHash(), 0, CScript()), CTxIn(), CTxOut(), CTxOut());
    CTxMemPoolEntry orphan2PoolEntry(orphan2Tx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(orphan2Tx.GetHash(), orphan2PoolEntry, true);

    //test
    int nextTipHeight = 10;
    CBlock dummyBlock;
    int64_t dummyMedianTimePast;
    std::vector<TxPriority> priorityVector;
    std::list<COrphan> orphansList;
    std::map<uint256, std::vector<COrphan*> > mapDependers;
    GetBlockTxPriorityData(&dummyBlock, nextTipHeight, dummyMedianTimePast, view, priorityVector, orphansList, mapDependers);

    //checks
    EXPECT_TRUE(priorityVector.size() == 1)<<priorityVector.size();
    EXPECT_TRUE(priorityVector.at(0).get<0>() == 2486558869.8947368)<<priorityVector.at(0).get<0>();
    EXPECT_TRUE(priorityVector.at(0).get<1>() == CFeeRate(txPoolEntry.GetFee(), spendingTx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)));
    EXPECT_TRUE(priorityVector.at(0).get<2>() == &mempool.mapTx[spendingTx.GetHash()].GetTx());

    EXPECT_TRUE(orphansList.size() == 2)<<orphansList.size();
    EXPECT_TRUE(orphansList.front().ptx = &mempool.mapTx[orphan2Tx.GetHash()].GetTx());
    EXPECT_TRUE(orphansList.front().setDependsOn.count(orphanTx.GetHash()));
    EXPECT_TRUE(orphansList.front().dPriority == 0.0)<<orphansList.front().dPriority;
    EXPECT_TRUE(orphansList.back().ptx = &mempool.mapTx[orphanTx.GetHash()].GetTx());
    EXPECT_TRUE(orphansList.back().setDependsOn.count(spendingTx.GetHash()));
    EXPECT_TRUE(orphansList.back().dPriority == 0.0)<<orphansList.back().dPriority;

    EXPECT_TRUE(mapDependers.size() == 2)<<mapDependers.size();
    ASSERT_TRUE(mapDependers.count(spendingTx.GetHash()));
    ASSERT_TRUE(mapDependers.at(spendingTx.GetHash()).size() == 1);
    EXPECT_TRUE(mapDependers.at(spendingTx.GetHash())[0] == &orphansList.back());
    ASSERT_TRUE(mapDependers.count(orphanTx.GetHash()));
    ASSERT_TRUE(mapDependers.at(orphanTx.GetHash()).size() == 1);
    EXPECT_TRUE(mapDependers.at(orphanTx.GetHash())[0] == &orphansList.front());
}

TEST_F(BlockPriorityTestSuite, MixedOrphans)
{
    //Create input
    CTransaction inputTx1 = makeTransparentTx(CTxIn(), CTxIn(), CTxOut(CAmount(20), CScript()), CTxOut(CAmount(30), CScript()));

    int inputHeight = 100;
    UpdateCoins(inputTx1, dummyState, view, dummyTxundo, inputHeight);
    ASSERT_TRUE(view.HaveCoins(inputTx1.GetHash()));

    //Create tx spending input
    CTransaction spendingTx = makeTransparentTx(CTxIn(inputTx1.GetHash(), 0, CScript()), CTxIn(), CTxOut(CAmount(10), CScript()), CTxOut());
    CTxMemPoolEntry txPoolEntry(spendingTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(spendingTx.GetHash(), txPoolEntry, true);

    //Create orphan tx
    CTransaction orphanTx = makeTransparentTx(CTxIn(spendingTx.GetHash(), 0, CScript()), CTxIn(inputTx1.GetHash(), 1, CScript()), CTxOut(), CTxOut());
    CTxMemPoolEntry orphanPoolEntry(orphanTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(orphanTx.GetHash(), orphanPoolEntry, true);

    //test
    int nextTipHeight = 2000;
    CBlock dummyBlock;
    int64_t dummyMedianTimePast;
    std::vector<TxPriority> priorityVector;
    std::list<COrphan> orphansList;
    std::map<uint256, std::vector<COrphan*> > mapDependers;
    GetBlockTxPriorityData(&dummyBlock, nextTipHeight, dummyMedianTimePast, view, priorityVector, orphansList, mapDependers);

    //checks
    EXPECT_TRUE(priorityVector.size() == 1)<<priorityVector.size();
    EXPECT_TRUE(priorityVector.at(0).get<0>() == 8.526315789473685)<<priorityVector.at(0).get<0>();
    EXPECT_TRUE(priorityVector.at(0).get<1>() == CFeeRate(txPoolEntry.GetFee(), spendingTx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)));
    EXPECT_TRUE(priorityVector.at(0).get<2>() == &mempool.mapTx[spendingTx.GetHash()].GetTx());

    EXPECT_TRUE(orphansList.size() == 1)<<orphansList.size();
    EXPECT_TRUE(orphansList.back().ptx = &mempool.mapTx[orphanTx.GetHash()].GetTx());
    EXPECT_TRUE(orphansList.back().setDependsOn.count(spendingTx.GetHash()));
    EXPECT_TRUE(orphansList.back().dPriority == 5700)<<orphansList.back().dPriority;

    EXPECT_TRUE(mapDependers.size() == 1)<<mapDependers.size();
    ASSERT_TRUE(mapDependers.count(spendingTx.GetHash()));
    ASSERT_TRUE(mapDependers.at(spendingTx.GetHash()).size() == 1);
    EXPECT_TRUE(mapDependers.at(spendingTx.GetHash())[0] == &orphansList.back());
}

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
