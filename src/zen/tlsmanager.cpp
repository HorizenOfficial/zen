#include <openssl/conf.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#ifndef HEADER_DH_H
#include <openssl/dh.h>
#endif

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include "tlsmanager.h"
#include "utiltls.h"

using namespace std;
namespace zen
{

// this is the 'dh crypto environment' to be shared between two peers and it is meant to be public, therefore
// it is OK to hard code it (or as an alternative to read it from a file)
// ----
// generated via: openssl dhparam -C  2048
static DH *get_dh2048(void)
{
    static unsigned char dhp_2048[] = {
        0xCC, 0x9B, 0xD8, 0x4E, 0x5F, 0xCE, 0xB9, 0x0D, 0x3E, 0x01,
        0x71, 0x9D, 0x26, 0x32, 0x04, 0xFB, 0xEF, 0x27, 0xD2, 0x82,
        0x11, 0x33, 0x50, 0x79, 0xFA, 0xFF, 0x98, 0xC7, 0x27, 0x3E,
        0x6F, 0x8B, 0xBC, 0xE8, 0x7F, 0x3B, 0xDF, 0xB2, 0x27, 0x12,
        0x8E, 0x56, 0x35, 0xE6, 0xCF, 0x31, 0x5B, 0xEB, 0xED, 0x1C,
        0xE1, 0x8C, 0x1B, 0x59, 0x1A, 0xE7, 0x80, 0x15, 0xBF, 0x5E,
        0x02, 0xAA, 0x90, 0xA0, 0xE6, 0x60, 0x11, 0x66, 0x78, 0xD9,
        0x42, 0x44, 0xE7, 0x95, 0x05, 0xF6, 0x98, 0xC2, 0xC5, 0xBF,
        0x8B, 0x33, 0x5F, 0x35, 0xD5, 0x00, 0xAD, 0x4E, 0x95, 0x7C,
        0x0E, 0xC3, 0xAB, 0x95, 0x86, 0xD0, 0x05, 0x79, 0x2D, 0x41,
        0x70, 0xDA, 0x24, 0x5A, 0xA9, 0x8B, 0x8B, 0xAB, 0x6E, 0x6E,
        0x73, 0xCF, 0xDE, 0x0A, 0xE4, 0xD2, 0xDC, 0xD0, 0xC7, 0xDB,
        0xC8, 0x4E, 0x01, 0x76, 0x17, 0x75, 0x17, 0x2A, 0xC3, 0xC7,
        0x8D, 0xF3, 0x60, 0xB6, 0x2C, 0xFA, 0xF2, 0xEC, 0x70, 0xDC,
        0x6B, 0xE4, 0xCA, 0x38, 0x2F, 0x37, 0x81, 0x7B, 0x8C, 0x63,
        0x95, 0xD0, 0x70, 0x4F, 0x36, 0x7A, 0x79, 0x94, 0xBC, 0x8D,
        0xD8, 0x61, 0xE4, 0xFD, 0xB8, 0x27, 0x27, 0xCF, 0x50, 0xFB,
        0x5F, 0xC5, 0xA9, 0x03, 0x56, 0x7F, 0x62, 0xAA, 0x5A, 0xCA,
        0xCE, 0x1E, 0xC3, 0x69, 0xD3, 0x1F, 0x50, 0x37, 0x15, 0x5F,
        0xA8, 0xBE, 0xB7, 0x55, 0xD8, 0xD2, 0xEC, 0xFB, 0x6E, 0x41,
        0x25, 0xB0, 0xB6, 0xD6, 0x75, 0xD5, 0x10, 0x2A, 0x44, 0x85,
        0xC2, 0x45, 0x6C, 0xA9, 0xEB, 0xEA, 0xBE, 0xAB, 0x59, 0x6A,
        0x0B, 0x78, 0x86, 0x5A, 0xCD, 0xE2, 0xDB, 0x14, 0x74, 0xAC,
        0xDD, 0x3D, 0x65, 0x75, 0xAC, 0xAE, 0x00, 0xB5, 0x01, 0xBF,
        0x64, 0xC4, 0xBC, 0x5B, 0x10, 0x67, 0xDE, 0xD1, 0x11, 0xFE,
        0x87, 0x9B, 0x7F, 0xF6, 0x1A, 0xD3
    };

    static unsigned char dhg_2048[] = {
        0x02
    };

    DH *dh = DH_new();
    BIGNUM *p, *g;

    if (dh == NULL)
    {
        unsigned long err_code = ERR_get_error();
        const char* error_str = ERR_error_string(err_code, NULL);
        LogPrintf("TLS: %s: %s():%d - ERROR: mem allocation failed (err=%s)\n",
            __FILE__, __func__, __LINE__, error_str);
        return NULL;
    }

    p = BN_bin2bn(dhp_2048, sizeof(dhp_2048), NULL);
    g = BN_bin2bn(dhg_2048, sizeof(dhg_2048), NULL);

    if (p == NULL || g == NULL || !DH_set0_pqg(dh, p, NULL, g))
    {
        unsigned long err_code = ERR_get_error();
        const char* error_str = ERR_error_string(err_code, NULL);
        DH_free(dh);
        BN_free(p);
        BN_free(g);
        LogPrintf("TLS: %s: %s():%d - ERROR: p[%p], g[%p] (err=%s)\n",
            __FILE__, __func__, __LINE__, p, g, error_str);
        return NULL;
    }
    return dh;
}

DH *tmp_dh_callback(SSL *ssl, int is_export, int keylength)
{
    LogPrint("tls", "TLS: %s: %s():%d - Using Diffie-Hellman param for PFS: is_export=%d, keylength=%d\n",
        __FILE__, __func__, __LINE__, is_export, keylength);

    return get_dh2048(); 
}

/** if 'tls' debug category is enabled, collect info about certificates relevant to the passed context and print them on logs */
static void dumpCertificateDebugInfo(int preverify_ok, X509_STORE_CTX* chainContext)
{
    if (!LogAcceptCategory("tls") )
        return;

    char    buf[256] = {};
    X509   *cert;
    int     err, depth;

    cert = X509_STORE_CTX_get_current_cert(chainContext);
    err = X509_STORE_CTX_get_error(chainContext);
    depth = X509_STORE_CTX_get_error_depth(chainContext);

    LogPrintf("TLS: %s: %s():%d - preverify_ok=%d, errCode=%d, depth=%d\n",
        __FILE__, __func__, __LINE__, preverify_ok, err, depth);

    // is not useful checking preverify_ok because, after the chain root verification, it is set accordingly
    // to the return value of this callback, and we choose to always return 1 
    if (err != X509_V_OK )
    {
        LogPrintf("TLS: %s: %s():%d - Certificate Verification ERROR=%d: [%s] at chain depth=%d\n",
            __FILE__, __func__, __LINE__, err, X509_verify_cert_error_string(err), depth);

        if (cert && err == X509_V_ERR_CERT_HAS_EXPIRED)
        {
            struct tm t = {};
            const ASN1_TIME * at = X509_get0_notAfter(cert);
            int ret = ASN1_TIME_to_tm(at, &t);
            if (ret == 1)
            {
                (void)strftime(buf, sizeof (buf), "%c", &t);
                LogPrintf("TLS: %s: %s():%d - expired on=%s\n", 
                    __FILE__, __func__, __LINE__, buf);
            }
        }
    }
    else if (cert)
    {
        X509_NAME_oneline(X509_get_subject_name(cert), buf, 256);
        LogPrintf("TLS: %s: %s():%d - subj name=%s\n",
            __FILE__, __func__, __LINE__, buf);

        X509_NAME_oneline(X509_get_issuer_name(cert), buf, 256);
        LogPrintf("TLS: %s: %s():%d - issuer name=%s\n",
            __FILE__, __func__, __LINE__, buf);

        struct tm t = {};
        const ASN1_TIME * at = X509_get0_notAfter(cert);
        int ret = ASN1_TIME_to_tm(at, &t);
        if (ret == 1)
        {
            (void)strftime(buf, sizeof (buf), "%c", &t);
            LogPrintf("TLS: %s: %s():%d - expiring on=%s\n", 
                __FILE__, __func__, __LINE__, buf);
        }
    }
    else
    {
        // should never happen
        LogPrintf("TLS: %s: %s():%d - invalid cert/err\n", __FILE__, __func__, __LINE__);
    }
}

/**
* @brief If verify_callback always returns 1, the TLS/SSL handshake will not be terminated with respect to verification failures and the connection will be established.
* 
* @param preverify_ok 
* @param chainContext 
* @return int 
*/
int tlsCertVerificationCallback(int preverify_ok, X509_STORE_CTX* chainContext)
{
    dumpCertificateDebugInfo(preverify_ok, chainContext);

    /* The return value controls the strategy of the further verification process. If it returns 0
     * the verification process is immediately stopped with "verification failed" state.
     * If SSL_VERIFY_PEER has been set in set_verify, a verification failure alert is sent to the peer and the TLS/SSL
     * handshake is terminated.
     * If it returns 1, the verification process is continued.
     * Here we choose to continue the verification process by returning 1 and to leave the optional cert
     * verification if we call ValidatePeerCertificate().
     */
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
int TLSManager::waitFor(SSLConnectionRoutine eRoutine, const CAddress& peerAddress, SSL* ssl, int timeoutSec, unsigned long& err_code)
{
    std::string eRoutine_str{};
    int retOp{0};
    const SOCKET hSocket = SSL_get_fd(ssl);

    err_code = 0;
    fd_set socketSet;
    struct timeval timeout {
        timeoutSec, 0
    };

    FD_ZERO(&socketSet);
    FD_SET(hSocket, &socketSet);

    while (true) {

        // clear the current thread's error queue
        ERR_clear_error();
        retOp = 0;

        switch (eRoutine) {
        case SSL_CONNECT:
            eRoutine_str = "SSL_CONNECT";
            LogPrint("tls", "TLS: %s initiated, fd=%d, peer=%s\n", eRoutine_str, hSocket, peerAddress.ToString());
            retOp = SSL_connect(ssl);
            break;

        case SSL_ACCEPT:
            eRoutine_str = "SSL_ACCEPT";
            LogPrint("tls", "TLS: %s initiated, fd=%d, peer=%s\n", eRoutine_str, hSocket, peerAddress.ToString());
            retOp = SSL_accept(ssl);
            break;

        case SSL_SHUTDOWN:
            eRoutine_str = "SSL_SHUTDOWN";
            LogPrint("tls", "TLS: %s initiated, fd=%d, peer=%s\n", eRoutine_str, hSocket, peerAddress.ToString());
            {
                int shutDownStatus(SSL_get_shutdown(ssl)); // Bitmask of shutdown state
                if(shutDownStatus == 3 /* 1 & 2*/) {
                    retOp = 1;// already shut down
                } else {
                    for (int i{1}; i < 3; ++i) {
                        if (!(shutDownStatus & i)) {
                            retOp = SSL_shutdown(ssl); // Send/receive close_notify
                            MilliSleep(5);
                        }
                        shutDownStatus = SSL_get_shutdown(ssl);
                    }
                }
            }
            break;

        default:
            assert(false); // should never happen
        }

        if (retOp == 1 /*success for any of the three operations*/) {
            err_code = 0;
            LogPrint("tls", "TLS: %s completed, fd=%d, peer=%s\n", eRoutine_str, hSocket, peerAddress.ToString());
            break;
        }
        
        // Examine error
        int sslErr = SSL_get_error(ssl, retOp);

        // if (sslErr != SSL_ERROR_WANT_READ && sslErr != SSL_ERROR_WANT_WRITE) {
        //     err_code = ERR_get_error();
        //     const char* error_str = ERR_error_string(err_code, NULL);
        //     LogPrint("tls", "TLS: WARNING: %s: %s():%d - routine(%s), sslErr[0x%x], retOp[%d], errno[0x%x], lib[0x%x], func[0x%x], reas[0x%x]-> err: %s\n",
        //              __FILE__, __func__, __LINE__,
        //              eRoutine_str, sslErr, retOp, errno, ERR_GET_LIB(err_code), ERR_GET_FUNC(err_code), ERR_GET_REASON(err_code), error_str);
        //     retOp = -1;
        //     break;
        // }

        
        std::string ssl_error_str{};
        int result{0};

        switch (sslErr) {
        case SSL_ERROR_SSL:
            // Handle the case for shutdown sent while the peer still sending data after we've sent close_notify
            // we should temporarily ignore this error and continue reading until the peer closes the connection
            // For other errors we intentionally do fail (no retries)
            err_code = ERR_get_error();
            if (auto reason{ERR_GET_REASON(err_code)}; reason != SSL_R_APPLICATION_DATA_AFTER_CLOSE_NOTIFY) {
                ssl_error_str = "SSL_ERROR_SSL:" + std::to_string(reason);
                result = -1;
                break;
            }
            [[fallthrough]]; // Need to read more
        case SSL_ERROR_WANT_READ:
            ssl_error_str = "SSL_ERROR_WANT_READ";
            result = select(hSocket + 1, &socketSet, NULL, NULL, &timeout);
            break;
        case SSL_ERROR_WANT_WRITE:
            ssl_error_str = "SSL_ERROR_WANT_WRITE";
            result = select(hSocket + 1, NULL, &socketSet, NULL, &timeout);
            break;
        default:
            // For all othe errors we intentionally do fail (no retries)
            err_code = ERR_get_error();
            {
                const char* error_str = ERR_error_string(err_code, NULL);
                LogPrint("tls", "TLS: %s: %s():%d - routine(%s), sslErr[0x%x], retOp[%d], errno[0x%x], lib[0x%x], func[0x%x], reas[0x%x]-> err: %s\n",
                         __FILE__, __func__, __LINE__,
                         eRoutine_str, sslErr, retOp, errno, ERR_GET_LIB(err_code), ERR_GET_FUNC(err_code), ERR_GET_REASON(err_code), "unknown");
            }
            return -1;
        }

        if (result == 0 /*timeout*/) {
            LogPrint("tls", "TLS: %s: %s():%d - %s timeout on %s\n", __FILE__, __func__, __LINE__,
                     ssl_error_str, eRoutine_str);
            retOp = -1;
            break;
        } else if (result == -1 /*error*/) {
            LogPrint("tls", "TLS: %s: %s: %s ssl_err_code: 0x%x; errno: %s\n",
                     __FILE__, __func__, eRoutine_str, sslErr, strerror(errno));
            retOp = -1;
            break;
        }

        // Something happened on the socket hence we continue the loop

    }

    return retOp;
}

/**
 * @brief establish TLS connection to an address
 * 
 * @param hSocket socket
 * @param addrConnect the outgoing address
 * @param tls_ctx_client TLS Client context
 * @return SSL* returns a ssl* if successful, otherwise returns NULL.
 */
SSL* TLSManager::connect(SOCKET hSocket, const CAddress& addrConnect, unsigned long& err_code)
{
    LogPrint("tls", "TLS: establishing connection (tid = %X), (peerid = %s)\n", pthread_self(), addrConnect.ToString());

    err_code = 0;
    SSL* ssl = NULL;
    bool bConnectedTLS = false;

    if ((ssl = SSL_new(tls_ctx_client))) {
        if (SSL_set_fd(ssl, hSocket)) {
            int ret = TLSManager::waitFor(SSL_CONNECT, addrConnect, ssl, (DEFAULT_CONNECT_TIMEOUT / 1000), err_code);
            if (ret == 1)
            {
                bConnectedTLS = true;
            }
        }
    }
    else
    {
        err_code = ERR_get_error();
        const char* error_str = ERR_error_string(err_code, NULL);
        LogPrint("tls", "TLS: %s: %s():%d - SSL_new failed err: %s\n",
            __FILE__, __func__, __LINE__, error_str);
    }


    if (bConnectedTLS) {
        LogPrintf("TLS: connection to %s has been established (tlsv = %s 0x%04x / ssl = %s 0x%x ). Using cipher: %s\n",
            addrConnect.ToString(), SSL_get_version(ssl), SSL_version(ssl), OpenSSL_version(OPENSSL_VERSION), OpenSSL_version_num(), SSL_get_cipher(ssl));
    } else {
        LogPrintf("TLS: %s: %s():%d - TLS connection to %s failed (err_code 0x%X)\n",
            __FILE__, __func__, __LINE__, addrConnect.ToString(), err_code);

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
    LogPrintf("TLS: %s: %s():%d - Initializing %s context\n", 
         __FILE__, __func__, __LINE__, ctxType == SERVER_CONTEXT ? "server" : "client");

    if (!boost::filesystem::exists(privateKeyFile) ||
        !boost::filesystem::exists(certificateFile))
        return NULL;

    bool bInitialized = false;
    SSL_CTX* tlsCtx = NULL;

    if ((tlsCtx = SSL_CTX_new(ctxType == SERVER_CONTEXT ? TLS_server_method() : TLS_client_method()))) {
        SSL_CTX_set_mode(tlsCtx, SSL_MODE_AUTO_RETRY);

        // Disable TLS 1.0. and 1.1
        int ret = SSL_CTX_set_min_proto_version(tlsCtx, TLS1_2_VERSION);
        if (ret == 0)
        {
            LogPrintf("TLS: WARNING: %s: %s():%d - failed to set min TLS version\n", __FILE__, __func__, __LINE__);
        }

        LogPrintf("TLS: %s: %s():%d - setting cipher list\n", __FILE__, __func__, __LINE__);

        // sets the list of available ciphers (TLSv1.2 and below) offering perfect forward secrecy
        // and using RSA aut method (we have RSA keys)
        const char *cipher_list =
              "ECDHE-RSA-AES256-GCM-SHA384:DHE-RSA-AES256-GCM-SHA384:"
              "ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES128-GCM-SHA256";

        int ciph_ret = SSL_CTX_set_cipher_list(tlsCtx, cipher_list);
        if (ciph_ret == 0) {
            LogPrintf("TLS: WARNING: %s: %s():%d - failed to set cipher list for TLSv1.2\n", __FILE__, __func__, __LINE__);
        }
        // TLS 1.3 has ephemeral Diffie-Hellman as the only key exchange mechanism, so that perfect forward
        // secrecy is ensured.

        if (ctxType == SERVER_CONTEXT)
        {
            // amongst the Cl/Srv mutually-acceptable set, pick the one that the server prefers most instead of the one that
            // the client prefers most
            SSL_CTX_set_options(tlsCtx, SSL_OP_CIPHER_SERVER_PREFERENCE);

            LogPrintf("TLS: %s: %s():%d - setting dh callback\n", __FILE__, __func__, __LINE__);
            SSL_CTX_set_tmp_dh_callback(tlsCtx, tmp_dh_callback);
        }

        // Fix for Secure Client-Initiated Renegotiation DoS threat
        SSL_CTX_set_options(tlsCtx, SSL_OP_NO_RENEGOTIATION);

        int min_ver = SSL_CTX_get_min_proto_version(tlsCtx);
        int max_ver = SSL_CTX_get_max_proto_version(tlsCtx); // 0x0 means auto
        int opt_mask = SSL_CTX_get_options(tlsCtx);

        LogPrintf("TLS: proto version: min/max 0x%04x/0x04%x, opt_mask=0x%x\n", min_ver, max_ver, opt_mask);

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
SSL* TLSManager::accept(SOCKET hSocket, const CAddress& addr, unsigned long& err_code)
{
    LogPrint("tls", "TLS: accepting connection from %s (tid = %X)\n", addr.ToString(), pthread_self());

    err_code = 0; 
    SSL* ssl = NULL;
    bool bAcceptedTLS = false;

    if ((ssl = SSL_new(tls_ctx_server))) {
        if (SSL_set_fd(ssl, hSocket)) {
            int ret = TLSManager::waitFor(SSL_ACCEPT, addr, ssl, (DEFAULT_CONNECT_TIMEOUT / 1000), err_code);
            if (ret == 1)
            {
                bAcceptedTLS = true;
            }
        }
    }
    else
    {
        err_code = ERR_get_error();
        const char* error_str = ERR_error_string(err_code, NULL);
        LogPrint("tls", "TLS: %s: %s():%d - SSL_new failed err: %s\n",
            __FILE__, __func__, __LINE__, error_str);
    }

    if (bAcceptedTLS) {
        LogPrintf("TLS: connection from %s has been accepted (tlsv = %s 0x%04x / ssl = %s 0x%x ). Using cipher: %s\n",
            addr.ToString(), SSL_get_version(ssl), SSL_version(ssl), OpenSSL_version(OPENSSL_VERSION), OpenSSL_version_num(), SSL_get_cipher(ssl));

        STACK_OF(SSL_CIPHER) *sk = SSL_get_ciphers(ssl); 
        for (int i = 0; i < sk_SSL_CIPHER_num(sk); i++) {
            const SSL_CIPHER *c = sk_SSL_CIPHER_value(sk, i);
            LogPrint("tls", "TLS: supporting cipher: %s\n", SSL_CIPHER_get_name(c));
        }
    } else {
        LogPrintf("TLS: %s: %s():%d - TLS connection from %s failed (err_code 0x%X)\n",
            __FILE__, __func__, __LINE__, addr.ToString(), err_code);

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
            LogPrint("tls", "TLS: Node %s is deleted from the non-TLS pool\n", nodeAddr.ipAddr);
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
                // maximum record size is 16kB for SSL/TLS (still valid as of 1.1.1 version)
                char pchBuf[0x10000];
                bool bIsSSL = false;
                int nBytes = 0, nRet = 0;

                {
                    LOCK(pnode->cs_hSocket);

                    if (pnode->hSocket == INVALID_SOCKET) {
                        LogPrint("tls", "Receive: connection with %s is already closed\n", pnode->addr.ToString());
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

                    if (bIsSSL) {
                        unsigned long error = ERR_get_error();
                        const char* error_str = ERR_error_string(error, NULL);
                        LogPrint("tls", "TLS: WARNING: %s: %s():%d - SSL_read err: %s\n",
                            __FILE__, __func__, __LINE__, error_str);
                    }
                    // socket closed gracefully (peer disconnected)
                    //
                    if (!pnode->fDisconnect)
                        LogPrint("tls", "socket closed (%s)\n", pnode->addr.ToString());
                    pnode->CloseSocketDisconnect();


                } else if (nBytes < 0) {
                    // error
                    //
                    if (bIsSSL) {
                        if (nRet != SSL_ERROR_WANT_READ && nRet != SSL_ERROR_WANT_WRITE) // SSL_read() operation has to be repeated because of SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE (https://wiki.openssl.org/index.php/Manual:SSL_read(3)#NOTES)
                        {
                            if (!pnode->fDisconnect)
                                LogPrintf("TLS: ERROR: SSL_read %s\n", ERR_error_string(nRet, NULL));
                            pnode->CloseSocketDisconnect();

                            unsigned long error = ERR_get_error();
                            const char* error_str = ERR_error_string(error, NULL);
                            LogPrint("tls", "TLS: WARNING: %s: %s():%d - SSL_read - code[0x%x], err: %s\n",
                                __FILE__, __func__, __LINE__, nRet, error_str);

                        } else {
                            // preventive measure from exhausting CPU usage
                            //
                            MilliSleep(1); // 1 msec
                        }
                    } else {
                        if (nRet != WSAEWOULDBLOCK && nRet != WSAEMSGSIZE && nRet != WSAEINTR && nRet != WSAEINPROGRESS) {
                            if (!pnode->fDisconnect)
                                LogPrintf("TLS: ERROR: socket recv %s\n", NetworkErrorString(nRet));
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
            LogPrint("tls", "TLS: contexts are initialized\n");
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
