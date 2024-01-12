// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2016-2017 The Zcash developers
// Copyright (c) 2017 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CLIENTVERSION_H
#define BITCOIN_CLIENTVERSION_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#else

/**
 * client versioning and copyright year
 */

//The components are used by convention as follows:
//
//Major: Assemblies with the same name but different major versions are not interchangeable. A higher version number
//       might indicate a major rewrite of a product where backward compatibility cannot be assumed.
//
//Minor: If the name and major version number on two assemblies are the same, but the minor version number is different,
//       this indicates significant enhancement with the intention of backward compatibility. This higher minor version
//       number might indicate a point release of a product or a fully backward-compatible new version of a product.
//
//Build: A difference in build number represents a recompilation of the same source. Different build numbers might be
//       used when the processor, platform, or compiler changes.
//
//Revision: Assemblies with the same name, major, and minor version numbers but different revisions are intended to be
//          fully interchangeable. A higher revision number might be used in a build that fixes a security hole in
//          a previously released assembly.

//! These need to be macros, as clientversion.cpp's and bitcoin*-res.rc's voodoo requires it
#define CLIENT_VERSION_MAJOR 5
#define CLIENT_VERSION_MINOR 0
#define CLIENT_VERSION_REVISION 0
#define CLIENT_VERSION_BUILD 25

//! Set to true for release, false for prerelease or test build
#define CLIENT_VERSION_IS_RELEASE false

/**
 * Copyright year (2009-this)
 * Todo: update this when changing our copyright comments in the source
 */
#define COPYRIGHT_YEAR 2024

#endif //HAVE_CONFIG_H

/**
 * Converts the parameter X to a string after macro replacement on X has been performed.
 * Don't merge these into one macro!
 */
#define STRINGIZE(X) DO_STRINGIZE(X)
#define DO_STRINGIZE(X) #X

//! Copyright string used in Windows .rc files
#define COPYRIGHT_STR "2009-" STRINGIZE(COPYRIGHT_YEAR) " The Bitcoin Core Developers, The Zcash developers, and Zen Blockchain Foundation"

/**
 * bitcoind-res.rc includes this file, but it cannot cope with real c++ code.
 * WINDRES_PREPROC is defined to indicate that its pre-processor is running.
 * Anything other than a define should be guarded below.
 */

#if !defined(WINDRES_PREPROC)

#include <string>
#include <vector>

static const int CLIENT_VERSION =
                           1000000 * CLIENT_VERSION_MAJOR
                         +   10000 * CLIENT_VERSION_MINOR
                         +     100 * CLIENT_VERSION_REVISION
                         +       1 * CLIENT_VERSION_BUILD;

extern const std::string CLIENT_NAME;
extern const std::string CLIENT_BUILD;
extern const std::string CLIENT_DATE;


std::string FormatVersion(int nVersion);
std::string FormatFullVersion();
std::string FormatSubVersion(const std::string& name, int nClientVersion, const std::vector<std::string>& comments);

#endif // WINDRES_PREPROC

#endif // BITCOIN_CLIENTVERSION_H
