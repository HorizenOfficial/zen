#include <openssl/conf.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "utiltls.h"

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include "../util.h"
#include "../protocol.h"

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include "tlsmanager.h"
using namespace std;
namespace zen
{
/**
* @brief If verify_callback always returns 1, the TLS/SSL handshake will not be terminated with respect to verification failures and the connection will be established.
* 
* @param preverify_ok 
* @param chainContext 
* @return int 
*/
int tlsCertVerificationCallback(int preverify_ok, X509_STORE_CTX* chainContext)
{
    return 1;
}
/**
 * @brief Wait for a given SSL connection event.
 * 
 * @param eRoutine a SSLConnectionRoutine value which determines the type of the event.
 * @param hSocket 
 * @param ssl pointer to an SSL instance.
 * @param timeoutSec timeout in seconds.
 * @return int returns nError corresponding to the connection event.
 */
int TLSManager::waitFor(SSLConnectionRoutine eRoutine, SOCKET hSocket, SSL* ssl, int timeoutSec)
{
    int nErr = 0;
    ERR_clear_error(); // clear the error queue

    while (true) {
        switch (eRoutine) {
        case SSL_CONNECT:
            nErr = SSL_connect(ssl);
            break;

        case SSL_ACCEPT:
            nErr = SSL_accept(ssl);
            break;

        case SSL_SHUTDOWN:
            nErr = SSL_shutdown(ssl);
            break;

        default:
            return -1;
        }

        if (eRoutine == SSL_SHUTDOWN) {
            if (nErr >= 0)
                break;
        } else {
            if (nErr == 1)
                break;
        }

        int sslErr = SSL_get_error(ssl, nErr);

        if (sslErr != SSL_ERROR_WANT_READ && sslErr != SSL_ERROR_WANT_WRITE) {
            LogPrint("net", "TLS: WARNING: %s: %s: ssl_err_code: %s; errno: %s\n", __FILE__, __func__, ERR_error_string(sslErr, NULL), strerror(errno));
            nErr = -1;
            break;
        }

        fd_set socketSet;
        FD_ZERO(&socketSet);
        FD_SET(hSocket, &socketSet);

        struct timeval timeout = {timeoutSec, 0};

        if (sslErr == SSL_ERROR_WANT_READ) {
            int result = select(hSocket + 1, &socketSet, NULL, NULL, &timeout);
            if (result == 0) {
                LogPrint("net", "TLS: ERROR: %s: %s: WANT_READ timeout\n", __FILE__, __func__);
                nErr = -1;
                break;
            } else if (result == -1) {
                LogPrint("net", "TLS: ERROR: %s: %s: WANT_READ ssl_err_code: %s; errno: %s\n", __FILE__, __func__, ERR_error_string(sslErr, NULL), strerror(errno));
                nErr = -1;
                break;
            }
        } else {
            int result = select(hSocket + 1, NULL, &socketSet, NULL, &timeout);
            if (result == 0) {
                LogPrint("net", "TLS: ERROR: %s: %s: WANT_WRITE timeout\n", __FILE__, __func__);
                nErr = -1;
                break;
            } else if (result == -1) {
                LogPrint("net", "TLS: ERROR: %s: %s: WANT_WRITE ssl_err_code: %s; errno: %s\n", __FILE__, __func__, ERR_error_string(sslErr, NULL), strerror(errno));
                nErr = -1;
                break;
            }
        }
    }

    return nErr;
}
/**
 * @brief establish TLS connection to an address
 * 
 * @param hSocket socket
 * @param addrConnect the outgoing address
 * @param tls_ctx_client TLS Client context
 * @return SSL* returns a ssl* if successful, otherwise returns NULL.
 */
SSL* TLSManager::connect(SOCKET hSocket, const CAddress& addrConnect)
{
    LogPrint("net", "TLS: establishing connection (tid = %X), (peerid = %s)\n", pthread_self(), addrConnect.ToString());

    SSL* ssl = NULL;
    bool bConnectedTLS = false;

    if ((ssl = SSL_new(tls_ctx_client))) {
        if (SSL_set_fd(ssl, hSocket)) {
            if (TLSManager::waitFor(SSL_CONNECT, hSocket, ssl, (DEFAULT_CONNECT_TIMEOUT / 1000)) == 1)

                bConnectedTLS = true;
        }
    }

    if (bConnectedTLS) {
        LogPrintf("TLS: connection to %s has been established. Using cipher: %s\n", addrConnect.ToString(), SSL_get_cipher(ssl));
    } else {
        LogPrintf("TLS: %s: %s: TLS connection to %s failed\n", __FILE__, __func__, addrConnect.ToString());

        if (ssl) {
            SSL_free(ssl);
            ssl = NULL;
        }
    }
    return ssl;
}
/**
 * @brief Initialize TLS Context
 * 
 * @param ctxType context type
 * @param privateKeyFile private key file path
 * @param certificateFile certificate key file path
 * @param trustedDirs trusted directories
 * @return SSL_CTX* returns the context.
 */
SSL_CTX* TLSManager::initCtx(
    TLSContextType ctxType,
    const boost::filesystem::path& privateKeyFile,
    const boost::filesystem::path& certificateFile,
    const std::vector<boost::filesystem::path>& trustedDirs)
{
    if (!boost::filesystem::exists(privateKeyFile) ||
        !boost::filesystem::exists(certificateFile))
        return NULL;

    bool bInitialized = false;
    SSL_CTX* tlsCtx = NULL;

    if ((tlsCtx = SSL_CTX_new(ctxType == SERVER_CONTEXT ? TLS_server_method() : TLS_client_method()))) {
        SSL_CTX_set_mode(tlsCtx, SSL_MODE_AUTO_RETRY);

        int rootCertsNum = LoadDefaultRootCertificates(tlsCtx);
        int trustedPathsNum = 0;

        for (boost::filesystem::path trustedDir : trustedDirs) {
            if (SSL_CTX_load_verify_locations(tlsCtx, NULL, trustedDir.string().c_str()) == 1)
                trustedPathsNum++;
        }

        if (rootCertsNum == 0 && trustedPathsNum == 0)
            LogPrintf("TLS: WARNING: %s: %s: failed to set up verified certificates. It will be impossible to verify peer certificates. \n", __FILE__, __func__);

        SSL_CTX_set_verify(tlsCtx, SSL_VERIFY_PEER, tlsCertVerificationCallback);

        if (SSL_CTX_use_certificate_file(tlsCtx, certificateFile.string().c_str(), SSL_FILETYPE_PEM) > 0) {
            if (SSL_CTX_use_PrivateKey_file(tlsCtx, privateKeyFile.string().c_str(), SSL_FILETYPE_PEM) > 0) {
                if (SSL_CTX_check_private_key(tlsCtx))
                    bInitialized = true;
                else
                    LogPrintf("TLS: ERROR: %s: %s: private key does not match the certificate public key\n", __FILE__, __func__);
            } else
                LogPrintf("TLS: ERROR: %s: %s: failed to use privateKey file\n", __FILE__, __func__);
        } else {
            LogPrintf("TLS: ERROR: %s: %s: failed to use certificate file\n", __FILE__, __func__);
            ERR_print_errors_fp(stderr);
        }
    } else
        LogPrintf("TLS: ERROR: %s: %s: failed to create TLS context\n", __FILE__, __func__);

    if (!bInitialized) {
        if (tlsCtx) {
            SSL_CTX_free(tlsCtx);
            tlsCtx = NULL;
        }
    }

    return tlsCtx;
}
/**
 * @brief load the certificate credentials from file.
 * 
 * @return true returns true is successful.
 * @return false returns false if an error has occured.
 */
bool TLSManager::prepareCredentials()
{
    boost::filesystem::path
        defaultKeyPath(GetDataDir() / TLS_KEY_FILE_NAME),
        defaultCertPath(GetDataDir() / TLS_CERT_FILE_NAME);

    CredentialsStatus credStatus =
        VerifyCredentials(
            boost::filesystem::path(GetArg("-tlskeypath", defaultKeyPath.string())),
            boost::filesystem::path(GetArg("-tlscertpath", defaultCertPath.string())),
            GetArg("-tlskeypwd", ""));

    bool bPrepared = (credStatus == credOk);

    if (!bPrepared) {
        if (!mapArgs.count("-tlskeypath") && !mapArgs.count("-tlscertpath")) {
            // Default paths were used

            if (credStatus == credAbsent) {
                // Generate new credentials (key and self-signed certificate on it) only if credentials were absent previously
                //
                bPrepared = GenerateCredentials(
                    defaultKeyPath,
                    defaultCertPath,
                    GetArg("-tlskeypwd", ""));
            }
        }
    }

    return bPrepared;
}
/**
 * @brief accept a TLS connection
 * 
 * @param hSocket the TLS socket.
 * @param addr incoming address.
 * @param tls_ctx_server TLS server context.
 * @return SSL* returns pointer to the ssl object if successful, otherwise returns NULL
 */
SSL* TLSManager::accept(SOCKET hSocket, const CAddress& addr)
{
    LogPrint("net", "TLS: accepting connection from %s (tid = %X)\n", addr.ToString(), pthread_self());

    SSL* ssl = NULL;
    bool bAcceptedTLS = false;

    if ((ssl = SSL_new(tls_ctx_server))) {
        if (SSL_set_fd(ssl, hSocket)) {
            if (TLSManager::waitFor(SSL_ACCEPT, hSocket, ssl, (DEFAULT_CONNECT_TIMEOUT / 1000)) == 1)
                bAcceptedTLS = true;
        }
    }

    if (bAcceptedTLS) {
        LogPrintf("TLS: connection from %s has been accepted. Using cipher: %s\n", addr.ToString(), SSL_get_cipher(ssl));
    } else {
        LogPrintf("TLS: ERROR: %s: %s: TLS connection from %s failed\n", __FILE__, __func__, addr.ToString());

        if (ssl) {
            SSL_free(ssl);
            ssl = NULL;
        }
    }

    return ssl;
}
/**
 * @brief Determines whether a string exists in the non-TLS address pool.
 * 
 * @param strAddr The address.
 * @param vPool Pool to search in.
 * @param cs reference to the corresponding CCriticalSection.
 * @return true returns true if address exists in the given pool.
 * @return false returns false if address doesnt exist in the given pool.
 */
bool TLSManager::isNonTLSAddr(const string& strAddr, const vector<NODE_ADDR>& vPool, CCriticalSection& cs)
{
    LOCK(cs);
    return (find(vPool.begin(), vPool.end(), NODE_ADDR(strAddr)) != vPool.end());
}
/**
 * @brief Removes non-TLS node addresses based on timeout.
 * 
 * @param vPool 
 * @param cs 
 */
void TLSManager::cleanNonTLSPool(std::vector<NODE_ADDR>& vPool, CCriticalSection& cs)
{
    LOCK(cs);

    vector<NODE_ADDR> vDeleted;

    BOOST_FOREACH (NODE_ADDR nodeAddr, vPool) {
        if ((GetTimeMillis() - nodeAddr.time) >= 900000) {
            vDeleted.push_back(nodeAddr);
            LogPrint("net", "TLS: Node %s is deleted from the non-TLS pool\n", nodeAddr.ipAddr);
        }
    }

    BOOST_FOREACH (NODE_ADDR nodeAddrDeleted, vDeleted) {
        vPool.erase(
            remove(
                vPool.begin(),
                vPool.end(),
                nodeAddrDeleted),
            vPool.end());
    }
}

/**
 * @brief Handles send and recieve functionality in TLS Sockets.
 * 
 * @param pnode reference to the CNode object.
 * @param fdsetRecv 
 * @param fdsetSend 
 * @param fdsetError 
 * @return int returns -1 when socket is invalid. returns 0 otherwise.
 */
int TLSManager::threadSocketHandler(CNode* pnode, fd_set& fdsetRecv, fd_set& fdsetSend, fd_set& fdsetError)
{
    //
    // Receive
    //
    bool recvSet = false, sendSet = false, errorSet = false;

    {
        LOCK(pnode->cs_hSocket);

        if (pnode->hSocket == INVALID_SOCKET)
            return -1;

        recvSet = FD_ISSET(pnode->hSocket, &fdsetRecv);
        sendSet = FD_ISSET(pnode->hSocket, &fdsetSend);
        errorSet = FD_ISSET(pnode->hSocket, &fdsetError);
    }

    if (recvSet || errorSet) {
        TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
        if (lockRecv) {
            {
                // typical socket buffer is 8K-64K
                // maximum record size is 16kB for SSLv3/TLSv1
                char pchBuf[0x10000];
                bool bIsSSL = false;
                int nBytes = 0, nRet = 0;

                {
                    LOCK(pnode->cs_hSocket);

                    if (pnode->hSocket == INVALID_SOCKET) {
                        LogPrint("net", "Receive: connection with %s is already closed\n", pnode->addr.ToString());
                        return -1;
                    }

                    bIsSSL = (pnode->ssl != NULL);

                    if (bIsSSL) {
                        ERR_clear_error(); // clear the error queue, otherwise we may be reading an old error that occurred previously in the current thread
                        nBytes = SSL_read(pnode->ssl, pchBuf, sizeof(pchBuf));
                        nRet = SSL_get_error(pnode->ssl, nBytes);
                    } else {
                        nBytes = recv(pnode->hSocket, pchBuf, sizeof(pchBuf), MSG_DONTWAIT);
                        nRet = WSAGetLastError();
                    }
                }

                if (nBytes > 0) {
                    if (!pnode->ReceiveMsgBytes(pchBuf, nBytes))
                        pnode->CloseSocketDisconnect();
                    pnode->nLastRecv = GetTime();
                    pnode->nRecvBytes += nBytes;
                    pnode->RecordBytesRecv(nBytes);
                } else if (nBytes == 0) {
                    // socket closed gracefully (peer disconnected)
                    //
                    if (!pnode->fDisconnect)
                        LogPrint("net", "socket closed (%s)\n", pnode->addr.ToString());
                    pnode->CloseSocketDisconnect();
                } else if (nBytes < 0) {
                    // error
                    //
                    if (bIsSSL) {
                        if (nRet != SSL_ERROR_WANT_READ && nRet != SSL_ERROR_WANT_WRITE) // SSL_read() operation has to be repeated because of SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE (https://wiki.openssl.org/index.php/Manual:SSL_read(3)#NOTES)
                        {
                            if (!pnode->fDisconnect)
                                LogPrintf("ERROR: SSL_read %s\n", ERR_error_string(nRet, NULL));
                            pnode->CloseSocketDisconnect();
                        } else {
                            // preventive measure from exhausting CPU usage
                            //
                            MilliSleep(1); // 1 msec
                        }
                    } else {
                        if (nRet != WSAEWOULDBLOCK && nRet != WSAEMSGSIZE && nRet != WSAEINTR && nRet != WSAEINPROGRESS) {
                            if (!pnode->fDisconnect)
                                LogPrintf("ERROR: socket recv %s\n", NetworkErrorString(nRet));
                            pnode->CloseSocketDisconnect();
                        }
                    }
                }
            }
        }
    }

    //
    // Send
    //
    if (sendSet) {
        TRY_LOCK(pnode->cs_vSend, lockSend);
        if (lockSend)
            SocketSendData(pnode);
    }
    return 0;
}
/**
 * @brief Initialization of the server and client contexts
 * 
 * @return true returns True if successful.
 * @return false returns False if an error has occured.
 */
bool TLSManager::initialize()
{
    bool bInitializationStatus = false;
    
    // Initialization routines for the OpenSSL library
    //
    SSL_load_error_strings();
    ERR_load_crypto_strings();
    OpenSSL_add_ssl_algorithms(); // OpenSSL_add_ssl_algorithms() always returns "1", so it is safe to discard the return value.
    
    namespace fs = boost::filesystem;
    fs::path certFile = GetArg("-tlscertpath", "");
    if (!fs::exists(certFile))
            certFile = (GetDataDir() / TLS_CERT_FILE_NAME);
    
    fs::path privKeyFile = GetArg("-tlskeypath", "");
    if (!fs::exists(privKeyFile))
            privKeyFile = (GetDataDir() / TLS_KEY_FILE_NAME);
    
    std::vector<fs::path> trustedDirs;
    fs::path trustedDir = GetArg("-tlstrustdir", "");
    if (fs::exists(trustedDir))
        // Use only the specified trusted directory
        trustedDirs.push_back(trustedDir);
    else
        // If specified directory can't be used, then setting the default trusted directories
        trustedDirs = GetDefaultTrustedDirectories();

    for (fs::path dir : trustedDirs)
        LogPrintf("TLS: trusted directory '%s' will be used\n", dir.string().c_str());

    // Initialization of the server and client contexts
    //
    if ((tls_ctx_server = TLSManager::initCtx(SERVER_CONTEXT, privKeyFile, certFile, trustedDirs)))
    {
        if ((tls_ctx_client = TLSManager::initCtx(CLIENT_CONTEXT, privKeyFile, certFile, trustedDirs)))
        {
            LogPrint("net", "TLS: contexts are initialized\n");
            bInitializationStatus = true;
        }
        else
        {
            LogPrintf("TLS: ERROR: %s: %s: failed to initialize TLS client context\n", __FILE__, __func__);
            SSL_CTX_free (tls_ctx_server);
        }
    }
    else
        LogPrintf("TLS: ERROR: %s: %s: failed to initialize TLS server context\n", __FILE__, __func__);

    return bInitializationStatus;
}
}