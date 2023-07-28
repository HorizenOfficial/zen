// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "txmempool.h"
#include "util.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <list>

BOOST_FIXTURE_TEST_SUITE(mempool_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(MempoolRemoveTest)
{
    // Test CTxMemPool::remove functionality

    // Parent transaction with three children,
    // and three grand-children:
    CMutableTransaction txParent;
    txParent.vin.resize(1);
    txParent.vin[0].scriptSig = CScript() << OP_11;
    txParent.resizeOut(3);
    for (int i = 0; i < 3; i++)
    {
        txParent.getOut(i).scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txParent.getOut(i).nValue = 33000LL;
    }
    CMutableTransaction txChild[3];
    for (int i = 0; i < 3; i++)
    {
        txChild[i].vin.resize(1);
        txChild[i].vin[0].scriptSig = CScript() << OP_11;
        txChild[i].vin[0].prevout.hash = txParent.GetHash();
        txChild[i].vin[0].prevout.n = i;
        txChild[i].resizeOut(1);
        txChild[i].getOut(0).scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txChild[i].getOut(0).nValue = 11000LL;
    }
    CMutableTransaction txGrandChild[3];
    for (int i = 0; i < 3; i++)
    {
        txGrandChild[i].vin.resize(1);
        txGrandChild[i].vin[0].scriptSig = CScript() << OP_11;
        txGrandChild[i].vin[0].prevout.hash = txChild[i].GetHash();
        txGrandChild[i].vin[0].prevout.n = 0;
        txGrandChild[i].resizeOut(1);
        txGrandChild[i].getOut(0).scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txGrandChild[i].getOut(0).nValue = 11000LL;
    }


    CTxMemPool testPool(CFeeRate(0), DEFAULT_MAX_MEMPOOL_SIZE_MB * 1000000);
    std::list<CTransaction>   removedTxs;
    std::list<CScCertificate> removedCerts;

    // Nothing in pool, remove should do nothing:
    testPool.remove(txParent, removedTxs, removedCerts, true);
    BOOST_CHECK_EQUAL(removedTxs.size(), 0);
    BOOST_CHECK_EQUAL(removedCerts.size(), 0);

    // Just the parent:
    testPool.addUnchecked(txParent.GetHash(), CTxMemPoolEntry(txParent, 0, 0, 0.0, 1));
    testPool.remove(txParent, removedTxs, removedCerts, true);
    BOOST_CHECK_EQUAL(removedTxs.size(), 1);
    BOOST_CHECK_EQUAL(removedCerts.size(), 0);
    removedTxs.clear();
    
    // Parent, children, grandchildren:
    testPool.addUnchecked(txParent.GetHash(), CTxMemPoolEntry(txParent, 0, 0, 0.0, 1));
    for (int i = 0; i < 3; i++)
    {
        testPool.addUnchecked(txChild[i].GetHash(), CTxMemPoolEntry(txChild[i], 0, 0, 0.0, 1));
        testPool.addUnchecked(txGrandChild[i].GetHash(), CTxMemPoolEntry(txGrandChild[i], 0, 0, 0.0, 1));
    }
    // Remove Child[0], GrandChild[0] should be removed:
    testPool.remove(txChild[0], removedTxs, removedCerts, true);
    BOOST_CHECK_EQUAL(removedTxs.size(), 2);
    removedTxs.clear();
    // ... make sure grandchild and child are gone:
    testPool.remove(txGrandChild[0], removedTxs, removedCerts, true);
    BOOST_CHECK_EQUAL(removedTxs.size(), 0);
    testPool.remove(txChild[0], removedTxs, removedCerts, true);
    BOOST_CHECK_EQUAL(removedTxs.size(), 0);
    // Remove parent, all children/grandchildren should go:
    testPool.remove(txParent, removedTxs, removedCerts, true);
    BOOST_CHECK_EQUAL(removedTxs.size(), 5);
    BOOST_CHECK_EQUAL(testPool.size(), 0);
    removedTxs.clear();

    // Add children and grandchildren, but NOT the parent (simulate the parent being in a block)
    for (int i = 0; i < 3; i++)
    {
        testPool.addUnchecked(txChild[i].GetHash(), CTxMemPoolEntry(txChild[i], 0, 0, 0.0, 1));
        testPool.addUnchecked(txGrandChild[i].GetHash(), CTxMemPoolEntry(txGrandChild[i], 0, 0, 0.0, 1));
    }
    // Now remove the parent, as might happen if a block-re-org occurs but the parent cannot be
    // put into the mempool (maybe because it is non-standard):
    testPool.remove(txParent, removedTxs, removedCerts, true);
    BOOST_CHECK_EQUAL(removedTxs.size(), 6);
    BOOST_CHECK_EQUAL(testPool.size(), 0);
    removedTxs.clear();
}

BOOST_AUTO_TEST_SUITE_END()
