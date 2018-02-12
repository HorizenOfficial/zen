#ifndef REPLAYPROTECTIONLEVEL_H
#define REPLAYPROTECTIONLEVEL_H

enum ReplayProtectionLevel {
    RPLEVEL_NONE = 0,       /**< No replay protection */
    RPLEVEL_BASIC,          /**< Original (buggy) replay protection */
    RPLEVEL_FIXED           /**< Fixed replay protection */
};

#endif // REPLAYPROTECTIONLEVEL_H
