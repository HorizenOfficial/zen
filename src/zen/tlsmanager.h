#include <openssl/conf.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "utiltls.h"
#include "tlsenums.h"
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include "../util.h"
#include "../protocol.h"
#include "../net.h"
#include "sync.h"
#include <boost/filesystem/path.hpp>
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
typedef struct _NODE_ADDR {
    std::string ipAddr;
    int64_t time; // time in msec, of an attempt to connect via TLS

    _NODE_ADDR(std::string _ipAddr, int64_t _time = 0) : ipAddr(_ipAddr), time(_time) {}
bool operator==(const _NODE_ADDR b) const
{
    return (ipAddr == b.ipAddr);
}
} NODE_ADDR, *PNODE_ADDR;

/**
 * @brief A class to wrap some of zen specific TLS functionalities used in the net.cpp
 * 
 */
class TLSManager
{
public:
     int waitFor(SSLConnectionRoutine eRoutine, SOCKET hSocket, SSL* ssl, int timeoutSec);
     SSL* connect(SOCKET hSocket, const CAddress& addrConnect);
     SSL_CTX* initCtx(
        TLSContextType ctxType,
        const boost::filesystem::path& privateKeyFile,
        const boost::filesystem::path& certificateFile,
        const std::vector<boost::filesystem::path>& trustedDirs);

     bool prepareCredentials();
     SSL* accept(SOCKET hSocket, const CAddress& addr);
     bool isNonTLSAddr(const string& strAddr, const vector<NODE_ADDR>& vPool, CCriticalSection& cs);
     void cleanNonTLSPool(std::vector<NODE_ADDR>& vPool, CCriticalSection& cs);
     int threadSocketHandler(CNode* pnode, fd_set& fdsetRecv, fd_set& fdsetSend, fd_set& fdsetError);
     bool initialize();
};
}
