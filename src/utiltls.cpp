// Copyright (c) 2017 The Zen Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdio.h>
#include <vector>

#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

#include "util.h"
#include "utiltls.h"

// Set of most common default trusted certificates directories used by OpenSSL
static const char* defaultTrustedDirs[] =
{
#ifdef WIN32
    ""
#elif MAC_OSX
    "/System/Library/OpenSSL/certs"
#else // Linux build
    "/etc/ssl/certs",
    "/usr/local/ssl/certs",
    "/usr/lib/ssl/certs",
    "/usr/share/ssl/certs",
    "/etc/pki/tls/certs",
    "/var/lib/ca-certificates"
#endif
};

// Default root certificates (PEM encoded)
static const char defaultRootCerts[] =
{
//    // Example of specifying a certificate
//    //
//    "-----BEGIN CERTIFICATE-----\n"
//    "MIIDYDCCAkigAwIBAgIJAJMakdoBYY67MA0GCSqGSIb3DQEBCwUAMEUxCzAJBgNV\n"
//    "BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
//    "aWRnaXRzIFB0eSBMdGQwHhcNMTcwODE0MTc0MTMyWhcNNDQxMjMwMTc0MTMyWjBF\n"
//    "MQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50\n"
//    "ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIB\n"
//    "CgKCAQEAzNV+SPRCKSEGlntfpCRMVSfz99NoEo3K1SRyw6GTSb1LNSTQCn1EsCSH\n"
//    "cVZTmyfjcTHpwz4aF14yw8lQC42f218AOsG1DV5suCaUXhSmZlajMkvEJVwfBOft\n"
//    "xpcqE1fA9wovXlnJLXVgyJGMc896S8tcbrCU/l/BsqKh5QX8N60MQ3w376nSGvVP\n"
//    "ussN8bVH3aKRwjhateqx1GRt0GPnM8/u7EkgF8Bc+m8WZYcUfkPC5Am2D0MO1HOA\n"
//    "u3IKxXZMs/fYd6nF5DZBwg+D23EP/V8oqenn8ilvrSORq5PguOl1QoDyY66PhmjN\n"
//    "L9c4Spxw8HXUDlrfuSQn2NJnw1XhdQIDAQABo1MwUTAdBgNVHQ4EFgQU/KD+n5Bz\n"
//    "QLbp09qKzwwyNwOQU4swHwYDVR0jBBgwFoAU/KD+n5BzQLbp09qKzwwyNwOQU4sw\n"
//    "DwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAVtprBxZD6O+WNYUM\n"
//    "ksdKiVVoszEJXlt7wajuaPBPK/K3buxE9FLVxS+LiH1PUhPCc6V28guyKWwn109/\n"
//    "4WnO51LQjygvd7SaePlbiO7iIatkOk4oETJQZ+tEJ7fv/NITY/GQUfgPNkANmPPz\n"
//    "Mz9I6He8XhIpO6NGuDG+74aR1RhvR3PWJJYT0QpL0STVR4qTc/HfnymF5XnnjOYZ\n"
//    "mwzT8jXX5dhLYwJmyPBS+uv+oa1quM/FitA63N9anYtRBiPaBtund9Ikjat1hM0h\n"
//    "neo2tz7Mfsgjb0aiORtiyaH2OetvwR0QuCSVPnknkfGWPDINdUdkgKyA1PX58Smw\n"
//    "vaXEcw==\n"
//    "-----END CERTIFICATE-----"

        ""
};

// Generates RSA keypair (a private key of 'bits' length for a specified 'uPublicKey')
//
static EVP_PKEY* GenerateRsaKey(int bits, BN_ULONG uPublicKey)
{
    EVP_PKEY *evpPrivKey = NULL;

    BIGNUM *pubKey = BN_new();
    if (pubKey)
    {
        if (BN_set_word(pubKey, uPublicKey))
        {
            RSA *privKey = RSA_new();
            if (privKey)
            {
                if (RAND_poll() &&  // The pseudo-random number generator must be seeded prior to calling RSA_generate_key_ex(). (https://www.openssl.org/docs/man1.1.0/crypto/RSA_generate_key.html)
                    RSA_generate_key_ex(privKey, bits, pubKey, NULL))
                {
                    if ((evpPrivKey = EVP_PKEY_new()))
                    {
                        if (!EVP_PKEY_assign_RSA(evpPrivKey, privKey))
                        {
                            EVP_PKEY_free(evpPrivKey);
                            evpPrivKey = NULL;
                        }
                    }
                }

                if(!evpPrivKey) // EVP_PKEY_assign_RSA uses the supplied key internally
                    RSA_free(privKey);
            }
        }
        BN_free(pubKey);
    }

    return evpPrivKey;
}

// Generates certificate for a specified public key using a corresponding private key (both of them should be specified in the 'keypair').
//
static X509* GenerateCertificate(EVP_PKEY *keypair)
{
    if (!keypair)
        return NULL;

    X509 *cert = X509_new();
    if (cert)
    {
        bool bCertSigned = false;
        long sn = 0;

        if (RAND_bytes((unsigned char*)&sn, sizeof sn) &&
            ASN1_INTEGER_set(X509_get_serialNumber(cert), sn))
        {
            X509_gmtime_adj(X509_get_notBefore(cert), 0);
            X509_gmtime_adj(X509_get_notAfter(cert), (60 * 60 * 24 * CERT_VALIDITY_DAYS));

            // setting a public key from the keypair
            if (X509_set_pubkey(cert, keypair))
            {
                X509_NAME *subjectName = X509_get_subject_name(cert);
                if (subjectName)
                {
                    // an issuer name is the same as a subject name, due to certificate is self-signed
                    if (X509_set_issuer_name(cert, subjectName))
                    {
                        // private key from keypair is used; signature will be set inside of the cert
                        bCertSigned = X509_sign(cert, keypair, EVP_sha512());
                    }
                }
            }
        }

        if (!bCertSigned)
        {
            X509_free(cert);
            cert = NULL;
        }
    }

    return cert;
}

// Stores key to file, specified by the 'filePath'
//
static bool StoreKey(EVP_PKEY *key, const boost::filesystem::path &filePath, const std::string &passphrase)
{
    if (!key)
        return false;

    bool bStored = false;

    FILE *keyfd = fopen(filePath.string().c_str(), "wb");
    if (keyfd)
    {
        const EVP_CIPHER* pCipher = NULL;

        if (passphrase.length() && (pCipher = EVP_aes_256_cbc()))
            bStored = PEM_write_PrivateKey(keyfd, key, pCipher, NULL, 0, NULL, (void*)passphrase.c_str());
        else
            bStored = PEM_write_PrivateKey(keyfd, key, NULL, NULL, 0, NULL, NULL);

        fclose(keyfd);
    }

    return bStored;
}

// Stores certificate to file, specified by the 'filePath'
//
static bool StoreCertificate(X509 *cert, const boost::filesystem::path &filePath)
{
    if (!cert)
        return false;

    bool bStored = false;

    FILE *certfd = fopen(filePath.string().c_str(), "wb");
    if (certfd)
    {
        bStored = PEM_write_X509(certfd, cert);
        fclose(certfd);
    }

    return bStored;
}

// Loads key from file, specified by the 'filePath'
//
static EVP_PKEY* LoadKey(const boost::filesystem::path &filePath, const std::string &passphrase)
{
    if (!boost::filesystem::exists(filePath))
        return NULL;

    EVP_PKEY *key = NULL;
    FILE *keyfd = fopen(filePath.string().c_str(), "rb");
    if (keyfd)
    {
        key = PEM_read_PrivateKey(keyfd, NULL, NULL, passphrase.length() ? (void*)passphrase.c_str() : NULL);
        fclose(keyfd);
    }

    return key;
}

// Loads certificate from file, specified by the 'filePath'
//
static X509* LoadCertificate(const boost::filesystem::path &filePath)
{
    if (!boost::filesystem::exists(filePath))
        return NULL;

    X509 *cert = NULL;
    FILE *certfd = fopen(filePath.string().c_str(), "rb");
    if (certfd)
    {
        cert = PEM_read_X509(certfd, NULL, NULL, NULL);
        fclose(certfd);
    }

    return cert;
}

// Verifies if the private key in 'key' matches the public key in 'cert'
// (Signs random bytes on 'key' and verifies signature correctness on public key from 'cert')
//
static bool IsMatching(EVP_PKEY *key, X509 *cert)
{
    if (!key || !cert)
        return false;

    bool bIsMatching = false;

    EVP_PKEY_CTX *ctxSign = EVP_PKEY_CTX_new(key, NULL);
    if (ctxSign)
    {
        if (EVP_PKEY_sign_init(ctxSign) == 1 &&
            EVP_PKEY_CTX_set_signature_md(ctxSign, EVP_sha512()) > 0)
        {
            unsigned char digest[SHA512_DIGEST_LENGTH] = { 0 };
            size_t digestSize = sizeof digest, signatureSize = 0;

            if (RAND_bytes((unsigned char*)&digest, digestSize) && // set random bytes as a digest
                EVP_PKEY_sign(ctxSign, NULL, &signatureSize, digest, digestSize) == 1) // determine buffer length
            {
                unsigned char *signature = (unsigned char*)OPENSSL_malloc(signatureSize);
                if (signature)
                {
                    if (EVP_PKEY_sign(ctxSign, signature, &signatureSize, digest, digestSize) == 1)
                    {
                        EVP_PKEY *pubkey = X509_get_pubkey(cert);
                        if (pubkey)
                        {
                            EVP_PKEY_CTX *ctxVerif = EVP_PKEY_CTX_new(pubkey, NULL);
                            if (ctxVerif)
                            {
                                if (EVP_PKEY_verify_init(ctxVerif) == 1 &&
                                    EVP_PKEY_CTX_set_signature_md(ctxVerif, EVP_sha512()) > 0)
                                {
                                    bIsMatching = (EVP_PKEY_verify(ctxVerif, signature, signatureSize, digest, digestSize) == 1);
                                }
                                EVP_PKEY_CTX_free(ctxVerif);
                            }
                            EVP_PKEY_free(pubkey);
                        }
                    }
                    OPENSSL_free(signature);
                }
            }
        }
        EVP_PKEY_CTX_free(ctxSign);
    }

    return bIsMatching;
}

// Checks the correctness of a private-public key pair and the validity of a certificate using public key from key pair
//
static bool CheckCredentials(EVP_PKEY *key, X509 *cert)
{
    if (!key || !cert)
        return false;

    bool bIsOk = false;

    // Validating the correctness of a private-public key pair, depending on a key type
    //
    switch (EVP_PKEY_base_id(key))
    {
        case EVP_PKEY_RSA:
        case EVP_PKEY_RSA2:
        {
            RSA *rsaKey = EVP_PKEY_get1_RSA(key);
            if (rsaKey)
            {
                bIsOk = (RSA_check_key(rsaKey) == 1);
                RSA_free(rsaKey);
            }
            break;
        }

        // Currently only RSA keys are supported.
        // Other key types can be added here in further.

        default:
            bIsOk = false;
    }

    // Verifying if the private key matches the public key in certificate
    if (bIsOk)
        bIsOk = IsMatching(key, cert);

    return bIsOk;
}

// Verifies credentials (a private key, a certificate for public key and a correspondence between the private and the public key)
//
CredentialsStatus VerifyCredentials(
        const boost::filesystem::path &keyPath,
        const boost::filesystem::path &certPath,
        const std::string             &passphrase)
{
    CredentialsStatus status = credAbsent;

    EVP_PKEY *key = NULL;
    X509 *cert = NULL;

    key  = LoadKey(keyPath, passphrase);
    cert = LoadCertificate(certPath);
    
    if (key && cert)
        status = CheckCredentials(key, cert) ? credOk : credNonConsistent;
    else if (!key && !cert)
        status = credAbsent;
    else
        status = credPartiallyAbsent;

    if (key)
        EVP_PKEY_free(key);
    if (cert)
        X509_free(cert);

    return status;
}

// Generates public key pair and the self-signed certificate for it, and then stores them by the specified paths 'keyPath' and 'certPath' respectively.
//
bool GenerateCredentials(
        const boost::filesystem::path &keyPath,
        const boost::filesystem::path &certPath,
        const std::string             &passphrase)
{
    bool bGenerated = false;

    EVP_PKEY *key = NULL;
    X509 *cert = NULL;

    // Generating RSA key and the self-signed certificate for it
    //
    key = GenerateRsaKey(TLS_RSA_KEY_SIZE, RSA_F4);
    if (key)
    {
        cert = GenerateCertificate(key);
        if (cert)
        {
            if (StoreKey(key, keyPath, passphrase) &&
                StoreCertificate(cert, certPath))
            {
                bGenerated = true;
                LogPrintStr("TLS: New private key and self-signed certificate were generated successfully\n");
            }

            X509_free(cert);
        }
        EVP_PKEY_free(key);
    }

    return bGenerated;
}

// Checks if certificate of a peer is valid (by internal means of the TLS protocol)
//
// Validates peer certificate using a chain of CA certificates.
// If some of intermediate CA certificates are absent in the trusted certificates store, then validation status will be 'false')
//
bool ValidatePeerCertificate(SSL *ssl)
{
    if (!ssl)
        return false;

    bool bIsOk = false;

    X509 *cert = SSL_get_peer_certificate (ssl);
    if (cert)
    {
        // NOTE: SSL_get_verify_result() is only useful in connection with SSL_get_peer_certificate (https://www.openssl.org/docs/man1.0.2/ssl/SSL_get_verify_result.html)
        //
        bIsOk = (SSL_get_verify_result(ssl) == X509_V_OK);
        X509_free(cert);
    }
    else
    {
        LogPrint("net", "TLS: Peer does not have certificate\n");
        bIsOk = false;
    }
    return bIsOk;
}

// Check if a given context is set up with a cert that can be validated by this context
//
bool ValidateCertificate(SSL_CTX *ssl_ctx)
{
    if (!ssl_ctx)
        return false;

    bool bIsOk = false;

    X509_STORE *store = SSL_CTX_get_cert_store(ssl_ctx);

    if (store)
    {
        X509_STORE_CTX *ctx = X509_STORE_CTX_new();
        if (ctx)
        {
            if (X509_STORE_CTX_init(ctx, store, SSL_CTX_get0_certificate(ssl_ctx), NULL) == 1)
                bIsOk = X509_verify_cert(ctx) == 1;

            X509_STORE_CTX_free(ctx);
        }
    }

    return bIsOk;
}

// Creates the list of available OpenSSL default directories for trusted certificates storage
//
std::vector<boost::filesystem::path> GetDefaultTrustedDirectories()
{
    namespace fs = boost::filesystem;
    std::vector<fs::path> defaultDirectoriesList;

    // Default certificates directory specified in OpenSSL build
    fs::path libDefaultDir = X509_get_default_cert_dir();

    if (fs::exists(libDefaultDir))
        defaultDirectoriesList.push_back(libDefaultDir);

    // Check and set all possible standard default directories
    for (const char *dir : defaultTrustedDirs)
    {
        fs::path defaultDir(dir);

        if (defaultDir != libDefaultDir &&
            fs::exists(defaultDir))
            defaultDirectoriesList.push_back(defaultDir);
    }

    return defaultDirectoriesList;
}

// Loads default root certificates (placed in the 'defaultRootCerts') into the specified context.
// Returns the number of loaded certificates.
//
int LoadDefaultRootCertificates(SSL_CTX *ctx)
{
    if (!ctx)
        return 0;

    int certsLoaded = 0;

    // Certificate text buffer 'defaultRootCerts' is a C string with certificates in PEM format
    BIO *memBuf = BIO_new_mem_buf(defaultRootCerts, -1);
    if (memBuf)
    {
        X509 *cert = NULL;
        while ((cert = PEM_read_bio_X509(memBuf, NULL, 0, NULL)))
        {
            if (X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), cert) > 0)
                certsLoaded++;

            X509_free(cert);
        }
        BIO_free(memBuf);
    }

    return certsLoaded;
}