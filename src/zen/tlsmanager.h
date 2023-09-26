#ifndef BITCOIN_TLSMANAGER_H
#define BITCOIN_TLSMANAGER_H

#include <openssl/conf.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "tlsenums.h"
#include <boost/filesystem.hpp>
#include "../util.h"
#include "../net.h"
#include "sync.h"
#include <boost/foreach.hpp>
#include <boost/signals2/signal.hpp>
#ifdef WIN32
#include <string.h>
#else
#include <fcntl.h>
#endif

using namespace std;

namespace zen
{

/**
 * @brief A class to wrap some of zen specific TLS functionalities used in the net.cpp
 * 
 */
class TLSManager
{
public:
     /* This is set as a custom error number which is not an error in OpenSSL protocol.
        A true (not null) OpenSSL error returned by ERR_get_error() consists of a library number,
        function code and reason code. */
     static const long SELECT_TIMEDOUT = 0xFFFFFFFF;

     int waitFor(SSLConnectionRoutine eRoutine, const CAddress& peerAddress, SSL* ssl, int timeoutMilliSec, unsigned long& err_code);

     SSL* connect(SOCKET hSocket, const CAddress& addrConnect, unsigned long& err_code);
     SSL_CTX* initCtx(
        TLSContextType ctxType,
        const boost::filesystem::path& privateKeyFile,
        const boost::filesystem::path& certificateFile,
        const std::vector<boost::filesystem::path>& trustedDirs);

     bool prepareCredentials();
     SSL* accept(SOCKET hSocket, const CAddress& addr, unsigned long& err_code);
     bool isNonTLSAddr(const string& strAddr, const vector<NODE_ADDR>& vPool, CCriticalSection& cs);
     void cleanNonTLSPool(std::vector<NODE_ADDR>& vPool, CCriticalSection& cs);
     int threadSocketHandler(CNode* pnode, fd_set& fdsetRecv, fd_set& fdsetSend, fd_set& fdsetError);
     bool initialize();
};
}

#endif // BITCOIN_TLSMANAGER_H