// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "net.h"

#include "addrman.h"
#include "chainparams.h"
#include "clientversion.h"
#include "primitives/transaction.h"
#include "scheduler.h"
#include "ui_interface.h"
#include "crypto/common.h"
#include "utiltls.h"

#ifdef WIN32
#include <string.h>
#else
#include <fcntl.h>
#endif

#ifdef USE_UPNP
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/miniwget.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#endif

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include <openssl/conf.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

// Dump addresses to peers.dat every 15 minutes (900s)
#define DUMP_ADDRESSES_INTERVAL 900

#if !defined(HAVE_MSG_NOSIGNAL) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

// Fix for ancient MinGW versions, that don't have defined these in ws2tcpip.h.
// Todo: Can be removed when our pull-tester is upgraded to a modern MinGW version.
#ifdef WIN32
#ifndef PROTECTION_LEVEL_UNRESTRICTED
#define PROTECTION_LEVEL_UNRESTRICTED 10
#endif
#ifndef IPV6_PROTECTION_LEVEL
#define IPV6_PROTECTION_LEVEL 23
#endif
#endif

#define USE_TLS
#define COMPAT_NON_TLS // enables compatibility with nodes, that still doesn't support TLS connections

typedef enum {sslAccept, sslConnect, sslShutdown} SSLConnectionRoutine;
typedef enum {clientContext, serverContext} TLSContextType;

using namespace std;

namespace {
    const int MAX_OUTBOUND_CONNECTIONS = 8;

    struct ListenSocket {
        SOCKET socket;
        bool whitelisted;

        ListenSocket(SOCKET socket, bool whitelisted) : socket(socket), whitelisted(whitelisted) {}
    };
}

//
// Global state variables
//
bool fDiscover = true;
bool fListen = true;
uint64_t nLocalServices = NODE_NETWORK;
CCriticalSection cs_mapLocalHost;
map<CNetAddr, LocalServiceInfo> mapLocalHost;
static bool vfLimited[NET_MAX] = {};
static CNode* pnodeLocalHost = NULL;
uint64_t nLocalHostNonce = 0;
static std::vector<ListenSocket> vhListenSocket;
CAddrMan addrman;
int nMaxConnections = DEFAULT_MAX_PEER_CONNECTIONS;
bool fAddressesInitialized = false;

vector<CNode*> vNodes;
CCriticalSection cs_vNodes;
map<CInv, CDataStream> mapRelay;
deque<pair<int64_t, CInv> > vRelayExpiration;
CCriticalSection cs_mapRelay;
limitedmap<CInv, int64_t> mapAlreadyAskedFor(MAX_INV_SZ);

static deque<string> vOneShots;
CCriticalSection cs_vOneShots;

set<CNetAddr> setservAddNodeAddresses;
CCriticalSection cs_setservAddNodeAddresses;

vector<std::string> vAddedNodes;
CCriticalSection cs_vAddedNodes;

NodeId nLastNodeId = 0;
CCriticalSection cs_nLastNodeId;

static CSemaphore *semOutbound = NULL;
boost::condition_variable messageHandlerCondition;

// Signals for message handling
static CNodeSignals g_signals;
CNodeSignals& GetNodeSignals() { return g_signals; }

// OpenSSL server and client contexts
SSL_CTX *tls_ctx_server, *tls_ctx_client;

typedef struct _NODE_ADDR
{
    std::string ipAddr;
    int64_t time;           // time in msec, of an attempt to connect via TLS

    _NODE_ADDR(std::string _ipAddr, int64_t _time = 0) : ipAddr(_ipAddr), time(_time){}
} NODE_ADDR, *PNODE_ADDR;


static bool operator==(_NODE_ADDR a, _NODE_ADDR b)
{
    return (a.ipAddr == b.ipAddr);
}

static std::vector<NODE_ADDR> vNonTLSNodesInbound;
static CCriticalSection cs_vNonTLSNodesInbound;

static std::vector<NODE_ADDR> vNonTLSNodesOutbound;
static CCriticalSection cs_vNonTLSNodesOutbound;

void AddOneShot(const std::string& strDest)
{
    LOCK(cs_vOneShots);
    vOneShots.push_back(strDest);
}

unsigned short GetListenPort()
{
    return (unsigned short)(GetArg("-port", Params().GetDefaultPort()));
}

// find 'best' local address for a particular peer
bool GetLocal(CService& addr, const CNetAddr *paddrPeer)
{
    if (!fListen)
        return false;

    int nBestScore = -1;
    int nBestReachability = -1;
    {
        LOCK(cs_mapLocalHost);
        for (map<CNetAddr, LocalServiceInfo>::iterator it = mapLocalHost.begin(); it != mapLocalHost.end(); it++)
        {
            int nScore = (*it).second.nScore;
            int nReachability = (*it).first.GetReachabilityFrom(paddrPeer);
            if (nReachability > nBestReachability || (nReachability == nBestReachability && nScore > nBestScore))
            {
                addr = CService((*it).first, (*it).second.nPort);
                nBestReachability = nReachability;
                nBestScore = nScore;
            }
        }
    }
    return nBestScore >= 0;
}

//! Convert the pnSeeds6 array into usable address objects.
static std::vector<CAddress> convertSeed6(const std::vector<SeedSpec6> &vSeedsIn)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7*24*60*60;
    std::vector<CAddress> vSeedsOut;
    vSeedsOut.reserve(vSeedsIn.size());
    for (std::vector<SeedSpec6>::const_iterator i(vSeedsIn.begin()); i != vSeedsIn.end(); ++i)
    {
        struct in6_addr ip;
        memcpy(&ip, i->addr, sizeof(ip));
        CAddress addr(CService(ip, i->port));
        addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
    return vSeedsOut;
}

// get best local address for a particular peer as a CAddress
// Otherwise, return the unroutable 0.0.0.0 but filled in with
// the normal parameters, since the IP may be changed to a useful
// one by discovery.
CAddress GetLocalAddress(const CNetAddr *paddrPeer)
{
    CAddress ret(CService("0.0.0.0",GetListenPort()),0);
    CService addr;
    if (GetLocal(addr, paddrPeer))
    {
        ret = CAddress(addr);
    }
    ret.nServices = nLocalServices;
    ret.nTime = GetAdjustedTime();
    return ret;
}

int GetnScore(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    if (mapLocalHost.count(addr) == LOCAL_NONE)
        return 0;
    return mapLocalHost[addr].nScore;
}

// Is our peer's addrLocal potentially useful as an external IP source?
bool IsPeerAddrLocalGood(CNode *pnode)
{
    return fDiscover && pnode->addr.IsRoutable() && pnode->addrLocal.IsRoutable() &&
           !IsLimited(pnode->addrLocal.GetNetwork());
}

// pushes our own address to a peer
void AdvertizeLocal(CNode *pnode)
{
    if (fListen && pnode->fSuccessfullyConnected)
    {
        CAddress addrLocal = GetLocalAddress(&pnode->addr);
        // If discovery is enabled, sometimes give our peer the address it
        // tells us that it sees us as in case it has a better idea of our
        // address than we do.
        if (IsPeerAddrLocalGood(pnode) && (!addrLocal.IsRoutable() ||
             GetRand((GetnScore(addrLocal) > LOCAL_MANUAL) ? 8:2) == 0))
        {
            addrLocal.SetIP(pnode->addrLocal);
        }
        if (addrLocal.IsRoutable())
        {
            LogPrintf("AdvertizeLocal: advertizing address %s\n", addrLocal.ToString());
            pnode->PushAddress(addrLocal);
        }
    }
}

// learn a new local address
bool AddLocal(const CService& addr, int nScore)
{
    if (!addr.IsRoutable())
        return false;

    if (!fDiscover && nScore < LOCAL_MANUAL)
        return false;

    if (IsLimited(addr))
        return false;

    LogPrintf("AddLocal(%s,%i)\n", addr.ToString(), nScore);

    {
        LOCK(cs_mapLocalHost);
        bool fAlready = mapLocalHost.count(addr) > 0;
        LocalServiceInfo &info = mapLocalHost[addr];
        if (!fAlready || nScore >= info.nScore) {
            info.nScore = nScore + (fAlready ? 1 : 0);
            info.nPort = addr.GetPort();
        }
    }

    return true;
}

bool AddLocal(const CNetAddr &addr, int nScore)
{
    return AddLocal(CService(addr, GetListenPort()), nScore);
}

bool RemoveLocal(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    LogPrintf("RemoveLocal(%s)\n", addr.ToString());
    mapLocalHost.erase(addr);
    return true;
}

/** Make a particular network entirely off-limits (no automatic connects to it) */
void SetLimited(enum Network net, bool fLimited)
{
    if (net == NET_UNROUTABLE)
        return;
    LOCK(cs_mapLocalHost);
    vfLimited[net] = fLimited;
}

bool IsLimited(enum Network net)
{
    LOCK(cs_mapLocalHost);
    return vfLimited[net];
}

bool IsLimited(const CNetAddr &addr)
{
    return IsLimited(addr.GetNetwork());
}

/** vote for a local address */
bool SeenLocal(const CService& addr)
{
    {
        LOCK(cs_mapLocalHost);
        if (mapLocalHost.count(addr) == 0)
            return false;
        mapLocalHost[addr].nScore++;
    }
    return true;
}


/** check whether a given address is potentially local */
bool IsLocal(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    return mapLocalHost.count(addr) > 0;
}

/** check whether a given network is one we can probably connect to */
bool IsReachable(enum Network net)
{
    LOCK(cs_mapLocalHost);
    return !vfLimited[net];
}

/** check whether a given address is in a network we can probably connect to */
bool IsReachable(const CNetAddr& addr)
{
    enum Network net = addr.GetNetwork();
    return IsReachable(net);
}

void AddressCurrentlyConnected(const CService& addr)
{
    addrman.Connected(addr);
}


uint64_t CNode::nTotalBytesRecv = 0;
uint64_t CNode::nTotalBytesSent = 0;
CCriticalSection CNode::cs_totalBytesRecv;
CCriticalSection CNode::cs_totalBytesSent;

CNode* FindNode(const CNetAddr& ip)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
        if ((CNetAddr)pnode->addr == ip)
            return (pnode);
    return NULL;
}

CNode* FindNode(const CSubNet& subNet)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    if (subNet.Match((CNetAddr)pnode->addr))
        return (pnode);
    return NULL;
}

CNode* FindNode(const std::string& addrName)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
        if (pnode->addrName == addrName)
            return (pnode);
    return NULL;
}

CNode* FindNode(const CService& addr)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
        if ((CService)pnode->addr == addr)
            return (pnode);
    return NULL;
}

#ifdef USE_TLS

static int WaitFor(SSLConnectionRoutine eRoutine, SOCKET hSocket, SSL *ssl, int timeoutSec)
{
    int nErr = 0;

    while (true)
    {
        switch (eRoutine)
        {
            case sslConnect:
                nErr = SSL_connect(ssl);
                break;

            case sslAccept:
                nErr = SSL_accept(ssl);
                break;

            case sslShutdown:
                nErr = SSL_shutdown(ssl);
                break;

            default:
                return -1;
        }

        if (eRoutine == sslShutdown)
        {
            if (nErr >= 0)
                break;
        }
        else
        {
            if (nErr == 1)
                break;
        }

        int sslErr = SSL_get_error(ssl, nErr);

        if (sslErr != SSL_ERROR_WANT_READ && sslErr != SSL_ERROR_WANT_WRITE)
        {
            LogPrint("net", "TLS: WARNING: %s: %s: ssl_err_code: %s; errno: %s\n", __FILE__, __func__, ERR_error_string(sslErr, NULL), strerror(errno));
            nErr = -1;
            break;
        }

        fd_set socketSet;
        FD_ZERO(&socketSet);
        FD_SET(hSocket, &socketSet);

        struct timeval timeout = { timeoutSec, 0 };

        if (sslErr == SSL_ERROR_WANT_READ)
        {
            int result = select(hSocket + 1, &socketSet, NULL, NULL, &timeout);
            if (result == 0)
            {
                LogPrint("net", "TLS: ERROR: %s: %s: WANT_READ timeout\n", __FILE__, __func__);
                nErr = -1;
                break;
            }
            else if (result == -1)
            {
                LogPrint("net", "TLS: ERROR: %s: %s: WANT_READ ssl_err_code: %s; errno: %s\n", __FILE__, __func__, ERR_error_string(sslErr, NULL), strerror(errno));
                nErr = -1;
                break;
            }
        }
        else
        {
            int result = select(hSocket + 1, NULL, &socketSet, NULL, &timeout);
            if (result == 0)
            {
                LogPrint("net", "TLS: ERROR: %s: %s: WANT_WRITE timeout\n", __FILE__, __func__);
                nErr = -1;
                break;
            }
            else if (result == -1)
            {
                LogPrint("net", "TLS: ERROR: %s: %s: WANT_WRITE ssl_err_code: %s; errno: %s\n", __FILE__, __func__, ERR_error_string(sslErr, NULL), strerror(errno));
                nErr = -1;
                break;
            }
        }
    }

    return nErr;
}

static SSL* TLSConnect(SOCKET hSocket, const CAddress &addrConnect)
{
    LogPrint("net", "TLS: establishing connection (tid = %X), (peerid = %s)\n", pthread_self(), addrConnect.ToString());

    SSL *ssl = NULL;
    bool bConnectedTLS = false;

    if ((ssl = SSL_new(tls_ctx_client)))
    {
        if (SSL_set_fd(ssl, hSocket))
        {
            if (WaitFor(sslConnect, hSocket, ssl, (DEFAULT_CONNECT_TIMEOUT / 1000)) == 1)
                bConnectedTLS = true;
        }
    }

    if (bConnectedTLS)
    {
        LogPrintf ("TLS: connection to %s has been established. Using cipher: %s\n", addrConnect.ToString(), SSL_get_cipher(ssl));
    }
    else
    {
        LogPrintf ("TLS: %s: %s: TLS connection to %s failed\n", __FILE__, __func__, addrConnect.ToString());

        if (ssl)
        {
            SSL_free(ssl);
            ssl = NULL;
        }
    }
    return ssl;
}

#endif  // USE_TLS

CNode* ConnectNode(CAddress addrConnect, const char *pszDest)
{
    if (pszDest == NULL) {
        if (IsLocal(addrConnect))
            return NULL;

        // Look for an existing connection
        CNode* pnode = FindNode((CService)addrConnect);
        if (pnode)
        {
            pnode->AddRef();
            return pnode;
        }
    }

    /// debug print
    LogPrint("net", "trying connection %s lastseen=%.1fhrs\n",
        pszDest ? pszDest : addrConnect.ToString(),
        pszDest ? 0.0 : (double)(GetAdjustedTime() - addrConnect.nTime)/3600.0);

    // Connect
    SOCKET hSocket;
    bool proxyConnectionFailed = false;
    if (pszDest ? ConnectSocketByName(addrConnect, hSocket, pszDest, Params().GetDefaultPort(), nConnectTimeout, &proxyConnectionFailed) :
                  ConnectSocket(addrConnect, hSocket, nConnectTimeout, &proxyConnectionFailed))
    {
        if (!IsSelectableSocket(hSocket)) {
            LogPrintf("Cannot create connection: non-selectable socket created (fd >= FD_SETSIZE ?)\n");
            CloseSocket(hSocket);
            return NULL;
        }

        addrman.Attempt(addrConnect);

        SSL *ssl = NULL;

#ifdef USE_TLS
        /* TCP connection is ready. Do client side SSL. */
#ifdef COMPAT_NON_TLS
        {
            LOCK(cs_vNonTLSNodesOutbound);

            NODE_ADDR nodeAddr(addrConnect.ToStringIP());

            bool bUseTLS = (find(vNonTLSNodesOutbound.begin(),
                                 vNonTLSNodesOutbound.end(),
                                 nodeAddr) == vNonTLSNodesOutbound.end());
            if (bUseTLS)
            {
                ssl = TLSConnect(hSocket, addrConnect);
                if (!ssl)
                {
                    // Further reconnection will be made in non-TLS (unencrypted) mode
                    vNonTLSNodesOutbound.push_back(NODE_ADDR(addrConnect.ToStringIP(), GetTimeMillis()));
                    CloseSocket(hSocket);
                    return NULL;
                }
            }
            else
            {
                LogPrintf ("Connection to %s will be unencrypted\n", addrConnect.ToString());

                vNonTLSNodesOutbound.erase(
                        remove(
                                vNonTLSNodesOutbound.begin(),
                                vNonTLSNodesOutbound.end(),
                                nodeAddr),
                        vNonTLSNodesOutbound.end());
            }
        }
#else
        ssl = TLSConnect(hSocket, addrConnect);
        if(!ssl)
        {
            CloseSocket(hSocket);
            return NULL;
        }
#endif  // COMPAT_NON_TLS

        if (GetBoolArg("-tlsvalidate", false))
        {
            if (ssl && !ValidatePeerCertificate(ssl))
            {
                LogPrintf ("TLS: ERROR: Wrong server certificate from %s. Connection will be closed.\n", addrConnect.ToString());

                SSL_shutdown(ssl);
                CloseSocket(hSocket);
                SSL_free(ssl);
                return NULL;
            }
        }
#endif  // USE_TLS

        // Add node
        CNode* pnode = new CNode(hSocket, addrConnect, pszDest ? pszDest : "", false, ssl);
        pnode->AddRef();

        {
            LOCK(cs_vNodes);
            vNodes.push_back(pnode);
        }

        pnode->nTimeConnected = GetTime();

        return pnode;
    } else if (!proxyConnectionFailed) {
        // If connecting to the node failed, and failure is not caused by a problem connecting to
        // the proxy, mark this as an attempt.
        addrman.Attempt(addrConnect);
    }

    return NULL;
}

void CNode::CloseSocketDisconnect()
{
    fDisconnect = true;

    {
        LOCK(cs_hSocket);

        if (hSocket != INVALID_SOCKET)
        {
            LogPrint("net", "disconnecting peer=%d\n", id);

            if (ssl)
            {
                WaitFor(sslShutdown, hSocket, ssl, (DEFAULT_CONNECT_TIMEOUT / 1000));

                SSL_free(ssl);
                ssl = NULL;
            }

            CloseSocket(hSocket);
        }
    }

    // in case this fails, we'll empty the recv buffer when the CNode is deleted
    TRY_LOCK(cs_vRecvMsg, lockRecv);
    if (lockRecv)
        vRecvMsg.clear();
}

void CNode::PushVersion()
{
    int nBestHeight = g_signals.GetHeight().get_value_or(0);

    int64_t nTime = (fInbound ? GetAdjustedTime() : GetTime());
    CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService("0.0.0.0",0)));
    CAddress addrMe = GetLocalAddress(&addr);
    GetRandBytes((unsigned char*)&nLocalHostNonce, sizeof(nLocalHostNonce));
    if (fLogIPs)
        LogPrint("net", "send version message: version %d, blocks=%d, us=%s, them=%s, peer=%d\n", PROTOCOL_VERSION, nBestHeight, addrMe.ToString(), addrYou.ToString(), id);
    else
        LogPrint("net", "send version message: version %d, blocks=%d, us=%s, peer=%d\n", PROTOCOL_VERSION, nBestHeight, addrMe.ToString(), id);
    PushMessage("version", PROTOCOL_VERSION, nLocalServices, nTime, addrYou, addrMe,
                nLocalHostNonce, FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, std::vector<string>()), nBestHeight, true);
}





std::map<CSubNet, int64_t> CNode::setBanned;
CCriticalSection CNode::cs_setBanned;

void CNode::ClearBanned()
{
    LOCK(cs_setBanned);
    setBanned.clear();
}

bool CNode::IsBanned(CNetAddr ip)
{
    bool fResult = false;
    {
        LOCK(cs_setBanned);
        for (std::map<CSubNet, int64_t>::iterator it = setBanned.begin(); it != setBanned.end(); it++)
        {
            CSubNet subNet = (*it).first;
            int64_t t = (*it).second;

            if(subNet.Match(ip) && GetTime() < t)
                fResult = true;
        }
    }
    return fResult;
}

bool CNode::IsBanned(CSubNet subnet)
{
    bool fResult = false;
    {
        LOCK(cs_setBanned);
        std::map<CSubNet, int64_t>::iterator i = setBanned.find(subnet);
        if (i != setBanned.end())
        {
            int64_t t = (*i).second;
            if (GetTime() < t)
                fResult = true;
        }
    }
    return fResult;
}

void CNode::Ban(const CNetAddr& addr, int64_t bantimeoffset, bool sinceUnixEpoch) {
    CSubNet subNet(addr.ToString()+(addr.IsIPv4() ? "/32" : "/128"));
    Ban(subNet, bantimeoffset, sinceUnixEpoch);
}

void CNode::Ban(const CSubNet& subNet, int64_t bantimeoffset, bool sinceUnixEpoch) {
    int64_t banTime = GetTime()+GetArg("-bantime", 60*60*24);  // Default 24-hour ban
    if (bantimeoffset > 0)
        banTime = (sinceUnixEpoch ? 0 : GetTime() )+bantimeoffset;

    LOCK(cs_setBanned);
    if (setBanned[subNet] < banTime)
        setBanned[subNet] = banTime;
}

bool CNode::Unban(const CNetAddr &addr) {
    CSubNet subNet(addr.ToString()+(addr.IsIPv4() ? "/32" : "/128"));
    return Unban(subNet);
}

bool CNode::Unban(const CSubNet &subNet) {
    LOCK(cs_setBanned);
    if (setBanned.erase(subNet))
        return true;
    return false;
}

void CNode::GetBanned(std::map<CSubNet, int64_t> &banMap)
{
    LOCK(cs_setBanned);
    banMap = setBanned; //create a thread safe copy
}


std::vector<CSubNet> CNode::vWhitelistedRange;
CCriticalSection CNode::cs_vWhitelistedRange;

bool CNode::IsWhitelistedRange(const CNetAddr &addr) {
    LOCK(cs_vWhitelistedRange);
    BOOST_FOREACH(const CSubNet& subnet, vWhitelistedRange) {
        if (subnet.Match(addr))
            return true;
    }
    return false;
}

void CNode::AddWhitelistedRange(const CSubNet &subnet) {
    LOCK(cs_vWhitelistedRange);
    vWhitelistedRange.push_back(subnet);
}

#undef X
#define X(name) stats.name = name
void CNode::copyStats(CNodeStats &stats)
{
    stats.nodeid = this->GetId();
    X(nServices);
    X(nLastSend);
    X(nLastRecv);
    X(nTimeConnected);
    X(nTimeOffset);
    X(addrName);
    X(nVersion);
    X(cleanSubVer);
    X(fInbound);
    X(nStartingHeight);
    X(nSendBytes);
    X(nRecvBytes);
    X(fWhitelisted);

    // It is common for nodes with good ping times to suddenly become lagged,
    // due to a new block arriving or other large transfer.
    // Merely reporting pingtime might fool the caller into thinking the node was still responsive,
    // since pingtime does not update until the ping is complete, which might take a while.
    // So, if a ping is taking an unusually long time in flight,
    // the caller can immediately detect that this is happening.
    int64_t nPingUsecWait = 0;
    if ((0 != nPingNonceSent) && (0 != nPingUsecStart)) {
        nPingUsecWait = GetTimeMicros() - nPingUsecStart;
    }

    // Raw ping time is in microseconds, but show it to user as whole seconds (Bitcoin users should be well used to small numbers with many decimal places by now :)
    stats.dPingTime = (((double)nPingUsecTime) / 1e6);
    stats.dPingWait = (((double)nPingUsecWait) / 1e6);

    // Leave string empty if addrLocal invalid (not filled in yet)
    stats.addrLocal = addrLocal.IsValid() ? addrLocal.ToString() : "";

    // If ssl != NULL it means TLS connection was established successfully
    {
        LOCK(cs_hSocket);
        stats.fTLSEstablished = (ssl != NULL) && (SSL_get_state(ssl) == TLS_ST_OK);
        stats.fTLSVerified = (ssl != NULL) && ValidatePeerCertificate(ssl);
    }
}
#undef X

// requires LOCK(cs_vRecvMsg)
bool CNode::ReceiveMsgBytes(const char *pch, unsigned int nBytes)
{
    while (nBytes > 0) {

        // get current incomplete message, or create a new one
        if (vRecvMsg.empty() ||
            vRecvMsg.back().complete())
            vRecvMsg.push_back(CNetMessage(Params().MessageStart(), SER_NETWORK, nRecvVersion));

        CNetMessage& msg = vRecvMsg.back();

        // absorb network data
        int handled;
        if (!msg.in_data)
            handled = msg.readHeader(pch, nBytes);
        else
            handled = msg.readData(pch, nBytes);

        if (handled < 0)
                return false;

        if (msg.in_data && msg.hdr.nMessageSize > MAX_PROTOCOL_MESSAGE_LENGTH) {
            LogPrint("net", "Oversized message from peer=%i, disconnecting\n", GetId());
            return false;
        }

        pch += handled;
        nBytes -= handled;

        if (msg.complete()) {
            msg.nTime = GetTimeMicros();
            messageHandlerCondition.notify_one();
        }
    }

    return true;
}

int CNetMessage::readHeader(const char *pch, unsigned int nBytes)
{
    // copy data to temporary parsing buffer
    unsigned int nRemaining = 24 - nHdrPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    memcpy(&hdrbuf[nHdrPos], pch, nCopy);
    nHdrPos += nCopy;

    // if header incomplete, exit
    if (nHdrPos < 24)
        return nCopy;

    // deserialize to CMessageHeader
    try {
        hdrbuf >> hdr;
    }
    catch (const std::exception&) {
        return -1;
    }

    // reject messages larger than MAX_SIZE
    if (hdr.nMessageSize > MAX_SIZE)
            return -1;

    // switch state to reading message data
    in_data = true;

    return nCopy;
}

int CNetMessage::readData(const char *pch, unsigned int nBytes)
{
    unsigned int nRemaining = hdr.nMessageSize - nDataPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    if (vRecv.size() < nDataPos + nCopy) {
        // Allocate up to 256 KiB ahead, but never more than the total message size.
        vRecv.resize(std::min(hdr.nMessageSize, nDataPos + nCopy + 256 * 1024));
    }

    memcpy(&vRecv[nDataPos], pch, nCopy);
    nDataPos += nCopy;

    return nCopy;
}









// requires LOCK(cs_vSend)
void SocketSendData(CNode *pnode)
{
    std::deque<CSerializeData>::iterator it = pnode->vSendMsg.begin();

    while (it != pnode->vSendMsg.end())
    {
        const CSerializeData &data = *it;
        assert(data.size() > pnode->nSendOffset);

        bool bIsSSL = false;
        int nBytes = 0, nRet = 0;

        {
            LOCK(pnode->cs_hSocket);

            if (pnode->hSocket == INVALID_SOCKET)
            {
                LogPrint("net", "Send: connection with %s is already closed\n", pnode->addr.ToString());
                break;
            }

            bIsSSL = (pnode->ssl != NULL);

            if (bIsSSL)
            {
                nBytes = SSL_write(pnode->ssl, &data[pnode->nSendOffset], data.size() - pnode->nSendOffset);
                nRet = SSL_get_error(pnode->ssl, nBytes);
            }
            else
            {
                nBytes = send(pnode->hSocket, &data[pnode->nSendOffset], data.size() - pnode->nSendOffset, MSG_NOSIGNAL | MSG_DONTWAIT);
                nRet = WSAGetLastError();
            }
        }
        if (nBytes > 0)
        {
            pnode->nLastSend = GetTime();
            pnode->nSendBytes += nBytes;
            pnode->nSendOffset += nBytes;
            pnode->RecordBytesSent(nBytes);

            if (pnode->nSendOffset == data.size())
            {
                pnode->nSendOffset = 0;
                pnode->nSendSize -= data.size();
                it++;
            }
            else
            {
                // could not send full message; stop sending more
                break;
            }
        }
        else
        {
            if (nBytes <= 0)
            {
                // error
                //
                if (bIsSSL)
                {
                    if (nRet != SSL_ERROR_WANT_READ && nRet != SSL_ERROR_WANT_WRITE)
                    {
                        LogPrintf("ERROR: SSL_write %s; closing connection\n", ERR_error_string(nRet, NULL));
                        pnode->CloseSocketDisconnect();
                    }
                    else
                    {
                        // preventive measure from exhausting CPU usage
                        //
                        MilliSleep(1);    // 1 msec
                    }
                }
                else
                {
                    if (nRet != WSAEWOULDBLOCK && nRet != WSAEMSGSIZE && nRet != WSAEINTR && nRet != WSAEINPROGRESS)
                    {
                        LogPrintf("ERROR: send %s; closing connection\n", NetworkErrorString(nRet));
                        pnode->CloseSocketDisconnect();
                    }
                }
            }

            // couldn't send anything at all
            break;
        }
    }

    if (it == pnode->vSendMsg.end())
    {
        assert(pnode->nSendOffset == 0);
        assert(pnode->nSendSize == 0);
    }
    pnode->vSendMsg.erase(pnode->vSendMsg.begin(), it);
}

static list<CNode*> vNodesDisconnected;

class CNodeRef {
public:
    CNodeRef(CNode *pnode) : _pnode(pnode) {
        LOCK(cs_vNodes);
        _pnode->AddRef();
    }

    ~CNodeRef() {
        LOCK(cs_vNodes);
        _pnode->Release();
    }

    CNode& operator *() const {return *_pnode;};
    CNode* operator ->() const {return _pnode;};

    CNodeRef& operator =(const CNodeRef& other)
    {
        if (this != &other) {
            LOCK(cs_vNodes);

            _pnode->Release();
            _pnode = other._pnode;
            _pnode->AddRef();
        }
        return *this;
    }

    CNodeRef(const CNodeRef& other):
        _pnode(other._pnode)
    {
        LOCK(cs_vNodes);
        _pnode->AddRef();
    }
private:
    CNode *_pnode;
};

static bool ReverseCompareNodeMinPingTime(const CNodeRef &a, const CNodeRef &b)
{
    return a->nMinPingUsecTime > b->nMinPingUsecTime;
}

static bool ReverseCompareNodeTimeConnected(const CNodeRef &a, const CNodeRef &b)
{
    return a->nTimeConnected > b->nTimeConnected;
}

class CompareNetGroupKeyed
{
    std::vector<unsigned char> vchSecretKey;
public:
    CompareNetGroupKeyed()
    {
        vchSecretKey.resize(32, 0);
        GetRandBytes(vchSecretKey.data(), vchSecretKey.size());
    }

    bool operator()(const CNodeRef &a, const CNodeRef &b)
    {
        std::vector<unsigned char> vchGroupA, vchGroupB;
        CSHA256 hashA, hashB;
        std::vector<unsigned char> vchA(32), vchB(32);

        vchGroupA = a->addr.GetGroup();
        vchGroupB = b->addr.GetGroup();

        hashA.Write(begin_ptr(vchGroupA), vchGroupA.size());
        hashB.Write(begin_ptr(vchGroupB), vchGroupB.size());

        hashA.Write(begin_ptr(vchSecretKey), vchSecretKey.size());
        hashB.Write(begin_ptr(vchSecretKey), vchSecretKey.size());

        hashA.Finalize(begin_ptr(vchA));
        hashB.Finalize(begin_ptr(vchB));

        return vchA < vchB;
    }
};

static bool AttemptToEvictConnection(bool fPreferNewConnection) {
    std::vector<CNodeRef> vEvictionCandidates;
    {
        LOCK(cs_vNodes);

        BOOST_FOREACH(CNode *node, vNodes) {
            if (node->fWhitelisted)
                continue;
            if (!node->fInbound)
                continue;
            if (node->fDisconnect)
                continue;
            vEvictionCandidates.push_back(CNodeRef(node));
        }
    }

    if (vEvictionCandidates.empty()) return false;

    // Protect connections with certain characteristics

    // Deterministically select 4 peers to protect by netgroup.
    // An attacker cannot predict which netgroups will be protected.
    static CompareNetGroupKeyed comparerNetGroupKeyed;
    std::sort(vEvictionCandidates.begin(), vEvictionCandidates.end(), comparerNetGroupKeyed);
    vEvictionCandidates.erase(vEvictionCandidates.end() - std::min(4, static_cast<int>(vEvictionCandidates.size())), vEvictionCandidates.end());

    if (vEvictionCandidates.empty()) return false;

    // Protect the 8 nodes with the best ping times.
    // An attacker cannot manipulate this metric without physically moving nodes closer to the target.
    std::sort(vEvictionCandidates.begin(), vEvictionCandidates.end(), ReverseCompareNodeMinPingTime);
    vEvictionCandidates.erase(vEvictionCandidates.end() - std::min(8, static_cast<int>(vEvictionCandidates.size())), vEvictionCandidates.end());

    if (vEvictionCandidates.empty()) return false;

    // Protect the half of the remaining nodes which have been connected the longest.
    // This replicates the existing implicit behavior.
    std::sort(vEvictionCandidates.begin(), vEvictionCandidates.end(), ReverseCompareNodeTimeConnected);
    vEvictionCandidates.erase(vEvictionCandidates.end() - static_cast<int>(vEvictionCandidates.size() / 2), vEvictionCandidates.end());

    if (vEvictionCandidates.empty()) return false;

    // Identify the network group with the most connections and youngest member.
    // (vEvictionCandidates is already sorted by reverse connect time)
    std::vector<unsigned char> naMostConnections;
    unsigned int nMostConnections = 0;
    int64_t nMostConnectionsTime = 0;
    std::map<std::vector<unsigned char>, std::vector<CNodeRef> > mapAddrCounts;
    BOOST_FOREACH(const CNodeRef &node, vEvictionCandidates) {
        mapAddrCounts[node->addr.GetGroup()].push_back(node);
        int64_t grouptime = mapAddrCounts[node->addr.GetGroup()][0]->nTimeConnected;
        size_t groupsize = mapAddrCounts[node->addr.GetGroup()].size();

        if (groupsize > nMostConnections || (groupsize == nMostConnections && grouptime > nMostConnectionsTime)) {
            nMostConnections = groupsize;
            nMostConnectionsTime = grouptime;
            naMostConnections = node->addr.GetGroup();
        }
    }

    // Reduce to the network group with the most connections
    vEvictionCandidates = mapAddrCounts[naMostConnections];

    // Do not disconnect peers if there is only one unprotected connection from their network group.
    if (vEvictionCandidates.size() <= 1)
        // unless we prefer the new connection (for whitelisted peers)
        if (!fPreferNewConnection)
            return false;

    // Disconnect from the network group with the most connections
    vEvictionCandidates[0]->fDisconnect = true;

    return true;
}

#ifdef USE_TLS

static SSL* TLSAccept(SOCKET hSocket, const CAddress &addr)
{
    LogPrint("net", "TLS: accepting connection from %s (tid = %X)\n", addr.ToString(), pthread_self());

    SSL *ssl = NULL;
    bool bAcceptedTLS = false;

    if ((ssl = SSL_new(tls_ctx_server)))
    {
        if (SSL_set_fd(ssl, hSocket))
        {
            if (WaitFor(sslAccept, hSocket, ssl, (DEFAULT_CONNECT_TIMEOUT / 1000)) == 1)
                bAcceptedTLS = true;
        }
    }

    if (bAcceptedTLS)
    {
        LogPrintf ("TLS: connection from %s has been accepted. Using cipher: %s\n", addr.ToString(), SSL_get_cipher(ssl));
    }
    else
    {
        LogPrintf ("TLS: ERROR: %s: %s: TLS connection from %s failed\n", __FILE__, __func__, addr.ToString());

        if (ssl)
        {
            SSL_free(ssl);
            ssl = NULL;
        }
    }

    return ssl;
}

#endif

static void AcceptConnection(const ListenSocket& hListenSocket) {
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    SOCKET hSocket = accept(hListenSocket.socket, (struct sockaddr*)&sockaddr, &len);
    CAddress addr;
    int nInbound = 0;
    int nMaxInbound = nMaxConnections - MAX_OUTBOUND_CONNECTIONS;

    if (hSocket != INVALID_SOCKET)
        if (!addr.SetSockAddr((const struct sockaddr*)&sockaddr))
            LogPrintf("Warning: Unknown socket family\n");

    bool whitelisted = hListenSocket.whitelisted || CNode::IsWhitelistedRange(addr);
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
            if (pnode->fInbound)
                nInbound++;
    }

    if (hSocket == INVALID_SOCKET)
    {
        int nErr = WSAGetLastError();
        if (nErr != WSAEWOULDBLOCK)
            LogPrintf("socket error accept failed: %s\n", NetworkErrorString(nErr));
        return;
    }

    if (!IsSelectableSocket(hSocket))
    {
        LogPrintf("connection from %s dropped: non-selectable socket\n", addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    if (CNode::IsBanned(addr) && !whitelisted)
    {
        LogPrintf("connection from %s dropped (banned)\n", addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    if (nInbound >= nMaxInbound)
    {
        if (!AttemptToEvictConnection(whitelisted)) {
            // No connection to evict, disconnect the new connection
            LogPrint("net", "failed to find an eviction candidate - connection dropped (full)\n");
            CloseSocket(hSocket);
            return;
        }
    }

    // According to the internet TCP_NODELAY is not carried into accepted sockets
    // on all platforms.  Set it again here just to be sure.
    int set = 1;
#ifdef WIN32
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&set, sizeof(int));
#else
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (void*)&set, sizeof(int));
#endif

    SSL *ssl = NULL;

    SetSocketNonBlocking(hSocket, true);

#ifdef USE_TLS
    /* TCP connection is ready. Do server side SSL. */
#ifdef COMPAT_NON_TLS
    {
        LOCK(cs_vNonTLSNodesInbound);

        NODE_ADDR nodeAddr(addr.ToStringIP());

        bool bUseTLS = (find(vNonTLSNodesInbound.begin(),
                             vNonTLSNodesInbound.end(),
                             nodeAddr) == vNonTLSNodesInbound.end());
        if (bUseTLS)
        {
            ssl = TLSAccept(hSocket, addr);
            if(!ssl)
            {
                // Further reconnection will be made in non-TLS (unencrypted) mode
                vNonTLSNodesInbound.push_back(NODE_ADDR(addr.ToStringIP(), GetTimeMillis()));
                CloseSocket(hSocket);
                return;
            }
        }
        else
        {
            LogPrintf ("TLS: Connection from %s will be unencrypted\n", addr.ToString());

            vNonTLSNodesInbound.erase(
                    remove(
                            vNonTLSNodesInbound.begin(),
                            vNonTLSNodesInbound.end(),
                            nodeAddr
                    ),
                    vNonTLSNodesInbound.end());
        }
    }
#else
    ssl = TLSAccept(hSocket, addr);
    if(!ssl)
    {
        CloseSocket(hSocket);
        return;
    }
#endif // COMPAT_NON_TLS

    if (GetBoolArg("-tlsvalidate", false))
    {
        if (ssl && !ValidatePeerCertificate(ssl))
        {
            LogPrintf ("TLS: ERROR: Wrong client certificate from %s. Connection will be closed.\n", addr.ToString());

            SSL_shutdown(ssl);
            CloseSocket(hSocket);
            SSL_free(ssl);
            return;
        }
    }
#endif // USE_TLS

    CNode* pnode = new CNode(hSocket, addr, "", true, ssl);
    pnode->AddRef();
    pnode->fWhitelisted = whitelisted;

    {
        LOCK(cs_vNodes);
        vNodes.push_back(pnode);
    }
}

#if defined(USE_TLS) && defined(COMPAT_NON_TLS)

static bool IsNonTLSAddr(const string &strAddr, const vector<NODE_ADDR> &vPool, CCriticalSection &cs)
{
    LOCK(cs);
    return (find(vPool.begin(), vPool.end(), NODE_ADDR(strAddr)) != vPool.end());
}

static void CleanNonTLSPool(std::vector<NODE_ADDR> &vPool, CCriticalSection &cs)
{
    LOCK(cs);

    vector<NODE_ADDR> vDeleted;

    BOOST_FOREACH(NODE_ADDR nodeAddr, vPool)
    {
        if ((GetTimeMillis() - nodeAddr.time) >= 900000)
        {
            vDeleted.push_back(nodeAddr);
            LogPrint("net", "TLS: Node %s is deleted from the non-TLS pool\n", nodeAddr.ipAddr);
        }
    }

    BOOST_FOREACH(NODE_ADDR nodeAddrDeleted, vDeleted)
    {
        vPool.erase(
                remove(
                        vPool.begin(),
                        vPool.end(),
                        nodeAddrDeleted),
                vPool.end());
    }
}

void ThreadNonTLSPoolsCleaner()
{
    while (true)
    {
        CleanNonTLSPool(vNonTLSNodesInbound,  cs_vNonTLSNodesInbound);
        CleanNonTLSPool(vNonTLSNodesOutbound, cs_vNonTLSNodesOutbound);
        MilliSleep(DEFAULT_CONNECT_TIMEOUT);  // sleep and sleep_for are interruption points, which will throw boost::thread_interrupted
    }
}

#endif // USE_TLS && COMPAT_NON_TLS

void ThreadSocketHandler()
{
    unsigned int nPrevNodeCount = 0;
    while (true)
    {
        //
        // Disconnect nodes
        //
        {
            LOCK(cs_vNodes);
            // Disconnect unused nodes
            vector<CNode*> vNodesCopy = vNodes;
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
            {
                if (pnode->fDisconnect ||
                    (pnode->GetRefCount() <= 0 && pnode->vRecvMsg.empty() && pnode->nSendSize == 0 && pnode->ssSend.empty()))
                {
                    // remove from vNodes
                    vNodes.erase(remove(vNodes.begin(), vNodes.end(), pnode), vNodes.end());

                    // release outbound grant (if any)
                    pnode->grantOutbound.Release();

                    // close socket and cleanup
                    pnode->CloseSocketDisconnect();

                    // hold in disconnected pool until all refs are released
                    if (pnode->fNetworkNode || pnode->fInbound)
                        pnode->Release();
                    vNodesDisconnected.push_back(pnode);
                }
            }
        }
        {
            // Delete disconnected nodes
            list<CNode*> vNodesDisconnectedCopy = vNodesDisconnected;
            BOOST_FOREACH(CNode* pnode, vNodesDisconnectedCopy)
            {
                // wait until threads are done using it
                if (pnode->GetRefCount() <= 0)
                {
                    bool fDelete = false;
                    {
                        TRY_LOCK(pnode->cs_vSend, lockSend);
                        if (lockSend)
                        {
                            TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                            if (lockRecv)
                            {
                                TRY_LOCK(pnode->cs_inventory, lockInv);
                                if (lockInv)
                                    fDelete = true;
                            }
                        }
                    }
                    if (fDelete)
                    {
                        vNodesDisconnected.remove(pnode);
                        delete pnode;
                    }
                }
            }
        }
        if(vNodes.size() != nPrevNodeCount) {
            nPrevNodeCount = vNodes.size();
            uiInterface.NotifyNumConnectionsChanged(nPrevNodeCount);
        }

        //
        // Find which sockets have data to receive
        //
        struct timeval timeout;
        timeout.tv_sec  = 0;
        timeout.tv_usec = 50000; // frequency to poll pnode->vSend

        fd_set fdsetRecv;
        fd_set fdsetSend;
        fd_set fdsetError;
        FD_ZERO(&fdsetRecv);
        FD_ZERO(&fdsetSend);
        FD_ZERO(&fdsetError);
        SOCKET hSocketMax = 0;
        bool have_fds = false;

        BOOST_FOREACH(const ListenSocket& hListenSocket, vhListenSocket) {
            FD_SET(hListenSocket.socket, &fdsetRecv);
            hSocketMax = max(hSocketMax, hListenSocket.socket);
            have_fds = true;
        }

        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes)
            {
                LOCK(pnode->cs_hSocket);

                if (pnode->hSocket == INVALID_SOCKET)
                    continue;

                FD_SET(pnode->hSocket, &fdsetError);
                hSocketMax = max(hSocketMax, pnode->hSocket);
                have_fds = true;

                // Implement the following logic:
                // * If there is data to send, select() for sending data. As this only
                //   happens when optimistic write failed, we choose to first drain the
                //   write buffer in this case before receiving more. This avoids
                //   needlessly queueing received data, if the remote peer is not themselves
                //   receiving data. This means properly utilizing TCP flow control signalling.
                // * Otherwise, if there is no (complete) message in the receive buffer,
                //   or there is space left in the buffer, select() for receiving data.
                // * (if neither of the above applies, there is certainly one message
                //   in the receiver buffer ready to be processed).
                // Together, that means that at least one of the following is always possible,
                // so we don't deadlock:
                // * We send some data.
                // * We wait for data to be received (and disconnect after timeout).
                // * We process a message in the buffer (message handler thread).
                {
                    TRY_LOCK(pnode->cs_vSend, lockSend);
                    if (lockSend && !pnode->vSendMsg.empty()) {
                        FD_SET(pnode->hSocket, &fdsetSend);
                        continue;
                    }
                }
                {
                    TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                    if (lockRecv && (
                        pnode->vRecvMsg.empty() || !pnode->vRecvMsg.front().complete() ||
                        pnode->GetTotalRecvSize() <= ReceiveFloodSize()))
                        FD_SET(pnode->hSocket, &fdsetRecv);
                }
            }
        }

        int nSelect = select(have_fds ? hSocketMax + 1 : 0,
                             &fdsetRecv, &fdsetSend, &fdsetError, &timeout);
        boost::this_thread::interruption_point();

        if (nSelect == SOCKET_ERROR)
        {
            if (have_fds)
            {
                int nErr = WSAGetLastError();
                LogPrintf("socket select error %s\n", NetworkErrorString(nErr));
                for (unsigned int i = 0; i <= hSocketMax; i++)
                    FD_SET(i, &fdsetRecv);
            }
            FD_ZERO(&fdsetSend);
            FD_ZERO(&fdsetError);
            MilliSleep(timeout.tv_usec/1000);
        }

        //
        // Accept new connections
        //
        BOOST_FOREACH(const ListenSocket& hListenSocket, vhListenSocket)
        {
            if (hListenSocket.socket != INVALID_SOCKET && FD_ISSET(hListenSocket.socket, &fdsetRecv))
            {
                AcceptConnection(hListenSocket);
            }
        }

        //
        // Service each socket
        //
        vector<CNode*> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
                pnode->AddRef();
        }
        BOOST_FOREACH(CNode* pnode, vNodesCopy)
        {
            boost::this_thread::interruption_point();

            //
            // Receive
            //
            bool recvSet = false, sendSet = false, errorSet = false;

            {
                LOCK(pnode->cs_hSocket);

                if (pnode->hSocket == INVALID_SOCKET)
                    continue;

                recvSet  = FD_ISSET(pnode->hSocket, &fdsetRecv);
                sendSet  = FD_ISSET(pnode->hSocket, &fdsetSend);
                errorSet = FD_ISSET(pnode->hSocket, &fdsetError);
            }

            if (recvSet || errorSet)
            {
                TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                if (lockRecv)
                {
                    {
                        // typical socket buffer is 8K-64K
                        // maximum record size is 16kB for SSLv3/TLSv1
                        char pchBuf[0x10000];
                        bool bIsSSL = false;
                        int  nBytes = 0, nRet = 0;

                        {
                            LOCK(pnode->cs_hSocket);

                            if (pnode->hSocket == INVALID_SOCKET)
                            {
                                LogPrint("net", "Receive: connection with %s is already closed\n", pnode->addr.ToString());
                                continue;
                            }

                            bIsSSL = (pnode->ssl != NULL);

                            if (bIsSSL)
                            {
                                nBytes = SSL_read(pnode->ssl, pchBuf, sizeof(pchBuf));
                                nRet = SSL_get_error(pnode->ssl, nBytes);
                            }
                            else
                            {
                                nBytes = recv(pnode->hSocket, pchBuf, sizeof(pchBuf), MSG_DONTWAIT);
                                nRet = WSAGetLastError();
                            }
                        }

                        if (nBytes > 0)
                        {
                            if (!pnode->ReceiveMsgBytes(pchBuf, nBytes))
                                pnode->CloseSocketDisconnect();
                            pnode->nLastRecv = GetTime();
                            pnode->nRecvBytes += nBytes;
                            pnode->RecordBytesRecv(nBytes);
                        }
                        else if (nBytes == 0)
                        {
                            // socket closed gracefully (peer disconnected)
                            //
                            if (!pnode->fDisconnect)
                                LogPrint("net", "socket closed (%s)\n", pnode->addr.ToString());
                            pnode->CloseSocketDisconnect();
                        }
                        else if (nBytes < 0)
                        {
                            // error
                            //
                            if (bIsSSL)
                            {
                                if (nRet != SSL_ERROR_WANT_READ && nRet != SSL_ERROR_WANT_WRITE) // SSL_read() operation has to be repeated because of SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE (https://wiki.openssl.org/index.php/Manual:SSL_read(3)#NOTES)
                                {
                                    if (!pnode->fDisconnect)
                                        LogPrintf("ERROR: SSL_read %s\n", ERR_error_string(nRet, NULL));
                                    pnode->CloseSocketDisconnect();
                                }
                                else
                                {
                                    // preventive measure from exhausting CPU usage
                                    //
                                    MilliSleep(1); // 1 msec
                                }
                            }
                            else
                            {
                                if (nRet != WSAEWOULDBLOCK && nRet != WSAEMSGSIZE && nRet != WSAEINTR && nRet != WSAEINPROGRESS)
                                {
                                    if (!pnode->fDisconnect)
                                        LogPrintf("ERROR: socket recv %s\n", NetworkErrorString(nRet));
                                    pnode->CloseSocketDisconnect();
                                }
                            }
                        }
                    }
                }
            }

            //
            // Send
            //
            if (sendSet)
            {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend)
                    SocketSendData(pnode);
            }

            //
            // Inactivity checking
            //
            int64_t nTime = GetTime();
            if (nTime - pnode->nTimeConnected > 60)
            {
                if (pnode->nLastRecv == 0 || pnode->nLastSend == 0)
                {
                    LogPrint("net", "socket no message in first 60 seconds, %d %d from %d\n", pnode->nLastRecv != 0, pnode->nLastSend != 0, pnode->id);
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastSend > TIMEOUT_INTERVAL)
                {
                    LogPrintf("socket sending timeout: %is\n", nTime - pnode->nLastSend);
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastRecv > (pnode->nVersion > BIP0031_VERSION ? TIMEOUT_INTERVAL : 90*60))
                {
                    LogPrintf("socket receive timeout: %is\n", nTime - pnode->nLastRecv);
                    pnode->fDisconnect = true;
                }
                else if (pnode->nPingNonceSent && pnode->nPingUsecStart + TIMEOUT_INTERVAL * 1000000 < GetTimeMicros())
                {
                    LogPrintf("ping timeout: %fs\n", 0.000001 * (GetTimeMicros() - pnode->nPingUsecStart));
                    pnode->fDisconnect = true;
                }
            }
        }
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
                pnode->Release();
        }
    }
}









#ifdef USE_UPNP
void ThreadMapPort()
{
    std::string port = strprintf("%u", GetListenPort());
    const char * multicastif = 0;
    const char * minissdpdpath = 0;
    struct UPNPDev * devlist = 0;
    char lanaddr[64];

#ifndef UPNPDISCOVER_SUCCESS
    /* miniupnpc 1.5 */
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0);
#elif MINIUPNPC_API_VERSION < 14
    /* miniupnpc 1.6 */
    int error = 0;
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, &error);
#else
    /* miniupnpc 1.9.20150730 */
    int error = 0;
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, 2, &error);
#endif

    struct UPNPUrls urls;
    struct IGDdatas data;
    int r;

    r = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));
    if (r == 1)
    {
        if (fDiscover) {
            char externalIPAddress[40];
            r = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIPAddress);
            if(r != UPNPCOMMAND_SUCCESS)
                LogPrintf("UPnP: GetExternalIPAddress() returned %d\n", r);
            else
            {
                if(externalIPAddress[0])
                {
                    LogPrintf("UPnP: ExternalIPAddress = %s\n", externalIPAddress);
                    AddLocal(CNetAddr(externalIPAddress), LOCAL_UPNP);
                }
                else
                    LogPrintf("UPnP: GetExternalIPAddress failed.\n");
            }
        }

        string strDesc = "Bitcoin " + FormatFullVersion();

        try {
            while (true) {
#ifndef UPNPDISCOVER_SUCCESS
                /* miniupnpc 1.5 */
                r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                    port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0);
#else
                /* miniupnpc 1.6 */
                r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                    port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0, "0");
#endif

                if(r!=UPNPCOMMAND_SUCCESS)
                    LogPrintf("AddPortMapping(%s, %s, %s) failed with code %d (%s)\n",
                        port, port, lanaddr, r, strupnperror(r));
                else
                    LogPrintf("UPnP Port Mapping successful.\n");;

                MilliSleep(20*60*1000); // Refresh every 20 minutes
            }
        }
        catch (const boost::thread_interrupted&)
        {
            r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port.c_str(), "TCP", 0);
            LogPrintf("UPNP_DeletePortMapping() returned: %d\n", r);
            freeUPNPDevlist(devlist); devlist = 0;
            FreeUPNPUrls(&urls);
            throw;
        }
    } else {
        LogPrintf("No valid UPnP IGDs found\n");
        freeUPNPDevlist(devlist); devlist = 0;
        if (r != 0)
            FreeUPNPUrls(&urls);
    }
}

void MapPort(bool fUseUPnP)
{
    static boost::thread* upnp_thread = NULL;

    if (fUseUPnP)
    {
        if (upnp_thread) {
            upnp_thread->interrupt();
            upnp_thread->join();
            delete upnp_thread;
        }
        upnp_thread = new boost::thread(boost::bind(&TraceThread<void (*)()>, "upnp", &ThreadMapPort));
    }
    else if (upnp_thread) {
        upnp_thread->interrupt();
        upnp_thread->join();
        delete upnp_thread;
        upnp_thread = NULL;
    }
}

#else
void MapPort(bool)
{
    // Intentionally left blank.
}
#endif






void ThreadDNSAddressSeed()
{
    // goal: only query DNS seeds if address need is acute
    if ((addrman.size() > 0) &&
        (!GetBoolArg("-forcednsseed", false))) {
        MilliSleep(11 * 1000);

        LOCK(cs_vNodes);
        if (vNodes.size() >= 2) {
            LogPrintf("P2P peers available. Skipped DNS seeding.\n");
            return;
        }
    }

    const vector<CDNSSeedData> &vSeeds = Params().DNSSeeds();
    int found = 0;

    LogPrintf("Loading addresses from DNS seeds (could take a while)\n");

    BOOST_FOREACH(const CDNSSeedData &seed, vSeeds) {
        if (HaveNameProxy()) {
            AddOneShot(seed.host);
        } else {
            vector<CNetAddr> vIPs;
            vector<CAddress> vAdd;
            if (LookupHost(seed.host.c_str(), vIPs))
            {
                BOOST_FOREACH(const CNetAddr& ip, vIPs)
                {
                    int nOneDay = 24*3600;
                    CAddress addr = CAddress(CService(ip, Params().GetDefaultPort()));
                    addr.nTime = GetTime() - 3*nOneDay - GetRand(4*nOneDay); // use a random age between 3 and 7 days old
                    vAdd.push_back(addr);
                    found++;
                }
            }
            addrman.Add(vAdd, CNetAddr(seed.name, true));
        }
    }

    LogPrintf("%d addresses found from DNS seeds\n", found);
}












void DumpAddresses()
{
    int64_t nStart = GetTimeMillis();

    CAddrDB adb;
    adb.Write(addrman);

    LogPrint("net", "Flushed %d addresses to peers.dat  %dms\n",
           addrman.size(), GetTimeMillis() - nStart);
}

void static ProcessOneShot()
{
    string strDest;
    {
        LOCK(cs_vOneShots);
        if (vOneShots.empty())
            return;
        strDest = vOneShots.front();
        vOneShots.pop_front();
    }
    CAddress addr;
    CSemaphoreGrant grant(*semOutbound, true);
    if (grant) {
        if (!OpenNetworkConnection(addr, &grant, strDest.c_str(), true))
            AddOneShot(strDest);
    }
}

void ThreadOpenConnections()
{
    // Connect to specific addresses
    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0)
    {
        for (int64_t nLoop = 0;; nLoop++)
        {
            ProcessOneShot();
            BOOST_FOREACH(const std::string& strAddr, mapMultiArgs["-connect"])
            {
                CAddress addr;
                OpenNetworkConnection(addr, NULL, strAddr.c_str());

                for (int i = 0; i < 10 && i < nLoop; i++)
                {
                    MilliSleep(500);
                }
            }
            MilliSleep(500);
        }
    }

    // Initiate network connections
    int64_t nStart = GetTime();
    while (true)
    {
        ProcessOneShot();

        MilliSleep(500);

        CSemaphoreGrant grant(*semOutbound);
        boost::this_thread::interruption_point();

        // Add seed nodes if DNS seeds are all down (an infrastructure attack?).
        if (addrman.size() == 0 && (GetTime() - nStart > 60)) {
            static bool done = false;
            if (!done) {
                LogPrintf("Adding fixed seed nodes as DNS doesn't seem to be available.\n");
                addrman.Add(convertSeed6(Params().FixedSeeds()), CNetAddr("127.0.0.1"));
                done = true;
            }
        }

        //
        // Choose an address to connect to based on most recently seen
        //
        CAddress addrConnect;

        // Only connect out to one peer per network group (/16 for IPv4).
        // Do this here so we don't have to critsect vNodes inside mapAddresses critsect.
        int nOutbound = 0;
        set<vector<unsigned char> > setConnected;
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes) {
                if (!pnode->fInbound) {
                    setConnected.insert(pnode->addr.GetGroup());
                    nOutbound++;
                }
            }
        }

        int64_t nANow = GetAdjustedTime();

        int nTries = 0;
        while (true)
        {
            CAddrInfo addr = addrman.Select();

            // if we selected an invalid address, restart
            if (!addr.IsValid() || setConnected.count(addr.GetGroup()) || IsLocal(addr))
                break;

            // If we didn't find an appropriate destination after trying 100 addresses fetched from addrman,
            // stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
            // already-connected network ranges, ...) before trying new addrman addresses.
            nTries++;
            if (nTries > 100)
                break;

            if (IsLimited(addr))
                continue;

            // only consider very recently tried nodes after 30 failed attempts
            if (nANow - addr.nLastTry < 600 && nTries < 30)
                continue;

            // do not allow non-default ports, unless after 50 invalid addresses selected already
            if (addr.GetPort() != Params().GetDefaultPort() && nTries < 50)
                continue;

            addrConnect = addr;
            break;
        }

        if (addrConnect.IsValid())
            OpenNetworkConnection(addrConnect, &grant);
    }
}

void ThreadOpenAddedConnections()
{
    {
        LOCK(cs_vAddedNodes);
        vAddedNodes = mapMultiArgs["-addnode"];
    }

    if (HaveNameProxy()) {
        while(true) {
            list<string> lAddresses(0);
            {
                LOCK(cs_vAddedNodes);
                BOOST_FOREACH(const std::string& strAddNode, vAddedNodes)
                    lAddresses.push_back(strAddNode);
            }
            BOOST_FOREACH(const std::string& strAddNode, lAddresses) {
                CAddress addr;
                CSemaphoreGrant grant(*semOutbound);
                OpenNetworkConnection(addr, &grant, strAddNode.c_str());
                MilliSleep(500);
            }
            MilliSleep(120000); // Retry every 2 minutes
        }
    }

    for (unsigned int i = 0; true; i++)
    {
        list<string> lAddresses(0);
        {
            LOCK(cs_vAddedNodes);
            BOOST_FOREACH(const std::string& strAddNode, vAddedNodes)
                lAddresses.push_back(strAddNode);
        }

        list<vector<CService> > lservAddressesToAdd(0);
        BOOST_FOREACH(const std::string& strAddNode, lAddresses) {
            vector<CService> vservNode(0);
            if(Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), fNameLookup, 0))
            {
                lservAddressesToAdd.push_back(vservNode);
                {
                    LOCK(cs_setservAddNodeAddresses);
                    BOOST_FOREACH(const CService& serv, vservNode)
                        setservAddNodeAddresses.insert(serv);
                }
            }
        }
        // Attempt to connect to each IP for each addnode entry until at least one is successful per addnode entry
        // (keeping in mind that addnode entries can have many IPs if fNameLookup)
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes)
                for (list<vector<CService> >::iterator it = lservAddressesToAdd.begin(); it != lservAddressesToAdd.end(); it++)
                    BOOST_FOREACH(const CService& addrNode, *(it))
                        if (pnode->addr == addrNode)
                        {
                            it = lservAddressesToAdd.erase(it);
                            it--;
                            break;
                        }
        }
        BOOST_FOREACH(vector<CService>& vserv, lservAddressesToAdd)
        {
            CSemaphoreGrant grant(*semOutbound);
            OpenNetworkConnection(CAddress(vserv[i % vserv.size()]), &grant);
            MilliSleep(500);
        }
        MilliSleep(120000); // Retry every 2 minutes
    }
}

// if successful, this moves the passed grant to the constructed node
bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant *grantOutbound, const char *pszDest, bool fOneShot)
{
    //
    // Initiate outbound network connection
    //
    boost::this_thread::interruption_point();
    if (!pszDest) {
        if (IsLocal(addrConnect) ||
            FindNode((CNetAddr)addrConnect) || CNode::IsBanned(addrConnect) ||
            FindNode(addrConnect.ToStringIPPort()))
            return false;
    } else if (FindNode(std::string(pszDest)))
        return false;

    CNode* pnode = ConnectNode(addrConnect, pszDest);
    boost::this_thread::interruption_point();

#if defined(USE_TLS) && defined(COMPAT_NON_TLS)

    if (!pnode)
    {
        string strDest;
        int port;

        if (!pszDest)
            strDest = addrConnect.ToStringIP();
        else
            SplitHostPort(string(pszDest), port, strDest);

        if (IsNonTLSAddr(strDest, vNonTLSNodesOutbound, cs_vNonTLSNodesOutbound))
        {
            // Attempt to reconnect in non-TLS mode
            pnode = ConnectNode(addrConnect, pszDest);
            boost::this_thread::interruption_point();
        }
    }

#endif

    if (!pnode)
        return false;
    if (grantOutbound)
        grantOutbound->MoveTo(pnode->grantOutbound);
    pnode->fNetworkNode = true;
    if (fOneShot)
        pnode->fOneShot = true;

    return true;
}


void ThreadMessageHandler()
{
    boost::mutex condition_mutex;
    boost::unique_lock<boost::mutex> lock(condition_mutex);

    SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);
    while (true)
    {
        vector<CNode*> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
            BOOST_FOREACH(CNode* pnode, vNodesCopy) {
                pnode->AddRef();
            }
        }

        // Poll the connected nodes for messages
        CNode* pnodeTrickle = NULL;
        if (!vNodesCopy.empty())
            pnodeTrickle = vNodesCopy[GetRand(vNodesCopy.size())];

        bool fSleep = true;

        BOOST_FOREACH(CNode* pnode, vNodesCopy)
        {
            if (pnode->fDisconnect)
                continue;

            // Receive messages
            {
                TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                if (lockRecv)
                {
                    if (!g_signals.ProcessMessages(pnode))
                        pnode->CloseSocketDisconnect();

                    if (pnode->nSendSize < SendBufferSize())
                    {
                        if (!pnode->vRecvGetData.empty() || (!pnode->vRecvMsg.empty() && pnode->vRecvMsg[0].complete()))
                        {
                            fSleep = false;
                        }
                    }
                }
            }
            boost::this_thread::interruption_point();

            // Send messages
            {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend)
                    g_signals.SendMessages(pnode, pnode == pnodeTrickle || pnode->fWhitelisted);
            }
            boost::this_thread::interruption_point();
        }

        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
                pnode->Release();
        }

        if (fSleep)
            messageHandlerCondition.timed_wait(lock, boost::posix_time::microsec_clock::universal_time() + boost::posix_time::milliseconds(100));
    }
}






bool BindListenPort(const CService &addrBind, string& strError, bool fWhitelisted)
{
    strError = "";
    int nOne = 1;

    // Create socket for listening for incoming connections
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    if (!addrBind.GetSockAddr((struct sockaddr*)&sockaddr, &len))
    {
        strError = strprintf("Error: Bind address family for %s not supported", addrBind.ToString());
        LogPrintf("%s\n", strError);
        return false;
    }

    SOCKET hListenSocket = socket(((struct sockaddr*)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (hListenSocket == INVALID_SOCKET)
    {
        strError = strprintf("Error: Couldn't open socket for incoming connections (socket returned error %s)", NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        return false;
    }
    if (!IsSelectableSocket(hListenSocket))
    {
        strError = "Error: Couldn't create a listenable socket for incoming connections";
        LogPrintf("%s\n", strError);
        return false;
    }


#ifndef WIN32
#ifdef SO_NOSIGPIPE
    // Different way of disabling SIGPIPE on BSD
    setsockopt(hListenSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&nOne, sizeof(int));
#endif
    // Allow binding if the port is still in TIME_WAIT state after
    // the program was closed and restarted.
    setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (void*)&nOne, sizeof(int));
    // Disable Nagle's algorithm
    setsockopt(hListenSocket, IPPROTO_TCP, TCP_NODELAY, (void*)&nOne, sizeof(int));
#else
    setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&nOne, sizeof(int));
    setsockopt(hListenSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nOne, sizeof(int));
#endif

    // Set to non-blocking, incoming connections will also inherit this
    //
    // WARNING!
    // On Linux, the new socket returned by accept() does not inherit file
    // status flags such as O_NONBLOCK and O_ASYNC from the listening
    // socket. http://man7.org/linux/man-pages/man2/accept.2.html
    if (!SetSocketNonBlocking(hListenSocket, true)) {
        strError = strprintf("BindListenPort: Setting listening socket to non-blocking failed, error %s\n", NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        return false;
    }

    // some systems don't have IPV6_V6ONLY but are always v6only; others do have the option
    // and enable it by default or not. Try to enable it, if possible.
    if (addrBind.IsIPv6()) {
#ifdef IPV6_V6ONLY
#ifdef WIN32
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&nOne, sizeof(int));
#else
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&nOne, sizeof(int));
#endif
#endif
#ifdef WIN32
        int nProtLevel = PROTECTION_LEVEL_UNRESTRICTED;
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_PROTECTION_LEVEL, (const char*)&nProtLevel, sizeof(int));
#endif
    }

    if (::bind(hListenSocket, (struct sockaddr*)&sockaddr, len) == SOCKET_ERROR)
    {
        int nErr = WSAGetLastError();
        if (nErr == WSAEADDRINUSE)
            strError = strprintf(_("Unable to bind to %s on this computer. Bitcoin Core is probably already running."), addrBind.ToString());
        else
            strError = strprintf(_("Unable to bind to %s on this computer (bind returned error %s)"), addrBind.ToString(), NetworkErrorString(nErr));
        LogPrintf("%s\n", strError);
        CloseSocket(hListenSocket);
        return false;
    }
    LogPrintf("Bound to %s\n", addrBind.ToString());

    // Listen for incoming connections
    if (listen(hListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        strError = strprintf(_("Error: Listening for incoming connections failed (listen returned error %s)"), NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        CloseSocket(hListenSocket);
        return false;
    }

    vhListenSocket.push_back(ListenSocket(hListenSocket, fWhitelisted));

    if (addrBind.IsRoutable() && fDiscover && !fWhitelisted)
        AddLocal(addrBind, LOCAL_BIND);

    return true;
}

void static Discover(boost::thread_group& threadGroup)
{
    if (!fDiscover)
        return;

#ifdef WIN32
    // Get local host IP
    char pszHostName[256] = "";
    if (gethostname(pszHostName, sizeof(pszHostName)) != SOCKET_ERROR)
    {
        vector<CNetAddr> vaddr;
        if (LookupHost(pszHostName, vaddr))
        {
            BOOST_FOREACH (const CNetAddr &addr, vaddr)
            {
                if (AddLocal(addr, LOCAL_IF))
                    LogPrintf("%s: %s - %s\n", __func__, pszHostName, addr.ToString());
            }
        }
    }
#else
    // Get local host ip
    struct ifaddrs* myaddrs;
    if (getifaddrs(&myaddrs) == 0)
    {
        for (struct ifaddrs* ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == NULL) continue;
            if ((ifa->ifa_flags & IFF_UP) == 0) continue;
            if (strcmp(ifa->ifa_name, "lo") == 0) continue;
            if (strcmp(ifa->ifa_name, "lo0") == 0) continue;
            if (ifa->ifa_addr->sa_family == AF_INET)
            {
                struct sockaddr_in* s4 = (struct sockaddr_in*)(ifa->ifa_addr);
                CNetAddr addr(s4->sin_addr);
                if (AddLocal(addr, LOCAL_IF))
                    LogPrintf("%s: IPv4 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
            }
            else if (ifa->ifa_addr->sa_family == AF_INET6)
            {
                struct sockaddr_in6* s6 = (struct sockaddr_in6*)(ifa->ifa_addr);
                CNetAddr addr(s6->sin6_addr);
                if (AddLocal(addr, LOCAL_IF))
                    LogPrintf("%s: IPv6 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
            }
        }
        freeifaddrs(myaddrs);
    }
#endif
}

#ifdef USE_TLS

static int TLSCertVerificationCallback(int preverify_ok, X509_STORE_CTX *chainContext)
{
    //If verify_callback always returns 1, the TLS/SSL handshake will not be terminated with respect to verification failures and the connection will be established.
    return 1;
}

static SSL_CTX* TLSInitCtx(
                    TLSContextType ctxType,
                    const boost::filesystem::path &privateKeyFile,
                    const boost::filesystem::path &certificateFile,
                    const std::vector<boost::filesystem::path> &trustedDirs)
{
    if (!boost::filesystem::exists(privateKeyFile)  ||
        !boost::filesystem::exists(certificateFile))
        return NULL;

    bool bInitialized = false;
    SSL_CTX *tlsCtx = NULL;

    if ((tlsCtx = SSL_CTX_new(ctxType == serverContext ? TLS_server_method() : TLS_client_method())))
    {
        SSL_CTX_set_mode(tlsCtx, SSL_MODE_AUTO_RETRY);

        int rootCertsNum    = LoadDefaultRootCertificates(tlsCtx);
        int trustedPathsNum = 0;

        for (boost::filesystem::path trustedDir : trustedDirs)
        {
            if (SSL_CTX_load_verify_locations(tlsCtx, NULL, trustedDir.string().c_str()) == 1)
                trustedPathsNum++;
        }

        if (rootCertsNum == 0 && trustedPathsNum == 0)
            LogPrintf("TLS: WARNING: %s: %s: failed to set up verified certificates. It will be impossible to verify peer certificates. \n", __FILE__, __func__);

        SSL_CTX_set_verify(tlsCtx, SSL_VERIFY_PEER, TLSCertVerificationCallback);

        if (SSL_CTX_use_certificate_file(tlsCtx, certificateFile.string().c_str(), SSL_FILETYPE_PEM) > 0)
        {
            if (SSL_CTX_use_PrivateKey_file(tlsCtx, privateKeyFile.string().c_str(), SSL_FILETYPE_PEM) > 0)
            {
                if (SSL_CTX_check_private_key(tlsCtx))
                    bInitialized = true;
                else
                    LogPrintf("TLS: ERROR: %s: %s: private key does not match the certificate public key\n", __FILE__, __func__);
            }
            else
                LogPrintf("TLS: ERROR: %s: %s: failed to use privateKey file\n", __FILE__, __func__);
        }
        else
        {
            LogPrintf("TLS: ERROR: %s: %s: failed to use certificate file\n", __FILE__, __func__);
            ERR_print_errors_fp(stderr);
        }
    }
    else
        LogPrintf("TLS: ERROR: %s: %s: failed to create TLS context\n", __FILE__, __func__);

    if (!bInitialized)
    {
        if (tlsCtx)
        {
            SSL_CTX_free(tlsCtx);
            tlsCtx = NULL;
        }
    }

    return tlsCtx;
}

static bool TLSInitialize()
{
    bool bInitializationStatus = false;

    // Initialization routines for the OpenSSL library
    //
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms(); // OpenSSL_add_ssl_algorithms() always returns "1", so it is safe to discard the return value.

    namespace fs = boost::filesystem;
    fs::path certFile = GetArg("-tlscertpath", "");
    if (!fs::exists(certFile))
            certFile = (GetDataDir() / TLS_CERT_FILE_NAME);

    fs::path privKeyFile = GetArg("-tlskeypath", "");
    if (!fs::exists(privKeyFile))
            privKeyFile = (GetDataDir() / TLS_KEY_FILE_NAME);

    std::vector<fs::path> trustedDirs;
    fs::path trustedDir = GetArg("-tlstrustdir", "");
    if (fs::exists(trustedDir))
        // Use only the specified trusted directory
        trustedDirs.push_back(trustedDir);
    else
        // If specified directory can't be used, then setting the default trusted directories
        trustedDirs = GetDefaultTrustedDirectories();

    for (fs::path dir : trustedDirs)
        LogPrintf("TLS: trusted directory '%s' will be used\n", dir.string().c_str());

    // Initialization of the server and client contexts
    //
    if ((tls_ctx_server = TLSInitCtx(serverContext, privKeyFile, certFile, trustedDirs)))
    {
        if ((tls_ctx_client = TLSInitCtx(clientContext, privKeyFile, certFile, trustedDirs)))
        {
            LogPrint("net", "TLS: contexts are initialized\n");
            bInitializationStatus = true;
        }
        else
        {
            LogPrintf("TLS: ERROR: %s: %s: failed to initialize TLS client context\n", __FILE__, __func__);
            SSL_CTX_free (tls_ctx_server);
        }
    }
    else
        LogPrintf("TLS: ERROR: %s: %s: failed to initialize TLS server context\n", __FILE__, __func__);

    return bInitializationStatus;
}

static bool TLSPrepareCredentials()
{
    boost::filesystem::path
            defaultKeyPath (GetDataDir() / TLS_KEY_FILE_NAME),
            defaultCertPath(GetDataDir() / TLS_CERT_FILE_NAME);

    CredentialsStatus credStatus =
            VerifyCredentials(
                    boost::filesystem::path(GetArg("-tlskeypath",  defaultKeyPath.string())),
                    boost::filesystem::path(GetArg("-tlscertpath", defaultCertPath.string())),
                    GetArg("-tlskeypwd",""));

    bool bPrepared = (credStatus == credOk);

    if (!bPrepared)
    {
        if (!mapArgs.count("-tlskeypath") && !mapArgs.count("-tlscertpath"))
        {
            // Default paths were used
            //
            if (credStatus == credAbsent)
            {
                // Generate new credentials (key and self-signed certificate on it) only if credentials were absent previously
                //
                bPrepared = GenerateCredentials(
                                    defaultKeyPath,
                                    defaultCertPath,
                                    GetArg("-tlskeypwd",""));
            }
        }
    }

    return bPrepared;
}

#endif // USE_TLS

void StartNode(boost::thread_group& threadGroup, CScheduler& scheduler)
{
    uiInterface.InitMessage(_("Loading addresses..."));
    // Load addresses for peers.dat
    int64_t nStart = GetTimeMillis();
    {
        CAddrDB adb;
        if (!adb.Read(addrman))
            LogPrintf("Invalid or missing peers.dat; recreating\n");
    }
    LogPrintf("Loaded %i addresses from peers.dat  %dms\n",
           addrman.size(), GetTimeMillis() - nStart);
    fAddressesInitialized = true;

    if (semOutbound == NULL) {
        // initialize semaphore
        int nMaxOutbound = min(MAX_OUTBOUND_CONNECTIONS, nMaxConnections);
        semOutbound = new CSemaphore(nMaxOutbound);
    }

    if (pnodeLocalHost == NULL)
        pnodeLocalHost = new CNode(INVALID_SOCKET, CAddress(CService("127.0.0.1", 0), nLocalServices));

    Discover(threadGroup);

#ifdef USE_TLS

    if (!TLSPrepareCredentials())
    {
        LogPrintf("TLS: ERROR: %s: %s: Credentials weren't loaded. Node can't be started.\n", __FILE__, __func__);
        return;
    }

    if (!TLSInitialize())
    {
        LogPrintf("TLS: ERROR: %s: %s: TLS initialization failed. Node can't be started.\n", __FILE__, __func__);
        return;
    }
#else
    LogPrintf("TLS is not used!\n");
#endif

    //
    // Start threads
    //

    if (!GetBoolArg("-dnsseed", true))
        LogPrintf("DNS seeding disabled\n");
    else
        threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "dnsseed", &ThreadDNSAddressSeed));

    // Map ports with UPnP
    MapPort(GetBoolArg("-upnp", DEFAULT_UPNP));

    // Send and receive from sockets, accept connections
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "net", &ThreadSocketHandler));

    // Initiate outbound connections from -addnode
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "addcon", &ThreadOpenAddedConnections));

    // Initiate outbound connections
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "opencon", &ThreadOpenConnections));

    // Process messages
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "msghand", &ThreadMessageHandler));

#if defined(USE_TLS) && defined(COMPAT_NON_TLS)
    // Clean pools of addresses for non-TLS connections
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "poolscleaner", &ThreadNonTLSPoolsCleaner));
#endif

    // Dump network addresses
    scheduler.scheduleEvery(&DumpAddresses, DUMP_ADDRESSES_INTERVAL);
}

bool StopNode()
{
    LogPrintf("StopNode()\n");
    MapPort(false);
    if (semOutbound)
        for (int i=0; i<MAX_OUTBOUND_CONNECTIONS; i++)
            semOutbound->post();

    if (fAddressesInitialized)
    {
        DumpAddresses();
        fAddressesInitialized = false;
    }

    return true;
}

class CNetCleanup
{
public:
    CNetCleanup() {}

    ~CNetCleanup()
    {
        // Close sockets
        BOOST_FOREACH(CNode* pnode, vNodes)
            pnode->CloseSocketDisconnect();
        BOOST_FOREACH(ListenSocket& hListenSocket, vhListenSocket)
            if (hListenSocket.socket != INVALID_SOCKET)
                if (!CloseSocket(hListenSocket.socket))
                    LogPrintf("CloseSocket(hListenSocket) failed with error %s\n", NetworkErrorString(WSAGetLastError()));

        // clean up some globals (to help leak detection)
        BOOST_FOREACH(CNode *pnode, vNodes)
            delete pnode;
        BOOST_FOREACH(CNode *pnode, vNodesDisconnected)
            delete pnode;
        vNodes.clear();
        vNodesDisconnected.clear();
        vhListenSocket.clear();
        delete semOutbound;
        semOutbound = NULL;
        delete pnodeLocalHost;
        pnodeLocalHost = NULL;

#ifdef WIN32
        // Shutdown Windows Sockets
        WSACleanup();
#endif
    }
}
instance_of_cnetcleanup;







void RelayTransaction(const CTransaction& tx)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(10000);
    ss << tx;
    RelayTransaction(tx, ss);
}

void RelayTransaction(const CTransaction& tx, const CDataStream& ss)
{
    CInv inv(MSG_TX, tx.GetHash());
    {
        LOCK(cs_mapRelay);
        // Expire old relay messages
        while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime())
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
    }
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if(!pnode->fRelayTxes)
            continue;
        LOCK(pnode->cs_filter);
        if (pnode->pfilter)
        {
            if (pnode->pfilter->IsRelevantAndUpdate(tx))
                pnode->PushInventory(inv);
        } else
            pnode->PushInventory(inv);
    }
}

void CNode::RecordBytesRecv(uint64_t bytes)
{
    LOCK(cs_totalBytesRecv);
    nTotalBytesRecv += bytes;
}

void CNode::RecordBytesSent(uint64_t bytes)
{
    LOCK(cs_totalBytesSent);
    nTotalBytesSent += bytes;
}

uint64_t CNode::GetTotalBytesRecv()
{
    LOCK(cs_totalBytesRecv);
    return nTotalBytesRecv;
}

uint64_t CNode::GetTotalBytesSent()
{
    LOCK(cs_totalBytesSent);
    return nTotalBytesSent;
}

void CNode::Fuzz(int nChance)
{
    if (!fSuccessfullyConnected) return; // Don't fuzz initial handshake
    if (GetRand(nChance) != 0) return; // Fuzz 1 of every nChance messages

    switch (GetRand(3))
    {
    case 0:
        // xor a random byte with a random value:
        if (!ssSend.empty()) {
            CDataStream::size_type pos = GetRand(ssSend.size());
            ssSend[pos] ^= (unsigned char)(GetRand(256));
        }
        break;
    case 1:
        // delete a random byte:
        if (!ssSend.empty()) {
            CDataStream::size_type pos = GetRand(ssSend.size());
            ssSend.erase(ssSend.begin()+pos);
        }
        break;
    case 2:
        // insert a random byte at a random position
        {
            CDataStream::size_type pos = GetRand(ssSend.size());
            char ch = (char)GetRand(256);
            ssSend.insert(ssSend.begin()+pos, ch);
        }
        break;
    }
    // Chance of more than one change half the time:
    // (more changes exponentially less likely):
    Fuzz(2);
}

//
// CAddrDB
//

CAddrDB::CAddrDB()
{
    pathAddr = GetDataDir() / "peers.dat";
}

bool CAddrDB::Write(const CAddrMan& addr)
{
    // Generate random temporary filename
    unsigned short randv = 0;
    GetRandBytes((unsigned char*)&randv, sizeof(randv));
    std::string tmpfn = strprintf("peers.dat.%04x", randv);

    // serialize addresses, checksum data up to that point, then append csum
    CDataStream ssPeers(SER_DISK, CLIENT_VERSION);
    ssPeers << FLATDATA(Params().MessageStart());
    ssPeers << addr;
    uint256 hash = Hash(ssPeers.begin(), ssPeers.end());
    ssPeers << hash;

    // open temp output file, and associate with CAutoFile
    boost::filesystem::path pathTmp = GetDataDir() / tmpfn;
    FILE *file = fopen(pathTmp.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s: Failed to open file %s", __func__, pathTmp.string());

    // Write and commit header, data
    try {
        fileout << ssPeers;
    }
    catch (const std::exception& e) {
        return error("%s: Serialize or I/O error - %s", __func__, e.what());
    }
    FileCommit(fileout.Get());
    fileout.fclose();

    // replace existing peers.dat, if any, with new peers.dat.XXXX
    if (!RenameOver(pathTmp, pathAddr))
        return error("%s: Rename-into-place failed", __func__);

    return true;
}

bool CAddrDB::Read(CAddrMan& addr)
{
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathAddr.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("%s: Failed to open file %s", __func__, pathAddr.string());

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathAddr);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char *)&vchData[0], dataSize);
        filein >> hashIn;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }
    filein.fclose();

    CDataStream ssPeers(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssPeers.begin(), ssPeers.end());
    if (hashIn != hashTmp)
        return error("%s: Checksum mismatch, data corrupted", __func__);

    unsigned char pchMsgTmp[4];
    try {
        // de-serialize file header (network specific magic number) and ..
        ssPeers >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
            return error("%s: Invalid network magic number", __func__);

        // de-serialize address data into one CAddrMan object
        ssPeers >> addr;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    return true;
}

unsigned int ReceiveFloodSize() { return 1000*GetArg("-maxreceivebuffer", 5*1000); }
unsigned int SendBufferSize() { return 1000*GetArg("-maxsendbuffer", 1*1000); }

CNode::CNode(SOCKET hSocketIn, const CAddress& addrIn, const std::string& addrNameIn, bool fInboundIn, SSL *sslIn) :
    ssSend(SER_NETWORK, INIT_PROTO_VERSION),
    addrKnown(5000, 0.001),
    setInventoryKnown(SendBufferSize() / 1000)
{
    ssl = sslIn;
    nServices = 0;
    hSocket = hSocketIn;
    nRecvVersion = INIT_PROTO_VERSION;
    nLastSend = 0;
    nLastRecv = 0;
    nSendBytes = 0;
    nRecvBytes = 0;
    nTimeConnected = GetTime();
    nTimeOffset = 0;
    addr = addrIn;
    addrName = addrNameIn == "" ? addr.ToStringIPPort() : addrNameIn;
    nVersion = 0;
    strSubVer = "";
    fWhitelisted = false;
    fOneShot = false;
    fClient = false; // set by version message
    fInbound = fInboundIn;
    fNetworkNode = false;
    fSuccessfullyConnected = false;
    fDisconnect = false;
    nRefCount = 0;
    nSendSize = 0;
    nSendOffset = 0;
    hashContinue = uint256();
    nStartingHeight = -1;
    fGetAddr = false;
    fRelayTxes = false;
    fSentAddr = false;
    pfilter = new CBloomFilter();
    nPingNonceSent = 0;
    nPingUsecStart = 0;
    nPingUsecTime = 0;
    fPingQueued = false;
    nMinPingUsecTime = std::numeric_limits<int64_t>::max();

    {
        LOCK(cs_nLastNodeId);
        id = nLastNodeId++;
    }

    if (fLogIPs)
        LogPrint("net", "Added connection to %s peer=%d\n", addrName, id);
    else
        LogPrint("net", "Added connection peer=%d\n", id);

    // Be shy and don't send version until we hear
    if (hSocket != INVALID_SOCKET && !fInbound)
        PushVersion();

    GetNodeSignals().InitializeNode(GetId(), this);
}

CNode::~CNode()
{
    // No need to make a lock on cs_hSocket, because before deletion CNode object is removed from the vNodes vector, so any other thread hasn't access to it.
    // Removal is synchronized with read and write routines, so all of them will be completed to this moment.

    if (hSocket != INVALID_SOCKET)
    {
        if (ssl)
        {
            WaitFor(sslShutdown, hSocket, ssl, (DEFAULT_CONNECT_TIMEOUT / 1000));

            SSL_free(ssl);
            ssl = NULL;
        }

        CloseSocket(hSocket);
    }

    if (pfilter)
        delete pfilter;

    GetNodeSignals().FinalizeNode(GetId());
}

void CNode::AskFor(const CInv& inv)
{
    if (mapAskFor.size() > MAPASKFOR_MAX_SZ || setAskFor.size() > SETASKFOR_MAX_SZ)
        return;
    // a peer may not have multiple non-responded queue positions for a single inv item
    if (!setAskFor.insert(inv.hash).second)
        return;

    // We're using mapAskFor as a priority queue,
    // the key is the earliest time the request can be sent
    int64_t nRequestTime;
    limitedmap<CInv, int64_t>::const_iterator it = mapAlreadyAskedFor.find(inv);
    if (it != mapAlreadyAskedFor.end())
        nRequestTime = it->second;
    else
        nRequestTime = 0;
    LogPrint("net", "askfor %s  %d (%s) peer=%d\n", inv.ToString(), nRequestTime, DateTimeStrFormat("%H:%M:%S", nRequestTime/1000000), id);

    // Make sure not to reuse time indexes to keep things in the same order
    int64_t nNow = GetTimeMicros() - 1000000;
    static int64_t nLastTime;
    ++nLastTime;
    nNow = std::max(nNow, nLastTime);
    nLastTime = nNow;

    // Each retry is 2 minutes after the last
    nRequestTime = std::max(nRequestTime + 2 * 60 * 1000000, nNow);
    if (it != mapAlreadyAskedFor.end())
        mapAlreadyAskedFor.update(it, nRequestTime);
    else
        mapAlreadyAskedFor.insert(std::make_pair(inv, nRequestTime));
    mapAskFor.insert(std::make_pair(nRequestTime, inv));
}

void CNode::BeginMessage(const char* pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSend)
{
    ENTER_CRITICAL_SECTION(cs_vSend);
    assert(ssSend.size() == 0);
    ssSend << CMessageHeader(Params().MessageStart(), pszCommand, 0);
    LogPrint("net", "sending: %s ", SanitizeString(pszCommand));
}

void CNode::AbortMessage() UNLOCK_FUNCTION(cs_vSend)
{
    ssSend.clear();

    LEAVE_CRITICAL_SECTION(cs_vSend);

    LogPrint("net", "(aborted)\n");
}

void CNode::EndMessage() UNLOCK_FUNCTION(cs_vSend)
{
    // The -*messagestest options are intentionally not documented in the help message,
    // since they are only used during development to debug the networking code and are
    // not intended for end-users.
    if (mapArgs.count("-dropmessagestest") && GetRand(GetArg("-dropmessagestest", 2)) == 0)
    {
        LogPrint("net", "dropmessages DROPPING SEND MESSAGE\n");
        AbortMessage();
        return;
    }
    if (mapArgs.count("-fuzzmessagestest"))
        Fuzz(GetArg("-fuzzmessagestest", 10));

    if (ssSend.size() == 0)
    {
        LEAVE_CRITICAL_SECTION(cs_vSend);
        return;
    }
    // Set the size
    unsigned int nSize = ssSend.size() - CMessageHeader::HEADER_SIZE;
    WriteLE32((uint8_t*)&ssSend[CMessageHeader::MESSAGE_SIZE_OFFSET], nSize);

    // Set the checksum
    uint256 hash = Hash(ssSend.begin() + CMessageHeader::HEADER_SIZE, ssSend.end());
    unsigned int nChecksum = 0;
    memcpy(&nChecksum, &hash, sizeof(nChecksum));
    assert(ssSend.size () >= CMessageHeader::CHECKSUM_OFFSET + sizeof(nChecksum));
    memcpy((char*)&ssSend[CMessageHeader::CHECKSUM_OFFSET], &nChecksum, sizeof(nChecksum));

    LogPrint("net", "(%d bytes) peer=%d\n", nSize, id);

    std::deque<CSerializeData>::iterator it = vSendMsg.insert(vSendMsg.end(), CSerializeData());
    ssSend.GetAndClear(*it);
    nSendSize += (*it).size();

    // If write queue empty, attempt "optimistic write"
    if (it == vSendMsg.begin())
        SocketSendData(this);

    LEAVE_CRITICAL_SECTION(cs_vSend);
}
