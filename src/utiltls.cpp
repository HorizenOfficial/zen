// Copyright (c) 2017 The Zen Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdio.h>

#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

#include "util.h"
#include "utiltls.h"

// Generates RSA keypair (a private key of 'bits' length for a specified 'uPublicKey')
//
EVP_PKEY* GenerateRsaKey(int bits, BN_ULONG uPublicKey)
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
X509* GenerateCertificate(EVP_PKEY *keypair)
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
bool StoreKey(EVP_PKEY *key, const boost::filesystem::path &filePath)
{
    if (!key)
        return false;
    
    bool bStored = false;
    
    FILE *keyfd = fopen(filePath.string().c_str(), "wb");
    if (keyfd)
    {
        // TODO: add encryption, using user password
        bStored = PEM_write_PrivateKey(keyfd, key, NULL, NULL, 0, NULL, NULL);
        fclose(keyfd);
    }
    
    return bStored;
}

// Stores certificate to file, specified by the 'filePath'
//
bool StoreCertificate(X509 *cert, const boost::filesystem::path &filePath)
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
EVP_PKEY* LoadKey(const boost::filesystem::path &filePath)
{
    if (!boost::filesystem::exists(filePath))
        return NULL;

    EVP_PKEY *key = NULL;
    FILE *keyfd = fopen(filePath.string().c_str(), "rb");
    if (keyfd)
    {
        // TODO: add decryption, using user password
        key = PEM_read_PrivateKey(keyfd, NULL, NULL, NULL);
        fclose(keyfd);
    }
    
    return key;
}

// Loads certificate from file, specified by the 'filePath'
//
X509* LoadCertificate(const boost::filesystem::path &filePath)
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

// Checks the correctness of a private-public key pair and the validity of a certificate using public key from key pair
//
bool CheckCredentials(EVP_PKEY *key, X509 *cert)
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
    
    // Verifies the signature of certificate using public key;
    // Only the signature is checked: no other checks (such as certificate chain validity) are performed.
    if (bIsOk)
        bIsOk = (X509_verify(cert, key) == 1);
    
    return bIsOk;
}

// Prepares credentials (a private key and a certificate for corresponding public key)
//
// Tries to load key and certificate from files located in the 'credentialsPath' directory.
// If some of files is absent, or has a wrong format, then generates public keypair and a self-signed certificate for it, and then stores them in the 'credentialsPath' directory.
//
bool PrepareCredentials(const boost::filesystem::path &credentialsPath)
{
    bool bPrepared = false;

    if (!boost::filesystem::is_directory(credentialsPath))
        return false;

    boost::filesystem::path keyPath = credentialsPath / TLS_KEY_FILE_NAME;
    boost::filesystem::path certPath = credentialsPath / TLS_CERT_FILE_NAME;
    
    EVP_PKEY *key = NULL;
    X509 *cert = NULL;
    
    key = LoadKey(keyPath);
    
    if (key)
        cert = LoadCertificate(certPath);
    
    if (key && cert)
        bPrepared = CheckCredentials(key, cert);
    
    if (key)
        EVP_PKEY_free(key);
    if (cert)
        X509_free(cert);
    
    // TODO: return error if either key or cert is available, we don't want to rewrite existing key data
    if (!bPrepared)
    {
        // Generating RSA key and self-signed certificate for it
        //
        // TODO: avoid usage of short public keys (generate public key of a size corresponding to a private key size)
        key = GenerateRsaKey(2048, RSA_F4);
        if (key)
        {
            cert = GenerateCertificate(key);
            if (cert)
            {
                if (StoreKey(key, keyPath) &&
                    StoreCertificate(cert, certPath))
                {
                    bPrepared = true;
                    LogPrintStr("New private key and self-signed certificate were generated successfully\n");
                }
                
                X509_free(cert);
            }
            EVP_PKEY_free(key);
        }
    }
    
    return bPrepared;
}