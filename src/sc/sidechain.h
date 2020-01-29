#ifndef _SIDECHAIN_CORE_H
#define _SIDECHAIN_CORE_H

#include "sc/sidechaintypes.h"
#include <coins.h>

//------------------------------------------------------------------------------------
class CTransaction;
class CValidationState;
class uint256;
class CTxMemPool;
class CBlockUndo;

namespace Sidechain
{
// Validation functions
bool checkTxSemanticValidity(const CTransaction& tx, CValidationState& state);
bool anyForwardTransaction(const CTransaction& tx, const uint256& scId);
bool existsInMempool(const CTxMemPool& pool, const CTransaction& tx, CValidationState& state);
// End of Validation functions

class CSidechainsViewCache : public CCoinsViewCache
{
public:
    CSidechainsViewCache(CCoinsView *cView);
    CSidechainsViewCache(const CSidechainsViewCache&) = delete;             //as in coins, forbid building cache on top of another
    CSidechainsViewCache& operator=(const CSidechainsViewCache &) = delete;

private:
    void Dump_info() const;
};

}; // end of namespace

#endif // _SIDECHAIN_CORE_H
