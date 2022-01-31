#ifndef _SIDECHAINVERSIONFORK_H
#define _SIDECHAINVERSIONFORK_H

#include "fork8_sidechainfork.h"

namespace zen {

class SidechainVersionFork : public SidechainFork
{
public:
    SidechainVersionFork();

    /**
     * @brief Get the maximum allowed sidechain version for a specific block height
     */
    inline virtual uint8_t getMaxSidechainVersion() const { return 1; }
};

}
#endif // _SIDECHAINVERSIONFORK_H
