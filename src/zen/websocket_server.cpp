//
// Copyright (c) 2016-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: WebSocket server, synchronous
//
//------------------------------------------------------------------------------

#include "util.h"
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


using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>

boost::thread ws_thread;
typedef boost::unordered_map<std::string, int> unordered_map;
unordered_map map_queue;

namespace net = boost::asio;
net::io_context ioc;
// websocket::stream<tcp::socket> ws(ioc);


std::string getClientId(UniValue& request)
{
	const UniValue& clientvalue = find_value(request, "clientid");
	if (clientvalue.isStr())
	{
		return clientvalue.get_str();
	}
	else if (clientvalue.isNull())
	{	return "";
	}
    // else throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid userid");
	return "";
}



void writeLoop(websocket::stream<tcp::socket>& localws)
{
	LogPrintf("start writing....%d\n", 1);
	localws.text(localws.got_text());
	for (;;)
	{
		if (localws.is_open())
		{
			boost::beast::error_code ec;
			//boost::asio::post(localws.write(boost::asio::buffer(std::string("Hello"))));
			localws.write(boost::asio::buffer(std::string("Hello")), ec);
			if (ec == websocket::error::closed) {
				LogPrintf("writeLoop websocket error closed \n");
				break;
			}

		}
		else
		{
			LogPrintf("writeLoop ws is closed \n");
			break;
		}

		LogPrintf("write loop tick....\n");
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	LogPrintf("writeLoop Writing thread exit for ws \n");

}

int readFromClient(websocket::stream<tcp::socket>& localws)
{

	try
	{
		std::string client_id;
		boost::beast::multi_buffer buffer;
		boost::beast::error_code ec;

		localws.read(buffer, ec);
		if (ec == websocket::error::closed) {
			LogPrintf("readFromClient websocket error close \n");
			return -1;
		}
		std::string msg = boost::beast::buffers_to_string(buffer.data());
		LogPrintf("readFromClient websocket msg: %s \n", msg);
		UniValue request;
		if (!request.read(msg)) {
			LogPrintf("readFromClient error parsing msg from websocket \n");
			//throw JSONRPCError(RPC_PARSE_ERROR, "Parse error");
			//continue; // or exit
			return -1;
		}
		client_id = getClientId(request);
		if (client_id.empty()) {
			LogPrintf("clientid is missing websocket closed \n");
			localws.close(1);
			return -1;
		}

		LogPrintf("clientID: %s \n", client_id);
		if (!map_queue.count(client_id)) {
			map_queue.emplace(client_id, std::atoi(client_id.c_str()));
			LogPrintf("insert client id in the map: %d \n",
					map_queue.find(client_id)->second);
		} else {
			LogPrintf("client id already in map: %d \n",
					map_queue.find(client_id)->second);
		}
		return 0;
	}
	catch (std::exception const& e)
	{
		LogPrintf("readFromClient errror read looop \n");
		return -1;
	}
}

void readLoop(websocket::stream<tcp::socket>& localws)
{
	 for(;;)
	 {
		int res = readFromClient(localws);
		if (res==-1)
		{
			LogPrintf("do_session websocket closed/error  exit reading loop \n ");
			break;
		}
		LogPrintf("do_session reading loop tick \n ");
	}
	LogPrintf("readLoop Reading thread exit for ws \n");

}

void do_session(tcp::socket& socket)
{
	std::string client_id;

    try
    {
        // Construct the stream by moving in the socket
        websocket::stream<tcp::socket> ws{std::move(socket)};
        ws.accept();

        boost::thread write_t(writeLoop, boost::ref(ws));
        boost::thread read_t(readLoop, boost::ref(ws));
        write_t.join();
        read_t.join();
        socket.close();
        LogPrintf("do_session exit ws accept/read thread \n");
	}
	catch(boost::system::system_error const& se)
	{
		LogPrintf("do_session catch boost::exception \n");
		// This indicates that the session was closed
		if(se.code() != websocket::error::closed)
			LogPrintf("do_session catch boost::exception:  %s\n",  se.code().message());
	}
	catch(std::exception const& e)
	{
		LogPrintf("do_session catch std::exception %s\n", std::string(e.what()));
	}
	LogPrintf("do_session exit 2 \n");

}

//------------------------------------------------------------------------------




void ws_main()
{
    try
    {
    	std::string tmpAddress = "127.0.0.1";
    	const int tmpPort = 1971;
        // Check command line arguments.
        auto const address = boost::asio::ip::make_address(tmpAddress);
        auto const port = static_cast<unsigned short>(tmpPort);

        // The io_context is required for all I/O
        net::io_context ioc{BOOST_ASIO_CONCURRENCY_HINT_SAFE};

        // The acceptor receives incoming connections
        tcp::acceptor acceptor{ioc, {address, port}};
        for(;;)
        {
            // This will receive the new connection
            tcp::socket socket{ioc};

            // Block until we get a connection
            acceptor.accept(socket);

            // Launch the session, transferring ownership of the socket
            // std::thread{std::bind(&do_session, std::move(socket))}.detach();

            boost::thread wshandler_t(do_session, boost::ref(socket));
            wshandler_t.detach();

        }
    }
    catch (const std::exception& e)
    {
    	LogPrintf("error ws_main: %s\n", std::string(e.what()));
        // return EXIT_FAILURE;
    }catch (boost::thread_interrupted&) {
    	//...log it
    	LogPrintf("boost::thread_interrupted main ws socket thread... \n");
    }
}


bool StartWsServer(){
	try
	{
		ws_thread = boost::thread(ws_main);
	}
	catch (const std::exception& e){
		LogPrintf("error StartWsServer %s\n", std::string(e.what()));
		return false;
	}
	return true;
}

bool StopWsServer(){
	ws_thread.interrupt();
	return true;
}



