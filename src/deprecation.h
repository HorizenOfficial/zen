// Copyright (c) 2017 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZCASH_DEPRECATION_H
#define ZCASH_DEPRECATION_H

static const int APPROX_RELEASE_HEIGHT = 384800;
static const int WEEKS_UNTIL_DEPRECATION = 16;
static const int DEPRECATION_HEIGHT = APPROX_RELEASE_HEIGHT + (WEEKS_UNTIL_DEPRECATION * 7 * 24 * 24);

// Number of blocks before deprecation to warn users
static const int DEPRECATION_WARN_LIMIT = 14 * 24 * 24; // 2 weeks

/**
 * Checks whether the node is deprecated based on the current block height, and
 * shuts down the node with an error if so (and deprecation is not disabled for
 * the current client version).
 */
void EnforceNodeDeprecation(int nHeight, bool forceLogging=false);

#endif // ZCASH_DEPRECATION_H
