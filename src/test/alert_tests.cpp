// Copyright (c) 2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for PartitionAlert system
//
#include <iostream>
#include <fstream>

#include "chainparams.h"
#include "main.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(Alert_tests, TestingSetup)

static bool falseFunc() { return false; }

BOOST_AUTO_TEST_CASE(PartitionAlert)
{
    // Test PartitionCheck
    CCriticalSection csDummy;
    CBlockIndex indexDummy[400];
    CChainParams& params = Params(CBaseChainParams::MAIN);
    int64_t nPowTargetSpacing = params.GetConsensus().nPowTargetSpacing;

    // Generate fake blockchain timestamps relative to
    // an arbitrary time:
    int64_t now = 1427379054;
    SetMockTime(now);
    for (int i = 0; i < 400; i++)
    {
        indexDummy[i].phashBlock = NULL;
        if (i == 0) indexDummy[i].pprev = NULL;
        else indexDummy[i].pprev = &indexDummy[i-1];
        indexDummy[i].nHeight = i;
        indexDummy[i].nTime = now - (400-i)*nPowTargetSpacing;
        // Other members don't matter, the partition check code doesn't
        // use them
    }

    // Test 1: chain with blocks every nPowTargetSpacing seconds,
    // as normal, no worries:
    PartitionCheck(falseFunc, csDummy, &indexDummy[399], nPowTargetSpacing);
    BOOST_CHECK(strMiscWarning.empty());

    // Test 2: go 3.5 hours without a block, expect a warning:
    now += 3*60*60+30*60;
    SetMockTime(now);
    PartitionCheck(falseFunc, csDummy, &indexDummy[399], nPowTargetSpacing);
    BOOST_CHECK(!strMiscWarning.empty());
    BOOST_TEST_MESSAGE(std::string("Got alert text: ")+strMiscWarning);
    strMiscWarning = "";

    // Test 3: test the "partition alerts only go off once per day"
    // code:
    now += 60*10;
    SetMockTime(now);
    PartitionCheck(falseFunc, csDummy, &indexDummy[399], nPowTargetSpacing);
    BOOST_CHECK(strMiscWarning.empty());

    // Test 4: get 2.5 times as many blocks as expected:
    now += 60*60*24; // Pretend it is a day later
    SetMockTime(now);
    int64_t quickSpacing = nPowTargetSpacing*2/5;
    for (int i = 0; i < 400; i++) // Tweak chain timestamps:
        indexDummy[i].nTime = now - (400-i)*quickSpacing;
    PartitionCheck(falseFunc, csDummy, &indexDummy[399], nPowTargetSpacing);
    BOOST_CHECK(!strMiscWarning.empty());
    BOOST_TEST_MESSAGE(std::string("Got alert text: ")+strMiscWarning);
    strMiscWarning = "";

    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(AlertNotifyFunction) {
    std::string tempStr;
    boost::filesystem::path temp = GetTempPath() / boost::filesystem::unique_path("alertnotify-%%%%.txt");
    std::vector<std::string> errs {
        "Hard fork detected at block 32",
        "Large-work fork detected",
        "Chain state database corruption likely"
    };

    mapArgs["-alertnotify"] = std::string("echo 'Reporting the following alert: %s' >> ") + temp.string();

    BOOST_TEST_MESSAGE(std::string("Resetting temporary alert notify file: ") + temp.string());
    std::ofstream outF(temp.string(), std::ios::out);
    outF.close();

    BOOST_TEST_MESSAGE(std::string("Reporting alerts to alert notify file through alertNotify() function"));
    for (const auto& errorString : errs) {
        alertNotify(errorString, false);    // Using thread-safe version to preserve ordering
    }

    std::ifstream inputF(temp.string(), std::ios::in);
    BOOST_CHECK(inputF.is_open());

    for (size_t i = 0; std::getline(inputF, tempStr); i++) {
        BOOST_CHECK(std::string("Reporting the following alert: " + SanitizeString(errs[i])) == tempStr);
    }

    inputF.close();
    boost::filesystem::remove(temp);
}

BOOST_AUTO_TEST_SUITE_END()
