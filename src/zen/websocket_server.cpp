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

static WsNotificationInterface* wsNotificationInterface = NULL;
std::list<WsHandler*> listWsHandler;
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

	explicit WsEvent(WsMsgType xn): type(xn), payload(NULL) {
        payload = new UniValue(UniValue::VOBJ);
        LogPrintf("%s():%d allocated %p\n", __func__, __LINE__, payload);
        payload->push_back(Pair("msgType", type));
	}
	WsEvent & operator=(const WsEvent& ws) = delete;
	WsEvent(const WsEvent& ws) = delete;
	~WsEvent(){
        LogPrintf("%s():%d deleting %p\n", __func__, __LINE__, payload);
        delete payload;
    };

	UniValue* getPayload() {
		return payload;
	}

private:
	WsMsgType type;
	UniValue* payload;
};



class WsHandler
{
private:
	websocket::stream<tcp::socket>* localWs = NULL;
	boost::lockfree::queue<WsEvent*, boost::lockfree::capacity<1024>>* wsq;
	std::atomic<bool> exit_rwhandler_thread_flag { false };

	void sendBlockEvent(int height, const std::string& strHash, const std::string& blockHex, WsEvent::WsEventType eventType)
    {
		// Send a message to the client:  type = eventType
		WsEvent* wse = new WsEvent(WsEvent::MSG_EVENT);
        LogPrintf("%s():%d allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
		rspPayload.push_back(Pair("height", height));
		rspPayload.push_back(Pair("hash", strHash));
		rspPayload.push_back(Pair("block", blockHex));

		UniValue* rv = wse->getPayload();
        rv->push_back(Pair("eventType", eventType));
        rv->push_back(Pair("eventPayload", rspPayload));
		wsq->push(wse);
	}

	void sendBlock(int height, const std::string& strHash, const std::string& blockHex,
			WsEvent::WsMsgType msgType, std::string clientRequestId = "")
    {
		// Send a message to the client:  type = eventType
		WsEvent* wse = new WsEvent(msgType);
        LogPrintf("%s():%d allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
		rspPayload.push_back(Pair("height", height));
		rspPayload.push_back(Pair("hash", strHash));
		rspPayload.push_back(Pair("block", blockHex));

		UniValue* rv = wse->getPayload();
		if (!clientRequestId.empty())
			rv->push_back(Pair("requestId", clientRequestId));
        rv->push_back(Pair("responsePayload", rspPayload));
		wsq->push(wse);
	}

	void sendHashes(int height, std::list<CBlockIndex*>& listBlock,
			WsEvent::WsMsgType msgType, std::string clientRequestId = "")
    {
		// Send a message to the client:  type = eventType
		WsEvent* wse = new WsEvent(msgType);
        LogPrintf("%s():%d allocated %p\n", __func__, __LINE__, wse);
        UniValue rspPayload(UniValue::VOBJ);
		rspPayload.push_back(Pair("height", height));

		UniValue hashes(UniValue::VARR);
		std::list<CBlockIndex*>::iterator it = listBlock.begin();
		while (it != listBlock.end()) {
			CBlockIndex* blockIndexIterator = *it;
			hashes.push_back(blockIndexIterator->GetBlockHash().GetHex());
			it++;
		}
		rspPayload.push_back(Pair("hashes", hashes));

		UniValue* rv = wse->getPayload();
		if (!clientRequestId.empty())
			rv->push_back(Pair("requestId", clientRequestId));
        rv->push_back(Pair("responsePayload", rspPayload));
		wsq->push(wse);
	}

	int getHashByHeight(std::string height, std::string& strHash)
    {
		int nHeight = -1;
		try {
			nHeight = std::stoi(height);
		} catch (const std::exception &e) {
			return INVALID_PARAMETER;
		}
		if (nHeight < 0 || nHeight > chainActive.Height()) {
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
			return r;
		return sendBlockByHash(strHash, clientRequestId);
	}

	int sendBlockByHash(const std::string& strHash, const std::string& clientRequestId) {
		CBlockIndex* pblockindex;
		{
			LOCK(cs_main);
			uint256 hash(uint256S(strHash));
			pblockindex = mapBlockIndex[hash];
			if (pblockindex == NULL) {
				return INVALID_PARAMETER;
			}
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
			return INVALID_PARAMETER;
		}
		if (len < 1)
			return INVALID_PARAMETER;
		if (len > MAX_BLOCKS_REQUEST)
			return INVALID_PARAMETER;

		std::list<CBlockIndex*> listBlock;
		CBlockIndex* pblockindex;
		{
			LOCK(cs_main);
			uint256 hash(uint256S(strHash));
			pblockindex = mapBlockIndex[hash];
			if (pblockindex == NULL) {
				return INVALID_PARAMETER;
			}
			CBlockIndex* nextBlock = chainActive.Next(pblockindex);
			if (nextBlock == NULL)
				return INVALID_PARAMETER;
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
			return INVALID_PARAMETER;
		}
		if (len < 1)
			return INVALID_PARAMETER;
		if (len > MAX_BLOCKS_REQUEST)
			return INVALID_PARAMETER;

		std::list<CBlockIndex*> listBlock;
		CBlockIndex* pblockindexStart;
		int lastH = 0;
		for (const UniValue& o : hashes.getValues()) {
				if (o.isObject()) {
					return INVALID_PARAMETER;
				}
				uint256 hash(uint256S(o.get_str()));
				{
					LOCK(cs_main);
					CBlockIndex* pblockindex = mapBlockIndex[hash];
			        if (pblockindex == NULL) {
				        return INVALID_PARAMETER;
			        }
					if (pblockindex->nHeight > lastH) {
						lastH = pblockindex->nHeight;
						pblockindexStart = mapBlockIndex[hash];
					}
				}
		}
		listBlock.push_back(pblockindexStart);
		{
			LOCK(cs_main);
			CBlockIndex* nextBlock = chainActive.Next(pblockindexStart);
			if (nextBlock == NULL)
				return INVALID_PARAMETER;
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
		LogPrintf("ping received... %s\n", payload);
		WsEvent* wse = new WsEvent(WsEvent::PONG);
		UniValue* rv = wse->getPayload();
		rv->push_back(Pair("pingPayload", payload));
		wsq->push(wse);
	}*/

	void writeLoop() {
		localWs->text(localWs->got_text());

		while (!exit_rwhandler_thread_flag) {
			if (!wsq->empty()) {
				WsEvent* wse;
				if (wsq->pop(wse)) {
					std::string msg = wse->getPayload()->write();
                    LogPrintf("%s():%d deleting %p\n", __func__, __LINE__, wse);
					delete wse;
					if (localWs->is_open()) {
						boost::beast::error_code ec;
						localWs->write(boost::asio::buffer(msg), ec);

						if (ec == websocket::error::closed) {
							LogPrintf(
									"wshandler:writeLoop: websocket error closed \n");
							break;
						}
					} else {
						LogPrintf("wshandler:writeLoop: ws is closed \n");
						break;
					}
				}
			} else {
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}
		LogPrintf("wshandler:writeLoop: write thread exit\n");
	}

	int parseClientMessage(WsEvent::WsRequestType& reqType, std::string& clientRequestId) {
		try {
			std::string msgType;
			std::string requestType;
			boost::beast::multi_buffer buffer;
			boost::beast::error_code ec;

			localWs->read(buffer, ec);
			if (ec == websocket::error::closed || ec == websocket::error::no_connection) {
				LogPrintf("wshandler:parseClientMessage websocket error close \n");
				return READ_ERROR;
			}

			std::string msg = boost::beast::buffers_to_string(buffer.data());
			UniValue request;
			if (!request.read(msg)) {
				LogPrintf("wshandler:parseClientMessage error parsing message from websocket: %s \n", msg);
				return INVALID_JSON_FORMAT;
			}

			msgType = findFieldValue("msgType", request);

			clientRequestId = findFieldValue("requestId", request);

			requestType = findFieldValue("requestType", request);
			if (requestType.empty()) {
				return INVALID_COMMAND;
			}

			if (requestType == std::to_string(WsEvent::GET_SINGLE_BLOCK))
            {
				reqType = WsEvent::GET_SINGLE_BLOCK;
				if (clientRequestId.empty()) {
					return MISSING_MSGID;
				}
                const UniValue& reqPayload = find_value(request, "requestPayload");
                if (reqPayload.isNull()) {
				    LogPrintf("wshandler:parseClientMessage error parsing message from websocket: %s \n", msg);
				    return INVALID_JSON_FORMAT;
                }

				std::string param1 = findFieldValue("height", reqPayload);
				if (param1.empty()) {
					param1 = findFieldValue("hash", reqPayload);
					if (param1.empty()) {
						return MISSING_PARAMETER; //
					}
					return sendBlockByHash(param1, clientRequestId);
				}
				return sendBlockByHeight(param1, clientRequestId);
			}

			if (requestType == std::to_string(WsEvent::GET_MULTIPLE_BLOCK_HASHES))
            {
				reqType = WsEvent::GET_MULTIPLE_BLOCK_HASHES;
				if (clientRequestId.empty()) {
					return MISSING_MSGID;
				}
                const UniValue& reqPayload = find_value(request, "requestPayload");
                if (reqPayload.isNull()) {
				    LogPrintf("wshandler:parseClientMessage error parsing message from websocket: %s \n", msg);
				    return INVALID_JSON_FORMAT;
                }

				std::string strLen = findFieldValue("limit", reqPayload);
				if (strLen.empty()) {
					return MISSING_PARAMETER;
				}

				std::string param1 = findFieldValue("afterHeight", reqPayload);
				if (param1.empty()) {
					param1 = findFieldValue("afterHash", reqPayload);
					if (param1.empty()) {
						return MISSING_PARAMETER; //
					}
					return sendBlocksFromHash(param1, strLen, clientRequestId);
				}
				return sendBlocksFromHeight(param1, strLen, clientRequestId);
			}

			if (requestType == std::to_string(WsEvent::GET_NEW_BLOCK_HASHES))
            {
				reqType = WsEvent::GET_NEW_BLOCK_HASHES;
				if (clientRequestId.empty()) {
					return MISSING_MSGID;
				}
                const UniValue& reqPayload = find_value(request, "requestPayload");
                if (reqPayload.isNull()) {
				    LogPrintf("wshandler:parseClientMessage error parsing message from websocket: %s \n", msg);
				    return INVALID_JSON_FORMAT;
                }

				std::string strLen = findFieldValue("limit", reqPayload);
				if (strLen.empty()) {
					return MISSING_PARAMETER;
				}

				const UniValue&  hashArray = find_value(reqPayload, "locatorHashes");
                if (hashArray.isNull()) {
				    LogPrintf("wshandler:parseClientMessage error parsing message from websocket: %s \n", msg);
					return MISSING_PARAMETER;
                }

				UniValue hashes = hashArray.get_array();
				if (hashes.size()==0) {
					LogPrintf("hashes = 0 \n");
					return MISSING_PARAMETER;
				}
				return sendHashFromLocator(hashes, strLen, clientRequestId);
			}

            // no valid request type
			return INVALID_COMMAND;
		}
        catch (std::runtime_error const& rte)
        {
            // most probably thrown by UniValue lib, that means 
            // json is formally correct but not compliant with protocol
			LogPrintf("wshandler:parseClientMessage: json not compliant: %s\n", rte.what());
			return INVALID_COMMAND;
		}
        catch (std::exception const& e)
        {
			LogPrintf("wshandler:parseClientMessage error read loop \n");
			return READ_ERROR;
		}
	}

	void readLoop() {
		while (!exit_rwhandler_thread_flag) {
			WsEvent::WsRequestType reqType = WsEvent::REQ_UNDEFINED;
			std::string clientRequestId = "";
			int res = parseClientMessage(reqType, clientRequestId);
			if (res == READ_ERROR) {
				LogPrintf("wshandler:readLoop: websocket closed/error exit reading loop \n ");
				break;
			}

			if (res != OK) {
				std::string msgError = "On requestType[" + std::to_string(reqType) + "]: ";
				switch (res) {
				case INVALID_PARAMETER:
					msgError += "Invalid parameter";
					break;
				case MISSING_PARAMETER:
					msgError += "Missing parameter";
					break;
				case MISSING_MSGID:
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
                LogPrintf("%s():%d allocated %p\n", __func__, __LINE__, wse);
				UniValue* rv = wse->getPayload();
				if (!clientRequestId.empty())
					rv->push_back(Pair("requestId", clientRequestId));
				rv->push_back(Pair("errorCode", res));
				rv->push_back(Pair("message", msgError));
				wsq->push(wse);
			}
		}
		LogPrintf("wshandler:readLoop: exit\n");
	}

public:

	enum CLIENT_PROCMSG_CODE {
		OK = 0,
		MISSING_PARAMETER = 1,
		INVALID_COMMAND = 2,
		INVALID_JSON_FORMAT = 3,
		INVALID_PARAMETER = 4,
		MISSING_MSGID = 5,
		READ_ERROR = 99
	};

	unsigned int t_id = 0;

	WsHandler():localWs(NULL) {
		wsq = new boost::lockfree::queue<WsEvent*, boost::lockfree::capacity<1024>>;
        LogPrintf("%s():%d allocated %p\n", __func__, __LINE__, wsq);
	}
	~WsHandler() {
        LogPrintf("%s():%d deleting %p\n", __func__, __LINE__, localWs);
        delete localWs;
        LogPrintf("%s():%d deleting %p\n", __func__, __LINE__, wsq);
        delete wsq;
	}
	WsHandler & operator=(const WsHandler& wsh) = delete;
	WsHandler(const WsHandler& wsh) = delete;

	void do_session(tcp::socket& socket, unsigned int t_id) {
		this->t_id = t_id;

		try {
			localWs = new websocket::stream<tcp::socket> { std::move(socket) };
            LogPrintf("%s():%d allocated %p\n", __func__, __LINE__, localWs);

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
					if (kind == websocket::frame_type::ping) {
						std::string payl(payload);
						LogPrintf("ping received... %s\n", payl);
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
		} catch (boost::system::system_error const& se) {
			if (se.code() != websocket::error::closed)
				LogPrintf("wshandler:do_session boost error %s\n", se.code().message());
		} catch (std::exception const& e) {
			LogPrintf("wshandler:do_session: error: %s\n", std::string(e.what()));
		}
		LogPrintf("wshandler:do_session: exit thread final\n");
		{
			std::unique_lock<std::mutex> lck(wsmtx);
			listWsHandler.remove(this);
            tot_connections--;
			LogPrintf("%s():%d - connection[%u] closed: tot[%d]\n",
                __func__, __LINE__, t_id, tot_connections);
            LogPrintf("%s():%d deleting %p\n", __func__, __LINE__, this);
            delete this;
		}
	}

	void send_tip_update(int height, const std::string& strHash, const std::string& blockHex) {
		sendBlockEvent(height, strHash, blockHex, WsEvent::UPDATE_TIP);
	}

	void shutdown() {
		try {
			LogPrintf("wshandler: closing socket...\n");
			exit_rwhandler_thread_flag = true;
			this->localWs->next_layer().close();
		} catch (std::exception const& e) {
			LogPrintf("wshandler: closing socket cexception %s\n", std::string(e.what()));
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
	    LogPrintf("ERROR: websocket: can not update tip!\n");
        return;
    }
	LogPrintf("websocket: update tip loop on ws clients.\n");
	{
		std::unique_lock<std::mutex> lck(wsmtx);
		std::list<WsHandler*>::iterator i;
		for (i = listWsHandler.begin(); i != listWsHandler.end(); ++i) {
			WsHandler* wsh = *i;
			LogPrintf("websocket: call wshandler_send_tip_update to: %u \n", wsh->t_id);
			wsh->send_tip_update(pindex->nHeight, pindex->GetBlockHash().GetHex(), strHex);
		}
	}
}


//------------------------------------------------------------------------------

static tcp::acceptor* acceptor = NULL;
void ws_main(std::string strAddressWs, int portWs)
{
    WsHandler* w = NULL;

	try {
		LogPrintf("start websocket service address: %s \n", strAddressWs);
		LogPrintf("start websocket service port: %s \n", portWs);

		auto const address = boost::asio::ip::make_address(strAddressWs);
		auto const port = static_cast<unsigned short>(portWs);

		net::io_context ioc { 1 };
		acceptor = new tcp::acceptor { ioc, { address, port } };
        LogPrintf("%s():%d allocated %p\n", __func__, __LINE__, acceptor);
        unsigned int t_id = 0;

		while (!exit_ws_thread) {
			tcp::socket socket { ioc };

            // TODO //  - possible DoS, limit number of connections
			LogPrintf("ws_main: waiting to get a new connection \n");
			acceptor->accept(socket);

			w = new WsHandler();
            LogPrintf("%s():%d allocated %p\n", __func__, __LINE__, w);
			std::thread { std::bind(&WsHandler::do_session, w,
					std::move(socket), t_id) }.detach();
			{
				std::unique_lock<std::mutex> lck(wsmtx);
				listWsHandler.push_back(w);
                tot_connections++;
			}
			t_id++;
			LogPrintf("%s():%d - new connection[%u] received: tot[%d]\n",
                __func__, __LINE__, t_id, tot_connections);
		}
	} catch (const std::exception& e) {
		LogPrintf("error ws_main: %s\n", std::string(e.what()));
	}
	LogPrintf("ws_main websocket service stop. \n");
}

static void shutdown()
{
	LogPrintf("shutdown all the threads/sockets thread... \n");
	{
		std::unique_lock<std::mutex> lck(wsmtx);
		std::list<WsHandler*>::iterator i;
		for (i = listWsHandler.begin(); i != listWsHandler.end(); ++i) {
			WsHandler* wsh = *i;
			wsh->shutdown();
		}
	}
}

bool StartWsServer()
{
	try {
		std::string strAddress = GetArg("-wsaddress", "127.0.0.1");
		int port = GetArg("-wsport", 8888);

		ws_thread = boost::thread(ws_main, strAddress, port);
		ws_thread.detach();

		wsNotificationInterface = new WsNotificationInterface();
        LogPrintf("%s():%d allocated %p\n", __func__, __LINE__, wsNotificationInterface);
		RegisterValidationInterface(wsNotificationInterface);
	} catch (const std::exception& e) {
		LogPrintf("error StartWsServer %s\n", std::string(e.what()));
		return false;
	}
	return true;
}

bool StopWsServer()
{
	try {
		shutdown();
		exit_ws_thread = true;
		acceptor->close();
        LogPrintf("%s():%d deleting %p\n", __func__, __LINE__, acceptor);
        delete acceptor;
        acceptor = NULL;
        LogPrintf("%s():%d deleting %p\n", __func__, __LINE__, wsNotificationInterface);
		delete wsNotificationInterface;
		wsNotificationInterface = NULL;
	} catch (const std::exception& e) {
		LogPrintf("Error stopping Websocket service\n");
		return false;
	}
	return true;
}



