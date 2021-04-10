// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include "primitives/block.h"

#include <boost/optional.hpp>
#include <boost/tuple/tuple.hpp>
#include <stdint.h>

class CBlockIndex;
class CScript;
#ifdef ENABLE_WALLET
class CReserveKey;
class CWallet;
#endif
namespace Consensus { struct Params; };

struct CBlockTemplate
{
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOps;
};

class CCoinsViewCache;
class COrphan;
typedef boost::tuple<double, CFeeRate, const CTransaction*> TxPriority;
/** Retrieve mempool transactions priority info */
void GetBlockTxPriorityData(const CBlock *pblock, int nHeight, int64_t nMedianTimePast, const CCoinsViewCache& view,
                               std::vector<TxPriority>& vecPriority, std::list<COrphan>& vOrphan, std::map<uint256, std::vector<COrphan*> >& mapDependers);
/** DEPRECATED. Retrieve mempool transactions priority info */
void GetBlockTxPriorityDataOld(const CBlock *pblock, int nHeight, int64_t nMedianTimePast, const CCoinsViewCache& view,
                               std::vector<TxPriority>& vecPriority, std::list<COrphan>& vOrphan, std::map<uint256, std::vector<COrphan*> >& mapDependers);

/** Generate a new block, without valid proof-of-work */
CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn);
CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn,  unsigned int nBlockMaxComplexitySize);
#ifdef ENABLE_WALLET
boost::optional<CScript> GetMinerScriptPubKey(CReserveKey& reservekey);
CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey);
#else
boost::optional<CScript> GetMinerScriptPubKey();
CBlockTemplate* CreateNewBlockWithKey();
#endif

CMutableTransaction createCoinbase(const CScript &scriptPubKeyIn, CAmount fees, const int nHeight);

#ifdef ENABLE_MINING
/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce);
/** Run the miner threads */
 #ifdef ENABLE_WALLET
void GenerateBitcoins(bool fGenerate, CWallet* pwallet, int nThreads);
 #else
void GenerateBitcoins(bool fGenerate, int nThreads);
 #endif
#endif

void UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev);

#endif // BITCOIN_MINER_H
