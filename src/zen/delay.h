#include "pow.h"
#include "arith_uint256.h"
#include "primitives/block.h"
#include "streams.h"
#include "uint256.h"
#include "util.h"
#include "chain.h"
#include "chainparams.h"
#include "tinyformat.h"
#include <boost/foreach.hpp>

int GetBlockDelay (const CBlockIndex& newBlock,const CBlockIndex& prevBlock, const int activeChainHeight, bool isStartupSyncing);
bool IsChainPenalised (const CChain& chain);
