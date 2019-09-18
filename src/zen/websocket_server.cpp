
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

class WsNotificationInterface;
class WsHandler;
static std::string getblock(const CBlockIndex *pindex);
static void ws_updatetip(const CBlockIndex *pindex);

static WsNotificationInterface* wsNotificationInterface = NULL;
std::list<WsHandler*> listWsHandler;
std::atomic<bool> exit_ws_thread{false};
boost::thread ws_thread;
std::mutex wsmtx;

static std::string findFieldValue(std::string field, UniValue& request)
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
		UPDATE_TIP = 1,
		GET_SINGLE_BLOCK = 2,
		GET_MULTIPLE_BLOCKS = 3,
		GET_MULTIPLE_BLOCK_HASHES = 4,
		GET_SYNC_INFO = 5,
		ERROR = -1
	};

	WsEvent(WsEventType xn) {
		type = xn;
		payload = new UniValue(UniValue::VOBJ);
		payload->push_back(Pair("type", type));
	}
	WsEvent(const WsEvent& ws) {
		type = ws.type;
		payload = ws.payload;
	}
	~WsEvent(){};

	UniValue* getPayload() {
		return payload;
	}

private:
	WsEventType type;
	UniValue* payload;
};



class WsHandler
{
private:
	websocket::stream<tcp::socket>* localWs = NULL;
	boost::lockfree::queue<WsEvent*, boost::lockfree::capacity<1024>>* wsq;
	std::atomic<bool> exit_rwhandler_thread_flag { false };

	void sendBlock(int height, std::string strHash, std::string blockHex,
			WsEvent::WsEventType eventType, std::string clientMsgId = "",
			int counter = 0) {
		// Send a message to the client:  type = eventType
		WsEvent* wse = new WsEvent(eventType);
		UniValue* rv = wse->getPayload();
		if (counter > 0)
			rv->push_back(Pair("counter", counter));
		rv->push_back(Pair("height", height));
		rv->push_back(Pair("hash", strHash));
		rv->push_back(Pair("block", blockHex));
		if (!clientMsgId.empty())
			rv->push_back(Pair("msgId", clientMsgId));
		wsq->push(wse);
	}

	void sendHashes(int height, std::list<CBlockIndex*>& listBlock,
			WsEvent::WsEventType eventType, std::string clientMsgId = "") {
		// Send a message to the client:  type = eventType
		WsEvent* wse = new WsEvent(eventType);
		UniValue* rv = wse->getPayload();
		rv->push_back(Pair("height", height));
		UniValue hashes(UniValue::VARR);

		std::list<CBlockIndex*>::iterator it = listBlock.begin();
		while (it != listBlock.end()) {
			CBlockIndex* blockIndexIterator = *it;
			hashes.push_back(blockIndexIterator->GetBlockHash().GetHex());
			it++;
		}
		rv->push_back(Pair("hashes", hashes));
		if (!clientMsgId.empty())
			rv->push_back(Pair("msgId", clientMsgId));
		wsq->push(wse);
	}

	int getHashByHeight(std::string height, std::string& strHash) {
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

	int sendBlockByHeight(std::string strHeight, std::string clientMsgId) {
		std::string strHash;
		int r = getHashByHeight(strHeight, strHash);
		if (r != OK)
			return r;
		return sendBlockByHash(strHash, clientMsgId);
	}

	int sendBlockByHash(std::string strHash, std::string clientMsgId) {
		CBlockIndex* pblockindex;
		{
			LOCK(cs_main);
			uint256 hash(uint256S(strHash));
			pblockindex = mapBlockIndex[hash];
			if (pblockindex == NULL) {
				return INVALID_PARAMETER;
			}
		}
		std::string block = getblock(pblockindex);
		sendBlock(pblockindex->nHeight, strHash, block, WsEvent::GET_SINGLE_BLOCK, clientMsgId);
		return OK;
	}

	int sendBlocksFromHeight(std::string strHeight, std::string strLen,
			std::string clientMsgId, bool includeBlock = true) {
		std::string strHash;
		int r = getHashByHeight(strHeight, strHash);
		if (r != OK)
			return r;
		return sendBlocksFromHash(strHash, strLen, clientMsgId, includeBlock);
	}

	int sendBlocksFromHash(std::string strHash, std::string strLen,
			std::string clientMsgId, bool includeBlock = true) {
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
		if (includeBlock) {
			int counter = 1;
			std::list<CBlockIndex*>::iterator it = listBlock.begin();
			while (it != listBlock.end()) {
				CBlockIndex* blockIndexIterator = *it;
				std::string blockHex = getblock(blockIndexIterator);
				sendBlock(blockIndexIterator->nHeight, blockIndexIterator->GetBlockHash().GetHex(), blockHex,
						WsEvent::GET_MULTIPLE_BLOCKS, clientMsgId, counter);
				counter++;
				it++;
			}
		} else {
			sendHashes(listBlock.front()->nHeight, listBlock,
					WsEvent::GET_MULTIPLE_BLOCK_HASHES, clientMsgId);
		}
		return OK;
	}


	int sendHashFromLocator(UniValue& hashes, std::string strLen, std::string clientMsgId) {
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
		sendHashes(listBlock.front()->nHeight, listBlock, WsEvent::GET_SYNC_INFO, clientMsgId);
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

	int parseClientMessage(WsEvent::WsEventType& commandType, std::string& clientMsgId) {
		try {
			std::string command;
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

			clientMsgId = findFieldValue("msgId", request);

			command = findFieldValue("command", request);
			if (command.empty()) {
				return INVALID_COMMAND;
			}
			if (command == "getBlock") {
				commandType = WsEvent::GET_SINGLE_BLOCK;
				if (clientMsgId.empty()) {
					return MISSING_MSGID;
				}
				std::string param1 = findFieldValue("height", request);
				if (param1.empty()) {
					param1 = findFieldValue("hash", request);
					if (param1.empty()) {
						return MISSING_PARAMETER; //
					}
					return sendBlockByHash(param1, clientMsgId);
				}
				return sendBlockByHeight(param1, clientMsgId);
			}
			if (command == "getBlocks") {
				commandType = WsEvent::GET_MULTIPLE_BLOCKS;
				if (clientMsgId.empty()) {
					return MISSING_MSGID;
				}
				std::string strLen = findFieldValue("len", request);
				if (strLen.empty()) {
					return MISSING_PARAMETER;
				}

				std::string param1 = findFieldValue("afterHeight", request);
				if (param1.empty()) {
					param1 = findFieldValue("afterHash", request);
					if (param1.empty()) {
						return MISSING_PARAMETER; //
					}
					return sendBlocksFromHash(param1, strLen, clientMsgId);
				}
				return sendBlocksFromHeight(param1, strLen, clientMsgId);
			}
			if (command == "getBlockHashes") {
				commandType = WsEvent::GET_MULTIPLE_BLOCK_HASHES;
				if (clientMsgId.empty()) {
					return MISSING_MSGID;
				}
				std::string strLen = findFieldValue("len", request);
				if (strLen.empty()) {
					return MISSING_PARAMETER;
				}

				std::string param1 = findFieldValue("afterHeight", request);
				if (param1.empty()) {
					param1 = findFieldValue("afterHash", request);
					if (param1.empty()) {
						return MISSING_PARAMETER; //
					}
					return sendBlocksFromHash(param1, strLen, clientMsgId, false);
				}
				return sendBlocksFromHeight(param1, strLen, clientMsgId, false);
			}
			if (command == "getSyncInfo") {
				commandType = WsEvent::GET_SYNC_INFO;
				if (clientMsgId.empty()) {
					return MISSING_MSGID;
				}
				std::string strLen = findFieldValue("len", request);
				if (strLen.empty()) {
					return MISSING_PARAMETER;
				}

				const UniValue&  hashArray = find_value(request, "hashes");
				UniValue hashes = hashArray.get_array();
				if (hashes.size()==0) {
					LogPrintf("hashes = 0 \n");
					return MISSING_PARAMETER;
				}
				return sendHashFromLocator(hashes, strLen, clientMsgId);
			}

			return INVALID_COMMAND;
		} catch (std::exception const& e) {
			LogPrintf("wshandler:parseClientMessage error read loop \n");
			return READ_ERROR;
		}
	}

	void readLoop() {
		while (!exit_rwhandler_thread_flag) {
			WsEvent::WsEventType commandType;
			std::string clientMsgId = "";
			int res = parseClientMessage(commandType, clientMsgId);
			if (res == READ_ERROR) {
				LogPrintf("wshandler:readLoop: websocket closed/error exit reading loop \n ");
				break;
			}

			if (res != OK) {
				std::string msgError;
				switch (res) {
				case INVALID_PARAMETER:
					msgError = "Invalid parameter";
					break;
				case MISSING_PARAMETER:
					msgError = "Missing parameter";
					break;
				case MISSING_MSGID:
					msgError = "Missing msgId";
					break;
				case INVALID_COMMAND:
					msgError = "Invalid command";
					break;
				case INVALID_JSON_FORMAT:
					commandType = WsEvent::ERROR;
					msgError = "Invalid JSON format";
					break;
				default:
					msgError = "Generic error";

				}
				// Send a message error to the client:  type = -1
				WsEvent* wse = new WsEvent(commandType);
				UniValue* rv = wse->getPayload();
				rv->push_back(Pair("errorCode", res));
				rv->push_back(Pair("message", msgError));
				if (!clientMsgId.empty())
					rv->push_back(Pair("msgId", clientMsgId));
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

	int t_id = 0;

	WsHandler() {
		wsq = new boost::lockfree::queue<WsEvent*, boost::lockfree::capacity<1024>>;
	}
	~WsHandler() {
	}

	void do_session(tcp::socket& socket, int t_id) {
		this->t_id = t_id;

		try {
			localWs = new websocket::stream<tcp::socket> { std::move(socket) };

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
		}
	}

	void send_tip_update(int height, std::string strHash, std::string blockHex) {
		sendBlock(height, strHash, blockHex, WsEvent::WsEventType::UPDATE_TIP);
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


static std::string getblock(const CBlockIndex *pindex)
{
	std::string strHex;
	CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
	{
		LOCK(cs_main);
		CBlock block;
		if (!ReadBlockFromDisk(block, pindex)) {
			// return ERRORR;
		}
		ss << block;
		strHex = HexStr(ss.begin(), ss.end());
	}
	return strHex;
}




static void ws_updatetip(const CBlockIndex *pindex)
{
	std::string strHex = getblock(pindex);
	LogPrintf("websocket: update tip loop on ws clients.\n");
	{
		std::unique_lock<std::mutex> lck(wsmtx);
		std::list<WsHandler*>::iterator i = listWsHandler.begin();
		for (i = listWsHandler.begin(); i != listWsHandler.end(); ++i) {
			WsHandler* wsh = *i;
			LogPrintf("websocket: call wshandler_send_tip_update to: %d \n", wsh->t_id);
			wsh->send_tip_update(pindex->nHeight, pindex->GetBlockHash().GetHex(), strHex);
		}
	}
}


//------------------------------------------------------------------------------

tcp::acceptor* acceptor;
void ws_main(std::string strAddressWs, int portWs)
{
	try {
		LogPrintf("start websocket service address: %s \n", strAddressWs);
		LogPrintf("start websocket service port: %s \n", portWs);

		auto const address = boost::asio::ip::make_address(strAddressWs);
		auto const port = static_cast<unsigned short>(portWs);

		net::io_context ioc { 1 };
		acceptor = new tcp::acceptor { ioc, { address, port } };
		int t_id = 0;

		while (!exit_ws_thread) {
			tcp::socket socket { ioc };

			LogPrintf("ws_main: waiting to get a new connection \n");
			acceptor->accept(socket);

			WsHandler* w = new WsHandler();
			std::thread { std::bind(&WsHandler::do_session, w,
					std::move(socket), t_id) }.detach();
			{
				std::unique_lock<std::mutex> lck(wsmtx);
				listWsHandler.push_back(w);
			}
			t_id++;
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
		std::list<WsHandler*>::iterator i = listWsHandler.begin();
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
	} catch (const std::exception& e) {
		LogPrintf("Error stopping Websocket service\n");
		return false;
	}
	return true;
}



