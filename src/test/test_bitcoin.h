// Copyright (c) 2021 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_TEST_TEST_BITCOIN_H
#define BITCOIN_TEST_TEST_BITCOIN_H

#include "chainparamsbase.h"
#include "pubkey.h"
#include "txdb.h"

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

/** Basic testing setup.
 * This just configures logging and chain parameters.
 */
struct BasicTestingSetup {
    ECCVerifyHandle globalVerifyHandle;

    BasicTestingSetup();
    ~BasicTestingSetup();
};

// Setup w.r.t. zk-SNARK API
struct JoinSplitTestingSetup: public BasicTestingSetup {
    JoinSplitTestingSetup();
    ~JoinSplitTestingSetup();
};

/** Testing setup that configures a complete environment.
 * Included are data directory, coins database, script check threads
 * and wallet (if enabled) setup.
 */
struct TestingSetup: public JoinSplitTestingSetup {
    CCoinsViewDB *pcoinsdbview;
    boost::filesystem::path pathTemp;
    boost::thread_group threadGroup;

    TestingSetup();
    ~TestingSetup();
};

#endif
