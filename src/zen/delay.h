#include <boost/foreach.hpp>

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "primitives/block.h"
#include "streams.h"
#include "tinyformat.h"
#include "uint256.h"
#include "util.h"

static const int PENALTY_THRESHOLD = 5;

int64_t GetBlockDelay(const CBlockIndex& newBlock, const CBlockIndex& prevBlock, const int activeChainHeight,
                      bool isStartupSyncing);
