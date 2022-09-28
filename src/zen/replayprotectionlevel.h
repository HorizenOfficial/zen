#ifndef REPLAYPROTECTIONLEVEL_H
#define REPLAYPROTECTIONLEVEL_H

enum ReplayProtectionLevel
{
    RPLEVEL_NONE = 0, /**< No replay protection */
    RPLEVEL_BASIC,    /**< Original (buggy) replay protection */
    RPLEVEL_FIXED_1,  /**< replay protection: patch 1 */
    RPLEVEL_FIXED_2   /**< replay protection: patch 2 */
};

#endif  // REPLAYPROTECTIONLEVEL_H
