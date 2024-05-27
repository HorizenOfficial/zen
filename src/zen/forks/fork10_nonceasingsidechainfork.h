// Copyright (c) 2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef _NONCEASINGSIDECHAINSFORK_H
#define _NONCEASINGSIDECHAINSFORK_H

#include "fork9_sidechainversionfork.h"

namespace zen {

class NonCeasingSidechainFork : public SidechainVersionFork
{
public:
    NonCeasingSidechainFork();

    /**
     * @brief Get the maximum allowed sidechain version for a specific block height
     */
    inline virtual uint8_t getMaxSidechainVersion() const { return 2; }
    inline virtual bool isNonCeasingSidechainActive() const { return true; }
};

}
#endif // _NONCEASINGSIDECHAINSFORK_H
