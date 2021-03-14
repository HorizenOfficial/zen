// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"

#include "clientversion.h"
#include "main.h"
#include "net.h"
#include "netbase.h"
#include "protocol.h"
#include "sync.h"
#include "util.h"
#include "version.h"
#include "zen/utiltls.h"

#include <boost/foreach.hpp>

#include <univalue.h>

using namespace std;
using namespace zen;

UniValue getconnectioncount(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getconnectioncount\n"
            "\nReturns the number of active connections to other peers.\n"
            
            "\nResult:\n"
            "n        (numeric) the connection count\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getconnectioncount", "")
            + HelpExampleRpc("getconnectioncount", "")
        );

    LOCK2(cs_main, cs_vNodes);

    return (int)vNodes.size();
}

UniValue ping(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "ping\n"
            "\nRequests that a ping be sent to all other nodes, to measure ping time.\n"
            "Results provided in getpeerinfo, pingtime and pingwait fields are decimal seconds.\n"
            "Ping command is handled in queue with all other commands, so it measures processing backlog, not just network ping.\n"
            
            "\nResult:\n"
            "Nothing\n"
            
            "\nExamples:\n"
            + HelpExampleCli("ping", "")
            + HelpExampleRpc("ping", "")
        );

    // Request that each node send a ping during next message processing pass
    LOCK2(cs_main, cs_vNodes);

    BOOST_FOREACH(CNode* pNode, vNodes) {
        pNode->fPingQueued = true;
    }

    return NullUniValue;
}

static void CopyNodeStats(std::vector<CNodeStats>& vstats)
{
    vstats.clear();

    LOCK(cs_vNodes);
    vstats.reserve(vNodes.size());
    BOOST_FOREACH(CNode* pnode, vNodes) {
        CNodeStats stats;
        pnode->copyStats(stats);
        vstats.push_back(stats);
    }
}

UniValue getpeerinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getpeerinfo\n"
            "\nReturns data about each connected network node as a json array of objects.\n"
            
            "\nbResult:\n"
            "[\n"
            "  {\n"
            "    \"id\": n,                              (numeric) peer index\n"
            "    \"addr\": \"host:port\",                (string) the ip address and port of the peer\n"
            "    \"addrlocal\": \"ip:port\",             (string) local address\n"
            "    \"services\":\"xxxxxxxxxxxxxxxx\",      (string) the services offered\n"
            "    \"tls_established\": true|false,        (boolean) status of TLS connection\n"
            "    \"tls_verified\": true|false,           (boolean) status of peer certificate. Will be true if a peer certificate can be verified with some trusted root certs \n"
            "    \"lastsend\": ttt,                      (numeric) the time in seconds since epoch (Jan 1 1970 GMT) of the last send\n"
            "    \"lastrecv\": ttt,                      (numeric) the time in seconds since epoch (Jan 1 1970 GMT) of the last receive\n"
            "    \"bytessent\": n,                       (numeric) the total bytes sent\n"
            "    \"bytesrecv\": n,                       (numeric) the total bytes received\n"
            "    \"conntime\": ttt,                      (numeric) the connection time in seconds since 1 Jan 1970 GMT\n"
            "    \"timeoffset\": ttt,                    (numeric) the time offset in seconds\n"
            "    \"pingtime\": n,                        (numeric) ping time\n"
            "    \"pingwait\": n,                        (numeric) ping wait\n"
            "    \"version\": v,                         (numeric) the protocol version of the peer\n"
            "    \"subver\": \"/MagicBean:x.y.z[-v]/\",  (string) the user agent of the peer\n"
            "    \"inbound\": true|false,                (boolean) inbound (true) or outbound (false)\n"
            "    \"startingheight\": n,                  (numeric) the starting height (block) of the peer\n"
            "    \"banscore\": n,                        (numeric) the ban score\n"
            "    \"synced_headers\": n,                  (numeric) the last header we have in common with this peer\n"
            "    \"synced_blocks\": n,                   (numeric) the last block we have in common with this peer\n"
            "    \"inflight\": [\n"
            "       n,                                   (numeric) the heights of blocks we're currently asking from this peer\n"
            "       ...\n"
            "    ],\n"
            "    \"whitelisted\": true|false             (boolean) whether the peer is whitelisted\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getpeerinfo", "")
            + HelpExampleRpc("getpeerinfo", "")
        );

    LOCK(cs_main);

    vector<CNodeStats> vstats;
    CopyNodeStats(vstats);

    UniValue ret(UniValue::VARR);

    BOOST_FOREACH(const CNodeStats& stats, vstats) {
        UniValue obj(UniValue::VOBJ);
        CNodeStateStats statestats;
        bool fStateStats = GetNodeStateStats(stats.nodeid, statestats);
        obj.pushKV("id", stats.nodeid);
        obj.pushKV("addr", stats.addrName);
        if (!(stats.addrLocal.empty()))
            obj.pushKV("addrlocal", stats.addrLocal);
        obj.pushKV("services", strprintf("%016x", stats.nServices));
        obj.pushKV("tls_established", stats.fTLSEstablished);
        obj.pushKV("tls_verified", stats.fTLSVerified);
        obj.pushKV("lastsend", stats.nLastSend);
        obj.pushKV("lastrecv", stats.nLastRecv);
        obj.pushKV("bytessent", stats.nSendBytes);
        obj.pushKV("bytesrecv", stats.nRecvBytes);
        obj.pushKV("conntime", stats.nTimeConnected);
        obj.pushKV("timeoffset", stats.nTimeOffset);
        obj.pushKV("pingtime", stats.dPingTime);
        if (stats.dPingWait > 0.0)
            obj.pushKV("pingwait", stats.dPingWait);
        obj.pushKV("version", stats.nVersion);
        // Use the sanitized form of subver here, to avoid tricksy remote peers from
        // corrupting or modifiying the JSON output by putting special characters in
        // their ver message.
        obj.pushKV("subver", stats.cleanSubVer);
        obj.pushKV("inbound", stats.fInbound);
        obj.pushKV("startingheight", stats.nStartingHeight);
        if (fStateStats) {
            obj.pushKV("banscore", statestats.nMisbehavior);
            obj.pushKV("synced_headers", statestats.nSyncHeight);
            obj.pushKV("synced_blocks", statestats.nCommonHeight);
            UniValue heights(UniValue::VARR);
            BOOST_FOREACH(int height, statestats.vHeightInFlight) {
                heights.push_back(height);
            }
            obj.pushKV("inflight", heights);
        }
        obj.pushKV("whitelisted", stats.fWhitelisted);

        ret.push_back(obj);
    }

    return ret;
}

UniValue addnode(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (params.size() == 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() != 2 ||
        (strCommand != "onetry" && strCommand != "add" && strCommand != "remove"))
        throw runtime_error(
            "addnode \"node\" \"add|remove|onetry\"\n"
            "\nAttempts add or remove a node from the addnode list.\n"
            "Or try a connection to a node once.\n"
            
            "\nArguments:\n"
            "1. \"node\"     (string, required) the node (see getpeerinfo for nodes)\n"
            "2. \"command\"  (string, required) 'add' to add a node to the list, 'remove' to remove a node from the list, 'onetry' to try a connection to the node once\n"
            
            "\nResult:\n"
            "Nothing\n"
            
            "\nExamples:\n"
            + HelpExampleCli("addnode", "\"192.168.0.6:8233\" \"onetry\"")
            + HelpExampleRpc("addnode", "\"192.168.0.6:8233\", \"onetry\"")
        );

    string strNode = params[0].get_str();

    if (strCommand == "onetry")
    {
        CAddress addr;
        OpenNetworkConnection(addr, NULL, strNode.c_str());
        return NullUniValue;
    }

    LOCK(cs_vAddedNodes);
    vector<string>::iterator it = vAddedNodes.begin();
    for(; it != vAddedNodes.end(); it++)
        if (strNode == *it)
            break;

    if (strCommand == "add")
    {
        if (it != vAddedNodes.end())
            throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: Node already added");
        vAddedNodes.push_back(strNode);
    }
    else if(strCommand == "remove")
    {
        if (it == vAddedNodes.end())
            throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
        vAddedNodes.erase(it);
    }

    return NullUniValue;
}

UniValue disconnectnode(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "disconnectnode \"node\" \n"
            "\nImmediately disconnects from the specified node.\n"
            
            "\nArguments:\n"
            "1. \"node\"     (string, required) the node (see getpeerinfo for nodes)\n"
            
            "\nResult:\n"
            "Nothing\n"
            
            "\nExamples:\n"
            + HelpExampleCli("disconnectnode", "\"192.168.0.6:8233\"")
            + HelpExampleRpc("disconnectnode", "\"192.168.0.6:8233\"")
        );

    CNode* pNode = FindNode(params[0].get_str());
    if (pNode == NULL)
        throw JSONRPCError(RPC_CLIENT_NODE_NOT_CONNECTED, "Node not found in connected nodes");

    pNode->fDisconnect = true;

    return NullUniValue;
}

UniValue getaddednodeinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
           "getaddednodeinfo dns ( \"node\" )\n"
            "\nReturns information about the given added node, or all added nodes\n"
            "(note that onetry addnodes are not listed here)\n"
            "If dns is false, only a list of added nodes will be provided,\n"
            "otherwise connected information will also be available.\n"
            
            "\nArguments:\n"
            "1. dns                                          (boolean, required) if false, only a list of added nodes will be provided, otherwise connected information will also be available\n"
            "2. \"node\"                                     (string, optional) if provided, return information about this specific node, otherwise all nodes are returned\n"
            
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"addednode\" : \"192.168.0.201\",          (string) the node ip address\n"
            "    \"connected\" : true|false,                 (boolean) if connected\n"
            "    \"addresses\" : [\n"
            "       {\n"
            "         \"address\" : \"192.168.0.201:8233\",  (string) the Horizen server host and port\n"
            "         \"connected\" : \"outbound\"           (string) connection, inbound or outbound\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getaddednodeinfo", "true")
            + HelpExampleCli("getaddednodeinfo", "true \"192.168.0.201\"")
            + HelpExampleRpc("getaddednodeinfo", "true, \"192.168.0.201\"")
        );
    bool fDns = params[0].get_bool();

    list<string> laddedNodes(0);
    if (params.size() == 1)
    {
        LOCK(cs_vAddedNodes);
        BOOST_FOREACH(const std::string& strAddNode, vAddedNodes)
            laddedNodes.push_back(strAddNode);
    }
    else
    {
        string strNode = params[1].get_str();
        LOCK(cs_vAddedNodes);
        BOOST_FOREACH(const std::string& strAddNode, vAddedNodes) {
            if (strAddNode == strNode)
            {
                laddedNodes.push_back(strAddNode);
                break;
            }
        }
        if (laddedNodes.size() == 0)
            throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
    }

    UniValue ret(UniValue::VARR);
    if (!fDns)
    {
        BOOST_FOREACH (const std::string& strAddNode, laddedNodes) {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("addednode", strAddNode);
            ret.push_back(obj);
        }
        return ret;
    }

    list<pair<string, vector<CService> > > laddedAddreses(0);
    BOOST_FOREACH(const std::string& strAddNode, laddedNodes) {
        vector<CService> vservNode(0);
        if(Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), fNameLookup, 0))
            laddedAddreses.push_back(make_pair(strAddNode, vservNode));
        else
        {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("addednode", strAddNode);
            obj.pushKV("connected", false);
            UniValue addresses(UniValue::VARR);
            obj.pushKV("addresses", addresses);
        }
    }

    LOCK(cs_vNodes);
    for (list<pair<string, vector<CService> > >::iterator it = laddedAddreses.begin(); it != laddedAddreses.end(); it++)
    {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("addednode", it->first);

        UniValue addresses(UniValue::VARR);
        bool fConnected = false;
        BOOST_FOREACH(const CService& addrNode, it->second) {
            bool fFound = false;
            UniValue node(UniValue::VOBJ);
            node.pushKV("address", addrNode.ToString());
            BOOST_FOREACH(CNode* pnode, vNodes) {
                if (pnode->addr == addrNode)
                {
                    fFound = true;
                    fConnected = true;
                    node.pushKV("connected", pnode->fInbound ? "inbound" : "outbound");
                    break;
                }
            }
            if (!fFound)
                node.pushKV("connected", "false");
            addresses.push_back(node);
        }
        obj.pushKV("connected", fConnected);
        obj.pushKV("addresses", addresses);
        ret.push_back(obj);
    }

    return ret;
}

UniValue getnettotals(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw runtime_error(
            "getnettotals\n"
            "\nReturns information about network traffic, including bytes in, bytes out,\n"
            "and current time.\n"
            
            "\nResult:\n"
            "{\n"
            "  \"totalbytesrecv\": n,   (numeric) total bytes received\n"
            "  \"totalbytessent\": n,   (numeric) total bytes sent\n"
            "  \"timemillis\": t        (numeric) number of milliseconds since 1 Jan 1970 GMT\n"
            "}\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getnettotals", "")
            + HelpExampleRpc("getnettotals", "")
       );

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("totalbytesrecv", CNode::GetTotalBytesRecv());
    obj.pushKV("totalbytessent", CNode::GetTotalBytesSent());
    obj.pushKV("timemillis", GetTimeMillis());
    return obj;
}

static UniValue GetNetworksInfo()
{
    UniValue networks(UniValue::VARR);
    for(int n=0; n<NET_MAX; ++n)
    {
        enum Network network = static_cast<enum Network>(n);
        if(network == NET_UNROUTABLE)
            continue;
        proxyType proxy;
        UniValue obj(UniValue::VOBJ);
        GetProxy(network, proxy);
        obj.pushKV("name", GetNetworkName(network));
        obj.pushKV("limited", IsLimited(network));
        obj.pushKV("reachable", IsReachable(network));
        obj.pushKV("proxy", proxy.IsValid() ? proxy.proxy.ToStringIPPort() : string());
        obj.pushKV("proxy_randomize_credentials", proxy.randomize_credentials);
        networks.push_back(obj);
    }
    return networks;
}

UniValue getnetworkinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getnetworkinfo\n"
            "Returns an object containing various state info regarding P2P networking.\n"
            
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,                            (numeric) the version of the node as a numeric\n"
            "  \"subversion\": \"/MagicBean:x.y.z[-v]/\",     (string) the subversion of the node, as advertised to peers\n"
            "  \"protocolversion\": xxxxx,                    (numeric) the protocol version of the node\n"
            "  \"localservices\": \"xxxxxxx\",                (string) the services supported by the node, as advertised in its version message\n"
            "  \"timeoffset\": 0,                             (numeric) the time offset (deprecated; always 0)\n"
            "  \"connections\": xxxxx,                        (numeric) the total number of open connections for the node\n"
            "  \"tls_cert_verified\": true|flase,             (boolean) true if the certificate of the current node is verified\n"
            "  \"networks\": [                                (array) an array of objects describing IPV4, IPV6 and Onion network interface states\n"
            "  {\n"
            "    \"name\": \"xxx\",                           (string) network (ipv4, ipv6 or onion)\n"
            "    \"limited\": true|false,                     (boolean) is the network limited using -onlynet?\n"
            "    \"reachable\": true|false,                   (boolean) is the network reachable?\n"
            "    \"proxy\": \"host:port\",                    (string) the proxy that is used for this network, or empty if none\n"
            "    \"proxy_randomize_credentials\": true|false  (boolean) whether randomized credentials are used\n"
            "  }\n"
            "  ,...\n"
            "  ],\n"
            "  \"relayfee\": xxxxxx,                          (numeric) minimum relay fee for non-free transactions in " + CURRENCY_UNIT + "/kB\n"
            "  \"localaddresses\": [                          (array) an array of objects describing local addresses being listened on by the node\n"
            "   {\n"
            "    \"address\": \"xxxx\",                       (string) network address\n"
            "    \"port\": xxx,                               (numeric) network port\n"
            "    \"score\": xxx                               (numeric) relative score\n"
            "   }\n"
            "   ,...\n"
            "  ],\n"
            "  \"warnings\": \"...\"                          (string) any network warnings (such as alert messages) \n"
            "}\n"
            
            "\nExamples:\n"
            + HelpExampleCli("getnetworkinfo", "")
            + HelpExampleRpc("getnetworkinfo", "")
        );

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("version",       CLIENT_VERSION);
    obj.pushKV("subversion",
        FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, std::vector<string>()));
    obj.pushKV("protocolversion",PROTOCOL_VERSION);
    obj.pushKV("localservices",       strprintf("%016x", nLocalServices));
    obj.pushKV("timeoffset",    0);
    obj.pushKV("connections",   (int)vNodes.size());
    obj.pushKV("tls_cert_verified", ValidateCertificate(tls_ctx_server));
    obj.pushKV("networks",      GetNetworksInfo());
    obj.pushKV("relayfee",      ValueFromAmount(::minRelayTxFee.GetFeePerK()));
    UniValue localAddresses(UniValue::VARR);
    {
        LOCK(cs_mapLocalHost);
        BOOST_FOREACH(const PAIRTYPE(CNetAddr, LocalServiceInfo) &item, mapLocalHost)
        {
            UniValue rec(UniValue::VOBJ);
            rec.pushKV("address", item.first.ToString());
            rec.pushKV("port", item.second.nPort);
            rec.pushKV("score", item.second.nScore);
            localAddresses.push_back(rec);
        }
    }
    obj.pushKV("localaddresses", localAddresses);
    obj.pushKV("warnings",       GetWarnings("statusbar"));
    return obj;
}

UniValue setban(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() < 2 ||
        (strCommand != "add" && strCommand != "remove"))
        throw runtime_error(
            "setban \"ip(/netmask)\" \"add|remove\" (bantime) (absolute)\n"
            "\nAttempts add or remove a IP/Subnet from the banned list.\n"
            
            "\nArguments:\n"
            "1. \"ip(/netmask)\" (string, required) the IP/Subnet (see getpeerinfo for nodes ip) with a optional netmask (default is /32 = single ip)\n"
            "2. \"command\"      (string, required) 'add' to add a IP/Subnet to the list, 'remove' to remove a IP/Subnet from the list\n"
            "3. \"bantime\"      (numeric, optional) time in seconds how long (or until when if [absolute] is set) the ip is banned (0 or empty means using the default time of 24h which can also be overwritten by the -bantime startup argument)\n"
            "4. \"absolute\"     (boolean, optional) if set, the bantime must be a absolute timestamp in seconds since epoch (Jan 1 1970 GMT)\n"
            
            "\nResult:\n"
            "Nothing\n"
            
            "\nExamples:\n"
            + HelpExampleCli("setban", "\"192.168.0.6\" \"add\" 86400")
            + HelpExampleCli("setban", "\"192.168.0.0/24\" \"add\"")
            + HelpExampleRpc("setban", "\"192.168.0.6\", \"add\", 86400")       
        );

    CSubNet subNet;
    CNetAddr netAddr;
    bool isSubnet = false;

    if (params[0].get_str().find("/") != string::npos)
        isSubnet = true;

    if (!isSubnet)
        netAddr = CNetAddr(params[0].get_str());
    else
        subNet = CSubNet(params[0].get_str());

    if (! (isSubnet ? subNet.IsValid() : netAddr.IsValid()) )
        throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: Invalid IP/Subnet");

    if (strCommand == "add")
    {
        if (isSubnet ? CNode::IsBanned(subNet) : CNode::IsBanned(netAddr))
            throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: IP/Subnet already banned");

        int64_t banTime = 0; //use standard bantime if not specified
        if (params.size() >= 3 && !params[2].isNull())
            banTime = params[2].get_int64();

        bool absolute = false;
        if (params.size() == 4 && params[3].isTrue())
            absolute = true;

        isSubnet ? CNode::Ban(subNet, banTime, absolute) : CNode::Ban(netAddr, banTime, absolute);

        //disconnect possible nodes
        while(CNode *bannedNode = (isSubnet ? FindNode(subNet) : FindNode(netAddr)))
            bannedNode->fDisconnect = true;
    }
    else if(strCommand == "remove")
    {
        if (!( isSubnet ? CNode::Unban(subNet) : CNode::Unban(netAddr) ))
            throw JSONRPCError(RPC_MISC_ERROR, "Error: Unban failed");
    }

    return NullUniValue;
}

UniValue listbanned(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "listbanned\n"
            "\nList all banned IPs/Subnets.\n"
            "If there are no banned IPs, it will return an empty array.\n"
            
            "\nResult:\n"
            "[\n"
            "   {\n"
            "       \"address\": \"xxxxxx\"  (numeric) IP/Subnet,\n"
            "       \"banned_until\": tttt   (numeric) time in seconds how long the ip is banned\n"
            "   }\n"
            "   ,...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listbanned", "")
            + HelpExampleRpc("listbanned", "")
        );

    std::map<CSubNet, int64_t> banMap;
    CNode::GetBanned(banMap);

    UniValue bannedAddresses(UniValue::VARR);
    for (std::map<CSubNet, int64_t>::iterator it = banMap.begin(); it != banMap.end(); it++)
    {
        UniValue rec(UniValue::VOBJ);
        rec.pushKV("address", (*it).first.ToString());
        rec.pushKV("banned_until", (*it).second);
        bannedAddresses.push_back(rec);
    }

    return bannedAddresses;
}

UniValue clearbanned(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "clearbanned\n"
            "\nClear all banned IPs.\n"
            
            "\nResult:\n"
            "Nothing\n"
            
            "\nExamples:\n"
            + HelpExampleCli("clearbanned", "")
            + HelpExampleRpc("clearbanned", "")
        );

    CNode::ClearBanned();

    return NullUniValue;
}
