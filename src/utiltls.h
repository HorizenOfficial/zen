// Copyright (c) 2017 The Zen Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef UTILTLS_H
#define UTILTLS_H

#include <boost/filesystem/path.hpp>

#define TLS_KEY_FILE_NAME   "key.pem"    // default name of a private key
#define TLS_CERT_FILE_NAME  "cert.pem"   // default name of a certificate

#define CERT_VALIDITY_DAYS  (365 * 10)   // period of validity, in days, for a self-signed certificate

#define TLS_RSA_KEY_SIZE    2048         // size of a private RSA key, in bits, that will be generated, if no other key is specified

typedef enum {credOk, credNonConsistent, credAbsent, credPartiallyAbsent} CredentialsStatus;

// Verifies credentials (a private key, a certificate for public key and a correspondence between the private and the public key)
//
CredentialsStatus VerifyCredentials(
        const boost::filesystem::path &keyPath,
        const boost::filesystem::path &certPath,
        const std::string             &passphrase);

// Generates public key pair and the self-signed certificate for it, and then stores them by the specified paths 'keyPath' and 'certPath' respectively.
//
bool GenerateCredentials(
        const boost::filesystem::path &keyPath,
        const boost::filesystem::path &certPath,
        const std::string             &passphrase);

// Checks if certificate of a peer is valid (by internal means of the TLS protocol)
//
// Validates peer certificate using a chain of CA certificates.
// If some of intermediate CA certificates are absent in the trusted certificates store, then validation status will be 'false')
//
bool ValidatePeerCertificate(SSL *ssl);

// Check if a given context is set up with a cert that can be validated by this context
//
bool ValidateCertificate(SSL_CTX *ssl_ctx);

// Creates the list of available OpenSSL default directories for trusted certificates storage
//
std::vector<boost::filesystem::path> GetDefaultTrustedDirectories();

// Loads default root certificates (placed in the 'defaultRootCerts') into the specified context.
// Returns the number of loaded certificates.
//
int LoadDefaultRootCertificates(SSL_CTX *ctx);

#endif // UTILTLS_H