// Copyright (c) 2018-2020 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef REPLAYPROTECTIONLEVEL_H
#define REPLAYPROTECTIONLEVEL_H

enum ReplayProtectionLevel {
    RPLEVEL_NONE = 0,       /**< No replay protection */
    RPLEVEL_BASIC,          /**< Original (buggy) replay protection */
    RPLEVEL_FIXED_1,        /**< replay protection: patch 1 */
    RPLEVEL_FIXED_2         /**< replay protection: patch 2 */
};

#endif // REPLAYPROTECTIONLEVEL_H
