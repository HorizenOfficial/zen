// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for denial-of-service detection/prevention code
//



#include "keystore.h"
#include "main.h"
#include "net.h"
#include "pow.h"
#include "script/sign.h"
#include "serialize.h"
#include "util.h"

#include "test/test_bitcoin.h"

#include <stdint.h>

#include <boost/assign/list_of.hpp> // for 'map_list_of()'
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>
#include <txBaseMsgProcessor.h>

CService ip(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), Params().GetDefaultPort());
}

BOOST_FIXTURE_TEST_SUITE(DoS_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(DoS_banning)
{
    CNode::ClearBanned();
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    Misbehaving(dummyNode1.GetId(), 100); // Should get banned
    SendMessages(&dummyNode1, false);
    BOOST_CHECK(CNode::IsBanned(addr1));
    BOOST_CHECK(!CNode::IsBanned(ip(0xa0b0c001|0x0000ff00))); // Different IP, not banned

    CAddress addr2(ip(0xa0b0c002));
    CNode dummyNode2(INVALID_SOCKET, addr2, "", true);
    dummyNode2.nVersion = 1;
    Misbehaving(dummyNode2.GetId(), 50);
    SendMessages(&dummyNode2, false);
    BOOST_CHECK(!CNode::IsBanned(addr2)); // 2 not banned yet...
    BOOST_CHECK(CNode::IsBanned(addr1));  // ... but 1 still should be
    Misbehaving(dummyNode2.GetId(), 50);
    SendMessages(&dummyNode2, false);
    BOOST_CHECK(CNode::IsBanned(addr2));
}

BOOST_AUTO_TEST_CASE(DoS_banscore)
{
    CNode::ClearBanned();
    mapArgs["-banscore"] = "111"; // because 11 is my favorite number
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    Misbehaving(dummyNode1.GetId(), 100);
    SendMessages(&dummyNode1, false);
    BOOST_CHECK(!CNode::IsBanned(addr1));
    Misbehaving(dummyNode1.GetId(), 10);
    SendMessages(&dummyNode1, false);
    BOOST_CHECK(!CNode::IsBanned(addr1));
    Misbehaving(dummyNode1.GetId(), 1);
    SendMessages(&dummyNode1, false);
    BOOST_CHECK(CNode::IsBanned(addr1));
    mapArgs.erase("-banscore");
}

BOOST_AUTO_TEST_CASE(DoS_bantime)
{
    CNode::ClearBanned();
    int64_t nStartTime = GetTime();
    SetMockTime(nStartTime); // Overrides future calls to GetTime()
    CAddress addr(ip(0xa0b0c001));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = 1;
    Misbehaving(dummyNode.GetId(), 100);
    SendMessages(&dummyNode, false);
    BOOST_CHECK(CNode::IsBanned(addr));

    SetMockTime(nStartTime+60*60);
    BOOST_CHECK(CNode::IsBanned(addr));

    SetMockTime(nStartTime+60*60*24+1);
    BOOST_CHECK(!CNode::IsBanned(addr));
    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(DoS_mapOrphans)
{
    CKey key;
    key.MakeNewKey(true);
    CBasicKeyStore keystore;
    keystore.AddKey(key);

    // 50 orphan transactions:
    for (NodeId nodeId = 0; nodeId < 50; nodeId++)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = GetRandHash();
        tx.vin[0].scriptSig << OP_1;
        tx.resizeOut(1);
        tx.getOut(0).nValue = 1*CENT;
        tx.getOut(0).scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

        TxBaseMsgProcessor::get().AddOrphanTx(tx, nodeId);
    }

    // ... and 50 that depend on other orphans:
    for (NodeId nodeId = 0; nodeId < 50; nodeId++)
    {
        const CTransactionBase* txPrev = TxBaseMsgProcessor::get().PickRandomOrphan();

        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = txPrev->GetHash();
        tx.resizeOut(1);
        tx.getOut(0).nValue = 1*CENT;
        tx.getOut(0).scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        if (dynamic_cast<const CTransaction*>(txPrev))
        {
            SignSignature(keystore, *dynamic_cast<const CTransaction*>(txPrev), tx, 0);
        }
        else
        if (dynamic_cast<const CScCertificate*>(txPrev))
        {
            SignSignature(keystore, *dynamic_cast<const CScCertificate*>(txPrev), tx, 0);
        }

        TxBaseMsgProcessor::get().AddOrphanTx(tx, nodeId);
    }


    // This really-big orphan should be ignored:
    for (NodeId nodeId = 0; nodeId < 10; nodeId++)
    {
        const CTransactionBase* txPrev = TxBaseMsgProcessor::get().PickRandomOrphan();

        CMutableTransaction tx;
        tx.resizeOut(1);
        tx.getOut(0).nValue = 1*CENT;
        tx.getOut(0).scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        tx.vin.resize(500);
        for (unsigned int j = 0; j < tx.vin.size(); j++)
        {
            tx.vin[j].prevout.n = j;
            tx.vin[j].prevout.hash = txPrev->GetHash();
        }
        if (dynamic_cast<const CTransaction*>(txPrev))
        {
            SignSignature(keystore, *dynamic_cast<const CTransaction*>(txPrev), tx, 0);
        }
        else
        if (dynamic_cast<const CScCertificate*>(txPrev))
        {
            SignSignature(keystore, *dynamic_cast<const CScCertificate*>(txPrev), tx, 0);
        }
        // Re-use same signature for other inputs
        // (they don't have to be valid for this test)
        for (unsigned int j = 1; j < tx.vin.size(); j++)
            tx.vin[j].scriptSig = tx.vin[0].scriptSig;

        BOOST_CHECK(!TxBaseMsgProcessor::get().AddOrphanTx(tx, nodeId));
    }

    // Test EraseOrphansFor:
    for (NodeId nodeId = 0; nodeId < 3; nodeId++)
    {
        size_t sizeBefore = TxBaseMsgProcessor::get().countOrphans();
        TxBaseMsgProcessor::get().EraseOrphansFor(nodeId);
        BOOST_CHECK(TxBaseMsgProcessor::get().countOrphans() < sizeBefore);
    }

    // Test LimitOrphanTxSize() function:
    TxBaseMsgProcessor::get().LimitOrphanTxSize(40);
    BOOST_CHECK(TxBaseMsgProcessor::get().countOrphans() <= 40);
    TxBaseMsgProcessor::get().LimitOrphanTxSize(10);
    BOOST_CHECK(TxBaseMsgProcessor::get().countOrphans() <= 10);
    TxBaseMsgProcessor::get().LimitOrphanTxSize(0);
    BOOST_CHECK(TxBaseMsgProcessor::get().countOrphans() == 0);
}

BOOST_AUTO_TEST_SUITE_END()
