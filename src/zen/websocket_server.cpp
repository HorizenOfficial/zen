#include "../util.h"
#include <univalue.h>
#include <boost/unordered_map.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <queue>
#include "validationinterface.h"
#include "main.h"
#include "consensus/validation.h"
#include <univalue.h>
#include "uint256.h"
#include "utilmoneystr.h"

extern UniValue sc_send_certificate(const UniValue& params, bool fHelp);
extern CAmount AmountFromValue(const UniValue& value);

using tcp = boost::asio::ip::tcp;

namespace websocket = boost::beast::websocket;
namespace http = boost::beast::http;

namespace net = boost::asio;
net::io_context ioc;

static int MAX_BLOCKS_REQUEST = 100;
static int MAX_HEADERS_REQUEST = 50;
static int MAX_SIDECHAINS_REQUEST = 50;
static int tot_connections = 0;

class WsNotificationInterface;
class WsHandler;

static int getblock(const CBlockIndex *pindex, std::string& blockHexStr);
static int getheader(const CBlockIndex *pindex, std::string& blockHexStr);
static void ws_updatetip(const CBlockIndex *pindex);

static boost::shared_ptr<WsNotificationInterface> wsNotificationInterface;
static std::list< boost::shared_ptr<WsHandler> > listWsHandler;

std::atomic<bool> exit_ws_thread{false};
boost::thread ws_thread;
std::mutex wsmtx;

static void dumpUniValueError(const UniValue& error, std::string& outMsg)
{
    UniValue errCode = find_value(error, "code");
    UniValue errMsg  = find_value(error, "message");
    std::string strPrint = errCode.isNull() ? "" : "error code: " + errCode.getValStr() + "; ";
    if (errMsg.isStr())
    {
        outMsg = errMsg.get_str();
    }
    LogPrint("ws", "%s():%d - JSON error: %s\n", __func__, __LINE__, (strPrint + outMsg));
}

static std::string findFieldValue(const std::string& field, const UniValue& request)
{
    const UniValue& clientvalue = find_value(request, field);
    if (clientvalue.isStr()) {
        return clientvalue.get_str();
    } else if (clientvalue.isNum()) {
        return std::to_string(clientvalue.get_int());
    } else if (clientvalue.isNull()) {
        return "";
    }
    return "";
}

class WsNotificationInterface: public CValidationInterface
{
protected:
    virtual void UpdatedBlockTip(const CBlockIndex *pindex) {
        ws_updatetip(pindex);
    };
public:
    ~WsNotificationInterface() 
    {
        LogPrint("ws", "%s():%d - called this=%p\n", __func__, __LINE__, this);
    }
};

class WsEvent
{
public:
    enum WsEventType {
        UPDATE_TIP = 0,
        EVT_UNDEFINED = 0xff
    };
    enum WsRequestType {
        GET_SINGLE_BLOCK = 0,
        GET_MULTIPLE_BLOCK_HASHES = 1,
        GET_NEW_BLOCK_HASHES = 2,
        SEND_CERTIFICATE = 3,
        GET_MULTIPLE_BLOCK_HEADERS = 4,
        GET_TOP_QUALITY_CERTIFICATES = 5,
        GET_SIDECHAIN_VERSIONS = 6,
        REQ_UNDEFINED = 0xff
    };
    
    enum WsMsgType {
        MSG_EVENT = 0,
        MSG_REQUEST = 1,
        MSG_RESPONSE = 2,
        MSG_ERROR = 3,
        MSG_UNDEFINED = 0xff
    };

    explicit WsEvent(WsMsgType xn): type(xn), payload(UniValue::VOBJ)
    {
        payload.pushKV("msgType", type);
    }
    WsEvent & operator=(const WsEvent& ws) = delete;
    WsEvent(const WsEvent& ws) = delete;

    UniValue* getPayload() {
        return &payload;
    }

private:
    WsMsgType type;
    UniValue payload;
};



class WsHandler
{
private:
    std::condition_variable writeCV;
    std::mutex writeMutex;

    boost::shared_ptr< websocket::stream<tcp::socket>> localWs;
    boost::lockfree::queue<WsEvent*, boost::lockfree::capacity<1024>> wsq;
    std::atomic<bool> exit_rwhandler_thread_flag { false };

    void write(WsEvent* wse)
    {
        wsq.push(wse);
        writeCV.notify_one();
    }
    void sendBlockEvent(int height, const std::string& strHash, const std::string& blockHex, WsEvent::WsEventType eventType)
    {
        // Send a message to the client:  type = eventType
        WsEvent* wse = new WsEvent(WsEvent::MSG_EVENT);
        LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
        rspPayload.pushKV("height", height);
        rspPayload.pushKV("hash", strHash);
        rspPayload.pushKV("block", blockHex);

        UniValue* rv = wse->getPayload();
        rv->pushKV("eventType", eventType);
        rv->pushKV("eventPayload", rspPayload);
        write(wse);
    }

    void sendBlock(int height, const std::string& strHash, const std::string& blockHex,
            WsEvent::WsMsgType msgType, std::string clientRequestId = "")
    {
        // Send a message to the client:  type = eventType
        WsEvent* wse = new WsEvent(msgType);
        LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
        rspPayload.pushKV("height", height);
        rspPayload.pushKV("hash", strHash);
        rspPayload.pushKV("block", blockHex);

        UniValue* rv = wse->getPayload();
        if (!clientRequestId.empty())
            rv->pushKV("requestId", clientRequestId);
        rv->pushKV("responsePayload", rspPayload);
        write(wse);
    }

    void sendHashes(int height, std::list<CBlockIndex*>& listBlock,
            WsEvent::WsMsgType msgType, std::string clientRequestId = "")
    {
        // Send a message to the client:  type = eventType
        WsEvent* wse = new WsEvent(msgType);
        LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
        rspPayload.pushKV("height", height);

        UniValue hashes(UniValue::VARR);
        std::list<CBlockIndex*>::iterator it = listBlock.begin();
        while (it != listBlock.end()) {
            CBlockIndex* blockIndexIterator = *it;
            hashes.push_back(blockIndexIterator->GetBlockHash().GetHex());
            ++it;
        }
        rspPayload.pushKV("hashes", hashes);

        UniValue* rv = wse->getPayload();
        if (!clientRequestId.empty())
            rv->pushKV("requestId", clientRequestId);
        rv->pushKV("responsePayload", rspPayload);
        write(wse);
    }

    void sendCertificateHash(const UniValue& retCert, WsEvent::WsMsgType msgType, std::string clientRequestId = "")
    {
        // Send a message to the client:  type = eventType
        WsEvent* wse = new WsEvent(msgType);
        LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);

        rspPayload.pushKV("certificateHash", retCert);

        UniValue* rv = wse->getPayload();
        if (!clientRequestId.empty())
            rv->pushKV("requestId", clientRequestId);
        rv->pushKV("responsePayload", rspPayload);
        write(wse);
    }

    void sendBlockHeaders(const UniValue& headers, WsEvent::WsMsgType msgType, std::string clientRequestId = "")
    {
        // Send a message to the client:  type = eventType
        WsEvent* wse = new WsEvent(msgType);
        LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
        
        rspPayload.pushKV("headers", headers);

        UniValue* rv = wse->getPayload();
        if (!clientRequestId.empty())
            rv->pushKV("requestId", clientRequestId);
        rv->pushKV("responsePayload", rspPayload);
        write(wse);
    }
    
    void sendTopQualityCertificates(const UniValue& mempoolCert, const UniValue& chainCert,
                                    WsEvent::WsMsgType msgType, std::string clientRequestId = "")
    {
        // Send a message to the client:  type = eventType
        WsEvent* wse = new WsEvent(msgType);
        LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
        
        rspPayload.push_back(Pair("mempoolTopQualityCert", mempoolCert));
        rspPayload.push_back(Pair("chainTopQualityCert", chainCert));

        UniValue* rv = wse->getPayload();
        if (!clientRequestId.empty())
            rv->push_back(Pair("requestId", clientRequestId));
        rv->push_back(Pair("responsePayload", rspPayload));
        write(wse);
    }

    void sendSidechainVersions(const UniValue& sidechainVersions, WsEvent::WsMsgType msgType, std::string clientRequestId = "")
    {
        WsEvent* wse = new WsEvent(msgType);
        LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
        
        rspPayload.push_back(Pair("sidechainVersions", sidechainVersions));

        UniValue* rv = wse->getPayload();
        if (!clientRequestId.empty())
            rv->push_back(Pair("requestId", clientRequestId));
        rv->push_back(Pair("responsePayload", rspPayload));
        write(wse);
    }

    int getHashByHeight(std::string height, std::string& strHash)
    {
        int nHeight = -1;
        try {
            nHeight = std::stoi(height);
        } catch (const std::exception &e) {
            LogPrint("ws", "%s():%d - %s\n", __func__, __LINE__, e.what());
            return INVALID_PARAMETER;
        }
        if (nHeight < 0 || nHeight > chainActive.Height()) {
            LogPrint("ws", "%s():%d - invalid height %d\n", __func__, __LINE__, nHeight);
            return INVALID_PARAMETER;
        }
        {
            LOCK(cs_main);
            strHash = chainActive[nHeight]->GetBlockHash().GetHex();
        }
        return OK;
    }

    int sendBlockByHeight(const std::string& strHeight, const std::string& clientRequestId)
    {
        std::string strHash;
        int r = getHashByHeight(strHeight, strHash);
        if (r != OK)
        {
            return r;
        }
        return sendBlockByHash(strHash, clientRequestId);
    }

    int sendBlockByHash(const std::string& strHash, const std::string& clientRequestId) {
        CBlockIndex* pblockindex = NULL;
        {
            LOCK(cs_main);
            uint256 hash(uint256S(strHash));
            BlockMap::iterator mi = mapBlockIndex.find(hash);
            if (mi != mapBlockIndex.end())
            {
                pblockindex = (*mi).second;
            }
        }
        if (pblockindex == NULL)
        {
            LogPrint("ws", "%s():%d - block index not found for hash[%s]\n", __func__, __LINE__, strHash);
            return INVALID_PARAMETER;
        }
        std::string block;
        int ret = getblock(pblockindex, block);
        if (ret != OK)
        {
            return ret;
        }
        sendBlock(pblockindex->nHeight, strHash, block, WsEvent::MSG_RESPONSE, clientRequestId);
        return OK;
    }

    int sendBlocksFromHeight(const std::string& strHeight, const std::string& strLen, const std::string& clientRequestId)
    {
        std::string strHash;
        int r = getHashByHeight(strHeight, strHash);
        if (r != OK)
            return r;
        return sendBlocksFromHash(strHash, strLen, clientRequestId);
    }

    int sendBlocksFromHash(const std::string& strHash, const std::string& strLen, const std::string& clientRequestId)
    {
        int len = -1;
        try {
            len = std::stoi(strLen);
        } catch (const std::exception &e) {
            LogPrint("ws", "%s():%d - %s\n", __func__, __LINE__, e.what());
            return INVALID_PARAMETER;
        }
        if (len < 1)
        {
            LogPrint("ws", "%s():%d - invalid len %d\n", __func__, __LINE__, len);
            return INVALID_PARAMETER;
        }
        if (len > MAX_BLOCKS_REQUEST)
        {
            LogPrint("ws", "%s():%d - invalid len %d (max is %d)\n", __func__, __LINE__, len, MAX_BLOCKS_REQUEST);
            return INVALID_PARAMETER;
        }

        std::list<CBlockIndex*> listBlock;
        CBlockIndex* pblockindex = NULL;
        {
            LOCK(cs_main);
            uint256 hash(uint256S(strHash));
            BlockMap::iterator mi = mapBlockIndex.find(hash);
            if (mi != mapBlockIndex.end())
            {
                pblockindex = (*mi).second;
            }
            if (pblockindex == NULL) {
                LogPrint("ws", "%s():%d - block index not found for hash[%s]\n", __func__, __LINE__, strHash);
                return INVALID_PARAMETER;
            }
            CBlockIndex* nextBlock = chainActive.Next(pblockindex);
            if (nextBlock == NULL)
            {
                LogPrint("ws", "%s():%d - next block index not found for hash[%s]\n", __func__, __LINE__, strHash);
                return INVALID_PARAMETER;
            }
            int n = 0;
            while (n < len) {
                listBlock.push_back(nextBlock);
                nextBlock = chainActive.Next(nextBlock);
                if (nextBlock == NULL)
                    break;
                n++;
            }
        }
        sendHashes(listBlock.front()->nHeight, listBlock, WsEvent::MSG_RESPONSE, clientRequestId);
        return OK;
    }


    int sendHashFromLocator(const UniValue& hashes, std::string strLen, const std::string& clientRequestId) {
        int len = -1;
        try {
            len = std::stoi(strLen);
        } catch (const std::exception &e) {
            LogPrint("ws", "%s():%d - %s\n", __func__, __LINE__, e.what());
            return INVALID_PARAMETER;
        }
        if (len < 1)
        {
            LogPrint("ws", "%s():%d - invalid len %d\n", __func__, __LINE__, len);
            return INVALID_PARAMETER;
        }
        if (len > MAX_BLOCKS_REQUEST)
        {
            LogPrint("ws", "%s():%d - invalid len %d (max is %d)\n", __func__, __LINE__, len, MAX_BLOCKS_REQUEST);
            return INVALID_PARAMETER;
        }

        std::list<CBlockIndex*> listBlock;
        CBlockIndex* pblockindexStart = NULL;
        int lastH = 0;
        for (const UniValue& o : hashes.getValues())
        {
            if (o.isObject())
            {
                LogPrint("ws", "%s():%d - invalid obj\n", __func__, __LINE__);
                return INVALID_PARAMETER;
            }
            CBlockIndex* pblockindex = NULL;
            uint256 hash(uint256S(o.get_str()));
            {
                LOCK(cs_main);
                BlockMap::iterator mi = mapBlockIndex.find(hash);
                if (mi != mapBlockIndex.end())
                {
                    pblockindex = (*mi).second;
                    if (!chainActive.Contains(pblockindex))
                        pblockindex = NULL;
                }
            }
            if (pblockindex == NULL)
            {
                LogPrint("ws", "%s():%d - block index not found for hash[%s], skipping it\n", __func__, __LINE__, o.get_str());
                continue;
            }
            if (pblockindex->nHeight > lastH)
            {
                lastH = pblockindex->nHeight;
                pblockindexStart = pblockindex;
            }
        }
        {
            LOCK(cs_main);
            CBlockIndex* nextBlock = pblockindexStart;
            if (nextBlock == NULL)
            {
                LogPrint("ws", "%s():%d - start block index not found\n", __func__, __LINE__);
                return INVALID_PARAMETER;
            }
            int n = 0;
            while (n < len) {
                listBlock.push_back(nextBlock);
                nextBlock = chainActive.Next(nextBlock);
                if (nextBlock == NULL)
                    break;
                n++;
            }
        }
        sendHashes(listBlock.front()->nHeight, listBlock, WsEvent::MSG_RESPONSE, clientRequestId);
        return OK;
    }

    int sendCertificate(const UniValue& cmdParams, const std::string& clientRequestId, std::string& outMsg) {
        UniValue ret;
        try {
             ret = sc_send_certificate(cmdParams, false);
        } catch (const UniValue& e) {
            dumpUniValueError(e, outMsg);
            return INVALID_PARAMETER;
        } catch (const std::exception& e) {
            std::string strPrint = std::string("error: ") + e.what();
            LogPrint("ws", "%s():%d - std exception: %s\n", __func__, __LINE__, strPrint);
            return INVALID_PARAMETER;
        } catch (...) {
            LogPrint("ws", "%s():%d - Generic exception\n", __func__, __LINE__);
            return INVALID_PARAMETER;
        }
        sendCertificateHash(ret, WsEvent::MSG_RESPONSE, clientRequestId);
        return OK;
    }

    int sendHeadersFromHashes(const UniValue& hashes, const std::string& clientRequestId)
    {
        if (hashes.size() > MAX_HEADERS_REQUEST) {
            LogPrint("ws", "%s():%d - invalid hashes amount %d (max is %d)\n", __func__, __LINE__, hashes.size(), MAX_HEADERS_REQUEST);
            return INVALID_PARAMETER;
        }
            
        UniValue headers(UniValue::VARR);
            
        for (const UniValue& o : hashes.getValues()) {
            if (o.isObject()) {
                LogPrint("ws", "%s():%d - invalid obj\n", __func__, __LINE__);
                return INVALID_PARAMETER;
            }
            
            std::string strHash = o.get_str();
            uint256 hash(uint256S(strHash));
            CBlockIndex* pblockindex = NULL;

            {
                LOCK(cs_main);

                BlockMap::iterator mi = mapBlockIndex.find(hash);
                if (mi != mapBlockIndex.end()) {
                    pblockindex = (*mi).second;
                }
            }
            
            if (pblockindex == NULL) {
                LogPrint("ws", "%s():%d - block index not found for hash[%s]\n", __func__, __LINE__, strHash);
                return INVALID_PARAMETER;
            }

            std::string header;
            int ret = getheader(pblockindex, header);
            if (ret != OK)
            {
                return ret;
            }
    
            headers.push_back(header);
        }

        sendBlockHeaders(headers, WsEvent::MSG_RESPONSE, clientRequestId);

        return OK;
    }

    int sendTopQualityCertificatesForScid(const std::string& scIdString, const std::string& clientRequestId)
    {
        uint256 scId;
        scId.SetHex(scIdString);
        
        const CCoinsViewCache &view = *pcoinsTip;
        
        UniValue mempoolTopQualityCert(UniValue::VOBJ);
        UniValue chainTopQualityCert(UniValue::VOBJ);

        {
            LOCK(cs_main);
            if (!view.HaveSidechain(scId)) {
                LogPrint("ws", "%s():%d - sidechain id not found[%s]\n", __func__, __LINE__, scIdString);
                return INVALID_PARAMETER;
            }
        
            if (mempool.hasSidechainCertificate(scId))
            {
                const uint256& topQualCertHash = mempool.mapSidechains.at(scId).GetTopQualityCert()->second;
                const CScCertificate& topQualCert = mempool.mapCertificate.at(topQualCertHash).GetCertificate();
                const CAmount certFee = mempool.mapCertificate.at(topQualCertHash).GetFee();
                CDataStream ssCert(SER_NETWORK, PROTOCOL_VERSION);
                ssCert << topQualCert;
                std::string certHex = HexStr(ssCert.begin(), ssCert.end());
     
                mempoolTopQualityCert.push_back(Pair("quality", topQualCert.quality));
                mempoolTopQualityCert.push_back(Pair("epoch", topQualCert.epochNumber));
                mempoolTopQualityCert.push_back(Pair("certHash", topQualCertHash.GetHex()));
                mempoolTopQualityCert.push_back(Pair("fee", FormatMoney(certFee)));
            }

            CSidechain sidechainInfo;

            if (view.GetSidechain(scId, sidechainInfo) && !sidechainInfo.lastTopQualityCertHash.IsNull()) {
                const int topQualityCertQuality = sidechainInfo.lastTopQualityCertQuality;
                CScCertificate topQualCert;
                uint256 blockHash;

                chainTopQualityCert.push_back(Pair("quality", topQualityCertQuality));
                chainTopQualityCert.push_back(Pair("certHash", sidechainInfo.lastTopQualityCertHash.GetHex()));
                chainTopQualityCert.push_back(Pair("epoch", sidechainInfo.lastTopQualityCertReferencedEpoch));
            }
        }

        sendTopQualityCertificates(mempoolTopQualityCert, chainTopQualityCert, WsEvent::MSG_RESPONSE, clientRequestId);

        return OK;
    }

    int sendSidechainVersionsFromId(const UniValue& sidechainIds, const std::string& clientRequestId)
    {
        if (sidechainIds.size() > MAX_SIDECHAINS_REQUEST)
        {
            LogPrint("ws", "%s():%d - too many sidechain ids as argument %d (max is %d)\n",
                     __func__, __LINE__, sidechainIds.size(), MAX_SIDECHAINS_REQUEST);
            return INVALID_PARAMETER;
        }
            
        UniValue versions(UniValue::VARR);
            
        for (const UniValue& o : sidechainIds.getValues())
        {
            if (o.isObject())
            {
                LogPrint("ws", "%s():%d - invalid obj\n", __func__, __LINE__);
                return INVALID_PARAMETER;
            }
            
            std::string strScId = o.get_str();
            uint256 scId;
            scId.SetHex(strScId);

            {
                LOCK(cs_main);
                CCoinsViewCache view(pcoinsTip);

                if (!view.HaveSidechain(scId))
                {
                    LogPrint("ws", "%s():%d - sidechain id not found[%s]\n", __func__, __LINE__, strScId);
                    return INVALID_PARAMETER;
                }

                CSidechain sidechainInfo;

                if (view.GetSidechain(scId, sidechainInfo))
                {
                    UniValue sidechainEntry(UniValue::VOBJ);
                    sidechainEntry.push_back(Pair("scId", strScId));
                    sidechainEntry.push_back(Pair("version", sidechainInfo.fixedParams.version));

                    versions.push_back(sidechainEntry);
                }
            }
        }

        sendSidechainVersions(versions, WsEvent::MSG_RESPONSE, clientRequestId);

        return OK;
    }

    /* this is not necessary boost/beast is handling the pong automatically,
     * the client should send a ping message the server will reply with a pong message (same payload)
    void sendPong(std::string payload) {
        LogPrint("ws", "ping received... %s\n", payload);
        WsEvent* wse = new WsEvent(WsEvent::PONG);
        UniValue* rv = wse->getPayload();
        rv->pushKV("pingPayload", payload);
        wsq->push(wse);
    }*/

    void writeLoop()
    {
        localWs->text(localWs->got_text());

        while (!exit_rwhandler_thread_flag)
        {
            std::unique_lock<std::mutex> lk(writeMutex);
            // Wait upto 1 sec and check the queue in any case
            writeCV.wait_for(lk, std::chrono::seconds(1));

            WsEvent* wse;
            while (wsq.pop(wse) && wse != NULL)
            {
                std::string msg = wse->getPayload()->write();
                LogPrint("ws", "%s():%d - deleting %p\n", __func__, __LINE__, wse);
                delete wse;
                if (localWs->is_open())
                {
                    boost::beast::error_code ec;
                    localWs->write(boost::asio::buffer(msg), ec);

                    if (ec == websocket::error::closed)
                    {
                        LogPrint("ws", "%s():%d - err[%d]: %s\n", __func__, __LINE__,ec.value(), ec.message());
                        break;
                    }
                    else
                    if (ec.value() != boost::system::errc::success)
                    {
                        LogPrint("ws", "%s():%d - err[%d]: %s\n", __func__, __LINE__, ec.value(), ec.message());
                        break;
                    }
                    LogPrint("ws", "%s():%d - msg[%s] written on client socket\n", __func__, __LINE__, msg);
                }
                else
                {
                    LogPrint("ws", "%s():%d - ws is closed\n", __func__, __LINE__);
                    break;
                }
            }
        }
        LogPrint("ws", "%s():%d - write thread exit (this=%p)\n", __func__, __LINE__, this);
    }

    int parseClientMessage(WsEvent::WsRequestType& reqType, std::string& clientRequestId, std::string& outMsg) 
    {
        try
        {
            std::string msgType;
            std::string requestType;
            boost::beast::multi_buffer buffer;
            boost::beast::error_code ec;

            localWs->read(buffer, ec);
            if (ec == websocket::error::closed || ec == websocket::error::no_connection)
            {
                // graceful disconnection
                LogPrint("ws", "%s():%d - code[%d]: %s\n", __func__, __LINE__,ec.value(), ec.message());
                return READ_ERROR;
            }
            else
            if (ec.value() != boost::system::errc::success)
            {
                // any other error but success
                LogPrint("ws", "%s():%d - connection is open[%s], err[%d]: %s\n", __func__, __LINE__,
                    (localWs->is_open()?"Y":"N") , ec.value(), ec.message());
                return READ_ERROR;
            }
            LogPrint("ws", "%s():%d - client message received of size=%d\n", __func__, __LINE__, buffer.size());

            std::string msg = boost::beast::buffers_to_string(buffer.data());
            UniValue request;
            if (!request.read(msg)) {
                LogPrint("ws", "%s():%d - error parsing message from websocket: [%s]\n", __func__, __LINE__, msg);
                return INVALID_JSON_FORMAT;
            }

            msgType         = findFieldValue("msgType", request);
            clientRequestId = findFieldValue("requestId", request);
            requestType     = findFieldValue("requestType", request);

            if (msgType != std::to_string(WsEvent::MSG_REQUEST)) {
                // just log it and assume it is a request
                LogPrint("ws", "%s():%d - WARNING: msgType[%d] invalid, assuming MSG_REQUEST (%d)\n",
                    __func__, __LINE__, msgType, WsEvent::MSG_REQUEST);
            }

            if (requestType.empty()) {
                LogPrint("ws", "%s():%d - requestType empty: msg[%s]\n", __func__, __LINE__, msg);
                return INVALID_COMMAND;
            }
            LogPrint("ws", "%s():%d - got msg[%s]\n", __func__, __LINE__, msg);

            if (requestType == std::to_string(WsEvent::GET_SINGLE_BLOCK))
            {
                reqType = WsEvent::GET_SINGLE_BLOCK;
                if (clientRequestId.empty()) {
                    LogPrint("ws", "%s():%d - clientRequestId empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_REQID;
                }
                const UniValue& reqPayload = find_value(request, "requestPayload");
                if (reqPayload.isNull())
                {
                    LogPrint("ws", "%s():%d - requestPayload null: msg[%s]\n", __func__, __LINE__, msg);
                    return INVALID_JSON_FORMAT;
                }

                std::string param1 = findFieldValue("height", reqPayload);
                if (param1.empty())
                {
                    param1 = findFieldValue("hash", reqPayload);
                    if (param1.empty()) {
                        LogPrint("ws", "%s():%d - height/hash param null: msg[%s]\n", __func__, __LINE__, msg);
                        return MISSING_PARAMETER;
                    }
                    return sendBlockByHash(param1, clientRequestId);
                }
                return sendBlockByHeight(param1, clientRequestId);
            }

            if (requestType == std::to_string(WsEvent::GET_MULTIPLE_BLOCK_HASHES))
            {
                reqType = WsEvent::GET_MULTIPLE_BLOCK_HASHES;
                if (clientRequestId.empty()) {
                    LogPrint("ws", "%s():%d - clientRequestId empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_REQID;
                }
                const UniValue& reqPayload = find_value(request, "requestPayload");
                if (reqPayload.isNull())
                {
                    LogPrint("ws", "%s():%d - requestPayload null: msg[%s]\n", __func__, __LINE__, msg);
                    return INVALID_JSON_FORMAT;
                }

                std::string strLen = findFieldValue("limit", reqPayload);
                if (strLen.empty()) {
                    LogPrint("ws", "%s():%d - limit empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_PARAMETER;
                }

                std::string param1 = findFieldValue("afterHeight", reqPayload);
                if (param1.empty())
                {
                    param1 = findFieldValue("afterHash", reqPayload);
                    if (param1.empty()) {
                        LogPrint("ws", "%s():%d - afterHeight/afterHash empty: msg[%s]\n", __func__, __LINE__, msg);
                        return MISSING_PARAMETER;
                    }
                    return sendBlocksFromHash(param1, strLen, clientRequestId);
                }
                return sendBlocksFromHeight(param1, strLen, clientRequestId);
            }

            if (requestType == std::to_string(WsEvent::GET_NEW_BLOCK_HASHES))
            {
                reqType = WsEvent::GET_NEW_BLOCK_HASHES;
                if (clientRequestId.empty()) {
                    LogPrint("ws", "%s():%d - clientRequestId empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_REQID;
                }
                const UniValue& reqPayload = find_value(request, "requestPayload");
                if (reqPayload.isNull()) {
                    LogPrint("ws", "%s():%d - requestPayload null: msg[%s]\n", __func__, __LINE__, msg);
                    return INVALID_JSON_FORMAT;
                }

                const std::string& strLen = findFieldValue("limit", reqPayload);
                if (strLen.empty()) {
                    LogPrint("ws", "%s():%d - limit empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_PARAMETER;
                }

                const UniValue&  hashArray = find_value(reqPayload, "locatorHashes");
                if (hashArray.isNull()) {
                    LogPrint("ws", "%s():%d - locatorHash empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_PARAMETER;
                }

                const UniValue& hashes = hashArray.get_array();
                if (hashes.size()==0) {
                    LogPrint("ws", "%s():%d - hash array empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_PARAMETER;
                }
                return sendHashFromLocator(hashes, strLen, clientRequestId);
            }

            if (requestType == std::to_string(WsEvent::SEND_CERTIFICATE))
            {
                reqType = WsEvent::SEND_CERTIFICATE;
                outMsg.clear();

                if (clientRequestId.empty()) {
                    outMsg = "clientRequestId null";
                    LogPrint("ws", "%s():%d - %s: msg[%s]\n", __func__, __LINE__, outMsg, msg);
                    return MISSING_REQID;
                }

                const UniValue& reqPayload = find_value(request, "requestPayload");
                if (reqPayload.isNull() || !reqPayload.isObject()) {
                    outMsg = "requestPayload invalid or missing";
                    LogPrint("ws", "%s():%d - %s: msg[%s]\n", __func__, __LINE__, outMsg, msg);
                    return INVALID_JSON_FORMAT;
                }

                // sanity check, report error if unknown/duplicate key-value pairs
                std::set<std::string> setKeyArgs;

                static const std::set<std::string> validKeyArgs = {
                    "scid", "epochNumber", "quality", "fee", "endEpochCumCommTreeHash", "scProof",
                    "backwardTransfers", "vFieldElementCertificateField", "vBitVectorCertificateField",
                    "forwardTransferScFee", "mainchainBackwardTransferScFee"
                };

                for (const std::string& s : reqPayload.getKeys()) {
                    if (!validKeyArgs.count(s))
                        outMsg += " - unknown key: " + s;
                    if (!setKeyArgs.insert(s).second)
                        outMsg += " - duplicate key: " + s;
                }

                if (!outMsg.empty() ) {
                    LogPrint("ws", "%s():%d - %s: msg[%s]\n", __func__, __LINE__, outMsg, msg);
                    return INVALID_PARAMETER;
                }

                UniValue cmdParams(UniValue::VARR);

                std::string scidStr = findFieldValue("scid", reqPayload);
                if (scidStr.empty()) {
                    outMsg = "scid empty";
                    LogPrint("ws", "%s():%d - %s: msg[%s]\n", __func__, __LINE__, outMsg, msg);
                    return MISSING_PARAMETER;
                }    
                cmdParams.push_back(scidStr);

                const UniValue& epNumVal = find_value(reqPayload, "epochNumber");
                if (!epNumVal.isNum() || epNumVal.isNull() ) {
                    outMsg = "epochNumber missing or invalid";
                    LogPrint("ws", "%s():%d - %s: msg[%s]\n", __func__, __LINE__, outMsg, msg);
                    return MISSING_PARAMETER;
                }
                cmdParams.push_back(epNumVal.get_int());

                const UniValue& qualVal = find_value(reqPayload, "quality");
                if (!qualVal.isNum() || qualVal.isNull()) {
                    outMsg = "quality missing or invalid";
                    LogPrint("ws", "%s():%d - %s: msg[%s]\n", __func__, __LINE__, outMsg, msg);
                    return MISSING_PARAMETER;
                }
                cmdParams.push_back(qualVal.get_int());

                std::string endEpochCumScTxCommTreeRootStr = findFieldValue("endEpochCumCommTreeHash", reqPayload);
                if (endEpochCumScTxCommTreeRootStr.empty()) {
                    outMsg = "endEpochCumCommTreeHash empty";
                    LogPrint("ws", "%s():%d - %s: msg[%s]\n", __func__, __LINE__, outMsg, msg);
                    return MISSING_PARAMETER;
                }    
                cmdParams.push_back(endEpochCumScTxCommTreeRootStr);

                std::string scProofStr = findFieldValue("scProof", reqPayload);
                if (scProofStr.empty()) {
                    outMsg = "scProof empty";
                    LogPrint("ws", "%s():%d - %s: msg[%s]\n", __func__, __LINE__, outMsg, msg);
                    return MISSING_PARAMETER;
                }    
                cmdParams.push_back(scProofStr);

                const UniValue& bwtParam = find_value(reqPayload, "backwardTransfers");
                if (bwtParam.isNull()) {
                    outMsg = "backwardTransfers missing (can be an empty array)";
                    LogPrint("ws", "%s():%d - %s: msg[%s]\n", __func__, __LINE__, outMsg, msg);
                    return MISSING_PARAMETER;
                }

                const UniValue& bwtArray = bwtParam.get_array();
                if (bwtArray.size() == 0) {
                    // can also be empty
                    LogPrint("ws", "%s():%d - bwtArray empty: msg[%s]\n", __func__, __LINE__, msg);
                }
                cmdParams.push_back(bwtArray);

                const UniValue& ftScFeeVal = find_value(reqPayload, "forwardTransferScFee");

                // can be null, it is optional. The default is set in the cmd
                if (!ftScFeeVal.isNull()) {
                    try {
                        cmdParams.push_back(ftScFeeVal);
                    } catch (const UniValue& e) {
                        dumpUniValueError(e, outMsg);
                        return INVALID_PARAMETER;
                    } catch (...) {
                        LogPrint("ws", "%s():%d - Generic exception\n", __func__, __LINE__);
                        return INVALID_PARAMETER;
                    }
                }

                const UniValue& mbtrScFeeVal = find_value(reqPayload, "mainchainBackwardTransferScFee");

                // can be null, it is optional. The default is set in the cmd
                if (!mbtrScFeeVal.isNull()) {
                    try {
                        cmdParams.push_back(mbtrScFeeVal);
                    } catch (const UniValue& e) {
                        dumpUniValueError(e, outMsg);
                        return INVALID_PARAMETER;
                    } catch (...) {
                        LogPrint("ws", "%s():%d - Generic exception\n", __func__, __LINE__);
                        return INVALID_PARAMETER;
                    }
                }

                const UniValue& feeVal = find_value(reqPayload, "fee");

                // can be null, it is optional. The default is set in the cmd
                if (!feeVal.isNull()) {
                    try {
                        cmdParams.push_back(feeVal);
                    } catch (const UniValue& e) {
                        dumpUniValueError(e, outMsg);
                        return INVALID_PARAMETER;
                    } catch (...) {
                        LogPrint("ws", "%s():%d - Generic exception\n", __func__, __LINE__);
                        return INVALID_PARAMETER;
                    }
                }

                // optional, can be null
                const UniValue& cfe = find_value(reqPayload, "vFieldElementCertificateField");
                if (!cfe.isNull())
                {
                    // Push the default value for fromAddress param(previous parameter).
                    cmdParams.push_back("");

                    const UniValue& vCfe = cfe.get_array();
                    LogPrint("ws", "%s():%d - adding vFieldElementCertificateField, sz(%d): msg[%s]\n", __func__, __LINE__, vCfe.size(), msg);
                    cmdParams.push_back(vCfe);
                }

                // optional, can be null
                const UniValue& cmt = find_value(reqPayload, "vBitVectorCertificateField");
                if (!cmt.isNull())
                {
                    const UniValue& vCmt = cmt.get_array();
                    LogPrint("ws", "%s():%d - adding vBitVectorCertificateField, sz(%d): msg[%s]\n", __func__, __LINE__, vCmt.size(), msg);
                    cmdParams.push_back(vCmt);
                }

                return sendCertificate(cmdParams, clientRequestId, outMsg);
            }
            
            if (requestType == std::to_string(WsEvent::GET_MULTIPLE_BLOCK_HEADERS))
            {
                reqType = WsEvent::GET_MULTIPLE_BLOCK_HEADERS;
                if (clientRequestId.empty()) {
                    LogPrint("ws", "%s():%d - clientRequestId empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_REQID;
                }
                const UniValue& reqPayload = find_value(request, "requestPayload");
                if (reqPayload.isNull())
                {
                    LogPrint("ws", "%s():%d - requestPayload null: msg[%s]\n", __func__, __LINE__, msg);
                    return INVALID_JSON_FORMAT;
                }

                const UniValue&  hashArray = find_value(reqPayload, "hashes");
                if (hashArray.isNull()) {
                    LogPrint("ws", "%s():%d - locatorHash empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_PARAMETER;
                }

                return sendHeadersFromHashes(hashArray, clientRequestId);
            }
            
            if (requestType == std::to_string(WsEvent::GET_TOP_QUALITY_CERTIFICATES))
            {
                reqType = WsEvent::GET_TOP_QUALITY_CERTIFICATES;
                if (clientRequestId.empty()) {
                    LogPrint("ws", "%s():%d - clientRequestId empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_REQID;
                }
                const UniValue& reqPayload = find_value(request, "requestPayload");
                if (reqPayload.isNull())
                {
                    LogPrint("ws", "%s():%d - requestPayload null: msg[%s]\n", __func__, __LINE__, msg);
                    return INVALID_JSON_FORMAT;
                }

                std::string scId = findFieldValue("scid", reqPayload);
                if (scId.empty())
                {
                    LogPrint("ws", "%s():%d - scid empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_PARAMETER;
                }

                return sendTopQualityCertificatesForScid(scId, clientRequestId);
            }

            if (requestType == std::to_string(WsEvent::GET_SIDECHAIN_VERSIONS))
            {
                reqType = WsEvent::GET_SIDECHAIN_VERSIONS;
                if (clientRequestId.empty()) {
                    LogPrint("ws", "%s():%d - clientRequestId empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_REQID;
                }
                const UniValue& reqPayload = find_value(request, "requestPayload");
                if (reqPayload.isNull())
                {
                    LogPrint("ws", "%s():%d - requestPayload null: msg[%s]\n", __func__, __LINE__, msg);
                    return INVALID_JSON_FORMAT;
                }

                const UniValue&  scIdArray = find_value(reqPayload, "sidechainIds");
                if (scIdArray.isNull()) {
                    LogPrint("ws", "%s():%d - sidechainIds empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_PARAMETER;
                }

                const UniValue& scIds = scIdArray.get_array();
                if (scIds.size()==0) {
                    LogPrint("ws", "%s():%d - sidechain ID array empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_PARAMETER;
                }

                return sendSidechainVersionsFromId(scIds, clientRequestId);
            }

            // if we are here that means it is no valid request type, and reqType is an enum defaulting to 255
            *((int*)(&reqType)) = std::stoi(requestType);

            outMsg = "Invalid RequestType Number: " + requestType; 
            LogPrint("ws", "%s():%d - %s: msg[%s]\n", __func__, __LINE__, outMsg, msg);

            return INVALID_COMMAND;
        }
        catch (std::runtime_error const& rte)
        {
            // most probably thrown by UniValue lib, that means 
            // json is formally correct but not compliant with protocol
            outMsg = rte.what();
            return INVALID_COMMAND;
        }
        catch (std::exception const& e)
        {
            outMsg = e.what();
            LogPrint("ws", "%s():%d - %s\n", __func__, __LINE__, outMsg);
            return READ_ERROR;
        }
    }

    void readLoop()
    {
        while (!exit_rwhandler_thread_flag)
        {
            WsEvent::WsRequestType reqType = WsEvent::REQ_UNDEFINED;
            std::string clientRequestId = "";
            std::string outMsg;
            int res = parseClientMessage(reqType, clientRequestId, outMsg);
            if (res == READ_ERROR)
            {
                LogPrint("ws", "%s():%d - websocket closed exit reading loop\n", __func__, __LINE__);
                break;
            }

            if (res != OK)
            {
                std::string msgError = "On requestType[" + std::to_string(reqType) + "]: ";
                switch (res)
                {
                case INVALID_PARAMETER:
                    msgError += "Invalid parameter";
                    break;
                case MISSING_PARAMETER:
                    msgError += "Missing parameter";
                    break;
                case MISSING_REQID:
                    msgError += "Missing requestId";
                    break;
                case INVALID_COMMAND:
                    msgError += "Invalid command";
                    break;
                case INVALID_JSON_FORMAT:
                    msgError += "Invalid JSON format";
                    break;
                default:
                    msgError += "Generic error";
                }
                if (!outMsg.empty())
                    msgError += " - Details: " + outMsg;

                // Send a message error to the client:  type = -1
                WsEvent* wse = new WsEvent(WsEvent::MSG_ERROR);
                LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
                UniValue* rv = wse->getPayload();
                if (!clientRequestId.empty())
                    rv->pushKV("requestId", clientRequestId);
                rv->pushKV("errorCode", res);
                rv->pushKV("message", msgError);
                write(wse);
            }
        }
        LogPrint("ws", "%s():%d - exit reading loop\n", __func__, __LINE__);
    }

public:

    enum CLIENT_PROCMSG_CODE {
        OK = 0,
        MISSING_PARAMETER = 1,
        INVALID_COMMAND = 2,
        INVALID_JSON_FORMAT = 3,
        INVALID_PARAMETER = 4,
        MISSING_REQID = 5,
        READ_ERROR = 99
    };

    unsigned int t_id = 0;

    WsHandler() {}
    ~WsHandler() {
        LogPrint("ws", "%s():%d - called this=%p\n", __func__, __LINE__, this);
    }

    WsHandler & operator=(const WsHandler& wsh) = delete;
    WsHandler(const WsHandler& wsh) = delete;

    static void getPeerIdentity(const tcp::socket& socket, std::string& id)
    { 
        auto peer = socket.remote_endpoint();
        std::string addr = peer.address().to_string();
        std::string port = std::to_string(peer.port());
        id = addr + ":" + port;
    }

    void do_session(tcp::socket& socket, unsigned int t_id)
    {
        // will be referenced when shutting down in order not destroying ptr while executing this thread
        boost::shared_ptr<WsHandler> thisRef;

        this->t_id = t_id;

        try
        {
            localWs.reset(new websocket::stream<tcp::socket> { std::move(socket) });
            LogPrint("ws", "%s():%d - allocated localWs %p\n", __func__, __LINE__, localWs.get());

            localWs->set_option(
                websocket::stream_base::decorator(
                    [](websocket::response_type& res)
                        {
                            res.set(http::field::server,
                            std::string(BOOST_BEAST_VERSION_STRING) + " Horizen-sidechain-connector");
                        }));

            localWs->control_callback(
                [](websocket::frame_type kind, boost::string_view payload)
                {
                    if (kind == websocket::frame_type::ping)
                    {
                        std::string payl(payload);
                        LogPrint("ws", "%s():%d - ping received... payload[%s]\n", __func__, __LINE__, payl);
                    }
                    // Do something with the payload
                    boost::ignore_unused(kind, payload);
                });

            localWs->accept();

            std::thread write_t(&WsHandler::writeLoop, this);
            readLoop();
            exit_rwhandler_thread_flag = true;
            write_t.join();
            socket.close();
        }
        catch (boost::system::system_error const& se)
        {
            if (se.code() != websocket::error::closed)
            {
                LogPrint("ws", "%s():%d - boost error %s\n", __func__, __LINE__, se.code().message());
            }
            LogPrint("ws", "%s():%d - websocket close\n", __func__, __LINE__);
        }
        catch (std::exception const& e)
        {
            LogPrint("ws", "%s():%d - error: %s\n", __func__, __LINE__, std::string(e.what()));
        }
        LogPrint("ws", "%s():%d - exit thread final\n", __func__, __LINE__);
        {
            std::unique_lock<std::mutex> lck(wsmtx);
            this->shutdown();
            tot_connections--;
            LogPrint("ws", "%s():%d - connection[%u] closed: tot[%d]\n", __func__, __LINE__, t_id, tot_connections);

            auto it = listWsHandler.begin();
            while (it != listWsHandler.end())
            {
                if (this == (*it).get() )
                {
                    thisRef = (*it);
                    LogPrint("ws", "%s():%d - removing handler obj from list\n", __func__, __LINE__);
                    listWsHandler.erase(it++);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    void send_tip_update(int height, const std::string& strHash, const std::string& blockHex)
    {
        sendBlockEvent(height, strHash, blockHex, WsEvent::UPDATE_TIP);
    }

    void shutdown()
    {
        try
        {
            exit_rwhandler_thread_flag = true;
            if (this->localWs)
            {
                LogPrint("ws", "%s():%d - closing socket\n", __func__, __LINE__);
                this->localWs->next_layer().close();
            }
        }
        catch (std::exception const& e)
        {
            LogPrint("ws", "%s():%d - error: %s\n", __func__, __LINE__, e.what());
        }
    }
};


static int getblock(const CBlockIndex *pindex, std::string& strHex)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    {
        LOCK(cs_main);
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex)) {
            LogPrint("ws", "%s():%d - error: could not read block from disk\n", __func__, __LINE__);
            return WsHandler::READ_ERROR;
        }
        ss << block;
        strHex = HexStr(ss.begin(), ss.end());
    }
    return WsHandler::OK;
}


static int getheader(const CBlockIndex *pindex, std::string& strHex)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    {
        LOCK(cs_main);
        ss << pindex->GetBlockHeader();
        strHex = HexStr(ss.begin(), ss.end());
    }
    return WsHandler::OK;
}


static void ws_updatetip(const CBlockIndex *pindex)
{
    std::string strHex;
    int ret = getblock(pindex, strHex);
    if (ret != WsHandler::OK)
    {
        // should not happen
        LogPrint("ws", "%s():%d - ERROR: can not update tip\n", __func__, __LINE__);
        return;
    }
    {
        std::unique_lock<std::mutex> lck(wsmtx);
        if (listWsHandler.size() )
        {
            LogPrint("ws", "%s():%d - update tip loop on ws clients\n", __func__, __LINE__);
            auto it = listWsHandler.begin();
            while (it != listWsHandler.end())
            {
                LogPrint("ws", "%s():%d - call wshandler_send_tip_update to connection[%u]\n", __func__, __LINE__, (*it)->t_id);
                (*it)->send_tip_update(pindex->nHeight, pindex->GetBlockHash().GetHex(), strHex);
                ++it;
            }
        }
        else
        {
            LogPrint("ws", "%s():%d - there are no connected ws clients\n", __func__, __LINE__);
        }
    }
}


//------------------------------------------------------------------------------

static tcp::acceptor* acceptor = NULL;
void ws_main(std::string strAddressWs, int portWs)
{
    std::string peerId;

    try {
        LogPrint("ws", "start websocket service address: %s \n", strAddressWs);
        LogPrint("ws", "start websocket service port: %s \n", portWs);

        auto const address = boost::asio::ip::make_address(strAddressWs);
        auto const port = static_cast<unsigned short>(portWs);

        net::io_context ioc { 1 };
        tcp::acceptor _acceptor = tcp::acceptor { ioc, { address, port } };
        acceptor = &_acceptor;
        LogPrint("ws", "%s():%d - assigned %p\n", __func__, __LINE__, acceptor);
        unsigned int t_id = 0;

        while (!exit_ws_thread)
        {
            tcp::socket socket { ioc };

            // TODO //  - possible DoS, limit number of connections
            LogPrint("ws", "%s():%d - waiting to get a new connection\n", __func__, __LINE__);
            acceptor->accept(socket);
            WsHandler::getPeerIdentity(socket, peerId);

            boost::shared_ptr<WsHandler> w(new WsHandler());
            LogPrint("ws", "%s():%d - allocated ws handler %p\n", __func__, __LINE__, w.get());

            std::thread { std::bind(&WsHandler::do_session, w.get(),
                    std::move(socket), t_id) }.detach();
            {
                std::unique_lock<std::mutex> lck(wsmtx);
                listWsHandler.push_back(w);
                tot_connections++;
            }
            t_id++;

            LogPrint("ws", "%s():%d - new connection[%u] received from %s: tot[%d]\n",
                __func__, __LINE__, t_id, peerId, tot_connections);
        }
    }
    catch (const std::exception& e)
    {
        LogPrint("ws", "%s():%d - error: %s\n", __func__, __LINE__, std::string(e.what()));
    }
    LogPrint("ws", "%s():%d - websocket service stop\n", __func__, __LINE__);
}

static void shutdown()
{
    if (listWsHandler.size() != 0)
    {
        // list objects are smart_ptrs and will clean up when they go out of scope
        LogPrint("ws", "%s():%d - shutdown %d threads/sockets thread... \n", __func__, __LINE__, listWsHandler.size());
        std::unique_lock<std::mutex> lck(wsmtx);
        auto it = listWsHandler.begin();
        while (it != listWsHandler.end())
        {
            LogPrint("ws", "%s():%d - calling shutdown on handler connection[%u]\n", __func__, __LINE__, (*it)->t_id);
            (*it)->shutdown();
            ++it;
        }
    }
}

bool StartWsServer()
{
    try
    {
        // websocket is still unauthenticated, care must be taken to not expose it publicly
        std::string strAddress = GetArg("-wsaddress", "127.0.0.1");
        int port = GetArg("-wsport", 8888);

        ws_thread = boost::thread(ws_main, strAddress, port);
        ws_thread.detach();

        wsNotificationInterface.reset(new WsNotificationInterface());
        LogPrint("ws", "%s():%d - starting server at %s:%d, allocated notif if %p\n",
            __func__, __LINE__, strAddress, port, wsNotificationInterface.get());
        RegisterValidationInterface(wsNotificationInterface.get());
    }
    catch (const std::exception& e)
    {
        LogPrint("ws", "%s():%d - error: %s\n", __func__, __LINE__, std::string(e.what()));
        return false;
    }
    return true;
}

bool StopWsServer()
{
    try
    {
        shutdown();
        exit_ws_thread = true;
        if (acceptor != NULL)
        {
            LogPrint("ws", "%s():%d - closing static acceptor %p\n", __func__, __LINE__, acceptor);
            acceptor->close();
            acceptor = NULL;
        }
        if (wsNotificationInterface.get() != NULL)
        {
            UnregisterValidationInterface(wsNotificationInterface.get());
        }
    }
    catch (const std::exception& e)
    {
        LogPrint("ws", "%s():%d - error: %s\n", __func__, __LINE__, e.what());
        return false;
    }
    return true;
}



