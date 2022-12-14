// Copyright (c) 2012-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/wallet.h"

#include <set>
#include <stdint.h>
#include <utility>
#include <vector>

#include "test/test_bitcoin.h"

#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>
#include "coinsselectionalgorithm.hpp"

using namespace std;

BOOST_FIXTURE_TEST_SUITE(wallet_tests, TestingSetup)

static CWallet wallet;
static vector<COutput> vCoins;

static void add_coin(const CAmount& nValue, int nAge = 6*24, bool fIsFromMe = false, int nInput=0)
{
    static int nextLockTime = 0;
    CMutableTransaction tx;
    tx.nLockTime = nextLockTime++;        // so all transactions get different hashes
    tx.resizeOut(nInput+1);
    tx.getOut(nInput).nValue = nValue;
    if (fIsFromMe) {
        // IsFromMe() returns (GetDebit() > 0), and GetDebit() is 0 if vin.empty(),
        // so stop vin being empty, and cache a non-zero Debit to fake out IsFromMe()
        tx.vin.resize(1);
    }
    CWalletTx* wtx = new CWalletTx(&wallet, tx);
    if (fIsFromMe)
    {
        wtx->SetfDebitCached(true);
        wtx->SetnDebitCached(1);
    }
    COutput output(wtx, nInput, nAge, true);
    vCoins.push_back(output);
}

static void empty_wallet(void)
{
    BOOST_FOREACH(COutput output, vCoins)
        delete output.tx;
    vCoins.clear();
}

BOOST_AUTO_TEST_CASE(coin_selection_tests)
{
    std::vector<COutput> vCoinsRet;
    CAmount nValueRet;
    size_t totalInputsBytes;

    LOCK(wallet.cs_wallet);

    empty_wallet();
    // with an empty wallet we can't even pay one cent
    BOOST_CHECK(!wallet.SelectCoinsMinConf( 1 * CENT, 1, 6, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    add_coin(1*CENT, 4);        // add a new 1 cent coin
    // with a new 1 cent coin, we still can't find a mature 1 cent
    BOOST_CHECK(!wallet.SelectCoinsMinConf( 1 * CENT, 1, 6, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    // but we can find a new 1 cent
    BOOST_CHECK( wallet.SelectCoinsMinConf( 1 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 1 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 1);
    add_coin(2*CENT);           // add a mature 2 cent coin
    // we can't make 3 cents of mature coins
    BOOST_CHECK(!wallet.SelectCoinsMinConf( 3 * CENT, 1, 6, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    // we can make 3 cents of new  coins
    BOOST_CHECK( wallet.SelectCoinsMinConf( 3 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 3 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 2);
    add_coin(5*CENT);           // add a mature 5 cent coin,
    add_coin(10*CENT, 3, true); // a new 10 cent coin sent from one of our own addresses
    add_coin(20*CENT);          // and a mature 20 cent coin
    // now we have new: 1+10=11 (of which 10 was self-sent), and mature: 2+5+20=27.  total = 38
    // we can't make 38 cents only if we disallow new coins:
    BOOST_CHECK(!wallet.SelectCoinsMinConf(38 * CENT, 1, 6, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    // we can't even make 37 cents if we don't allow new coins even if they're from us
    BOOST_CHECK(!wallet.SelectCoinsMinConf(38 * CENT, 6, 6, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    // but we can make 37 cents if we accept new coins from ourself
    BOOST_CHECK( wallet.SelectCoinsMinConf(37 * CENT, 1, 6, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 37 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 4);
    // and we can make 38 cents if we accept all new coins
    BOOST_CHECK( wallet.SelectCoinsMinConf(38 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 38 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 5);
    // try making 34 cents from 1,2,5,10,20 - we can't do it exactly
    BOOST_CHECK( wallet.SelectCoinsMinConf(34 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_GT(nValueRet, 34 * CENT); // but should get more than 34 cents
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 3); // the best should be 20+10+5
    // when we try making 7 cents, the smaller coins (1,2,5) are enough.  We should see just 2+5
    BOOST_CHECK( wallet.SelectCoinsMinConf( 7 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 7 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 2);
    // when we try making 8 cents, the smaller coins (1,2,5) are exactly enough.
    BOOST_CHECK( wallet.SelectCoinsMinConf( 8 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK(nValueRet == 8 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 3);
    // when we try making 9 cents, no subset of smaller coins is enough, and we get the next bigger coin (10)
    BOOST_CHECK( wallet.SelectCoinsMinConf( 9 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 10 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 1);
    // now clear out the wallet and start again to test choosing between subsets of smaller coins and the next biggest coin
    empty_wallet();
    add_coin( 6*CENT);
    add_coin( 7*CENT);
    add_coin( 8*CENT);
    add_coin(20*CENT);
    add_coin(30*CENT); // now we have 6+7+8+20+30 = 71 cents total
    // check that we have 71 and not 72
    BOOST_CHECK( wallet.SelectCoinsMinConf(71 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 71 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 5);
    BOOST_CHECK(!wallet.SelectCoinsMinConf(72 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    // now try making 16 cents.  the best smaller coins can do is 6+7+8 = 21; not as good as the next biggest coin, 20
    BOOST_CHECK( wallet.SelectCoinsMinConf(16 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 20 * CENT); // we should get 20 in one coin
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 1);
    add_coin( 5*CENT); // now we have 5+6+7+8+20+30 = 75 cents total
    // now if we try making 16 cents again, the smaller coins can make 5+6+7 = 18 cents, better than the next biggest coin, 20
    BOOST_CHECK( wallet.SelectCoinsMinConf(16 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 18 * CENT); // we should get 18 in 3 coins
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 3);
    add_coin( 18*CENT); // now we have 5+6+7+8+18+20+30
    // and now if we try making 16 cents again, the smaller coins can make 5+6+7 = 18 cents, the same as the next biggest coin, 18
    BOOST_CHECK( wallet.SelectCoinsMinConf(16 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 18 * CENT);  // we should get 18 in 3 coins
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 3); // because in the event of a tie, the larger utxos set wins
    // now try making 11 cents.  we should get 5+6
    BOOST_CHECK( wallet.SelectCoinsMinConf(11 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 11 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 2);
    // check that the smallest bigger coin is used
    add_coin( 1*COIN);
    add_coin( 2*COIN);
    add_coin( 3*COIN);
    add_coin( 4*COIN); // now we have 5+6+7+8+18+20+30+100+200+300+400 = 1094 cents
    BOOST_CHECK( wallet.SelectCoinsMinConf(95 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 1 * COIN);  // we should get 1 BTC in 1 coin
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 1);
    BOOST_CHECK( wallet.SelectCoinsMinConf(195 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 2 * COIN);  // we should get 2 BTC in 1 coin
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 1);
    // empty the wallet and start again, now with fractions of a cent, to test sub-cent change avoidance
    empty_wallet();
    add_coin(0.1*CENT);
    add_coin(0.2*CENT);
    add_coin(0.3*CENT);
    add_coin(0.4*CENT);
    add_coin(0.5*CENT);
    // try making 1 cent from 0.1 + 0.2 + 0.3 + 0.4 + 0.5 = 1.5 cents
    BOOST_CHECK( wallet.SelectCoinsMinConf(1 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 1 * CENT); // in this case 1 cents
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 4);
    // and if we add a bigger coin nothing changes:
    add_coin(1111*CENT);
    // try making 1 cent from 0.1 + 0.2 + 0.3 + 0.4 + 0.5 + 1111 = 1112.5 cents
    BOOST_CHECK( wallet.SelectCoinsMinConf(1 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 1 * CENT); // we get 1.0 cents in four coins
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 4); // also 0.5 + 0.4 + 0.1 was a candidate, but excluded
                                                                       // because in the event of a tie, the larger utxos set wins
    // if we add more sub-cent coins:
    add_coin(0.6*CENT);
    add_coin(0.7*CENT);
    // and try again to make 1.0 cents, again nothing changes
    BOOST_CHECK( wallet.SelectCoinsMinConf(1 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 1.0 * CENT); // in this case 1.0 cents in four coins
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 4);
    
    // run the 'mtgox' test (see http://blockexplorer.com/tx/29a3efd3ef04f9153d47a990bd7b048a4b2d213daaa5fb8ed670fb85f13bdbcf)
    // they tried to consolidate 10 50k coins into one 500k coin, and ended up with 50k in change
    empty_wallet();
    for (int i = 0; i < 20; i++)
        add_coin(50000 * COIN);
    BOOST_CHECK( wallet.SelectCoinsMinConf(500000 * COIN, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 500000 * COIN); // in this case 500000 coins
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 10); // in eleven coins
    // in case of sets of equal size we prioritize lower change
    empty_wallet();
    add_coin(0.5 * CENT);
    add_coin(0.6 * CENT);
    add_coin(0.7 * CENT);
    add_coin(1111 * CENT);
    BOOST_CHECK( wallet.SelectCoinsMinConf(1 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 1.1 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 2); // in two coins 0.5+0.6
    // again prioritizing lower change
    empty_wallet();
    add_coin(0.4 * CENT);
    add_coin(0.6 * CENT);
    add_coin(0.8 * CENT);
    add_coin(1111 * CENT);
    BOOST_CHECK( wallet.SelectCoinsMinConf(1 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 1 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 2); // in two coins 0.4+0.6
    
    empty_wallet();

    // create many small coins and one big coin
    for (int i = 0; i < 3000; i++)
    {
        add_coin(1*CENT);
    }
    add_coin(1000*CENT);
    // target an amount greater than the big coin, it is expected it gets included because using only small coins would result in oversizing
    BOOST_CHECK( wallet.SelectCoinsMinConf(3000 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, false));
    BOOST_CHECK_EQUAL(nValueRet, 3000 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 1 + 2000); // the big one and many small

    // setting useInputsNetValues to true leads to some counterintuitive outcomes at first glance
    // but if we consider that net value of a coin is always very slightly less than its gross value everything makes sense
    BOOST_CHECK( wallet.SelectCoinsMinConf(500 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, true));
    BOOST_CHECK_GT(nValueRet, 500 * CENT);
    BOOST_CHECK_GT(vCoinsRet.size(), 500); // few more than 500 coins are expected due to use of inputs net values

    empty_wallet();
    add_coin(0.1*CENT);
    add_coin(0.1*CENT);
    add_coin(0.2*CENT);
    add_coin(0.2*CENT);
    add_coin(0.3*CENT);
    add_coin(0.3*CENT);
    add_coin(0.4*CENT);
    add_coin(0.4*CENT);
    BOOST_CHECK( wallet.SelectCoinsMinConf(0.5 * CENT, 1, 1, vCoins, vCoinsRet, nValueRet, totalInputsBytes, MAX_TX_SIZE, true));
    // surely we cannot get a set of coins with total net value exactly 0.5
    // so we have to move to the first change level, in this case it is defined starting from sum of lower coins
    // totalLower = 2.0 -> changeLevel = totalLower / 10 = 0.2, hence our second try at nTargetValue would be [5.5, 0.7 = 0.5 + 0.2]
    // now a set of coins with total net value equals to 0.6-epsilon can be found, and the corresponding total gross value would be 0.6
    BOOST_CHECK_EQUAL(nValueRet, 0.6 * CENT);
    BOOST_CHECK_EQUAL(vCoinsRet.size(), 4);
}

BOOST_AUTO_TEST_SUITE_END()
