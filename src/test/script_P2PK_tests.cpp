// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/script.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(script_P2PK_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(IsPayToPublicKey)
{
    // Test CScript::IsPayToPublicKey()
    vector<unsigned char> publicKey(33, 0);
    CScript p2pk;
    p2pk << ToByteVector(publicKey) << OP_CHECKSIG;
    BOOST_CHECK(p2pk.IsPayToPublicKey());

    static const unsigned char direct[] = {
        33, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_CHECKSIG
    };
    BOOST_CHECK(CScript(direct, direct+sizeof(direct)).IsPayToPublicKey());

    static const unsigned char p2pkh[] = {
        OP_DUP, OP_HASH160, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUALVERIFY, OP_CHECKSIG
    };
    BOOST_CHECK(!CScript(p2pkh, p2pkh+sizeof(p2pkh)).IsPayToPublicKey());

    static const unsigned char p2sh[] = {
        OP_HASH160, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUAL
    };
    BOOST_CHECK(!CScript(p2sh, p2sh+sizeof(p2sh)).IsPayToPublicKey());

    static const unsigned char notp2pkerrorbegin[] = {
        32, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_CHECKSIG
    };
    BOOST_CHECK(!CScript(notp2pkerrorbegin, notp2pkerrorbegin+sizeof(notp2pkerrorbegin)).IsPayToPublicKey());

    static const unsigned char notp2pkerrorend[] = {
        33, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 171
    };
    BOOST_CHECK(!CScript(notp2pkerrorend, notp2pkerrorend+sizeof(notp2pkerrorend)).IsPayToPublicKey());

    static const unsigned char notp2pkerrorduplicatebegin[] = {
        33, 33, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_CHECKSIG
    };
    BOOST_CHECK(!CScript(notp2pkerrorduplicatebegin, notp2pkerrorduplicatebegin+sizeof(notp2pkerrorduplicatebegin)).IsPayToPublicKey());

    static const unsigned char notp2pkerrorduplicateend[] = {
        33, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_CHECKSIG, OP_CHECKSIG
    };
    BOOST_CHECK(!CScript(notp2pkerrorduplicateend, notp2pkerrorduplicateend+sizeof(notp2pkerrorduplicateend)).IsPayToPublicKey());

    static const unsigned char notp2pkvoid[] = {
        0
    };
    BOOST_CHECK(!CScript(notp2pkvoid, notp2pkvoid+sizeof(notp2pkvoid)).IsPayToPublicKey());
}

BOOST_AUTO_TEST_SUITE_END()
