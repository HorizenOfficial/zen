// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Copyright (c) 2018-2023 Zen Blockchain Foundation
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
#include <fstream>
#include <regex>

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

namespace {

void CopyDefaultConfigFile(const std::string& destination, const std::string& filename, const std::regex& regex_src, const std::string& regex_dst) {
    try
    {
        // Warn user about using default config file
        fprintf(stdout,
            "------------------------------------------------------------------\n"
            "                        WARNING:\n"
            "Automatically copying the default config file to:\n"
            "\n"
            "%s\n"
            "\n"
            " Running the default configuration file without review is considered a potential risk, as zend\n"
            " might accidentally compromise your privacy if there is a default option that you need to change!\n"
            "\n"
            "           Please restart zend to continue.\n"
            "           You will not see this warning again.\n"
            "------------------------------------------------------------------\n", destination.c_str());


#ifdef __APPLE__
        // On Mac OS try to copy the default config file if zend is started from source folder zen/src/zend
        std::string strConfPath("../contrib/debian/examples/" + filename);
        if (!boost::filesystem::exists(strConfPath)){
            strConfPath = "contrib/debian/examples/" + filename;
        }
#else
        std::string strConfPath("/usr/share/doc/zen/examples/" + filename);

        if (!boost::filesystem::exists(strConfPath))
        {
            strConfPath = "contrib/debian/examples/" + filename;
        }

        if (!boost::filesystem::exists(strConfPath))
        {
            strConfPath = "../contrib/debian/examples/" + filename;
        }
#endif
        // Copy default config file
        std::ifstream src(strConfPath, std::ios::binary);
        if (!src.is_open()) {
            throw std::runtime_error("Could not find default config file");
        }
        src.exceptions(std::ifstream::badbit);

        std::ofstream dst(destination.c_str(), std::ios::binary);
        std::string tmpline;
        while (std::getline(src, tmpline)) {
            dst << std::regex_replace(tmpline, regex_src, regex_dst) << '\n';
        }
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
            " https://github.com/HorizenOfficial/zen/blob/master/contrib/debian/examples/%s\n"
            "\n"
            "                        WARNING:\n"
            " Running the default configuration file without review is considered a potential risk, as zend\n"
            " might accidentally compromise your privacy if there is a default option that you need to change!\n"
            "------------------------------------------------------------------\n", filename.c_str());
        fprintf(stderr, "Error copying configuration file: %s\n", e.what());
    }
}

#ifdef WIN32
void PrintFileMissingError(const std::string& filename) {
    fprintf(stdout,
        "------------------------------------------------------------------\n"
        "                        ERROR:\n"
        " The configuration file %s is missing.\n"
        " Please create a valid %s in the application data directory.\n"
        " The default application data directories are:\n"
        "\n"
        " Windows (pre Vista): C:\\Documents and Settings\\Username\\Application Data\\Zen\n"
        " Windows (Vista and later): C:\\Users\\Username\\AppData\\Roaming\\Zen\n"
        "\n"
        " You can find the default configuration file at:\n"
        " https://github.com/HorizenOfficial/zen/blob/master/contrib/debian/examples/%s\n"
        "\n"
        "                        WARNING:\n"
        " Running the default configuration file without review is considered a potential risk, as zend\n"
        " might accidentally compromise your privacy if there is a default option that you need to change!\n"
        "\n"
        " Please create a valid %s and restart to zend continue.\n"
        "------------------------------------------------------------------\n",
        filename.c_str(),
        filename.c_str(),
        filename.c_str(),
        filename.c_str()
        );
}
#endif

} // namespace

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

        bool should_return = false;
        // zen.conf
        try
        {
            ReadConfigFile(mapArgs, mapMultiArgs);
        } catch (const missing_zcash_conf& e) {

#ifdef WIN32
            PrintFileMissingError("zen.conf");
#else
            CopyDefaultConfigFile(GetConfigFile().string(), "zen.conf", std::regex(), "");
#endif
            should_return = true;
        } catch (const std::exception& e) {
            fprintf(stderr,"Error reading configuration file: %s\n", e.what());
            should_return = true;
        }

        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        if (!SelectParamsFromCommandLine()) {
            fprintf(stderr, "Error: Invalid combination of -regtest and -testnet.\n");
            return false;
        }

        // mc_crypto_log_config.yaml
        if (GetBoolArg("-enable_mc_crypto_logger", false)) {
            LogPrintf("mc-crypto logger enabled\n");
            // Create the configuration file if it does not exist
            if (!boost::filesystem::exists(GetMcCryptoConfigFile()))
                createMcCryptoLogConfigFile();
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
    SetupEnvironment();

    // Connect bitcoind signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? 0 : 1);
}
