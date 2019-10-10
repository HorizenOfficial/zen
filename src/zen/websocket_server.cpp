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

using tcp = boost::asio::ip::tcp;

namespace websocket = boost::beast::websocket;
namespace http = boost::beast::http;

namespace net = boost::asio;
net::io_context ioc;

static int MAX_BLOCKS_REQUEST = 50;
static int tot_connections = 0;

class WsNotificationInterface;
class WsHandler;

static int getblock(const CBlockIndex *pindex, std::string& blockHexStr);
static void ws_updatetip(const CBlockIndex *pindex);

static boost::shared_ptr<WsNotificationInterface> wsNotificationInterface;
static std::list< boost::shared_ptr<WsHandler> > listWsHandler;

std::atomic<bool> exit_ws_thread{false};
boost::thread ws_thread;
std::mutex wsmtx;

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
        payload.push_back(Pair("msgType", type));
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
    boost::shared_ptr< websocket::stream<tcp::socket>> localWs;
    boost::lockfree::queue<WsEvent*, boost::lockfree::capacity<1024>> wsq;
    std::atomic<bool> exit_rwhandler_thread_flag { false };

    void sendBlockEvent(int height, const std::string& strHash, const std::string& blockHex, WsEvent::WsEventType eventType)
    {
        // Send a message to the client:  type = eventType
        WsEvent* wse = new WsEvent(WsEvent::MSG_EVENT);
        LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
        rspPayload.push_back(Pair("height", height));
        rspPayload.push_back(Pair("hash", strHash));
        rspPayload.push_back(Pair("block", blockHex));

        UniValue* rv = wse->getPayload();
        rv->push_back(Pair("eventType", eventType));
        rv->push_back(Pair("eventPayload", rspPayload));
        wsq.push(wse);
    }

    void sendBlock(int height, const std::string& strHash, const std::string& blockHex,
            WsEvent::WsMsgType msgType, std::string clientRequestId = "")
    {
        // Send a message to the client:  type = eventType
        WsEvent* wse = new WsEvent(msgType);
        LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
        rspPayload.push_back(Pair("height", height));
        rspPayload.push_back(Pair("hash", strHash));
        rspPayload.push_back(Pair("block", blockHex));

        UniValue* rv = wse->getPayload();
        if (!clientRequestId.empty())
            rv->push_back(Pair("requestId", clientRequestId));
        rv->push_back(Pair("responsePayload", rspPayload));
        wsq.push(wse);
    }

    void sendHashes(int height, std::list<CBlockIndex*>& listBlock,
            WsEvent::WsMsgType msgType, std::string clientRequestId = "")
    {
        // Send a message to the client:  type = eventType
        WsEvent* wse = new WsEvent(msgType);
        LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
        rspPayload.push_back(Pair("height", height));

        UniValue hashes(UniValue::VARR);
        std::list<CBlockIndex*>::iterator it = listBlock.begin();
        while (it != listBlock.end()) {
            CBlockIndex* blockIndexIterator = *it;
            hashes.push_back(blockIndexIterator->GetBlockHash().GetHex());
            ++it;
        }
        rspPayload.push_back(Pair("hashes", hashes));

        UniValue* rv = wse->getPayload();
        if (!clientRequestId.empty())
            rv->push_back(Pair("requestId", clientRequestId));
        rv->push_back(Pair("responsePayload", rspPayload));
        wsq.push(wse);
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


    int sendHashFromLocator(UniValue& hashes, std::string strLen, const std::string& clientRequestId) {
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
        CBlockIndex* pblockindexStart;
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
                }
            }
            if (pblockindex == NULL)
            {
                LogPrint("ws", "%s():%d - block index not found for hash[%s]\n", __func__, __LINE__, o.get_str());
                return INVALID_PARAMETER;
            }
            if (pblockindex->nHeight > lastH)
            {
                lastH = pblockindex->nHeight;
                pblockindexStart = pblockindex;
            }
        }
        listBlock.push_back(pblockindexStart);
        {
            LOCK(cs_main);
            CBlockIndex* nextBlock = chainActive.Next(pblockindexStart);
            if (nextBlock == NULL)
            {
                LogPrint("ws", "%s():%d - next block index not found\n", __func__, __LINE__);
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


    /* this is not necessary boost/beast is handling the pong automatically,
     * the client should send a ping message the server will reply with a pong message (same payload)
    void sendPong(std::string payload) {
        LogPrint("ws", "ping received... %s\n", payload);
        WsEvent* wse = new WsEvent(WsEvent::PONG);
        UniValue* rv = wse->getPayload();
        rv->push_back(Pair("pingPayload", payload));
        wsq->push(wse);
    }*/

    void writeLoop()
    {
        localWs->text(localWs->got_text());

        while (!exit_rwhandler_thread_flag)
        {
            if (!wsq.empty())
            {
                WsEvent* wse;
                if (wsq.pop(wse) && wse != NULL)
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
                else
                {
                    // should never happen because pop is false only when queue is empty
                    LogPrint("ws", "%s():%d - could not pop!\n", __func__, __LINE__);
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        LogPrint("ws", "%s():%d - write thread exit (this=%p)\n", __func__, __LINE__, this);
    }

    int parseClientMessage(WsEvent::WsRequestType& reqType, std::string& clientRequestId) 
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
                LogPrint("ws", "%s():%d - err[%d]: %s\n", __func__, __LINE__,ec.value(), ec.message());
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

                std::string strLen = findFieldValue("limit", reqPayload);
                if (strLen.empty()) {
                    LogPrint("ws", "%s():%d - limit empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_PARAMETER;
                }

                const UniValue&  hashArray = find_value(reqPayload, "locatorHashes");
                if (hashArray.isNull()) {
                    LogPrint("ws", "%s():%d - locatorHash empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_PARAMETER;
                }

                UniValue hashes = hashArray.get_array();
                if (hashes.size()==0) {
                    LogPrint("ws", "%s():%d - hash array empty: msg[%s]\n", __func__, __LINE__, msg);
                    return MISSING_PARAMETER;
                }
                return sendHashFromLocator(hashes, strLen, clientRequestId);
            }

            // no valid request type
            LogPrint("ws", "%s():%d - invalid requestType: msg[%s]\n", __func__, __LINE__, msg);
            return INVALID_COMMAND;
        }
        catch (std::runtime_error const& rte)
        {
            // most probably thrown by UniValue lib, that means 
            // json is formally correct but not compliant with protocol
            LogPrint("ws", "%s():%d - %s\n", __func__, __LINE__, rte.what());
            return INVALID_COMMAND;
        }
        catch (std::exception const& e)
        {
            LogPrint("ws", "%s():%d - %s\n", __func__, __LINE__, e.what());
            return READ_ERROR;
        }
    }

    void readLoop()
    {
        while (!exit_rwhandler_thread_flag)
        {
            WsEvent::WsRequestType reqType = WsEvent::REQ_UNDEFINED;
            std::string clientRequestId = "";
            int res = parseClientMessage(reqType, clientRequestId);
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
                // Send a message error to the client:  type = -1
                WsEvent* wse = new WsEvent(WsEvent::MSG_ERROR);
                LogPrint("ws", "%s():%d - allocated %p\n", __func__, __LINE__, wse);
                UniValue* rv = wse->getPayload();
                if (!clientRequestId.empty())
                    rv->push_back(Pair("requestId", clientRequestId));
                rv->push_back(Pair("errorCode", res));
                rv->push_back(Pair("message", msgError));
                wsq.push(wse);
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
        std::string strAddress = GetArg("-wsaddress", "127.0.0.1");
        int port = GetArg("-wsport", 8888);

        ws_thread = boost::thread(ws_main, strAddress, port);
        ws_thread.detach();

        wsNotificationInterface.reset(new WsNotificationInterface());
        LogPrint("ws", "%s():%d - allocated notif if %p\n", __func__, __LINE__, wsNotificationInterface.get());
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



