// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "clientversion.h"
#include "rpcserver.h"
#include "init.h"
#include "main.h"
#include "noui.h"
#include "scheduler.h"
#include "util.h"
#include "httpserver.h"
#include "httprpc.h"
#include "rpcserver.h"

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
bool AppInit(int argc, char* argv[])
{
    boost::thread_group threadGroup;
    CScheduler scheduler;

    bool fRet = false;

    //
    // Parameters
    //
    // ZEN_MOD_START
    // If Qt is used, parameters/zen.conf are parsed in qt/bitcoin.cpp's main()
    // ZEN_MOD_END
    ParseParameters(argc, argv);

    // Process help and version before taking care about datadir
    if (mapArgs.count("-?") || mapArgs.count("-h") ||  mapArgs.count("-help") || mapArgs.count("-version"))
    {
        // ZEN_MOD_START
        std::string strUsage = _("Zen Daemon") + " " + _("version") + " " + FormatFullVersion() + "\n";
        // ZEN_MOD_END

        if (mapArgs.count("-version"))
        {
            strUsage += LicenseInfo();
        }
        else
        {
            strUsage += "\n" + _("Usage:") + "\n" +
                    // ZEN_MOD_START
                  "  zend [options]                     " + _("Start Zen Daemon") + "\n";
                    // ZEN_MOD_END

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
// ZEN_MOD_START
            try
            {

#ifdef WIN32
                fprintf(stdout,
                    "------------------------------------------------------------------\n"
                    "                        WARNING:\n"
                    "The configuration file zen.conf is missing.\n"
                    "Please create a valid zen.conf in the default application data directory:\n"
                    "Windows < Windows Vista: C:\Documents and Settings\Username\Application Data\Zen\n"
                    "Windows >= Windows Vista: C:\Users\Username\AppData\\Roaming\Zen\n"
                    "\n"
                    "You can find a configuration file template on:\n"
                    "https://github.com/ZencashOfficial/zen/blob/master/contrib/debian/examples/zen.conf\n"
                    "\n"
                    "This template is a default option that you need to change!\n"
                    "           Please create a valid zen.conf and restart to continue.\n"
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
                    "This is a potential risk, as zend might accidentally compromise\n"
                    "your privacy if there is a default option that you need to change!\n"
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
                "\n\nThere was an error copying the default configuration file!!!!\n\n"
                "You can find a configuration file template on:\n"
                "https://github.com/ZencashOfficial/zen/blob/master/contrib/debian/examples/zen.conf\n"
                "\n"
                "This template is a default option that you need to change!\n\n");
                fprintf(stderr, "Error copying configuration file: %s\n", e.what());
                return false;
            }
// ZEN_MOD_END
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
            // ZEN_MOD_START
            if (!IsSwitchChar(argv[i][0]) && !boost::algorithm::istarts_with(argv[i], "zen:"))
            // ZEN_MOD_END
                fCommandLine = true;

        if (fCommandLine)
        {
            // ZEN_MOD_START
            fprintf(stderr, "Error: There is no RPC client functionality in zend. Use the zen-cli utility instead.\n");
            // ZEN_MOD_END
            exit(1);
        }
#ifndef WIN32
        fDaemon = GetBoolArg("-daemon", false);
        if (fDaemon)
        {
            // ZEN_MOD_START
            fprintf(stdout, "Zen server starting\n");
            // ZEN_MOD_END

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
    SetupEnvironment();

    // Connect bitcoind signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? 0 : 1);
}
