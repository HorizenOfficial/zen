// Copyright (c) 2018 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
namespace zen
{
typedef enum { SSL_ACCEPT,
               SSL_CONNECT,
               SSL_SHUTDOWN } SSLConnectionRoutine;
typedef enum { CLIENT_CONTEXT,
               SERVER_CONTEXT } TLSContextType;
}
