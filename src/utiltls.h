// Copyright (c) 2017 The Zen Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef UTILTLS_H
#define UTILTLS_H

#include <boost/filesystem/path.hpp>

#define TLS_KEY_FILE_NAME "key.pem"
#define TLS_CERT_FILE_NAME "cert.pem"

#define CERT_VALIDITY_DAYS (365 * 10) // period of validity, in days, for self-signed certificate

bool PrepareCredentials(const boost::filesystem::path &credentialsPath);

#endif // UTILTLS_H