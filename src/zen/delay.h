// Copyright (c) 2018-2020 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
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

static const int PENALTY_THRESHOLD = 5;

int64_t GetBlockDelay (const CBlockIndex& newBlock,const CBlockIndex& prevBlock, const int activeChainHeight, bool isStartupSyncing);
