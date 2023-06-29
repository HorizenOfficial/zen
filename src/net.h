// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_H
#define BITCOIN_NET_H

#include "bloom.h"
#include "compat.h"
#include "hash.h"
#include "limitedmap.h"
#include "mruset.h"
#include "netbase.h"
#include "protocol.h"
#include "random.h"
#include "streams.h"
#include "sync.h"
#include "uint256.h"
#include "utilstrencodings.h"

#include <deque>
#include <stdint.h>

#ifndef WIN32
#include <arpa/inet.h>
#endif

#include <boost/filesystem/path.hpp>
#include <boost/foreach.hpp>
#include <boost/signals2/signal.hpp>

// Enable OpenSSL Support for Zen
#include <openssl/bio.h>
#include <openssl/ssl.h>

// for max block size
#include <consensus/consensus.h>



class CAddrMan;
class CScheduler;
class CNode;

namespace boost {
    class thread_group;
} // namespace boost

/** Time between pings automatically sent out for latency probing and keepalive (in seconds). */
static const int PING_INTERVAL = 2 * 60;
/** Time after which to disconnect, after waiting for a ping response (or inactivity). */
static const int TIMEOUT_INTERVAL = 20 * 60;
/** The maximum number of entries in an 'inv' protocol message */
static const unsigned int MAX_INV_SZ = 50000;
/** The maximum number of new addresses to accumulate before announcing. */
static const unsigned int MAX_ADDR_TO_SEND = 1000;
/** The maximum rate of address records we're willing to process on average. Can be bypassed using
 *  the NetPermissionFlags::Addr permission. */
static constexpr double MAX_ADDR_RATE_PER_SECOND = 0.1;
/** The soft limit of the address processing token bucket (the regular MAX_ADDR_RATE_PER_SECOND
 *  based increments won't go above this, but the MAX_ADDR_TO_SEND increment following GETADDR
 *  is exempt from this limit. */
static constexpr size_t MAX_ADDR_PROCESSING_TOKEN_BUCKET = MAX_ADDR_TO_SEND;
/** Maximum length of incoming protocol messages (no message over 4 MiB is currently acceptable). */
static const unsigned int MAX_PROTOCOL_MESSAGE_LENGTH = 4 * 1024 * 1024;
static_assert((MAX_PROTOCOL_MESSAGE_LENGTH >= MAX_BLOCK_SIZE),
    "net.h MAX_PROTOCOL_MESSAGE_LENGTH must be greater or equal than max block size!");

/** -listen default */
static const bool DEFAULT_LISTEN = true;
/** The maximum number of entries in mapAskFor */
static const size_t MAPASKFOR_MAX_SZ = MAX_INV_SZ;
/** The maximum number of entries in setAskFor (larger due to getdata latency)*/
static const size_t SETASKFOR_MAX_SZ = 2 * MAX_INV_SZ;
/** The maximum number of entries in mapAlreadyReceived (8 peers * 2min additional delay each * 100tx/s)*/
static const size_t MAPRECEIVED_MAX_SZ = 8 * 120 * 100;
/** The maximum number of peer connections to maintain. */
static const unsigned int DEFAULT_MAX_PEER_CONNECTIONS = 125;

static const int MAX_OUTBOUND_CONNECTIONS = 8;

unsigned int ReceiveFloodSize();
unsigned int SendBufferSize();

void AddressCurrentlyConnected(const CService& addr);
unsigned short GetListenPort();
bool BindListenPort(const CService &bindAddr, std::string& strError, bool fWhitelisted = false);

SSL_CTX* create_context(bool server_side);
EVP_PKEY *generate_key();
X509 *generate_x509(EVP_PKEY *pkey);
bool write_to_disk(EVP_PKEY *pkey, X509 *x509);
void configure_context(SSL_CTX *ctx, bool server_side);

// OpenSSL related variables for metrics.cpp
static std::string routingsecrecy;
static std::string cipherdescription;
static std::string securitylevel;
static std::string validationdescription;

typedef int NodeId;

struct CombinerAll
{
    typedef bool result_type;

    template<typename I>
    bool operator()(I first, I last) const
    {
        while (first != last) {
            if (!(*first)) return false;
            ++first;
        }
        return true;
    }
};

// Signals for message handling
struct CNodeSignals
{
    boost::signals2::signal<int ()> GetHeight;
    boost::signals2::signal<bool (CNode*), CombinerAll> ProcessMessages;
    boost::signals2::signal<bool (CNode*, bool), CombinerAll> SendMessages;
    boost::signals2::signal<void (NodeId, const CNode*)> InitializeNode;
    boost::signals2::signal<void (NodeId)> FinalizeNode;
};


CNodeSignals& GetNodeSignals();


enum
{
    LOCAL_NONE,   // unknown
    LOCAL_IF,     // address a local interface listens on
    LOCAL_BIND,   // address explicit bound to
    LOCAL_UPNP,   // unused (was: address reported by UPnP)
    LOCAL_MANUAL, // address explicitly specified (-externalip=)

    LOCAL_MAX
};

bool IsPeerAddrLocalGood(CNode *pnode);
void AdvertizeLocal(CNode *pnode);
void SetLimited(enum Network net, bool fLimited = true);
bool IsLimited(enum Network net);
bool IsLimited(const CNetAddr& addr);
bool AddLocal(const CService& addr, int nScore = LOCAL_NONE);
bool AddLocal(const CNetAddr& addr, int nScore = LOCAL_NONE);
bool RemoveLocal(const CService& addr);
bool SeenLocal(const CService& addr);
bool IsLocal(const CService& addr);
bool GetLocal(CService &addr, const CNetAddr *paddrPeer = NULL);
bool IsReachable(enum Network net);
bool IsReachable(const CNetAddr &addr);
CAddress GetLocalAddress(const CNetAddr *paddrPeer = NULL);


extern bool fDiscover;
extern bool fListen;
extern uint64_t nLocalServices;
extern uint64_t nLocalHostNonce;
extern CAddrMan addrman;
/** Maximum number of connections to simultaneously allow (aka connection slots) */
extern int nMaxConnections;

//// extern std::vector<CNode*> vNodes;
//// extern CCriticalSection cs_vNodes;
extern std::map<CInv, CDataStream> mapRelay;
extern std::deque<std::pair<int64_t, CInv> > vRelayExpiration;
extern CCriticalSection cs_mapRelay;
extern LimitedMap<CInv, int64_t> mapAlreadyAskedFor;
extern LimitedMap<CInv, int64_t> mapAlreadyReceived;

extern SSL_CTX *tls_ctx_server;
extern SSL_CTX *tls_ctx_client;

struct LocalServiceInfo {
    int nScore;
    int nPort;
};


extern CCriticalSection cs_mapLocalHost;
extern std::map<CNetAddr, LocalServiceInfo> mapLocalHost;

class CNodeStats
{
public:
    NodeId nodeid;
    uint64_t nServices;
    bool fTLSEstablished;
    bool fTLSVerified;
    int64_t nLastSend;
    int64_t nLastRecv;
    int64_t nTimeConnected;
    int64_t nTimeOffset;
    std::string addrName;
    int nVersion;
    std::string cleanSubVer;
    bool fInbound;
    int nStartingHeight;
    uint64_t nSendBytes;
    uint64_t nRecvBytes;
    bool fWhitelisted;
    double dPingTime;
    double dPingWait;
    std::string addrLocal;
    uint64_t m_addr_rate_limited;
    uint64_t m_addr_processed;
};




class CNetMessage {
public:
    bool in_data;                   // parsing header (false) or data (true)

    CDataStream hdrbuf;             // partially received header
    CMessageHeader hdr;             // complete header
    unsigned int nHdrPos;

    CDataStream vRecv;              // received message data
    unsigned int nDataPos;

    int64_t nTime;                  // time (in microseconds) of message receipt.

    CNetMessage(const CMessageHeader::MessageStartChars& pchMessageStartIn, int nTypeIn, int nVersionIn) : hdrbuf(nTypeIn, nVersionIn), hdr(pchMessageStartIn), vRecv(nTypeIn, nVersionIn) {
        hdrbuf.resize(24);
        in_data = false;
        nHdrPos = 0;
        nDataPos = 0;
        nTime = 0;
    }

    bool complete() const
    {
        if (!in_data)
            return false;
        return (hdr.nMessageSize == nDataPos);
    }

    void SetVersion(int nVersionIn)
    {
        hdrbuf.SetVersion(nVersionIn);
        vRecv.SetVersion(nVersionIn);
    }

    int readHeader(const char *pch, unsigned int nBytes);
    int readData(const char *pch, unsigned int nBytes);
};





/** Information about a peer */
class CNode
{
public:
    // OpenSSL
    SSL *ssl;

    // socket
    uint64_t nServices;
    SOCKET hSocket;
    CCriticalSection cs_hSocket;
    CDataStream ssSend;
    size_t nSendSize; // total size of all vSendMsg entries
    size_t nSendOffset; // offset inside the first vSendMsg already sent
    uint64_t nSendBytes;
    std::deque<CSerializeData> vSendMsg;
    CCriticalSection cs_vSend;

    std::deque<CInv> vRecvGetData;
    std::deque<CNetMessage> vRecvMsg;
    CCriticalSection cs_vRecvMsg;
    uint64_t nRecvBytes;
    int nRecvVersion;

    int64_t nLastSend;
    int64_t nLastRecv;
    int64_t nTimeConnected;
    int64_t nTimeOffset;
    CAddress addr;
    std::string addrName;
    CService addrLocal;
    int nVersion;
    bool fSentVersion;
    // strSubVer is whatever byte array we read from the wire. However, this field is intended
    // to be printed out, displayed to humans in various forms and so on. So we sanitize it and
    // store the sanitized version in cleanSubVer. The original should be used when dealing with
    // the network or wire types and the cleaned string used when displayed or logged.
    std::string strSubVer, cleanSubVer;
    bool fWhitelisted; // This peer can bypass DoS banning.
    bool fOneShot;
    bool fClient;
    bool fInbound;
    bool fNetworkNode;
    bool fSuccessfullyConnected;
    bool fDisconnect;
    // We use fRelayTxes for two purposes -
    // a) it allows us to not relay tx invs before receiving the peer's version message
    // b) the peer may tell us in its version message that we should not relay tx invs
    //    until it has initialized its bloom filter.
    bool fRelayTxes;
    bool fSentAddr;
    CSemaphoreGrant grantOutbound;
    CCriticalSection cs_filter;
    CBloomFilter* pfilter;
    int nRefCount;
    NodeId id;

    /** Number of addresses that can be processed from this peer. Start at 1 to
     *  permit self-announcement. */
    double m_addr_token_bucket = 1.0;
       /** When m_addr_token_bucket was last updated */
    int64_t m_addr_token_timestamp = 0;
    /** Total number of addresses that were dropped due to rate limiting. */
    uint64_t m_addr_rate_limited = 0;
    /** Total number of addresses that were processed (excludes rate limited ones). */
    uint64_t m_addr_processed = 0;
protected:

    // Denial-of-service detection/prevention
    // Key is IP address, value is banned-until-time
    static std::map<CSubNet, int64_t> setBanned;
    static CCriticalSection cs_setBanned;

    // Whitelisted ranges. Any node connecting from these is automatically
    // whitelisted (as well as those connecting to whitelisted binds).
    static std::vector<CSubNet> vWhitelistedRange;
    static CCriticalSection cs_vWhitelistedRange;

    // Basic fuzz-testing
    void Fuzz(int nChance); // modifies ssSend

    enum class eTlsOption {
        FALLBACK_UNSET = 0,
        FALLBACK_FALSE = 1,
        FALLBACK_TRUE = 2
    };
    static eTlsOption tlsFallbackNonTls;
    static eTlsOption tlsValidate;

public:
    uint256 hashContinue;
    int nStartingHeight;

    // flood relay
    std::vector<CAddress> vAddrToSend;
    CRollingBloomFilter addrKnown;
    bool fGetAddr;
    std::set<uint256> setKnown;

    // inventory based relay
    mruset<CInv> setInventoryKnown;
    std::vector<CInv> vInventoryToSend;
    CCriticalSection cs_inventory;
    std::set<uint256> setAskFor;
    std::multimap<int64_t, CInv> mapAskFor;

    // Ping time measurement:
    // The pong reply we're expecting, or 0 if no pong expected.
    uint64_t nPingNonceSent;
    // Time (in usec) the last ping was sent, or 0 if no ping was ever sent.
    int64_t nPingUsecStart;
    // Last measured round-trip time.
    int64_t nPingUsecTime;
    // Best measured round-trip time.
    int64_t nMinPingUsecTime;
    // Whether a ping is requested.
    bool fPingQueued;

    CNode(SOCKET hSocketIn, const CAddress &addrIn, const std::string &addrNameIn = "", bool fInboundIn = false, SSL *sslIn = NULL);
    ~CNode();

private:

    CNode(const CNode&);
    void operator=(const CNode&);

public:

    NodeId GetId() const {
      return id;
    }

    int GetRefCount()
    {
        assert(nRefCount >= 0);
        return nRefCount;
    }

    // requires LOCK(cs_vRecvMsg)
    unsigned int GetTotalRecvSize()
    {
        unsigned int total = 0;
        BOOST_FOREACH(const CNetMessage &msg, vRecvMsg)
            total += msg.vRecv.size() + 24;
        return total;
    }

    // requires LOCK(cs_vRecvMsg)
    bool ReceiveMsgBytes(const char *pch, unsigned int nBytes);

    // requires LOCK(cs_vRecvMsg)
    void SetRecvVersion(int nVersionIn)
    {
        nRecvVersion = nVersionIn;
        BOOST_FOREACH(CNetMessage &msg, vRecvMsg)
            msg.SetVersion(nVersionIn);
    }

    CNode* AddRef()
    {
        nRefCount++;
        return this;
    }

    void Release()
    {
        nRefCount--;
    }



    void AddAddressKnown(const CAddress& addr)
    {
        addrKnown.insert(addr.GetKey());
    }

    void PushAddress(const CAddress& addr)
    {
        // Known checking here is only to save space from duplicates.
        // SendMessages will filter it again for knowns that were added
        // after addresses were pushed.
        if (addr.IsValid() && !addrKnown.contains(addr.GetKey())) {
            if (vAddrToSend.size() >= MAX_ADDR_TO_SEND) {
                vAddrToSend[insecure_rand() % vAddrToSend.size()] = addr;
            } else {
                vAddrToSend.push_back(addr);
            }
        }
    }


    void AddInventoryKnown(const CInv& inv)
    {
        {
            LOCK(cs_inventory);
            setInventoryKnown.insert(inv);
        }
    }

    void PushInventory(const CInv& inv)
    {
        {
            LOCK(cs_inventory);
            if (!setInventoryKnown.count(inv))
                vInventoryToSend.push_back(inv);
        }
    }

    void AskFor(const CInv& inv);

    // TODO: Document the postcondition of this function.  Is cs_vSend locked?
    void BeginMessage(const char* pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSend);

    // TODO: Document the precondition of this function.  Is cs_vSend locked?
    void AbortMessage() UNLOCK_FUNCTION(cs_vSend);

    // TODO: Document the precondition of this function.  Is cs_vSend locked?
    void EndMessage() UNLOCK_FUNCTION(cs_vSend);

    void PushVersion();


    void PushMessage(const char* pszCommand)
    {
        try
        {
            BeginMessage(pszCommand);
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    // virtual for UT
    virtual void PushInvs(const char* pszCommand, const std::vector<CInv>& invVec)
    {
    	return PushMessage(pszCommand, invVec);
    }

    template<typename T1>
    void PushMessage(const char* pszCommand, const T1& a1)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4 << a5;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4 << a5 << a6;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7, const T8& a8)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7, const T8& a8, const T9& a9)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8 << a9;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    void CloseSocketDisconnect();

    // Denial-of-service detection/prevention
    // The idea is to detect peers that are behaving
    // badly and disconnect/ban them, but do it in a
    // one-coding-mistake-won't-shatter-the-entire-network
    // way.
    // IMPORTANT:  There should be nothing I can give a
    // node that it will forward on that will make that
    // node's peers drop it. If there is, an attacker
    // can isolate a node and/or try to split the network.
    // Dropping a node for sending stuff that is invalid
    // now but might be valid in a later version is also
    // dangerous, because it can cause a network split
    // between nodes running old code and nodes running
    // new code.
    static void ClearBanned(); // needed for unit testing
    static bool IsBanned(CNetAddr ip);
    static bool IsBanned(CSubNet subnet);
    static void Ban(const CNetAddr &ip, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    static void Ban(const CSubNet &subNet, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    static bool Unban(const CNetAddr &ip);
    static bool Unban(const CSubNet &ip);
    static void GetBanned(std::map<CSubNet, int64_t> &banmap);

    void copyStats(CNodeStats &stats);

    static bool IsWhitelistedRange(const CNetAddr &ip);
    static void AddWhitelistedRange(const CSubNet &subnet);

    // resource deallocation on cleanup, called at node shutdown
    static void NetCleanup();

    // returns the value of the tlsfallbacknontls and tlsvalidate flags set at zend startup (see init.cpp)
    static bool GetTlsFallbackNonTls();
    static bool GetTlsValidate();
};



class CTransaction;
class CScCertificate;
void Relay(const CTransaction& tx);
void Relay(const CScCertificate& cert);
void Relay(const CTransactionBase& tx, const CDataStream& ss);

/** Access to the (IP) address database (peers.dat) */
class CAddrDB
{
private:
    boost::filesystem::path pathAddr;
public:
    CAddrDB();
    bool Write(const CAddrMan& addr);
    bool Read(CAddrMan& addr);
};

struct ListenSocket {
    SOCKET socket;
    bool whitelisted;

    ListenSocket(SOCKET socket, bool whitelisted) : socket(socket), whitelisted(whitelisted) {}
};


namespace zen {
typedef struct _NODE_ADDR {
    std::string ipAddr;
    int64_t time; // time in msec, of an attempt to connect via TLS

    _NODE_ADDR(std::string _ipAddr, int64_t _time = 0) : ipAddr(_ipAddr), time(_time) {}
    bool operator==(const _NODE_ADDR b) const
    {
        return (ipAddr == b.ipAddr);
    }
} NODE_ADDR, *PNODE_ADDR;
}

class CConnman {
public:
    struct Options
    {
        uint64_t nLocalServices = 0;
        int nMaxConnections = 0;
        int m_max_outbound_full_relay = 0;
        int m_max_outbound_block_relay = 0;
        int nMaxAddnode = 0;
        int nMaxFeeler = 0;
        // CClientUIInterface* uiInterface = nullptr;
        // NetEventsInterface* m_msgproc = nullptr;
        // BanMan* m_banman = nullptr;
        unsigned int nSendBufferMaxSize = 0;
        unsigned int nReceiveFloodSize = 0;
        uint64_t nMaxOutboundLimit = 0;
        // int64_t m_peer_connect_timeout = DEFAULT_PEER_CONNECT_TIMEOUT;
        std::vector<std::string> vSeedNodes;
        // std::vector<NetWhitelistPermissions> vWhitelistedRange;
        // std::vector<NetWhitebindPermissions> vWhiteBinds;
        std::vector<CService> vBinds;
        std::vector<CService> onion_binds;
        bool bind_on_any;
        bool m_use_addrman_outgoing = true;
        std::vector<std::string> m_specified_outgoing;
        std::vector<std::string> m_added_nodes;
        bool m_i2p_accept_incoming;
    };

    void Init(const Options& connOptions) EXCLUSIVE_LOCKS_REQUIRED(!m_added_nodes_mutex, !m_total_bytes_sent_mutex)
    {
        // AssertLockNotHeld(m_total_bytes_sent_mutex);

        // nLocalServices = connOptions.nLocalServices;
        // nMaxConnections = connOptions.nMaxConnections;
        // m_max_outbound_full_relay = std::min(connOptions.m_max_outbound_full_relay, connOptions.nMaxConnections);
        // m_max_outbound_block_relay = connOptions.m_max_outbound_block_relay;
        // m_use_addrman_outgoing = connOptions.m_use_addrman_outgoing;
        // nMaxAddnode = connOptions.nMaxAddnode;
        // nMaxFeeler = connOptions.nMaxFeeler;
        // m_max_outbound = m_max_outbound_full_relay + m_max_outbound_block_relay + nMaxFeeler;
        // // m_client_interface = connOptions.uiInterface;
        // // m_banman = connOptions.m_banman;
        // // m_msgproc = connOptions.m_msgproc;
        // nSendBufferMaxSize = connOptions.nSendBufferMaxSize;
        // nReceiveFloodSize = connOptions.nReceiveFloodSize;
        // // m_peer_connect_timeout = std::chrono::seconds{connOptions.m_peer_connect_timeout};
        // {
        //     LOCK(m_total_bytes_sent_mutex);
        //     nMaxOutboundLimit = connOptions.nMaxOutboundLimit;
        // }
        // // vWhitelistedRange = connOptions.vWhitelistedRange;
        // {
        //     LOCK(m_added_nodes_mutex);
        //     m_added_nodes = connOptions.m_added_nodes;
        // }
        // m_onion_binds = connOptions.onion_binds;
    }

    void StartNode(boost::thread_group& threadGroup, CScheduler& scheduler /*, const Options& connOptions*/);
    bool StopNode();

    void AddOneShot(const std::string& strDest);
    void ProcessOneShot();
    bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant *grantOutbound = NULL, const char *strDest = NULL, bool fOneShot = false);
    void AcceptConnection(const ListenSocket& hListenSocket);
    CNode* FindNode(const CNetAddr& ip);
    CNode* FindNode(const CSubNet& subNet);
    CNode* FindNode(const std::string& addrName);
    CNode* FindNode(const CService& ip);
    CNode* ConnectNode(CAddress addrConnect, const char *pszDest = NULL);

    void SocketSendData(CNode *pnode);

    CConnman();
    ~CConnman();

    bool fAddressesInitialized {false};
    std::unique_ptr<CSemaphore> semOutbound = nullptr;
    std::unique_ptr<CNode> pnodeLocalHost = nullptr;

    std::vector<CNode*> vNodes;
    CCriticalSection cs_vNodes;
    std::vector<std::string> vAddedNodes;
    CCriticalSection cs_vAddedNodes;
    std::atomic<NodeId> nLastNodeId{0};
    NodeId GetNewNodeId();

    std::vector<zen::NODE_ADDR> vNonTLSNodesInbound;
    CCriticalSection cs_vNonTLSNodesInbound;
    std::vector<zen::NODE_ADDR> vNonTLSNodesOutbound;
    CCriticalSection cs_vNonTLSNodesOutbound;

    std::deque<std::string> vOneShots;
    CCriticalSection cs_vOneShots;

    // Network stats
    void RecordBytesRecv(uint64_t bytes);
    void RecordBytesSent(uint64_t bytes);
    uint64_t GetTotalBytesRecv();
    uint64_t GetTotalBytesSent();

private:
    std::atomic<uint64_t> nTotalBytesRecv = 0;
    std::atomic<uint64_t> nTotalBytesSent = 0;

    // // To be moved here in the next PR, when we will get rid of boost::thread!
    // std::thread threadDNSAddressSeed;
    // std::thread threadSocketHandler;
    // std::thread threadOpenAddedConnections;
    // std::thread threadOpenConnections;
    // std::thread threadMessageHandler;
    // std::thread threadNonTLSPoolsCleaner;
    // void ThreadOpenConnections();
    // void ThreadOpenAddedConnections();

};

#endif // BITCOIN_NET_H
