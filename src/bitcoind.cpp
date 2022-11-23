// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "clientversion.h"
#include "rpc/server.h"
#include "init.h"
#include "main.h"
#include "noui.h"
#include "scheduler.h"
#include "util.h"
#include "httpserver.h"
#include "httprpc.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include <stdio.h>

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for an experimental new digital currency called Bitcoin (https://www.bitcoin.org/),
 * which enables instant payments to anyone, anywhere in the world. Bitcoin uses peer-to-peer technology to operate
 * with no central authority: managing transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start navigating the code.
 */

static bool fDaemon;

void WaitForShutdown(boost::thread_group* threadGroup)
{
    bool fShutdown = ShutdownRequested();
    // Tell the main threads to shutdown.
    while (!fShutdown)
    {
        MilliSleep(200);
        fShutdown = ShutdownRequested();
    }
    if (threadGroup)
    {
        Interrupt(*threadGroup);
        threadGroup->join_all();
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//

#include "coinsselectionalgorithm.hpp"

bool AppInit(int argc, char* argv[])
{
    {
        for (int i = 0; i <= 10; i++)
        {
            int testFlow = 8;
            std::vector<std::pair<CAmount, size_t>> _amountsAndSizes;
            CAmount _targetAmount = 0;
            CAmount _targetAmountPlusOffset = 0;
            size_t _availableTotalSize = 0;

            if (testFlow == 3)
            {
                _amountsAndSizes = { std::make_pair(80,20),
                                     std::make_pair(11,15),
                                     std::make_pair(28,16),
                                     std::make_pair(99,17),
                                     std::make_pair(15,21),
                                     std::make_pair(19,20),
                                     std::make_pair(55,20),
                                     std::make_pair(65,19),
                                     std::make_pair(40,21),
                                     std::make_pair(10,22) };
                _targetAmount = 80;
                _targetAmountPlusOffset = 80 + i;
                _availableTotalSize = 100;
            }
            if (testFlow == 8)
            {
                _amountsAndSizes = { std::make_pair(80,20),
                                     std::make_pair(11,15),
                                     std::make_pair(28,16),
                                     std::make_pair(99,17),
                                     std::make_pair(15,21),
                                     std::make_pair(19,20),
                                     std::make_pair(55,20),
                                     std::make_pair(65,19),
                                     std::make_pair(40,21),
                                     std::make_pair(10,22),
                                     std::make_pair(01,21),
                                     std::make_pair(02,19),
                                     std::make_pair(10,18),
                                     std::make_pair(49,22),
                                     std::make_pair(98,20),
                                     std::make_pair(44,20),
                                     std::make_pair(56,20),
                                     std::make_pair(15,19),
                                     std::make_pair(99,15),
                                     std::make_pair(10,13),
                                     std::make_pair(80,20),
                                     std::make_pair(11,15),
                                     std::make_pair(28,16),
                                     std::make_pair(99,17),
                                     std::make_pair(15,21),
                                     std::make_pair(19,20),
                                     std::make_pair(55,20),
                                     std::make_pair(65,19),
                                     std::make_pair(40,21),
                                     std::make_pair(10,22),
                                     std::make_pair(01,21),
                                     std::make_pair(02,19),
                                     std::make_pair(10,18),
                                     std::make_pair(49,22),
                                     std::make_pair(98,20),
                                     std::make_pair(44,20),
                                     std::make_pair(56,20),
                                     std::make_pair(15,19),
                                     std::make_pair(99,15),
                                     std::make_pair(10,13),
                                     std::make_pair(80,20),
                                     std::make_pair(11,15),
                                     std::make_pair(28,16),
                                     std::make_pair(99,17),
                                     std::make_pair(15,21),
                                     std::make_pair(19,20),
                                     std::make_pair(55,20),
                                     std::make_pair(65,19),
                                     std::make_pair(40,21),
                                     std::make_pair(10,22),
                                     std::make_pair(01,21),
                                     std::make_pair(02,19),
                                     std::make_pair(10,18),
                                     std::make_pair(49,22),
                                     std::make_pair(98,20),
                                     std::make_pair(44,20),
                                     std::make_pair(56,20),
                                     std::make_pair(15,19),
                                     std::make_pair(99,15),
                                     std::make_pair(10,13),
                                     std::make_pair(80,20),
                                     std::make_pair(11,15),
                                     std::make_pair(28,16),
                                     std::make_pair(99,17),
                                     std::make_pair(15,21),
                                     std::make_pair(19,20),
                                     std::make_pair(55,20),
                                     std::make_pair(65,19),
                                     std::make_pair(40,21),
                                     std::make_pair(10,22),
                                     std::make_pair(01,21),
                                     std::make_pair(02,19),
                                     std::make_pair(10,18),
                                     std::make_pair(49,22),
                                     std::make_pair(98,20),
                                     std::make_pair(44,20),
                                     std::make_pair(56,20),
                                     std::make_pair(15,19),
                                     std::make_pair(99,15),
                                     std::make_pair(10,13),
                                     std::make_pair(80,20),
                                     std::make_pair(11,15),
                                     std::make_pair(28,16),
                                     std::make_pair(99,17),
                                     std::make_pair(15,21),
                                     std::make_pair(19,20),
                                     std::make_pair(55,20),
                                     std::make_pair(65,19),
                                     std::make_pair(40,21),
                                     std::make_pair(10,22),
                                     std::make_pair(01,21),
                                     std::make_pair(02,19),
                                     std::make_pair(10,18),
                                     std::make_pair(49,22),
                                     std::make_pair(98,20),
                                     std::make_pair(44,20),
                                     std::make_pair(56,20),
                                     std::make_pair(15,19),
                                     std::make_pair(99,15),
                                     std::make_pair(10,13),
                                     std::make_pair(80,20),
                                     std::make_pair(11,15),
                                     std::make_pair(28,16),
                                     std::make_pair(99,17),
                                     std::make_pair(15,21),
                                     std::make_pair(19,20),
                                     std::make_pair(55,20),
                                     std::make_pair(65,19),
                                     std::make_pair(40,21),
                                     std::make_pair(10,22),
                                     std::make_pair(01,21),
                                     std::make_pair(02,19),
                                     std::make_pair(10,18),
                                     std::make_pair(49,22),
                                     std::make_pair(98,20),
                                     std::make_pair(44,20),
                                     std::make_pair(56,20),
                                     std::make_pair(15,19),
                                     std::make_pair(99,15),
                                     std::make_pair(10,13) };
                _targetAmount = 80;
                _targetAmountPlusOffset = 80 + i;
                _availableTotalSize = 1000;
            }

            CCoinsSelectionBranchAndBound algo(_amountsAndSizes, _targetAmount, _targetAmountPlusOffset, _availableTotalSize);
            algo.Solve();
        }
    }
    
    boost::thread_group threadGroup;
    CScheduler scheduler;

    bool fRet = false;

    //
    // Parameters
    //
    // If Qt is used, parameters/zen.conf are parsed in qt/bitcoin.cpp's main()
    ParseParameters(argc, argv);

    // Process help and version before taking care about datadir
    if (mapArgs.count("-?") || mapArgs.count("-h") ||  mapArgs.count("-help") || mapArgs.count("-version"))
    {
        std::string strUsage = _("Zen Daemon") + " " + _("version") + " " + FormatFullVersion() + "\n";

        if (mapArgs.count("-version"))
        {
            strUsage += LicenseInfo();
        }
        else
        {
            strUsage += "\n" + _("Usage:") + "\n" +
                  "  zend [options]                     " + _("Start Zen Daemon") + "\n";

            strUsage += "\n" + HelpMessage(HMM_BITCOIND);
        }

        fprintf(stdout, "%s", strUsage.c_str());
        return false;
    }

    try
    {
        if (!boost::filesystem::is_directory(GetDataDir(false)))
        {
            fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", mapArgs["-datadir"].c_str());
            return false;
        }
        try
        {
            ReadConfigFile(mapArgs, mapMultiArgs);
        } catch (const missing_zcash_conf& e) {
            try
            {

#ifdef WIN32
                fprintf(stdout,
                    "------------------------------------------------------------------\n"
                    "                        ERROR:\n"
                    " The configuration file zen.conf is missing.\n"
                    " Please create a valid zen.conf in the application data directory.\n"
                    " The default application data directories are:\n"
                    "\n"
                    " Windows (pre Vista): C:\\Documents and Settings\\Username\\Application Data\\Zen\n"
                    " Windows (Vista and later): C:\\Users\\Username\\AppData\\Roaming\\Zen\n"
                    "\n"
                    " You can find the default configuration file at:\n"
                    " https://github.com/HorizenOfficial/zen/blob/master/contrib/debian/examples/zen.conf\n"
                    "\n"
                    "                        WARNING:\n"
                    " Running the default configuration file without review is considered a potential risk, as zend\n"
                    " might accidentally compromise your privacy if there is a default option that you need to change!\n"
                    "\n"
                    " Please create a valid zen.conf and restart to zend continue.\n"
                    "------------------------------------------------------------------\n");
                return false;
#endif
                // Warn user about using default config file
                fprintf(stdout,
                    "------------------------------------------------------------------\n"
                    "                        WARNING:\n"
                    "Automatically copying the default config file to:\n"
                    "\n"
#ifdef  __APPLE__
                    "~/Library/Application Support/Zen\n"
#else
                    "~/.zen/zen.conf\n"
#endif
                    "\n"
                    " Running the default configuration file without review is considered a potential risk, as zend\n"
                    " might accidentally compromise your privacy if there is a default option that you need to change!\n"
                    "\n"
                    "           Please restart zend to continue.\n"
                    "           You will not see this warning again.\n"
                    "------------------------------------------------------------------\n");


#ifdef __APPLE__
                // On Mac OS try to copy the default config file if zend is started from source folder zen/src/zend
                std::string strConfPath("../contrib/debian/examples/zen.conf");
                if (!boost::filesystem::exists(strConfPath)){
                    strConfPath = "contrib/debian/examples/zen.conf";
                }
#else
                std::string strConfPath("/usr/share/doc/zen/examples/zen.conf");

                if (!boost::filesystem::exists(strConfPath))
                {
                    strConfPath = "contrib/debian/examples/zen.conf";
                }

                if (!boost::filesystem::exists(strConfPath))
                {
                    strConfPath = "../contrib/debian/examples/zen.conf";
                }
#endif
                // Copy default config file
                std::ifstream src(strConfPath, std::ios::binary);
                src.exceptions(std::ifstream::failbit | std::ifstream::badbit);

                std::ofstream dst(GetConfigFile().string().c_str(), std::ios::binary);
                dst << src.rdbuf();
                return false;
            } catch (const std::exception& e) {
                fprintf(stdout,
                    "------------------------------------------------------------------\n"
                    " There was an error copying the default configuration file!!!!\n"
                    "\n"
                    " Please create a configuration file in the data directory.\n"
                    " The default application data directories are:\n"
                    " Windows (pre Vista): C:\\Documents and Settings\\Username\\Application Data\\Zen\n"
                    " Windows (Vista and later): C:\\Users\\Username\\AppData\\Roaming\\Zen\n"
                    "\n"
                    " You can find the default configuration file at:\n"
                    " https://github.com/HorizenOfficial/zen/blob/master/contrib/debian/examples/zen.conf\n"
                    "\n"
                    "                        WARNING:\n"
                    " Running the default configuration file without review is considered a potential risk, as zend\n"
                    " might accidentally compromise your privacy if there is a default option that you need to change!\n"
                    "------------------------------------------------------------------\n");
                fprintf(stderr, "Error copying configuration file: %s\n", e.what());
                return false;
            }
        } catch (const std::exception& e) {
            fprintf(stderr,"Error reading configuration file: %s\n", e.what());
            return false;
        }
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        if (!SelectParamsFromCommandLine()) {
            fprintf(stderr, "Error: Invalid combination of -regtest and -testnet.\n");
            return false;
        }

        // Command-line RPC
        bool fCommandLine = false;
        for (int i = 1; i < argc; i++)
            if (!IsSwitchChar(argv[i][0]) && !boost::algorithm::istarts_with(argv[i], "zen:"))
                fCommandLine = true;

        if (fCommandLine)
        {
            fprintf(stderr, "Error: There is no RPC client functionality in zend. Use the zen-cli utility instead.\n");
            exit(1);
        }
#ifndef WIN32
        fDaemon = GetBoolArg("-daemon", false);
        if (fDaemon)
        {
            fprintf(stdout, "Zen server starting\n");

            // Daemonize
            pid_t pid = fork();
            if (pid < 0)
            {
                fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
                return false;
            }
            if (pid > 0) // Parent process, pid is child process id
            {
                return true;
            }
            // Child process falls through to rest of initialization

            pid_t sid = setsid();
            if (sid < 0)
                fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
        }
#endif
        SoftSetBoolArg("-server", true);

        fRet = AppInit2(threadGroup, scheduler);
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(NULL, "AppInit()");
    }

    if (!fRet)
    {
        Interrupt(threadGroup);
        // threadGroup.join_all(); was left out intentionally here, because we didn't re-test all of
        // the startup-failure cases to make sure they don't result in a hang due to some
        // thread-blocking-waiting-for-another-thread-during-startup case
    } else {
        WaitForShutdown(&threadGroup);
    }
    Shutdown();

    return fRet;
}

int main(int argc, char* argv[])
{
    //SetupEnvironment();

    // Connect bitcoind signal handlers
    //noui_connect();

    return (AppInit(argc, argv) ? 0 : 1);
}
