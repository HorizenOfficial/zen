// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 Zen Blockchain Foundation
// Copyright (c) 2023 The Horizen Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "init.h"
#include "crypto/common.h"
#include "addrman.h"
#include "amount.h"
#ifdef ENABLE_MINING
#include "base58.h"
#endif
#include "checkpoints.h"
#include "compat/sanity.h"
#include "consensus/validation.h"
#include "httpserver.h"
#include "httprpc.h"
#include "key.h"
#include "main.h"
#include "metrics.h"
#include "miner.h"
#include "net.h"
#include "rpc/server.h"
#include "script/standard.h"
#include "scheduler.h"
#include "txdb.h"
#include "torcontrol.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validationinterface.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif
#include <stdint.h>
#include <stdio.h>
#include <thread>

#ifndef WIN32
#include <signal.h>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/function.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>

#include <libsnark/common/profiling.hpp>

#if ENABLE_ZMQ
#include "zmq/zmqnotificationinterface.h"
#endif

#if ENABLE_PROTON
#include "amqp/amqpnotificationinterface.h"
#endif

#include "librustzcash.h"
#include "zen/websocket_server.h"
#include <zen/forks/fork2_replayprotectionfork.h>

#include "sc/asyncproofverifier.h"

using namespace std;

ZCJoinSplit* pzcashParams = NULL;

#ifdef ENABLE_WALLET
CWallet* pwalletMain = NULL;
#endif
bool fFeeEstimatesInitialized = false;

#if ENABLE_ZMQ
static CZMQNotificationInterface* pzmqNotificationInterface = NULL;
#endif

#if ENABLE_PROTON
static AMQPNotificationInterface* pAMQPNotificationInterface = NULL;
#endif

#ifdef WIN32
// Win32 LevelDB doesn't use file descriptors, and the ones used for
// accessing block files don't count towards the fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 825
#endif

static const char* FEE_ESTIMATES_FILENAME="fee_estimates.dat";
CClientUIInterface uiInterface; // Declared but not defined in ui_interface.h

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit().
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets fRequestShutdown, which triggers
// the DetectShutdownThread(), which interrupts the main thread group.
// DetectShutdownThread() then exits, which causes AppInit() to
// continue (it .joins the shutdown thread).
// Shutdown() is then
// called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Note that if running -daemon the parent process returns from AppInit2
// before adding any threads to the threadGroup, so .join_all() returns
// immediately and the parent exits from main().
//

std::atomic<bool> fRequestShutdown(false);

void StartShutdown()
{
    fRequestShutdown = true;
}
bool ShutdownRequested()
{
    return fRequestShutdown;
}

class CCoinsViewErrorCatcher : public CCoinsViewBacked
{
public:
    CCoinsViewErrorCatcher(CCoinsView* view) : CCoinsViewBacked(view) {}
    bool GetCoins(const uint256 &txid, CCoins &coins) const {
        try {
            return CCoinsViewBacked::GetCoins(txid, coins);
        } catch(const std::runtime_error& e) {
            uiInterface.ThreadSafeMessageBox(_("Error reading from database, shutting down."), "", CClientUIInterface::MSG_ERROR);
            LogPrintf("Error reading from database: %s\n", e.what());
            // Starting the shutdown sequence and returning false to the caller would be
            // interpreted as 'entry not found' (as opposed to unable to read data), and
            // could lead to invalid interpretation. Just exit immediately, as we can't
            // continue anyway, and all writes should be atomic.
            abort();
        }
    }
    // Writes do not need similar protection, as failure to write is handled by the caller.
};

static CCoinsViewDB *pcoinsdbview = NULL;
static CCoinsViewErrorCatcher *pcoinscatcher = NULL;
static boost::scoped_ptr<ECCVerifyHandle> globalVerifyHandle;

void Interrupt(boost::thread_group& threadGroup)
{
    InterruptHTTPServer();
    InterruptHTTPRPC();
    InterruptRPC();
    InterruptREST();
    InterruptTorControl();
    threadGroup.interrupt_all();
}

void Shutdown()
{
    LogPrintf("%s: In progress...\n", __func__);
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown)
        return;

    /// Note: Shutdown() must be able to handle cases in which AppInit2() failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    RenameThread("horizen-shutoff");
    mempool->AddTransactionsUpdated(1);

    StopWsServer();
    StopHTTPRPC();
    StopREST();
    StopRPC();
    StopHTTPServer();
#ifdef ENABLE_WALLET
    if (pwalletMain)
        pwalletMain->Flush(false);
#endif
#ifdef ENABLE_MINING
 #ifdef ENABLE_WALLET
    GenerateBitcoins(false, NULL, 0);
 #else
    GenerateBitcoins(false, 0);
 #endif
#endif
    StopTorControl();
    UnregisterNodeSignals(GetNodeSignals());

    if (fFeeEstimatesInitialized)
    {
        boost::filesystem::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
        CAutoFile est_fileout(fopen(est_path.string().c_str(), "wb"), SER_DISK, CLIENT_VERSION);
        if (!est_fileout.IsNull())
            mempool->WriteFeeEstimates(est_fileout);
        else
            LogPrintf("%s: Failed to write fee estimates to %s\n", __func__, est_path.string());
        fFeeEstimatesInitialized = false;
    }

    {
        LOCK(cs_main);
        if (pcoinsTip != NULL) {
            FlushStateToDisk();
        }
        delete pcoinsTip;
        pcoinsTip = NULL;
        delete pcoinscatcher;
        pcoinscatcher = NULL;
        delete pcoinsdbview;
        pcoinsdbview = NULL;
        delete pblocktree;
        pblocktree = NULL;
    }
#ifdef ENABLE_WALLET
    if (pwalletMain)
        pwalletMain->Flush(true);
#endif

#if ENABLE_ZMQ
    if (pzmqNotificationInterface) {
        UnregisterValidationInterface(pzmqNotificationInterface);
        delete pzmqNotificationInterface;
        pzmqNotificationInterface = NULL;
    }
#endif

#if ENABLE_PROTON
    if (pAMQPNotificationInterface) {
        UnregisterValidationInterface(pAMQPNotificationInterface);
        delete pAMQPNotificationInterface;
        pAMQPNotificationInterface = NULL;
    }
#endif

#ifndef WIN32
    try {
        boost::filesystem::remove(GetPidFile());
    } catch (const boost::filesystem::filesystem_error& e) {
        LogPrintf("%s: Unable to remove pidfile: %s\n", __func__, e.what());
    }
#endif
    UnregisterAllValidationInterfaces();
#ifdef ENABLE_WALLET
    delete pwalletMain;
    pwalletMain = NULL;
#endif
    delete pzcashParams;
    pzcashParams = NULL;
    globalVerifyHandle.reset();
    ECC_Stop();
    connman.reset();
    LogPrintf("%s: done\n", __func__);
}

/**
 * Signal handlers are very limited in what they are allowed to do, so:
 */
void HandleSIGTERM(int)
{
    fRequestShutdown = true;
}

void HandleSIGHUP(int)
{
    fReopenDebugLog = true;
}

bool InitError(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

bool static InitWarning(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
    return true;
}

void OnRPCStopped()
{
    cvBlockChange.notify_all();
    LogPrint("rpc", "RPC stopped.\n");
}

void OnRPCPreCommand(const CRPCCommand& cmd)
{
    // Observe safe mode
    string strWarning = GetWarnings("rpc");
    if (strWarning != "" && !GetBoolArg("-disablesafemode", false) &&
        !cmd.okSafeMode)
        throw JSONRPCError(RPC_FORBIDDEN_BY_SAFE_MODE, string("Safe mode: ") + strWarning);
}

std::string HelpMessage(HelpMessageMode mode)
{
    const bool showDebug = GetBoolArg("-help-debug", false);

    // When adding new options to the categories, please keep and ensure alphabetical ordering.
    // Do not translate _(...) -help-debug options, Many technical terms, and only a very small audience, so is unnecessary stress to translators

    string strUsage = HelpMessageGroup(_("Options:"));
    strUsage += HelpMessageOpt("-?", _("This help message"));
    strUsage += HelpMessageOpt("-alertnotify=<cmd>", _("Execute command when a relevant internal alert is risen or we see a really long fork (%s in cmd is replaced by message)"));
    strUsage += HelpMessageOpt("-blocknotify=<cmd>", _("Execute command when the best block changes (%s in cmd is replaced by block hash)"));
    strUsage += HelpMessageOpt("-checkblocks=<n>", strprintf(_("How many blocks to check at startup (default: %u, 0 = all)"), 288));
    strUsage += HelpMessageOpt("-checklevel=<n>", strprintf(_("How thorough the block verification of -checkblocks is (0-4, default: %u)"), 3));
    strUsage += HelpMessageOpt("-conf=<file>", strprintf(_("Specify configuration file (default: %s)"), "zen.conf"));
    if (mode == HMM_BITCOIND)
    {
#if !defined(WIN32)
        strUsage += HelpMessageOpt("-daemon", _("Run in the background as a daemon and accept commands"));
#endif
    }
    strUsage += HelpMessageOpt("-datadir=<dir>", _("Specify data directory"));
    strUsage += HelpMessageOpt("-disabledeprecation=<version>", strprintf(_("Disable block-height node deprecation and automatic shutdown (example: -disabledeprecation=%s)"),
        FormatVersion(CLIENT_VERSION)));
    strUsage += HelpMessageOpt("-enable_mc_crypto_logger", strprintf(_("Enable libzendoo logging to file. It creates a new configuration file in the current datadir, if it does not already exist.")));
    strUsage += HelpMessageOpt("-exportdir=<dir>", _("Specify directory to be used when exporting data"));
    strUsage += HelpMessageOpt("-dbcache=<n>", strprintf(_("Set database cache size in megabytes (%d to %d, default: %d)"), nMinDbCache, nMaxDbCache, nDefaultDbCache));
    strUsage += HelpMessageOpt("-loadblock=<file>", _("Imports blocks from external blk000??.dat file") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-maxorphantx=<n>", strprintf(_("Keep at most <n> unconnectable transactions in memory (default: %u)"), DEFAULT_MAX_ORPHAN_TRANSACTIONS));
    strUsage += HelpMessageOpt("-mempooltxinputlimit=<n>", _("Set the maximum number of transparent inputs in a transaction that the mempool will accept (default: 0 = no limit applied)"));
    strUsage += HelpMessageOpt("-maxmempool=<n>", strprintf(_("Keep the transaction memory pool below <n> megabytes (default: %u)"), DEFAULT_MAX_MEMPOOL_SIZE_MB));
    strUsage += HelpMessageOpt("-par=<n>", strprintf(_("Set the number of script verification threads (%u to %d, 0 = auto, <0 = leave that many cores free, default: %d)"),
        -GetNumCores(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS));
#ifndef WIN32
    strUsage += HelpMessageOpt("-pid=<file>", strprintf(_("Specify pid file (default: %s)"), "zend.pid"));
#endif
    strUsage += HelpMessageOpt("-prune=<n>", strprintf(_("Reduce storage requirements by pruning (deleting) old blocks. This mode disables wallet support and is incompatible with -txindex. "
            "Warning: Reverting this setting requires re-downloading the entire blockchain. "
            "(default: 0 = disable pruning blocks, >%u = target size in MiB to use for block files)"), MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
    strUsage += HelpMessageOpt("-reindex", _("Rebuild block chain index from current blk000??.dat files on startup"));
    strUsage += HelpMessageOpt("-reindexfast", _("Rebuild block chain index from current blk000??.dat files on startup, skipping expensive checks for blocks below checkpoints. It is incompatible with reindex"));
    #if !defined(WIN32)
    strUsage += HelpMessageOpt("-sysperms", _("Create new files with system default permissions, instead of umask 077 (only effective with disabled wallet functionality)"));
#endif
    strUsage += HelpMessageOpt("-txindex", strprintf(_("Maintain a full transaction index, used by the getrawtransaction rpc call (default: %u)"), DEFAULT_TXINDEX));
    strUsage += HelpMessageOpt("-maturityheightindex", strprintf(_("Maintain a maturity height index that stores for every height the cerficates that became mature, used by the getblockexpanded rpc call. It requires -txindex (default: %u)"), DEFAULT_MATURITYHEIGHTINDEX));

    strUsage += HelpMessageOpt("-addressindex", strprintf(_("Maintain a full address index, used to query for the balance, txids and unspent outputs for addresses (default: %u)"), DEFAULT_ADDRESSINDEX));
    strUsage += HelpMessageOpt("-timestampindex", strprintf(_("Maintain a timestamp index for block hashes, used to query blocks hashes by a range of timestamps (default: %u)"), DEFAULT_TIMESTAMPINDEX));
    strUsage += HelpMessageOpt("-spentindex", strprintf(_("Maintain a full spent index, used to query the spending txid and input index for an outpoint (default: %u)"), DEFAULT_SPENTINDEX));

    strUsage += HelpMessageOpt("-blocktreedbmaxopenfiles", strprintf(_("Maximum number of open files for the Block Tree LevelDB (default: %u)"), DEFAULT_DB_MAX_OPEN_FILES));
    strUsage += HelpMessageOpt("-coinsviewdbmaxopenfiles", strprintf(_("Maximum number of open files for the Coins View LevelDB (default: %u)"), DEFAULT_DB_MAX_OPEN_FILES));

    strUsage += HelpMessageGroup(_("Connection options:"));
    strUsage += HelpMessageOpt("-addnode=<ip>", _("Add a node to connect to and attempt to keep the connection open"));
    strUsage += HelpMessageOpt("-banscore=<n>", strprintf(_("Threshold for disconnecting misbehaving peers (default: %u)"), 100));
    strUsage += HelpMessageOpt("-bantime=<n>", strprintf(_("Number of seconds to keep misbehaving peers from reconnecting (default: %u)"), 86400));
    strUsage += HelpMessageOpt("-bind=<addr>", _("Bind to given address and always listen on it. Use [host]:port notation for IPv6"));
    strUsage += HelpMessageOpt("-connect=<ip>", _("Connect only to the specified node(s)"));
    strUsage += HelpMessageOpt("-discover", _("Discover own IP addresses (default: 1 when listening and no -externalip or -proxy)"));
    strUsage += HelpMessageOpt("-dns", _("Allow DNS lookups for -addnode, -seednode and -connect") + " " + _("(default: 1)"));
    strUsage += HelpMessageOpt("-dnsseed", _("Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless -connect)"));
    strUsage += HelpMessageOpt("-externalip=<ip>", _("Specify your own public address"));
    strUsage += HelpMessageOpt("-forcednsseed", strprintf(_("Always query for peer addresses via DNS lookup (default: %u)"), 0));
    strUsage += HelpMessageOpt("-listen", _("Accept connections from outside (default: 1 if no -proxy or -connect)"));
    strUsage += HelpMessageOpt("-listenonion", strprintf(_("Automatically create Tor hidden service (default: %d)"), DEFAULT_LISTEN_ONION));
    strUsage += HelpMessageOpt("-maxconnections=<n>", strprintf(_("Maintain at most <n> connections to peers (default: %u)"), DEFAULT_MAX_PEER_CONNECTIONS));
    strUsage += HelpMessageOpt("-maxreceivebuffer=<n>", strprintf(_("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)"), DEFAULT_MAX_RECEIVE_BUFFER));
    strUsage += HelpMessageOpt("-maxsendbuffer=<n>", strprintf(_("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)"), DEFAULT_MAX_SEND_BUFFER));
    strUsage += HelpMessageOpt("-onion=<ip:port>", strprintf(_("Use separate SOCKS5 proxy to reach peers via Tor hidden services (default: %s)"), "-proxy"));
    strUsage += HelpMessageOpt("-onlynet=<net>", _("Only connect to nodes in network <net> (ipv4, ipv6 or onion)"));
    strUsage += HelpMessageOpt("-permitbaremultisig", strprintf(_("Relay non-P2SH multisig (default: %u)"), 1));
    strUsage += HelpMessageOpt("-port=<port>", strprintf(_("Listen for connections on <port> (default: %u or testnet: %u)"), 9033, 19033));
    strUsage += HelpMessageOpt("-proxy=<ip:port>", _("Connect through SOCKS5 proxy"));
    strUsage += HelpMessageOpt("-proxyrandomize", strprintf(_("Randomize credentials for every proxy connection. This enables Tor stream isolation (default: %u)"), 1));
    strUsage += HelpMessageOpt("-seednode=<ip>", _("Connect to a node to retrieve peer addresses, and disconnect"));
    strUsage += HelpMessageOpt("-timeout=<n>", strprintf(_("Specify connection timeout in milliseconds (minimum: 1, default: %d)"), DEFAULT_CONNECT_TIMEOUT));
    strUsage += HelpMessageOpt("-torcontrol=<ip>:<port>", strprintf(_("Tor control port to use if onion listening enabled (default: %s)"), DEFAULT_TOR_CONTROL));
    strUsage += HelpMessageOpt("-torpassword=<pass>", _("Tor control port password (default: empty)"));
    strUsage += HelpMessageOpt("-tlsfallbacknontls=<0 or 1>", _("If a TLS connection fails, the next connection attempt of the same peer (based on IP address) takes place without TLS (default: 1)"));
    strUsage += HelpMessageOpt("-tlsvalidate=<0 or 1>", _("Connect to peers only with valid certificates (default: 0)"));
    strUsage += HelpMessageOpt("-tlskeypath=<path>", _("Full path to a private key"));
    strUsage += HelpMessageOpt("-tlskeypwd=<password>", _("Password for a private key encryption (default: not set, i.e. private key will be stored unencrypted)"));
    strUsage += HelpMessageOpt("-tlscertpath=<path>", _("Full path to a certificate"));
    strUsage += HelpMessageOpt("-tlstrustdir=<path>", _("Full path to a trusted certificates directory"));
    strUsage += HelpMessageOpt("-websocket=<0 or 1>", _("If set to 1 opens a websocket channel listening for client connections (default: 0)"));
    strUsage += HelpMessageOpt("-wsaddress=<ip address>", _("If websocket=1, listen for ws connections at this ip address (default: 127.0.0.1)"));
    strUsage += HelpMessageOpt("-wsport=<port>", _("If websocket=1, listen for ws connections at <wsaddress>:<wsport> (default: 8888)"));
#ifdef USE_UPNP
#if USE_UPNP
    strUsage += HelpMessageOpt("-upnp", _("Use UPnP to map the listening port (default: 1 when listening and no -proxy)"));
#else
    strUsage += HelpMessageOpt("-upnp", strprintf(_("Use UPnP to map the listening port (default: %u)"), 0));
#endif
#endif
    strUsage += HelpMessageOpt("-whitebind=<addr>", _("Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6"));
    strUsage += HelpMessageOpt("-whitelist=<netmask>", _("Whitelist peers connecting from the given netmask or IP address. Can be specified multiple times.") +
        " " + _("Whitelisted peers cannot be DoS banned and their transactions are always relayed, even if they are already in the mempool, useful e.g. for a gateway"));

#ifdef ENABLE_WALLET
    strUsage += HelpMessageGroup(_("Wallet options:"));
    strUsage += HelpMessageOpt("-disablewallet", _("Do not load the wallet and disable wallet RPC calls"));
    strUsage += HelpMessageOpt("-keypool=<n>", strprintf(_("Set key pool size to <n> (default: %u)"), 100));
    if (showDebug)
        strUsage += HelpMessageOpt("-mintxfee=<amt>", strprintf("Fees (in %s/kB) smaller than this are considered zero fee for transaction creation (default: %s)",
            CURRENCY_UNIT, FormatMoney(CWallet::minTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-paytxfee=<amt>", strprintf(_("Fee (in %s/kB) to add to transactions you send (default: %s)"),
        CURRENCY_UNIT, FormatMoney(payTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-rescan", _("Rescan the block chain for missing wallet transactions") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-salvagewallet", _("Attempt to recover private keys from a corrupt wallet.dat") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-sendfreetransactions", strprintf(_("Send transactions as zero-fee transactions if possible (default: %u)"), 0));
    strUsage += HelpMessageOpt("-spendzeroconfchange", strprintf(_("Spend unconfirmed change when sending transactions (default: %u)"), 1));
    strUsage += HelpMessageOpt("-txconfirmtarget=<n>", strprintf(_("If paytxfee is not set, include enough fee so transactions begin confirmation on average within n blocks (default: %u)"), DEFAULT_TX_CONFIRM_TARGET));
    strUsage += HelpMessageOpt("-maxtxfee=<amt>", strprintf(_("Maximum total fees (in %s) to use in a single wallet transaction; setting this too low may abort large transactions (default: %s)"),
        CURRENCY_UNIT, FormatMoney(maxTxFee)));
    strUsage += HelpMessageOpt("-upgradewallet", _("Upgrade wallet to latest format") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-wallet=<file>", _("Specify wallet file (within data directory)") + " " + strprintf(_("(default: %s)"), "wallet.dat"));
    strUsage += HelpMessageOpt("-walletbroadcast", _("Make the wallet broadcast transactions") + " " + strprintf(_("(default: %u)"), true));
    strUsage += HelpMessageOpt("-walletnotify=<cmd>", _("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)"));
    strUsage += HelpMessageOpt("-zapwallettxes=<mode>", _("Delete all wallet transactions and only recover those parts of the blockchain through -rescan on startup") +
        " " + _("(1 = keep tx meta data e.g. account owner and payment request information, 2 = drop tx meta data)"));
#endif

#if ENABLE_ZMQ
    strUsage += HelpMessageGroup(_("ZeroMQ notification options:"));
    strUsage += HelpMessageOpt("-zmqpubhashblock=<address>", _("Enable publish hash block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubhashtx=<address>", _("Enable publish hash transaction in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawblock=<address>", _("Enable publish raw block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawtx=<address>", _("Enable publish raw transaction in <address>"));
#endif

#if ENABLE_PROTON
    strUsage += HelpMessageGroup(_("AMQP 1.0 notification options:"));
    strUsage += HelpMessageOpt("-amqppubhashblock=<address>", _("Enable publish hash block in <address>"));
    strUsage += HelpMessageOpt("-amqppubhashtx=<address>", _("Enable publish hash transaction in <address>"));
    strUsage += HelpMessageOpt("-amqppubrawblock=<address>", _("Enable publish raw block in <address>"));
    strUsage += HelpMessageOpt("-amqppubrawtx=<address>", _("Enable publish raw transaction in <address>"));
#endif

    strUsage += HelpMessageGroup(_("Debugging/Testing options:"));
    if (showDebug)
    {
        strUsage += HelpMessageOpt("-checkpoints", strprintf("Disable expensive verification for known chain history (default: %u)", 1));
        strUsage += HelpMessageOpt("-dblogsize=<n>", strprintf("Flush database activity from memory pool to disk log every <n> megabytes (default: %u)", 100));
        strUsage += HelpMessageOpt("-disablesafemode", strprintf("Disable safemode, override a real safe mode event (default: %u)", 0));
        strUsage += HelpMessageOpt("-testsafemode", strprintf("Force safe mode (default: %u)", 0));
        strUsage += HelpMessageOpt("-dropmessagestest=<n>", "Randomly drop 1 of every <n> network messages");
        strUsage += HelpMessageOpt("-fuzzmessagestest=<n>", "Randomly fuzz 1 of every <n> network messages");
        strUsage += HelpMessageOpt("-flushwallet", strprintf("Run a thread to flush wallet periodically (default: %u)", 1));
        strUsage += HelpMessageOpt("-stopafterblockimport", strprintf("Stop running after importing blocks from disk (default: %u)", 0));
    }
    string debugCategories = "addrman, alert, bench, cert, coindb, db, estimatefee, forks, http, libevent, lock, mempool, net, partitioncheck, pow, proxy, prune, "
                             "rand, reindex, rpc, sc, selectcoins, tor, ws, zendoo_mc_cryptolib, zmq, zrpc, zrpcunsafe (implies zrpc)"; // Don't translate these
    strUsage += HelpMessageOpt("-debug=<category>", strprintf(_("Output debugging information (default: %u, supplying <category> is optional)"), 0) + ". " +
        _("If <category> is not supplied or if <category> = 1, output all debugging information.") + " " + _("<category> can be:") + " " + debugCategories + ".");
    strUsage += HelpMessageOpt("-experimentalfeatures", _("Enable use of experimental features"));
    strUsage += HelpMessageOpt("-help-debug", _("Show all debugging options (usage: --help -help-debug)"));
    strUsage += HelpMessageOpt("-logips", strprintf(_("Include IP addresses in debug output (default: %u)"), 0));
    strUsage += HelpMessageOpt("-logtimestamps", strprintf(_("Prepend debug output with timestamp (default: %u)"), 1));
    strUsage += HelpMessageOpt("-logtimemicros", strprintf(_("Meaningful if -logtimestamps=1. In debug output timestamp reports microseconds (default: %u)"), 0));
    if (showDebug)
    {
        strUsage += HelpMessageOpt("-limitfreerelay=<n>", strprintf("Continuously rate-limit free transactions to <n>*1000 bytes per minute (default: %u)", 15));
        strUsage += HelpMessageOpt("-relaypriority", strprintf("Require high priority for relaying free or low-fee transactions (default: %u)", 0));
        strUsage += HelpMessageOpt("-maxsigcachesize=<n>", strprintf("Limit size of signature cache to <n> entries (default: %u)", 50000));
    }
    strUsage += HelpMessageOpt("-minrelaytxfee=<amt>", strprintf(_("Fees (in %s/kB) smaller than this are considered zero fee for relaying (default: %s)"),
        CURRENCY_UNIT, FormatMoney(::minRelayTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-printtoconsole", _("Send trace/debug info to console instead of debug.log file"));
    if (showDebug)
    {
        strUsage += HelpMessageOpt("-printpriority", strprintf("Log transaction priority and fee per kB when mining blocks (default: %u)", 0));
        strUsage += HelpMessageOpt("-privdb", strprintf("Sets the DB_PRIVATE flag in the wallet db environment (default: %u)", 1));
        strUsage += HelpMessageOpt("-regtest", "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
            "This is intended for regression testing tools and app development.");
    }
    strUsage += HelpMessageOpt("-shrinkdebugfile", _("Shrink debug.log file on client startup (default: 1 when no -debug)"));
    strUsage += HelpMessageOpt("-limitdebuglogsize", _("Limit the debug.log file size to 10Mb (default: 1 when no -debug)"));
    strUsage += HelpMessageOpt("-testnet", _("Use the test network"));

    strUsage += HelpMessageGroup(_("Node relay options:"));
    strUsage += HelpMessageOpt("-datacarrier", strprintf(_("Relay and mine data carrier transactions (default: %u)"), 1));
    strUsage += HelpMessageOpt("-datacarriersize", strprintf(_("Maximum size of data in data carrier transactions we relay and mine (default: %u)"), MAX_OP_RETURN_RELAY));

    strUsage += HelpMessageGroup(_("Block creation options:"));
    strUsage += HelpMessageOpt("-blockminsize=<n>", strprintf(_("Set minimum block size in bytes (default: %u)"), 0));
    strUsage += HelpMessageOpt("-blockmaxsize=<n>", strprintf(_("Set maximum block size in bytes (default: %d)"), DEFAULT_BLOCK_MAX_SIZE));
    
    strUsage += HelpMessageOpt("-blocktxpartitionmaxsize=<n> (regtest only)", strprintf(_("Set maximum partition block size for transcations in bytes (default: %u)"), DEFAULT_BLOCK_TX_PART_MAX_SIZE));

    strUsage += HelpMessageOpt("-blockprioritysize=<n>", strprintf(_("Set maximum size of high-priority/low-fee transactions/certificates in bytes (default: %d)"), DEFAULT_BLOCK_PRIORITY_SIZE));
    strUsage += HelpMessageOpt("-blockmaxcomplexity=<n>",
        strprintf(_("Limit transactions to be included into blocks based on block complexity. "
        " Block complexity is the sum of transaction complexity per block. Transaction complexity is the number of inputs of a transaction squared. "
        " Like -mempooltxinputlimit this switch is intended as a last resort when unable to build blocks fast enough because of poor GBT performance. "
        " 0  or negative values means no limit is applied. (default: %d)"
        ), DEFAULT_BLOCK_MAX_COMPLEXITY_SIZE)
    );
    strUsage += HelpMessageOpt("-deprecatedgetblocktemplate", (_("Disable block complexity calculation and use the previous GetBlockTemplate implementation")));

    strUsage += HelpMessageOpt("-scproofverificationdelay=<time>",
        strprintf(_("The maximum delay in milliseconds between sc proof batch verification requests. (default: %d)"), CScAsyncProofVerifier::BATCH_VERIFICATION_MAX_DELAY));

    strUsage += HelpMessageOpt("-scproofqueuesize=<size>",
        strprintf(_("The threshold size of the sc proof queue that triggers a call to the batch verification. (default: %d)"), CScAsyncProofVerifier::BATCH_VERIFICATION_MAX_SIZE));

    strUsage += HelpMessageOpt("-cbhsafedepth=<n>",
        "regtest only - Set safe depth for skipping checkblockatheight in txout scripts (default depends on regtest/testnet params)");
        
    strUsage += HelpMessageOpt("-cbhminage=<n>",
        "regtest only - Set the minimum legal age of the referenced block for checkblockatheight in txout scripts (default depends on regtest/testnet params)");

    strUsage += HelpMessageOpt("-allownonstandardtx",
        "regtest/testnet only - allow non-standard tx (default depends on regtest/testnet params)");

    strUsage += HelpMessageOpt("-allowdustoutput",
        "regtest only - when checking a tx to be standard, regtest allows by default a tx to have a null or dust output. Setting this option to 0 will prevent that (default: 1 = allow dust output)");

    strUsage += HelpMessageOpt("-subsidyhalvinginterval=<n>", "regtest only - Set halving interval for testing purposes (default=2000; must be > 100)");
        
    if (GetBoolArg("-help-debug", false))
        strUsage += HelpMessageOpt("-blockversion=<n>", "Override block version to test forking scenarios");

    strUsage += HelpMessageOpt("-sccoinsmaturity=<n>",
        "regtest only - Set the maturity of sc funds as number of blocks to be mined before they are computed in the sc balance (default depends on regtest/testnet params)");

    strUsage += HelpMessageOpt("-blocksforscfeecheck=<n>",
        "regtest only - Set the number of blocks used for computing the number of epoch to go back for getting the active data cert sc fee value and check on it for removing a FT or mbtr tx from mempool (default depends on regtest params)");

    strUsage += HelpMessageOpt("-skipscproof",
    "regtest only - Skip the proof verification for sidechain certificates or CSW transactions (by default it is never skipped)");

    strUsage += HelpMessageOpt("-forcelocalban",
    "regtest only - Override the default behavior that prevents the ban of a misbehaving local node");
        
#ifdef ENABLE_MINING
    strUsage += HelpMessageGroup(_("Mining options:"));
    strUsage += HelpMessageOpt("-gen", strprintf(_("Generate coins (default: %u)"), 0));
    strUsage += HelpMessageOpt("-genproclimit=<n>", strprintf(_("Set the number of threads for coin generation if enabled (-1 = all cores, default: %d)"), 1));
    strUsage += HelpMessageOpt("-equihashsolver=<name>", _("Specify the Equihash solver to be used if enabled (default: \"default\")"));
    strUsage += HelpMessageOpt("-mineraddress=<addr>", _("Send mined coins to a specific single address"));
    strUsage += HelpMessageOpt("-minetolocalwallet", strprintf(
            _("Require that mined blocks use a coinbase address in the local wallet (default: %u)"),
 #ifdef ENABLE_WALLET
            1
 #else
            0
 #endif
            ));
#endif

    strUsage += HelpMessageGroup(_("RPC server options:"));
    strUsage += HelpMessageOpt("-server", _("Accept command line and JSON-RPC commands"));
    strUsage += HelpMessageOpt("-rest", strprintf(_("Accept public REST requests (default: %u)"), 0));
    strUsage += HelpMessageOpt("-rpcbind=<addr>", _("Bind to given address to listen for JSON-RPC connections. Use [host]:port notation for IPv6. This option can be specified multiple times (default: bind to all interfaces)"));
    strUsage += HelpMessageOpt("-rpcuser=<user>", _("Username for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcpassword=<pw>", _("Password for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcport=<port>", strprintf(_("Listen for JSON-RPC connections on <port> (default: %u or testnet: %u)"), 8231, 18231));
    strUsage += HelpMessageOpt("-rpcallowip=<ip>", _("Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This option can be specified multiple times"));
    strUsage += HelpMessageOpt("-rpcthreads=<n>", strprintf(_("Set the number of threads to service RPC calls (default: %d)"), DEFAULT_HTTP_THREADS));
    if (showDebug) {
        strUsage += HelpMessageOpt("-rpcworkqueue=<n>", strprintf("Set the depth of the work queue to service RPC calls (default: %d)", DEFAULT_HTTP_WORKQUEUE));
        strUsage += HelpMessageOpt("-rpcservertimeout=<n>", strprintf("Timeout during HTTP requests (default: %d)", DEFAULT_HTTP_SERVER_TIMEOUT));
    }

    // Disabled until we can lock notes and also tune performance of libsnark which by default uses multiple threads
    //strUsage += HelpMessageOpt("-rpcasyncthreads=<n>", strprintf(_("Set the number of threads to service Async RPC calls (default: %d)"), 1));

    if (mode == HMM_BITCOIND) {
        strUsage += HelpMessageGroup(_("Metrics Options (only if -daemon and -printtoconsole are not set):"));
        strUsage += HelpMessageOpt("-showmetrics", _("Show metrics on stdout (default: 1 if running in a console, 0 otherwise)"));
        strUsage += HelpMessageOpt("-metricsui", _("Set to 1 for a persistent metrics screen, 0 for sequential metrics output (default: 1 if running in a console, 0 otherwise)"));
        strUsage += HelpMessageOpt("-metricsrefreshtime", strprintf(_("Number of seconds between metrics refreshes (default: %u if running in a console, %u otherwise)"), 1, 600));
    }

    strUsage += HelpMessageOpt("-maxtipage=<n>", "regtest only - Set the maximum tip age used for the relay of chain blocks)");
    strUsage += HelpMessageOpt("-maxblockheightforalign=<n>", strprintf("Do not process blocks received past this height (default: %d))", INT_MAX));

    return strUsage;
}

static void BlockNotifyCallback(const uint256& hashNewTip)
{
    std::string strCmd = GetArg("-blocknotify", "");

    boost::replace_all(strCmd, "%s", hashNewTip.GetHex());
    boost::thread t(runCommand, strCmd); // thread runs free
}

struct CImportingNow
{
    CImportingNow() {
        assert(fImporting == false);
        fImporting = true;
    }

    ~CImportingNow() {
        assert(fImporting == true);
        fImporting = false;
    }
};


// If we're using -prune with -reindex, then delete block files that will be ignored by the
// reindex.  Since reindexing works by starting at block file 0 and looping until a blockfile
// is missing, do the same here to delete any later block files after a gap.  Also delete all
// rev files since they'll be rewritten by the reindex anyway.  This ensures that vinfoBlockFile
// is in sync with what's actually on disk by the time we start downloading, so that pruning
// works correctly.
void CleanupBlockRevFiles()
{
    using namespace boost::filesystem;
    map<string, path> mapBlockFiles;

    // Glob all blk?????.dat and rev?????.dat files from the blocks directory.
    // Remove the rev files immediately and insert the blk file paths into an
    // ordered map keyed by block file index.
    LogPrintf("Removing unusable blk?????.dat and rev?????.dat files for -reindex with -prune\n");
    path blocksdir = GetDataDir() / "blocks";
    for (directory_iterator it(blocksdir); it != directory_iterator(); it++) {
        if (is_regular_file(*it) &&
            it->path().filename().string().length() == 12 &&
            it->path().filename().string().substr(8,4) == ".dat")
        {
            if (it->path().filename().string().substr(0,3) == "blk")
                mapBlockFiles[it->path().filename().string().substr(3,5)] = it->path();
            else if (it->path().filename().string().substr(0,3) == "rev")
                remove(it->path());
        }
    }

    // Remove all block files that aren't part of a contiguous set starting at
    // zero by walking the ordered map (keys are block file indices) by
    // keeping a separate counter.  Once we hit a gap (or if 0 doesn't exist)
    // start removing block files.
    int nContigCounter = 0;
    BOOST_FOREACH(const PAIRTYPE(string, path)& item, mapBlockFiles) {
        if (atoi(item.first) == nContigCounter) {
            nContigCounter++;
            continue;
        }
        remove(item.second);
    }
}

void ThreadImport(std::vector<boost::filesystem::path> vImportFiles)
{
    RenameThread("horizen-loadblk");

    // -reindex or -reindexfast
    if (fReindex || fReindexFast)
    {
        CImportingNow imp;
        CDiskBlockPos pos(0, 0);
        while (fReindexFast)
        {
            if (!boost::filesystem::exists(GetBlockPosFilename(pos, "blk")))
                break; // No block files left to reindex
            FILE *file = OpenBlockFile(pos, true);
            assert(file); // This error is logged in OpenBlockFile
            if (chainActive.Tip() == NULL)
                uiInterface.InitMessage(tfm::format(_("Reindexing block headers from files...") + "(blk%05i.dat)", pos.nFile));
            else
                uiInterface.InitMessageAfterLoading(tfm::format(_("Reindexing block headers from files...") + "(blk%05i.dat)", pos.nFile));
            LogPrintf("Reindexing block file blk%05i.dat, headers-only...\n", pos.nFile);
            LoadBlocksFromExternalFile(file, &pos, /*loadHeadersOnly*/true);
            ++pos.nFile;
            pos.nPos = 0;
        }
        if (fReindexFast)
        {
            LogPrintf("Headers-only reindexing finished. Going on with blocks\n");
            pos.nFile = 0;
            pos.nPos = 0;
        }

        while (true)
        {
            if (!boost::filesystem::exists(GetBlockPosFilename(pos, "blk")))
                break; // No block files left to reindex
            FILE *file = OpenBlockFile(pos, true);
            assert(file); // This error is logged in OpenBlockFile
            uiInterface.InitMessageAfterLoading(tfm::format(_("Reindexing block from files...") + "(blk%05i.dat)", pos.nFile));
            LogPrintf("Reindexing block file blk%05i.dat...\n", pos.nFile);
            LoadBlocksFromExternalFile(file, &pos, /*loadHeadersOnly*/false);
            ++pos.nFile;
            pos.nPos = 0;
        }

        pblocktree->WriteReindexing(false);
        fReindex = false;
        pblocktree->WriteFastReindexing(false);
        fReindexFast = false;
        LogPrintf("Reindexing finished\n");
        uiInterface.InitMessage(_("Reindexing finished"));

        // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
        InitBlockIndex();
    }

    // hardcoded $DATADIR/bootstrap.dat
    boost::filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (boost::filesystem::exists(pathBootstrap)) {
        FILE *file = fopen(pathBootstrap.string().c_str(), "rb");
        if (file) {
            CImportingNow imp;
            boost::filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LogPrintf("Importing bootstrap.dat...\n");
            LoadBlocksFromExternalFile(file, nullptr, /*loadHeadersOnly*/false);
            RenameOver(pathBootstrap, pathBootstrapOld);
        } else {
            LogPrintf("Warning: Could not open bootstrap file %s\n", pathBootstrap.string());
        }
    }

    // -loadblock=
    BOOST_FOREACH(const boost::filesystem::path& path, vImportFiles) {
        FILE *file = fopen(path.string().c_str(), "rb");
        if (file) {
            CImportingNow imp;
            LogPrintf("Importing blocks file %s...\n", path.string());
            LoadBlocksFromExternalFile(file, nullptr, /*loadHeadersOnly*/false);
        } else {
            LogPrintf("Warning: Could not open blocks file %s\n", path.string());
        }
    }

    if (GetBoolArg("-stopafterblockimport", false)) {
        LogPrintf("Stopping after block import\n");
        StartShutdown();
    }
}

void ThreadNotifyRecentlyAdded()
{
    while (true) {
        // Run the notifier on an integer second in the steady clock.
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        auto nextFire = std::chrono::duration_cast<std::chrono::seconds>(
            now + std::chrono::seconds(1));
        std::this_thread::sleep_until(
            std::chrono::time_point<std::chrono::steady_clock>(nextFire));

        boost::this_thread::interruption_point();

        mempool->NotifyRecentlyAdded();
    }
}

/** Sanity checks
 *  Ensure that Bitcoin is running in a usable environment with all
 *  necessary library support.
 */
bool InitSanityCheck(void)
{
    if(!ECC_InitSanityCheck()) {
        InitError("Elliptic curve cryptography sanity check failure. Aborting.");
        return false;
    }
    if (!glibc_sanity_test() || !glibcxx_sanity_test())
        return false;

    return true;
}


static void ZC_LoadParams()
{
    struct timeval tv_start, tv_end;
    float elapsed;

    boost::filesystem::path pk_path = ZC_GetParamsDir() / "sprout-proving.key";
    boost::filesystem::path vk_path = ZC_GetParamsDir() / "sprout-verifying.key";
    boost::filesystem::path sapling_spend = ZC_GetParamsDir() / "sapling-spend.params";
    boost::filesystem::path sapling_output = ZC_GetParamsDir() / "sapling-output.params";
    boost::filesystem::path sprout_groth16 = ZC_GetParamsDir() / "sprout-groth16.params";

    if (!(
        boost::filesystem::exists(pk_path) &&
        boost::filesystem::exists(vk_path) &&
        boost::filesystem::exists(sapling_spend) &&
        boost::filesystem::exists(sapling_output) &&
        boost::filesystem::exists(sprout_groth16)
    )) {
        uiInterface.ThreadSafeMessageBox(strprintf(
            _("Cannot find the Horizen network parameters in the following directory:\n"
              "%s\n"
              "Please run 'zen-fetch-params' or './zcutil/fetch-params.sh' and then restart."),
                ZC_GetParamsDir()),
            "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return;
    }

    LogPrintf("Loading verifying key from %s\n", vk_path.string().c_str());
    gettimeofday(&tv_start, 0);

    pzcashParams = ZCJoinSplit::Prepared(vk_path.string(), pk_path.string());

    gettimeofday(&tv_end, 0);
    elapsed = float(tv_end.tv_sec-tv_start.tv_sec) + (tv_end.tv_usec-tv_start.tv_usec)/float(1000000);
    LogPrintf("Loaded verifying key in %fs seconds.\n", elapsed);

    std::string sapling_spend_str = sapling_spend.string();
    std::string sapling_output_str = sapling_output.string();
    std::string sprout_groth16_str = sprout_groth16.string();

    LogPrintf("Loading Sapling (Spend) parameters from %s\n", sapling_spend_str.c_str());
    LogPrintf("Loading Sapling (Output) parameters from %s\n", sapling_output_str.c_str());
    LogPrintf("Loading Sapling (Sprout Groth16) parameters from %s\n", sprout_groth16_str.c_str());
    gettimeofday(&tv_start, 0);

    librustzcash_init_zksnark_params(
        sapling_spend_str.c_str(),
        "8270785a1a0d0bc77196f000ee6d221c9c9894f55307bd9357c3f0105d31ca63991ab91324160d8f53e2bbd3c2633a6eb8bdf5205d822e7f3f73edac51b2b70c",
        sapling_output_str.c_str(),
        "657e3d38dbb5cb5e7dd2970e8b03d69b4787dd907285b5a7f0790dcc8072f60bf593b32cc2d1c030e00ff5ae64bf84c5c3beb84ddc841d48264b4a171744d028",
        sprout_groth16_str.c_str(),
        "e9b238411bd6c0ec4791e9d04245ec350c9c5744f5610dfcce4365d5ca49dfefd5054e371842b3f88fa1b9d7e8e075249b3ebabd167fa8b0f3161292d36c180a"
    );

    gettimeofday(&tv_end, 0);
    elapsed = float(tv_end.tv_sec-tv_start.tv_sec) + (tv_end.tv_usec-tv_start.tv_usec)/float(1000000);
    LogPrintf("Loaded Sapling parameters in %fs seconds.\n", elapsed);
}

bool AppInitServers()
{
    RPCServer::OnStopped(&OnRPCStopped);
    RPCServer::OnPreCommand(&OnRPCPreCommand);
    if (!InitHTTPServer())
        return false;
    if (!StartRPC())
        return false;
    if (!StartHTTPRPC())
        return false;
    if (GetBoolArg("-rest", false) && !StartREST())
        return false;
    if (!StartHTTPServer())
        return false;
    if (GetBoolArg("-websocket", false) && !StartWsServer())
        return false;
    return true;
}

// Variables internal to initialization process only
namespace {
    uint64_t nLocalServices = NODE_NETWORK;
    int nMaxConnections = DEFAULT_MAX_PEER_CONNECTIONS;
} // namespace

/** Initialize bitcoin.
 *  @pre Parameters should be parsed and config file should be read.
 */
bool AppInit2(boost::thread_group& threadGroup, CScheduler& scheduler)
{
    
    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    // Enable Data Execution Prevention (DEP)
    // Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
    // A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
    // We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
    // which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL (WINAPI *PSETPROCDEPPOL)(DWORD);
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    if (setProcDEPPol != NULL) setProcDEPPol(PROCESS_DEP_ENABLE);
#endif

    if (!SetupNetworking())
        return InitError("Error: Initializing networking failed");

#ifndef WIN32
    if (GetBoolArg("-sysperms", false)) {
#ifdef ENABLE_WALLET
        if (!GetBoolArg("-disablewallet", false))
            return InitError("Error: -sysperms is not allowed in combination with enabled wallet functionality");
#endif
    } else {
        umask(077);
    }

    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Reopen debug.log on SIGHUP
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSIGHUP;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, NULL);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
#endif

    // ********************************************************* Step 2: parameter interactions
    const CChainParams& chainparams = Params();

    // Set this early so that experimental features are correctly enabled/disabled
    fExperimentalMode = GetBoolArg("-experimentalfeatures", false);

    // Fail early if user has set experimental options without the global flag
    if (!fExperimentalMode) {
        if (mapArgs.count("-developerencryptwallet")) {
            return InitError(_("Wallet encryption requires -experimentalfeatures."));
        }
        else if (mapArgs.count("-paymentdisclosure")) {
            return InitError(_("Payment disclosure requires -experimentalfeatures."));
        } else if (mapArgs.count("-zmergetoaddress")) {
            return InitError(_("RPC method z_mergetoaddress requires -experimentalfeatures."));
        }
    }

    // Set this early so that parameter interactions go to console
    fPrintToConsole = GetBoolArg("-printtoconsole", false);
    fLogTimestamps = GetBoolArg("-logtimestamps", true);
    fLogTimeMicros = GetBoolArg("-logtimemicros", false);
    fLogIPs = GetBoolArg("-logips", false);

    // when specifying an explicit binding address, you want to listen on it
    // even when -connect or -proxy is specified
    if (mapArgs.count("-bind")) {
        if (SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -bind set -> setting -listen=1\n", __func__);
    }
    if (mapArgs.count("-whitebind")) {
        if (SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -whitebind set -> setting -listen=1\n", __func__);
    }

    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (SoftSetBoolArg("-dnsseed", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -dnsseed=0\n", __func__);
        if (SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -listen=0\n", __func__);
    }

    if (mapArgs.count("-proxy")) {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -listen=0\n", __func__);
        // to protect privacy, do not discover addresses by default
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -discover=0\n", __func__);
    }

    if (!GetBoolArg("-listen", DEFAULT_LISTEN)) {
        // do not try to retrieve public IP when not listening (pointless)
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -discover=0\n", __func__);
        if (SoftSetBoolArg("-listenonion", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -listenonion=0\n", __func__);
    }

    if (mapArgs.count("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -externalip set -> setting -discover=0\n", __func__);
    }

    if (GetBoolArg("-salvagewallet", false)) {
        // Rewrite just private keys: rescan to find transactions
        if (SoftSetBoolArg("-rescan", true))
            LogPrintf("%s: parameter interaction: -salvagewallet=1 -> setting -rescan=1\n", __func__);
    }

    // -zapwallettx implies a rescan
    if (GetBoolArg("-zapwallettxes", false)) {
        if (SoftSetBoolArg("-rescan", true))
            LogPrintf("%s: parameter interaction: -zapwallettxes=<mode> -> setting -rescan=1\n", __func__);
    }

    // Make sure enough file descriptors are available
    int nBind = std::max((int)mapArgs.count("-bind") + (int)mapArgs.count("-whitebind"), 1);
    nMaxConnections = GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS);
    nMaxConnections = std::max(std::min(nMaxConnections, (int)(FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS)), 0);
    int nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(_("Not enough file descriptors available."));
    if (nFD - MIN_CORE_FILEDESCRIPTORS < nMaxConnections)
        nMaxConnections = nFD - MIN_CORE_FILEDESCRIPTORS;

    // if using block pruning, then disable txindex
    // also disable the wallet (for now, until SPV support is implemented in wallet)
    if (GetArg("-prune", 0)) {
        if (GetBoolArg("-txindex", DEFAULT_TXINDEX))
            return InitError(_("Prune mode is incompatible with -txindex."));
#ifdef ENABLE_WALLET
        if (!GetBoolArg("-disablewallet", false)) {
            if (SoftSetBoolArg("-disablewallet", true))
                LogPrintf("%s : parameter interaction: -prune -> setting -disablewallet=1\n", __func__);
            else
                return InitError(_("Can't run with a wallet in prune mode."));
        }
#endif
    }

    if (GetBoolArg("-reindex", false) && GetBoolArg("-reindexfast", false))
        return InitError(_("-reindex is incompatible with -reindexfast."));

    if ( (Params().NetworkIDString() == "regtest") && mapArgs.count("-subsidyhalvinginterval"))
    {
        int val = (int)(GetArg("-subsidyhalvinginterval", Params().GetConsensus().nSubsidyHalvingInterval));
        if (val != Params().GetConsensus().nSubsidyHalvingInterval)
        {
            LogPrintf("%s():%d - nSubsidyHalvingInterval = %d\n",
                __func__, __LINE__, Params().GetConsensus().nSubsidyHalvingInterval);

            // Halving period cannot be shortened below fork2 activation height. In fact fork1_chainsplitfork imposes
            // a maximal height for community rewards generation, currently set to the first halving height.
            // Should be the first halving height be reduced below fork2 activation, the maximal height constraing could
            // be violated for otherwise valid height, causing an assert to crash the node
            ::zen::ReplayProtectionFork targetFork;
            if (val <= targetFork.getHeight(CBaseChainParams::REGTEST))
            	assert(!"subsidyhalvinginterval too low");

            Params(CBaseChainParams::REGTEST).SetSubsidyHalvingInterval(val);
            LogPrintf("%s():%d - %s: set nSubsidyHalvingInterval = %d) \n",
                __func__, __LINE__, Params().NetworkIDString(), Params().GetConsensus().nSubsidyHalvingInterval);
        }
    }

    // ********************************************************* Step 3: parameter-to-internal-flags

    fDebug = !mapMultiArgs["-debug"].empty();
    // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
    const vector<string>& categories = mapMultiArgs["-debug"];
    if (GetBoolArg("-nodebug", false) || find(categories.begin(), categories.end(), string("0")) != categories.end())
        fDebug = false;

    // Special case: if debug=zrpcunsafe, implies debug=zrpc, so add it to debug categories
    if (find(categories.begin(), categories.end(), string("zrpcunsafe")) != categories.end()) {
        if (find(categories.begin(), categories.end(), string("zrpc")) == categories.end()) {
            LogPrintf("%s: parameter interaction: setting -debug=zrpcunsafe -> -debug=zrpc\n", __func__);
            vector<string>& v = mapMultiArgs["-debug"];
            v.push_back("zrpc");
        }
    }

    // Check for -debugnet
    if (GetBoolArg("-debugnet", false))
        InitWarning(_("Warning: Unsupported argument -debugnet ignored, use -debug=net."));
    // Check for -socks - as this is a privacy risk to continue, exit here
    if (mapArgs.count("-socks"))
        return InitError(_("Error: Unsupported argument -socks found. Setting SOCKS version isn't possible anymore, only SOCKS5 proxies are supported."));
    // Check for -tor - as this is a privacy risk to continue, exit here
    if (GetBoolArg("-tor", false))
        return InitError(_("Error: Unsupported argument -tor found, use -onion."));

    if (GetBoolArg("-benchmark", false))
        InitWarning(_("Warning: Unsupported argument -benchmark ignored, use -debug=bench."));

    // Fee-per-kilobyte amount considered the same as "free"
    // If you are mining, be careful setting this:
    // if you set it to zero then
    // a transaction spammer can cheaply fill blocks using
    // 1-satoshi-fee transactions. It should be set above the real
    // cost to you of processing a transaction.
    if (mapArgs.count("-minrelaytxfee"))
    {
        CAmount n = 0;
        if (ParseMoney(mapArgs["-minrelaytxfee"], n) && n > 0)
            ::minRelayTxFee = CFeeRate(n);
        else
            return InitError(strprintf(_("Invalid amount for -minrelaytxfee=<amount>: '%s'"), mapArgs["-minrelaytxfee"]));
    }

    // -mempoollimit limits
    int64_t nMempoolSizeLimit = GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE_MB) * 1000000;
    if (nMempoolSizeLimit < 4 * 1000000) {
        return InitError(strprintf(_("Error: -maxmempool must be at least %d MB"), 4));
    }
    mempool.reset(new CTxMemPool(::minRelayTxFee, nMempoolSizeLimit));

    // Checkmempool and checkblockindex default to true in regtest mode
    mempool->setSanityCheck(GetBoolArg("-checkmempool", chainparams.DefaultConsistencyChecks()));
    fCheckBlockIndex = GetBoolArg("-checkblockindex", chainparams.DefaultConsistencyChecks());
    fCheckpointsEnabled = GetBoolArg("-checkpoints", true);

    // -par=0 means autodetect, but nScriptCheckThreads==0 means no concurrency
    nScriptCheckThreads = GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS);
    if (nScriptCheckThreads <= 0)
        nScriptCheckThreads += GetNumCores();
    if (nScriptCheckThreads <= 1)
        nScriptCheckThreads = 0;
    else if (nScriptCheckThreads > MAX_SCRIPTCHECK_THREADS)
        nScriptCheckThreads = MAX_SCRIPTCHECK_THREADS;

    fServer = GetBoolArg("-server", false);

    // block pruning; get the amount of disk space (in MB) to allot for block & undo files
    int64_t nSignedPruneTarget = GetArg("-prune", 0) * 1024 * 1024;
    if (nSignedPruneTarget < 0) {
        return InitError(_("Prune cannot be configured with a negative value."));
    }
    nPruneTarget = (uint64_t) nSignedPruneTarget;
    if (nPruneTarget) {
        if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES) {
            return InitError(strprintf(_("Prune configured below the minimum of %d MB.  Please use a higher number."), MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
        }
        LogPrintf("Prune configured to target %uMiB on disk for block and undo files.\n", nPruneTarget / 1024 / 1024);
        fPruneMode = true;
    }


#ifdef ENABLE_WALLET
    bool fDisableWallet = GetBoolArg("-disablewallet", false);
#endif

    nConnectTimeout = GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0)
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

#ifdef ENABLE_WALLET
    if (mapArgs.count("-mintxfee"))
    {
        CAmount n = 0;
        if (ParseMoney(mapArgs["-mintxfee"], n) && n > 0)
            CWallet::minTxFee = CFeeRate(n);
        else
            return InitError(strprintf(_("Invalid amount for -mintxfee=<amount>: '%s'"), mapArgs["-mintxfee"]));
    }
    if (mapArgs.count("-paytxfee"))
    {
        CAmount nFeePerK = 0;
        if (!ParseMoney(mapArgs["-paytxfee"], nFeePerK))
            return InitError(strprintf(_("Invalid amount for -paytxfee=<amount>: '%s'"), mapArgs["-paytxfee"]));
        if (nFeePerK > nHighTransactionFeeWarning)
            InitWarning(_("Warning: -paytxfee is set very high! This is the transaction fee you will pay if you send a transaction."));
        payTxFee = CFeeRate(nFeePerK, 1000);
        if (payTxFee < ::minRelayTxFee)
        {
            return InitError(strprintf(_("Invalid amount for -paytxfee=<amount>: '%s' (must be at least %s)"),
                                       mapArgs["-paytxfee"], ::minRelayTxFee.ToString()));
        }
    }
    if (mapArgs.count("-maxtxfee"))
    {
        CAmount nMaxFee = 0;
        if (!ParseMoney(mapArgs["-maxtxfee"], nMaxFee))
            return InitError(strprintf(_("Invalid amount for -maxtxfee=<amount>: '%s'"), mapArgs["-maxtxfee"]));
        if (nMaxFee > nHighTransactionMaxFeeWarning)
            InitWarning(_("Warning: -maxtxfee is set very high! Fees this large could be paid on a single transaction."));
        maxTxFee = nMaxFee;
        if (CFeeRate(maxTxFee, 1000) < ::minRelayTxFee)
        {
            return InitError(strprintf(_("Invalid amount for -maxtxfee=<amount>: '%s' (must be at least the minrelay fee of %s to prevent stuck transactions)"),
                                       mapArgs["-maxtxfee"], ::minRelayTxFee.ToString()));
        }
    }
    nTxConfirmTarget = GetArg("-txconfirmtarget", DEFAULT_TX_CONFIRM_TARGET);
    bSpendZeroConfChange = GetBoolArg("-spendzeroconfchange", true);
    fSendFreeTransactions = GetBoolArg("-sendfreetransactions", false);

    std::string strWalletFile = GetArg("-wallet", "wallet.dat");
#endif // ENABLE_WALLET

    fIsBareMultisigStd = GetBoolArg("-permitbaremultisig", true);
    nMaxDatacarrierBytes = GetArg("-datacarriersize", nMaxDatacarrierBytes);

    // Option to startup with mocktime set (used for regression testing):
    SetMockTime(GetArg("-mocktime", 0)); // SetMockTime(0) is a no-op

#ifdef ENABLE_MINING
    if (mapArgs.count("-mineraddress")) {
        CBitcoinAddress addr;
        if (!addr.SetString(mapArgs["-mineraddress"])) {
            return InitError(strprintf(
                _("Invalid address for -mineraddress=<addr>: '%s' (must be a transparent address)"),
                mapArgs["-mineraddress"]));
        }
    }
#endif

    // Default value of 0 for mempooltxinputlimit means no limit is applied
    if (mapArgs.count("-mempooltxinputlimit")) {
        int64_t limit = GetArg("-mempooltxinputlimit", 0);
        if (limit < 0) {
            return InitError(_("Mempool limit on transparent inputs to a transaction cannot be negative"));
        } else if (limit > 0) {
            LogPrintf("Mempool configured to reject transactions with greater than %lld transparent inputs\n", limit);
        }
    }

    // ********************************************************* Step 4: application initialization: dir lock, daemonize, pidfile, debug log

    // Initialize libsodium
    if (init_and_check_sodium() == -1) {
        return false;
    }

    // Initialize elliptic curve code
    ECC_Start();
    globalVerifyHandle.reset(new ECCVerifyHandle());

    // Sanity check
    if (!InitSanityCheck())
        return InitError(_("Initialization sanity check failed. Zen is shutting down."));

#ifdef ENABLE_WALLET
    // Wallet file must be a plain filename without a directory
    if (strWalletFile != boost::filesystem::path(strWalletFile).filename().string())
        return InitError(strprintf(_("Wallet %s resides outside data directory %s"), strWalletFile, GetDataDir().string()));
#endif
    // Make sure only a single Bitcoin process is using the data directory.
    boost::filesystem::path pathLockFile = GetDataDir() / ".lock";
    FILE* file = fopen(pathLockFile.string().c_str(), "a"); // empty lock file; created if it doesn't exist.
    if (file) fclose(file);

    try {
        static boost::interprocess::file_lock lock(pathLockFile.string().c_str());
        if (!lock.try_lock())
            return InitError(strprintf(_("Cannot obtain a lock on data directory %s. Zen is probably already running."), GetDataDir().string()));

    } catch(const boost::interprocess::interprocess_exception& e) {
        return InitError(strprintf(_("Cannot obtain a lock on data directory %s. Zen is probably already running.") + " %s.", GetDataDir().string(), e.what()));
    }

    // Initialize sidechains folder, hosting keys for sidechains validations
    if(!Sidechain::InitSidechainsFolder())
        return InitError(strprintf(_("Cannot create or access sidechains folder.")));

    // Initialize Zendoo
    if(!Sidechain::InitZendoo())
        return InitError(strprintf(_("Cannot initialize Zendoo.")));

    // Initialize DLog keys
    if(!Sidechain::InitDLogKeys())
        return InitError(strprintf(_("Cannot initialize DLog keys in sidechains folder.")));

#ifndef WIN32
    CreatePidFile(GetPidFile(), getpid());
#endif
    fLimitDebugLogSize = GetBoolArg("-limitdebuglogsize", !fDebug);

    if (GetBoolArg("-shrinkdebugfile", !fDebug))
        ShrinkDebugFile();

    LogPrintf("Zen version %s (%s)\n", FormatFullVersion(), CLIENT_DATE);

    if (fPrintToDebugLog)
        OpenDebugLog();

    LogPrintf("Using OpenSSL version %s\n", SSLeay_version(SSLEAY_VERSION));
#ifdef ENABLE_WALLET
    LogPrintf("Using BerkeleyDB version %s\n", DbEnv::version(0, 0, 0));
#endif
    if (!fLogTimestamps)
        LogPrintf("Startup time: %s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()));
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
    LogPrintf("Using data directory %s\n", GetDataDir().string());
    // This print might not be correct, since at this point the options in the zen.conf have already been read, and if a custom datadir has
    // ben specified, the config file would be erroneously printed to be there
    //    LogPrintf("Using config file %s\n", GetConfigFile().string());
    LogPrintf("Using at most %i connections (%i file descriptors available)\n", nMaxConnections, nFD);
    std::ostringstream strErrors;

    LogPrintf("Using %u threads for script verification\n", nScriptCheckThreads);
    if (nScriptCheckThreads) {
        for (int i=0; i<nScriptCheckThreads-1; i++)
            threadGroup.create_thread(&ThreadScriptCheck);
    }

    // Start the lightweight task scheduler thread
    CScheduler::Function serviceLoop = boost::bind(&CScheduler::serviceQueue, &scheduler);
    threadGroup.create_thread(boost::bind(&TraceThread<CScheduler::Function>, "scheduler", serviceLoop));

    // Count uptime
    MarkStartTime();

    if ((chainparams.NetworkIDString() != "regtest") &&
            GetBoolArg("-showmetrics", isatty(STDOUT_FILENO)) &&
            !fPrintToConsole && !GetBoolArg("-daemon", false)) {
        // Start the persistent metrics interface
        ConnectMetricsScreen();
        threadGroup.create_thread(&ThreadShowMetricsScreen);
    }

    // These must be disabled for now, they are buggy and we probably don't
    // want any of libsnark's profiling in production anyway.
    libsnark::inhibit_profiling_info = true;
    libsnark::inhibit_profiling_counters = true;

    // Initialize Zcash circuit parameters
    ZC_LoadParams();

    // check type sizes in crypto lib are as expected and assert() in case of failure
    CZendooCctpLibraryChecker::CheckTypeSizes();

    /* Start the RPC server already.  It will be started in "warmup" mode
     * and not really process calls already (but it will signify connections
     * that the server is there and will be ready later).  Warmup mode will
     * be disabled when initialisation is finished.
     */
    if (fServer)
    {
        uiInterface.InitMessage.connect(SetRPCWarmupStatus);
        if (!AppInitServers())
            return InitError(_("Unable to start HTTP server. See debug log for details."));
    }

    int64_t nStart;

    // ********************************************************* Step 5: verify wallet database integrity
#ifdef ENABLE_WALLET
    if (!fDisableWallet) {
        LogPrintf("Using wallet %s\n", strWalletFile);
        uiInterface.InitMessage(_("Verifying wallet..."));

        std::string warningString;
        std::string errorString;

        if (!CWallet::Verify(strWalletFile, warningString, errorString))
            return false;

        if (!warningString.empty())
            InitWarning(warningString);
        if (!errorString.empty())
            return InitError(warningString);

    } // (!fDisableWallet)
#endif // ENABLE_WALLET
    // ********************************************************* Step 6: network initialization

    RegisterNodeSignals(GetNodeSignals());

    assert(!connman);
    connman = std::make_unique<CConnman>();
    CConnman::Options connOptions;

    if (mapArgs.count("-onlynet")) {
        std::set<enum Network> nets;
        BOOST_FOREACH(const std::string& snet, mapMultiArgs["-onlynet"]) {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++) {
            enum Network net = (enum Network)n;
            if (!nets.count(net))
                SetLimited(net);
        }
    }

    if (mapArgs.count("-whitelist")) {
        LOCK(connman->cs_vWhitelistedRange);
        BOOST_FOREACH(const std::string& net, mapMultiArgs["-whitelist"]) {
            CSubNet subnet(net);
            if (!subnet.IsValid())
                return InitError(strprintf(_("Invalid netmask specified in -whitelist: '%s'"), net));
            connOptions.vWhitelistedRange.push_back(subnet);
        }
    }

    bool proxyRandomize = GetBoolArg("-proxyrandomize", true);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
    std::string proxyArg = GetArg("-proxy", "");
    SetLimited(NET_TOR);
    if (proxyArg != "" && proxyArg != "0") {
        proxyType addrProxy = proxyType(CService(proxyArg, 9050), proxyRandomize);
        if (!addrProxy.IsValid())
            return InitError(strprintf(_("Invalid -proxy address: '%s'"), proxyArg));

        SetProxy(NET_IPV4, addrProxy);
        SetProxy(NET_IPV6, addrProxy);
        SetProxy(NET_TOR, addrProxy);
        SetNameProxy(addrProxy);
        SetLimited(NET_TOR, false); // by default, -proxy sets onion as reachable, unless -noonion later
    }

    // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
    // -noonion (or -onion=0) disables connecting to .onion entirely
    // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
    std::string onionArg = GetArg("-onion", "");
    if (onionArg != "") {
        if (onionArg == "0") { // Handle -noonion/-onion=0
            SetLimited(NET_TOR); // set onions as unreachable
        } else {
            proxyType addrOnion = proxyType(CService(onionArg, 9050), proxyRandomize);
            if (!addrOnion.IsValid())
                return InitError(strprintf(_("Invalid -onion address: '%s'"), onionArg));
            SetProxy(NET_TOR, addrOnion);
            SetLimited(NET_TOR, false);
        }
    }

    // see Step 2: parameter interactions for more information about these
    fListen = GetBoolArg("-listen", DEFAULT_LISTEN);
    fDiscover = GetBoolArg("-discover", true);
    fNameLookup = GetBoolArg("-dns", true);

    bool fBound = false;
    if (fListen) {
        if (mapArgs.count("-bind") || mapArgs.count("-whitebind")) {
            BOOST_FOREACH(const std::string& strBind, mapMultiArgs["-bind"]) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
                    return InitError(strprintf(_("Cannot resolve -bind address: '%s'"), strBind));
                fBound |= connman->Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR));
            }
            BOOST_FOREACH(const std::string& strBind, mapMultiArgs["-whitebind"]) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, 0, false))
                    return InitError(strprintf(_("Cannot resolve -whitebind address: '%s'"), strBind));
                if (addrBind.GetPort() == 0)
                    return InitError(strprintf(_("Need to specify a port with -whitebind: '%s'"), strBind));
                fBound |= connman->Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR | BF_WHITELIST));
            }
        }
        else {
            struct in_addr inaddr_any;
            inaddr_any.s_addr = INADDR_ANY;
            fBound |= connman->Bind(CService(in6addr_any, GetListenPort()), BF_NONE);
            fBound |= connman->Bind(CService(inaddr_any, GetListenPort()), !fBound ? BF_REPORT_ERROR : BF_NONE);
        }
        if (!fBound)
            return InitError(_("Failed to listen on any port. Use -listen=0 if you want this."));
    }

    if (mapArgs.count("-externalip")) {
        BOOST_FOREACH(const std::string& strAddr, mapMultiArgs["-externalip"]) {
            CService addrLocal(strAddr, GetListenPort(), fNameLookup);
            if (!addrLocal.IsValid())
                return InitError(strprintf(_("Cannot resolve -externalip address: '%s'"), strAddr));
            AddLocal(CService(strAddr, GetListenPort(), fNameLookup), LOCAL_MANUAL);
        }
    }

    BOOST_FOREACH(const std::string& strDest, mapMultiArgs["-seednode"])
        connman->AddOneShot(strDest);

    if (mapArgs.count("-tlskeypath")) {
        boost::filesystem::path pathTLSKey(GetArg("-tlskeypath", ""));
    if (!boost::filesystem::exists(pathTLSKey))
         return InitError(strprintf(_("Cannot find TLS key file: '%s'"), pathTLSKey.string()));
    }

    if (mapArgs.count("-tlscertpath")) {
        boost::filesystem::path pathTLSCert(GetArg("-tlscertpath", ""));
    if (!boost::filesystem::exists(pathTLSCert))
        return InitError(strprintf(_("Cannot find TLS cert file: '%s'"), pathTLSCert.string()));
    }

    if (mapArgs.count("-tlstrustdir")) {
        boost::filesystem::path pathTLSTrustredDir(GetArg("-tlstrustdir", ""));
        if (!boost::filesystem::exists(pathTLSTrustredDir))
            return InitError(strprintf(_("Cannot find trusted certificates directory: '%s'"), pathTLSTrustredDir.string()));
    }

#if ENABLE_ZMQ
    pzmqNotificationInterface = CZMQNotificationInterface::CreateWithArguments(mapArgs);

    if (pzmqNotificationInterface) {
        RegisterValidationInterface(pzmqNotificationInterface);
    }
#endif

#if ENABLE_PROTON
    pAMQPNotificationInterface = AMQPNotificationInterface::CreateWithArguments(mapArgs);

    if (pAMQPNotificationInterface) {

        // AMQP support is currently an experimental feature, so fail if user configured AMQP notifications
        // without enabling experimental features.
        if (!fExperimentalMode) {
            return InitError(_("AMQP support requires -experimentalfeatures."));
        }

        RegisterValidationInterface(pAMQPNotificationInterface);
    }
#endif

    // ********************************************************* Step 7: load block chain

    fReindex = GetBoolArg("-reindex", false);
    fReindexFast = GetBoolArg("-reindexfast", false);

    // Upgrading to 0.8; hard-link the old blknnnn.dat files into /blocks/
    boost::filesystem::path blocksDir = GetDataDir() / "blocks";
    if (!boost::filesystem::exists(blocksDir))
    {
        boost::filesystem::create_directories(blocksDir);
        bool linked = false;
        for (unsigned int i = 1; i < 10000; i++) {
            boost::filesystem::path source = GetDataDir() / strprintf("blk%04u.dat", i);
            if (!boost::filesystem::exists(source)) break;
            boost::filesystem::path dest = blocksDir / strprintf("blk%05u.dat", i-1);
            try {
                boost::filesystem::create_hard_link(source, dest);
                LogPrintf("Hardlinked %s -> %s\n", source.string(), dest.string());
                linked = true;
            } catch (const boost::filesystem::filesystem_error& e) {
                // Note: hardlink creation failing is not a disaster, it just means
                // blocks will get re-downloaded from peers.
                LogPrintf("Error hardlinking blk%04u.dat: %s\n", i, e.what());
                break;
            }
        }
        if (linked)
        {
            fReindex = true;
        }
    }

    // block tree db settings
    int blocktreedbMaxOpenFiles = GetArg("-blocktreedbmaxopenfiles", DEFAULT_DB_MAX_OPEN_FILES);
    int coinsviewdbMaxOpenFiles = GetArg("-coinsviewdbmaxopenfiles", DEFAULT_DB_MAX_OPEN_FILES);

    LogPrintf("LevelDB database configuration:\n");
    LogPrintf("* Using %d max open files (blocktreedb)\n", blocktreedbMaxOpenFiles);
    LogPrintf("* Using %d max open files (coinsviewdb)\n", coinsviewdbMaxOpenFiles);

    // cache size calculations
    int64_t nTotalCache = (GetArg("-dbcache", nDefaultDbCache) << 20);
    nTotalCache = std::max(nTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    nTotalCache = std::min(nTotalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbcache
    int64_t nBlockTreeDBCache = nTotalCache / 8;

    if (GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX) || GetBoolArg("-spentindex", DEFAULT_SPENTINDEX)) {
        // enable 3/4 of the cache if addressindex and/or spentindex is enabled
        nBlockTreeDBCache = nTotalCache * 3 / 4;
    } else {
        if (nBlockTreeDBCache > (1 << 21) && !GetBoolArg("-txindex", DEFAULT_TXINDEX)) {
            nBlockTreeDBCache = (1 << 21); // block tree db cache shouldn't be larger than 2 MiB
        }
    }

    nTotalCache -= nBlockTreeDBCache;
    int64_t nCoinDBCache = std::min(nTotalCache / 2, (nTotalCache / 4) + (1 << 23)); // use 25%-50% of the remainder for disk cache
    nTotalCache -= nCoinDBCache;
    nCoinCacheUsage = nTotalCache; // the rest goes to in-memory cache
    LogPrintf("Cache configuration:\n");
    LogPrintf("* Max cache setting possible %.1fMiB\n", nMaxDbCache);
    LogPrintf("* Using %.1fMiB for block index database\n", nBlockTreeDBCache * (1.0 / 1024 / 1024));
    LogPrintf("* Using %.1fMiB for chain state database\n", nCoinDBCache * (1.0 / 1024 / 1024));
    LogPrintf("* Using %.1fMiB for in-memory UTXO set\n", nCoinCacheUsage * (1.0 / 1024 / 1024));

    bool fLoaded = false;
    while (!fLoaded) {
        bool fReset = fReindex || fReindexFast;
        std::string strLoadError;

        uiInterface.InitMessage(_("Loading block index..."));

        nStart = GetTimeMillis();
        do {
            try {
                UnloadBlockIndex();
                delete pcoinsTip;
                delete pcoinsdbview;
                delete pcoinscatcher;
                delete pblocktree;

                pblocktree = new CBlockTreeDB(nBlockTreeDBCache, blocktreedbMaxOpenFiles, false, fReindex || fReindexFast);
                pcoinsdbview = new CCoinsViewDB(nCoinDBCache, coinsviewdbMaxOpenFiles, false, fReindex || fReindexFast);
                pcoinscatcher = new CCoinsViewErrorCatcher(pcoinsdbview);
                pcoinsTip = new CCoinsViewCache(pcoinscatcher);

                if (fReindex || fReindexFast) {
                    if (fReindex) pblocktree->WriteReindexing(true);
                    if (fReindexFast) pblocktree->WriteFastReindexing(true);

                    //If we're reindexing in prune mode, wipe away unusable block files and all undo data files
                    if (fPruneMode)
                        CleanupBlockRevFiles();

                    Sidechain::ClearSidechainsFolder();
                }

                if (!LoadBlockIndex()) {
                    strLoadError = _("Error loading block database");
                    break;
                }

                // If the loaded chain has a wrong genesis, bail out immediately
                // (we're likely using a testnet datadir, or the other way around).
                if (!mapBlockIndex.empty() && mapBlockIndex.count(chainparams.GetConsensus().hashGenesisBlock) == 0)
                    return InitError(_("Incorrect or no genesis block found. Wrong datadir for network?"));

                // Initialize the block index (no-op if non-empty database was already loaded)
                if (!InitBlockIndex()) {
                    strLoadError = _("Error initializing block database");
                    break;
                }

                // Check for changed -txindex state
                if (fTxIndex != GetBoolArg("-txindex", DEFAULT_TXINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -txindex");
                    break;
                }

                // Check for changed -maturityheightindex state
                if (fMaturityHeightIndex != GetBoolArg("-maturityheightindex", DEFAULT_MATURITYHEIGHTINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -maturityheightindex");
                    break;
                }

                // read the version of the txindexing related version
                std::string indexVersionStr = DEFAULT_INDEX_VERSION_STR;
                pblocktree->ReadString("indexVersion", indexVersionStr);
                LogPrintf("%s: indexVersion %s\n", __func__, indexVersionStr);  
   
                // Check for changed -addressindex state
                if (fAddressIndex != GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -addressindex");
                    break;
                }

                // Check for changed -timestampindex state
                if (fTimestampIndex != GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -timestampindex");
                    break;
                }

                // Check for changed -spentindex state
                if (fSpentIndex != GetBoolArg("-spentindex", DEFAULT_SPENTINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -spentindex");
                    break;
                }

                // Check that -txindex is enabled when -addressindex is enabled
                if (fAddressIndex && !fTxIndex) {
                    strLoadError = _("You need to enable -txindex in order to use -addressindex");
                    break;
                }

                if (fAddressIndex && indexVersionStr == DEFAULT_INDEX_VERSION_STR) {
                    strLoadError = _("You need to reindex in order to use -addressindex");
                    break;
                }

                if (fMaturityHeightIndex && indexVersionStr == DEFAULT_INDEX_VERSION_STR)
                {
                    indexVersionStr = CURRENT_INDEX_VERSION_STR;
                    pblocktree->WriteString("indexVersion", indexVersionStr); 
   
                    std::string dum;
                    pblocktree->ReadString("indexVersion", dum);
                    LogPrintf("%s: indexVersion %s\n", __func__, dum);  
                }

                // Check that -txindex is enabled when -maturityheightindex is enabled
                if (fMaturityHeightIndex && !fTxIndex) {
                    strLoadError = _("You need to enable -txindex in order to use -maturityheightindex");
                    break;
                }

                // Check for changed -prune state.  What we are concerned about is a user who has pruned blocks
                // in the past, but is now trying to run unpruned.
                if (fHavePruned && !fPruneMode) {
                    strLoadError = _("You need to rebuild the database using -reindex or -reindexfast to go back to unpruned mode.  This will redownload the entire blockchain");
                    break;
                }

                uiInterface.InitMessage(_("Verifying blocks..."));
                if (fHavePruned && GetArg("-checkblocks", 288) > MIN_BLOCKS_TO_KEEP) {
                    LogPrintf("Prune: pruned datadir may not have more than %d blocks; -checkblocks=%d may fail\n",
                        MIN_BLOCKS_TO_KEEP, GetArg("-checkblocks", 288));
                }
                if (!CVerifyDB().VerifyDB(pcoinsdbview, GetArg("-checklevel", 3),
                              GetArg("-checkblocks", 288))) {
                    strLoadError = _("Corrupted block database detected");
                    break;
                }
            } catch (const std::exception& e) {
                if (fDebug) LogPrintf("%s\n", e.what());
                strLoadError = _("Error opening block database");
                break;
            }

            fLoaded = true;
        } while(false);

        if (!fLoaded) {
            // first suggest a reindex
            if (!fReset) {
                bool fRet = uiInterface.ThreadSafeQuestion(
                    strLoadError + ".\n\n" + _("Do you want to rebuild the block database now?"),
                    strLoadError + ".\nPlease restart with -reindex to recover.",
                    "", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
                if (fRet) {
                    fReindex = true;
                    fRequestShutdown = false;
                } else {
                    LogPrintf("Aborted block database rebuild. Exiting.\n");
                    return false;
                }
            } else {
                return InitError(strLoadError);
            }
        }
    }

    // As LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (fRequestShutdown)
    {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }
    LogPrintf(" block index %15dms\n", GetTimeMillis() - nStart);

    boost::filesystem::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
    CAutoFile est_filein(fopen(est_path.string().c_str(), "rb"), SER_DISK, CLIENT_VERSION);
    // Allowed to fail as this file IS missing on first startup.
    if (!est_filein.IsNull())
        mempool->ReadFeeEstimates(est_filein);
    fFeeEstimatesInitialized = true;


    // ********************************************************* Step 8: load wallet
#ifdef ENABLE_WALLET
    if (fDisableWallet) {
        pwalletMain = NULL;
        LogPrintf("Wallet disabled!\n");
    } else {

        // needed to restore wallet transaction meta data after -zapwallettxes
        std::vector<std::shared_ptr<CWalletTransactionBase>> vWtx;

        if (GetBoolArg("-zapwallettxes", false)) {
            uiInterface.InitMessage(_("Zapping all transactions from wallet..."));

            pwalletMain = new CWallet(strWalletFile);
            DBErrors nZapWalletRet = pwalletMain->ZapWalletTx(vWtx);
            if (nZapWalletRet != DB_LOAD_OK) {
                uiInterface.InitMessage(_("Error loading wallet.dat: Wallet corrupted"));
                return false;
            }

            delete pwalletMain;
            pwalletMain = NULL;
        }

        uiInterface.InitMessage(_("Loading wallet..."));

        nStart = GetTimeMillis();
        bool fFirstRun = true;
        pwalletMain = new CWallet(strWalletFile);
        DBErrors nLoadWalletRet = pwalletMain->LoadWallet(fFirstRun);
        if (nLoadWalletRet != DB_LOAD_OK)
        {
            if (nLoadWalletRet == DB_CORRUPT)
                strErrors << _("Error loading wallet.dat: Wallet corrupted") << "\n";
            else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
            {
                string msg(_("error reading wallet.dat! All keys read correctly, but transaction data"
                             " or address book entries might be missing or incorrect."));

                bool reindexing = false;
                pblocktree->ReadReindexing(reindexing);
                if (reindexing)
                {
                    msg = string(_("(Reindexing in progress...) ")) + msg;
                }
                InitWarning(msg);
            }
            else if (nLoadWalletRet == DB_TOO_NEW)
                strErrors << _("Error loading wallet.dat: Wallet requires newer version of Horizen") << "\n";
            else if (nLoadWalletRet == DB_NEED_REWRITE)
            {
                strErrors << _("Wallet needed to be rewritten: restart Horizen to complete") << "\n";
                LogPrintf("%s", strErrors.str());
                return InitError(strErrors.str());
            }
            else
                strErrors << _("Error loading wallet.dat") << "\n";
        }

        if (GetBoolArg("-upgradewallet", fFirstRun))
        {
            int nMaxVersion = GetArg("-upgradewallet", 0);
            if (nMaxVersion == 0) // the -upgradewallet without argument case
            {
                LogPrintf("Performing wallet upgrade to %i\n", FEATURE_LATEST);
                nMaxVersion = CLIENT_VERSION;
                pwalletMain->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
            }
            else
                LogPrintf("Allowing wallet upgrade up to %i\n", nMaxVersion);
            if (nMaxVersion < pwalletMain->GetVersion())
                strErrors << _("Cannot downgrade wallet") << "\n";
            pwalletMain->SetMaxVersion(nMaxVersion);
        }

        if (fFirstRun)
        {
            // Create new keyUser and set as default key
            CPubKey newDefaultKey;
            if (pwalletMain->GetKeyFromPool(newDefaultKey)) {
                pwalletMain->SetDefaultKey(newDefaultKey);
                if (!pwalletMain->SetAddressBook(pwalletMain->vchDefaultKey.GetID(), "", "receive"))
                    strErrors << _("Cannot write default address") << "\n";
            }

            pwalletMain->SetBestChain(chainActive.GetLocator());
        }

        LogPrintf("%s", strErrors.str());
        LogPrintf(" wallet      %15dms\n", GetTimeMillis() - nStart);

        RegisterValidationInterface(pwalletMain);

        CBlockIndex *pindexRescan = chainActive.Tip();
        if (GetBoolArg("-rescan", false))
        {
            pwalletMain->ClearNoteWitnessCache();
            pindexRescan = chainActive.Genesis();
        }
        else
        {
            CWalletDB walletdb(strWalletFile);
            CBlockLocator locator;
            if (walletdb.ReadBestBlock(locator))
                pindexRescan = FindForkInGlobalIndex(chainActive, locator);
            else
                pindexRescan = chainActive.Genesis();
        }
        if (chainActive.Tip() && chainActive.Tip() != pindexRescan)
        {
            uiInterface.InitMessage(_("Rescanning..."));
            LogPrintf("Rescanning last %i blocks (from block %i)...\n", chainActive.Height() - pindexRescan->nHeight, pindexRescan->nHeight);
            nStart = GetTimeMillis();
            pwalletMain->ScanForWalletTransactions(pindexRescan, true);
            LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);
            pwalletMain->SetBestChain(chainActive.GetLocator());
            nWalletDBUpdated++;

            // Restore wallet transaction metadata after -zapwallettxes=1
            if (GetBoolArg("-zapwallettxes", false) && GetArg("-zapwallettxes", "1") != "2")
            {
                CWalletDB walletdb(strWalletFile);

                for(const auto& wtxOld: vWtx)
                {
                    uint256 hash = wtxOld->getTxBase()->GetHash();
                    auto mi = pwalletMain->getMapWallet().find(hash);
                    if (mi != pwalletMain->getMapWallet().end())
                    {
                        const auto* copyFrom = wtxOld.get();
                        CWalletTransactionBase* copyTo = mi->second.get();
                        copyTo->mapValue = copyFrom->mapValue;
                        copyTo->vOrderForm = copyFrom->vOrderForm;
                        copyTo->nTimeReceived = copyFrom->nTimeReceived;
                        copyTo->nTimeSmart = copyFrom->nTimeSmart;
                        copyTo->fFromMe = copyFrom->fFromMe;
                        copyTo->strFromAccount = copyFrom->strFromAccount;
                        copyTo->nOrderPos = copyFrom->nOrderPos;
                        copyTo->WriteToDisk(&walletdb);
                    }
                }
            }
        }
        pwalletMain->SetBroadcastTransactions(GetBoolArg("-walletbroadcast", true));
    } // (!fDisableWallet)
#else // ENABLE_WALLET
    LogPrintf("No wallet support compiled in!\n");
#endif // !ENABLE_WALLET

#ifdef ENABLE_MINING
 #ifndef ENABLE_WALLET
    if (GetBoolArg("-minetolocalwallet", false)) {
        return InitError(_("Horizen was not built with wallet support. Set -minetolocalwallet=0 to use -mineraddress, or rebuild Horizen with wallet support."));
    }
    if (GetArg("-mineraddress", "").empty() && GetBoolArg("-gen", false)) {
        return InitError(_("Horizen was not built with wallet support. Set -mineraddress, or rebuild Horizen with wallet support."));
    }
 #endif // !ENABLE_WALLET

    if (mapArgs.count("-mineraddress")) {
 #ifdef ENABLE_WALLET
        bool minerAddressInLocalWallet = false;
        if (pwalletMain) {
            // Address has alreday been validated
            CBitcoinAddress addr(mapArgs["-mineraddress"]);
            CKeyID keyID;
            addr.GetKeyID(keyID);
            minerAddressInLocalWallet = pwalletMain->HaveKey(keyID);
        }
        if (GetBoolArg("-minetolocalwallet", true) && !minerAddressInLocalWallet) {
            return InitError(_("-mineraddress is not in the local wallet. Either use a local address, or set -minetolocalwallet=0"));
        }
 #endif // ENABLE_WALLET
    }
#endif // ENABLE_MINING

    // ********************************************************* Step 9: data directory maintenance

    // if pruning, unset the service bit and perform the initial blockstore prune
    // after any wallet rescanning has taken place.
    if (fPruneMode) {
        LogPrintf("Unsetting NODE_NETWORK on prune mode\n");
        nLocalServices &= ~NODE_NETWORK;
        if (!(fReindex || fReindexFast)) {
            uiInterface.InitMessage(_("Pruning blockstore..."));
            PruneAndFlush();
        }
    }

    // ********************************************************* Step 10: import blocks

    if (mapArgs.count("-blocknotify"))
        uiInterface.NotifyBlockTip.connect(BlockNotifyCallback);

    uiInterface.InitMessage(_("Activating best chain..."));
    // scan for better chains in the block chain database, that are not yet connected in the active best chain
    CValidationState state;
    if (!ActivateBestChain(state))
        strErrors << "Failed to connect best block";

    std::vector<boost::filesystem::path> vImportFiles;
    if (mapArgs.count("-loadblock"))
    {
        BOOST_FOREACH(const std::string& strFile, mapMultiArgs["-loadblock"])
            vImportFiles.push_back(strFile);
    }
    threadGroup.create_thread(boost::bind(&ThreadImport, vImportFiles));
    if (chainActive.Tip() == NULL) {
        LogPrintf("Waiting for genesis block to be imported...\n");
        while (!fRequestShutdown && chainActive.Tip() == NULL)
            MilliSleep(10);
    }

    // ********************************************************* Step 11: start node

    if (!CheckDiskSpace())
        return false;

    if (!strErrors.str().empty())
        return InitError(strErrors.str());

    //// debug print
    LogPrintf("mapBlockIndex.size() = %u\n",   mapBlockIndex.size());
    LogPrintf("nBestHeight = %d\n",                   chainActive.Height());
#ifdef ENABLE_WALLET
    LogPrintf("setKeyPool.size() = %u\n",      pwalletMain ? pwalletMain->setKeyPool.size() : 0);
    LogPrintf("mapWallet.size() = %u\n",       pwalletMain ? pwalletMain->getMapWallet().size() : 0);
    LogPrintf("mapAddressBook.size() = %u\n",  pwalletMain ? pwalletMain->mapAddressBook.size() : 0);
#endif

    // Start the thread that notifies listeners of transactions that have been
    // recently added to the mempool.
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "txnotify", &ThreadNotifyRecentlyAdded));

    if (GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION))
        StartTorControl(threadGroup, scheduler);

    connOptions.nLocalServices      = nLocalServices;
    connOptions.nMaxConnections     = nMaxConnections;

    const unsigned int fromKBtoBfactor = 1000; // from kilobytes to bytes
    const std::pair<int64_t, int64_t> bufferMinMax = {1, std::numeric_limits<unsigned int>::max() / fromKBtoBfactor};
    connOptions.nSendBufferMaxSize = fromKBtoBfactor * static_cast<unsigned int>(GetArgWithinLimits("-maxsendbuffer", DEFAULT_MAX_SEND_BUFFER, bufferMinMax));
    connOptions.nReceiveFloodSize = fromKBtoBfactor * static_cast<unsigned int>(GetArgWithinLimits("-maxreceivebuffer", DEFAULT_MAX_RECEIVE_BUFFER, bufferMinMax));
    
    connman->StartNode(scheduler, connOptions);

    // Monitor the chain, and alert if we get blocks much quicker or slower than expected
    int64_t nPowTargetSpacing = Params().GetConsensus().nPowTargetSpacing;
    CScheduler::Function f = boost::bind(&PartitionCheck, &IsInitialBlockDownload,
                                         boost::ref(cs_main), boost::cref(pindexBestHeader), nPowTargetSpacing);
    scheduler.scheduleEvery(f, nPowTargetSpacing);

#ifdef ENABLE_MINING
    // Generate coins in the background
 #ifdef ENABLE_WALLET
    if (pwalletMain || !GetArg("-mineraddress", "").empty())
        GenerateBitcoins(GetBoolArg("-gen", false), pwalletMain, GetArg("-genproclimit", 1));
 #else
    GenerateBitcoins(GetBoolArg("-gen", false), GetArg("-genproclimit", 1));
 #endif
#endif

    if (Params().NetworkIDString() == "regtest")
    {
        fRegtestAllowDustOutput = GetBoolArg("-allowdustoutput", true);
    }

    // ********************************************************* Step 11: finished

    SetRPCWarmupFinished();
    SetDoneLoadingUI();

#ifdef ENABLE_WALLET
    if (pwalletMain) {
        // Add wallet transactions that aren't already in a block to mapTransactions
        pwalletMain->ReacceptWalletTransactions();

        // Run a thread to flush wallet periodically
        threadGroup.create_thread(boost::bind(&ThreadFlushWalletDB, boost::ref(pwalletMain->strWalletFile)));
    }
#endif

    // Start the thread for async sidechain proof verification
    threadGroup.create_thread(
            boost::bind(
                    &CScAsyncProofVerifier::RunPeriodicVerification,
                    &CScAsyncProofVerifier::GetInstance()
            )
    );

    return !fRequestShutdown;
}
