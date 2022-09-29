#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/signals2/signal.hpp>
#include <boost/thread.hpp>
#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "../net.h"
#include "../util.h"
#include "sync.h"
#include "tlsenums.h"
#ifdef WIN32
#include <string.h>
#else
#include <fcntl.h>
#endif

using namespace std;

namespace zen {
typedef struct _NODE_ADDR {
    std::string ipAddr;
    int64_t time;  // time in msec, of an attempt to connect via TLS

    _NODE_ADDR(std::string _ipAddr, int64_t _time = 0) : ipAddr(_ipAddr), time(_time) {}
    bool operator==(const _NODE_ADDR b) const { return (ipAddr == b.ipAddr); }
} NODE_ADDR, *PNODE_ADDR;

/**
 * @brief A class to wrap some of zen specific TLS functionalities used in the net.cpp
 *
 */
class TLSManager {
  public:
    /* This is set as a custom error number which is not an error in OpenSSL protocol.
       A true (not null) OpenSSL error returned by ERR_get_error() consists of a library number,
       function code and reason code. */
    static const long SELECT_TIMEDOUT = 0xFFFFFFFF;

    int waitFor(SSLConnectionRoutine eRoutine, SOCKET hSocket, SSL* ssl, int timeoutSec, unsigned long& err_code);

    SSL* connect(SOCKET hSocket, const CAddress& addrConnect, unsigned long& err_code);
    SSL_CTX* initCtx(TLSContextType ctxType, const boost::filesystem::path& privateKeyFile,
                     const boost::filesystem::path& certificateFile, const std::vector<boost::filesystem::path>& trustedDirs);

    bool prepareCredentials();
    SSL* accept(SOCKET hSocket, const CAddress& addr, unsigned long& err_code);
    bool isNonTLSAddr(const string& strAddr, const vector<NODE_ADDR>& vPool, CCriticalSection& cs);
    void cleanNonTLSPool(std::vector<NODE_ADDR>& vPool, CCriticalSection& cs);
    int threadSocketHandler(CNode* pnode, fd_set& fdsetRecv, fd_set& fdsetSend, fd_set& fdsetError);
    bool initialize();
};
}  // namespace zen
