// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include "sodium.h"

#include "addrman.h"
#include "arith_uint256.h"
#include "checkpoints.h"
#include "checkqueue.h"
#include "consensus/validation.h"
#include "deprecation.h"
#include "init.h"
#include "merkleblock.h"
#include "metrics.h"
#include "pow.h"
#include "txdb.h"
#include "ui_interface.h"
#include "undo.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validationinterface.h"
#include "wallet/asyncrpcoperation_sendmany.h"
#include "wallet/asyncrpcoperation_shieldcoinbase.h"
#include "maturityheightindex.h"

#include <sstream>
#include <algorithm> // std::shuffle
#include <random>
#include <regex>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/math/distributions/poisson.hpp>
#include <boost/thread.hpp>
#include <boost/static_assert.hpp>

#include "zen/forkmanager.h"
#include "zen/delay.h"

#include "core_io.h"
#include "sc/asyncproofverifier.h"
#include "sc/proofverifier.h"
#include "sc/sidechain.h"
#include "sc/sidechainTxsCommitmentBuilder.h"
#include "sc/sidechainTxsCommitmentGuard.h"

#include "script/sigcache.h"
#include "script/standard.h"

using namespace zen;
using namespace std;

#if defined(NDEBUG)
# error "Zen cannot be compiled without assertions."
#endif

/**
 * Global state
 */

CCriticalSection cs_main;

BlockSet sGlobalForkTips;
BlockTimeMap mGlobalForkTips;

BlockMap mapBlockIndex;
ScCumTreeRootMap mapCumtreeHeight;
CChain chainActive;
CBlockIndex *pindexBestHeader = NULL;
int64_t nTimeBestReceived = 0;
CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
int nScriptCheckThreads = 0;
bool fExperimentalMode = false;
bool fImporting = false;
bool fReindex = false;
bool fReindexFast = false;
bool fTxIndex = false;
bool fMaturityHeightIndex = false;

bool fAddressIndex = false;
bool fTimestampIndex = false;
bool fSpentIndex = false;

bool fHavePruned = false;
bool fPruneMode = false;
bool fIsBareMultisigStd = true;
bool fCheckBlockIndex = false;
bool fCheckpointsEnabled = true;
bool fRegtestAllowDustOutput = true;
//true in case we still have not reached the highest known block from server startup
bool fIsStartupSyncing = true;
size_t nCoinCacheUsage = 5000 * 300;
uint64_t nPruneTarget = 0;

/** Fees smaller than this (in satoshi) are considered zero fee (for relaying and mining) */
CFeeRate minRelayTxFee = CFeeRate(DEFAULT_MIN_RELAY_TX_FEE);

CTxMemPool mempool(::minRelayTxFee);

map<uint256, COrphanTx> mapOrphanTransactions GUARDED_BY(cs_main);;
map<uint256, set<uint256> > mapOrphanTransactionsByPrev GUARDED_BY(cs_main);;

void EraseOrphansFor(NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

static void CheckBlockIndex();

/** Constant stuff for coinbase transactions we create: */
CScript COINBASE_FLAGS;

const string strMessageMagic = "Zcash Signed Message:\n";

// Internal stuff
namespace {

    struct CBlockIndexWorkComparator
    {
        bool operator()(CBlockIndex *pa, CBlockIndex *pb) const {
            // First sort by total delay in chain.
            if (pa->nChainDelay < pb->nChainDelay) return false;
            if (pa->nChainDelay > pb->nChainDelay) return true;

            // Then sort by most total work, ...
            if (pa->nChainWork > pb->nChainWork) return false;
            if (pa->nChainWork < pb->nChainWork) return true;

            // ... then by earliest time received, ...
            if (pa->nSequenceId < pb->nSequenceId) return false;
            if (pa->nSequenceId > pb->nSequenceId) return true;

            // Use pointer address as tie breaker (should only happen with blocks
            // loaded from disk, as those all have id 0).
            if (pa < pb) return false;
            if (pa > pb) return true;

            // Identical blocks.
            return false;
        }
    };

    struct CBlockIndexRealWorkComparator
    {
        bool operator()(CBlockIndex *pa, CBlockIndex *pb) const {

            // Then sort by most total work, ...
            if (pa->nChainWork > pb->nChainWork) return false;
            if (pa->nChainWork < pb->nChainWork) return true;

            // ... then by earliest time received, ...
            if (pa->nSequenceId < pb->nSequenceId) return false;
            if (pa->nSequenceId > pb->nSequenceId) return true;

            // Use pointer address as tie breaker (should only happen with blocks
            // loaded from disk, as those all have id 0).
            if (pa < pb) return false;
            if (pa > pb) return true;

            // Identical blocks.
            return false;
        }
    };

    CBlockIndex *pindexBestInvalid;

    /**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
     * missing the data for the block.
     */
    set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;
    /** Number of nodes with fSyncStarted. */
    int nSyncStarted = 0;
    /** All pairs A->B, where A (or one if its ancestors) misses transactions, but B has transactions.
      * Pruned nodes may have entries where B is missing data.
      */
    multimap<CBlockIndex*, CBlockIndex*> mapBlocksUnlinked;

    CCriticalSection cs_LastBlockFile;
    std::vector<CBlockFileInfo> vinfoBlockFile;
    int nLastBlockFile = 0;
    /** Global flag to indicate we should check to see if there are
     *  block/undo files that should be deleted.  Set on startup
     *  or if we allocate more file space when we're in prune mode
     */
    bool fCheckForPruning = false;

    /**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
    CCriticalSection cs_nBlockSequenceId;
    /** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
    uint32_t nBlockSequenceId = 1;

    /**
     * Sources of received blocks, saved to be able to send them reject
     * messages or ban them when processing happens afterwards. Protected by
     * cs_main.
     */
    map<uint256, NodeId> mapBlockSource;

    /**
     * Filter for transactions that were recently rejected by
     * AcceptToMemoryPool. These are not rerequested until the chain tip
     * changes, at which point the entire filter is reset. Protected by
     * cs_main.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100
     * peers, half of which relay a tx we don't accept, that might be a 50x
     * bandwidth increase. A flooding attacker attempting to roll-over the
     * filter using minimum-sized, 60byte, transactions might manage to send
     * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
     * two minute window to send invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this
     * filter.
     *
     * Memory used: 1.7MB
     */
    boost::scoped_ptr<CRollingBloomFilter> recentRejects;
    uint256 hashRecentRejectsChainTip;

    /** Blocks that are in flight, and that are in the queue to be downloaded. Protected by cs_main. */
    struct QueuedBlock {
        uint256 hash;
        CBlockIndex *pindex;  //! Optional.
        int64_t nTime;  //! Time of "getdata" request in microseconds.
        bool fValidatedHeaders;  //! Whether this block has validated headers at the time of request.
        int64_t nTimeDisconnect; //! The timeout for this block request (for disconnecting a slow peer)
    };
    map<uint256, pair<NodeId, list<QueuedBlock>::iterator> > mapBlocksInFlight;

    /** Number of blocks in flight with validated headers. */
    int nQueuedValidatedHeaders = 0;

    /** Number of preferable block download peers. */
    int nPreferredDownload = 0;

    /** Dirty block index entries. */
    set<CBlockIndex*> setDirtyBlockIndex;

    /** Dirty block file entries. */
    set<int> setDirtyFileInfo;
} // anon namespace

//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//

namespace {

struct CBlockReject {
    CValidationState::Code chRejectCode;
    string strRejectReason;
    uint256 hashBlock;
};

/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState {
    //! The peer's address
    CService address;
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected;
    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior;
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool fShouldBan;
    //! String name of this peer (debugging/logging purposes).
    std::string name;
    //! List of asynchronously-determined block rejections to notify this peer about.
    std::vector<CBlockReject> rejects;
    //! The best known block we know this peer has announced.
    CBlockIndex *pindexBestKnownBlock;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    CBlockIndex *pindexLastCommonBlock;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted;
    //! Since when we're stalling block download progress (in microseconds), or 0.
    int64_t nStallingSince;
    list<QueuedBlock> vBlocksInFlight;
    int nBlocksInFlight;
    int nBlocksInFlightValidHeaders;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload;

    CNodeState() {
        fCurrentlyConnected = false;
        nMisbehavior = 0;
        fShouldBan = false;
        pindexBestKnownBlock = NULL;
        hashLastUnknownBlock.SetNull();
        pindexLastCommonBlock = NULL;
        fSyncStarted = false;
        nStallingSince = 0;
        nBlocksInFlight = 0;
        nBlocksInFlightValidHeaders = 0;
        fPreferredDownload = false;
    }
};

/** Map maintaining per-node state. Requires cs_main. */
map<NodeId, CNodeState> mapNodeState;

// Requires cs_main.
CNodeState *State(NodeId pnode) {
    map<NodeId, CNodeState>::iterator it = mapNodeState.find(pnode);
    if (it == mapNodeState.end())
        return NULL;
    return &it->second;
}

int GetHeight()
{
    LOCK(cs_main);
    return chainActive.Height();
}

void UpdatePreferredDownload(CNode* node, CNodeState* state)
{
    nPreferredDownload -= state->fPreferredDownload;

    // Whether this node should be marked as a preferred download node.
    state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;

    nPreferredDownload += state->fPreferredDownload;
}

// Returns time at which to timeout block request (nTime in microseconds)
int64_t GetBlockTimeout(int64_t nTime, int nValidatedQueuedBefore, const Consensus::Params &consensusParams)
{
    return nTime + 500000 * consensusParams.nPowTargetSpacing * (4 + nValidatedQueuedBefore);
}

void InitializeNode(NodeId nodeid, const CNode *pnode) {
    LOCK(cs_main);
    CNodeState &state = mapNodeState.insert(std::make_pair(nodeid, CNodeState())).first->second;
    state.name = pnode->addrName;
    state.address = pnode->addr;
}

void FinalizeNode(NodeId nodeid) {
    LOCK(cs_main);
    CNodeState *state = State(nodeid);

    if (state->fSyncStarted)
        nSyncStarted--;

    if (state->nMisbehavior == 0 && state->fCurrentlyConnected) {
        AddressCurrentlyConnected(state->address);
    }

    BOOST_FOREACH(const QueuedBlock& entry, state->vBlocksInFlight)
        mapBlocksInFlight.erase(entry.hash);
    EraseOrphansFor(nodeid);
    nPreferredDownload -= state->fPreferredDownload;

    mapNodeState.erase(nodeid);
}

// Requires cs_main.
// Returns a bool indicating whether we requested this block.
bool MarkBlockAsReceived(const uint256& hash) {
    map<uint256, pair<NodeId, list<QueuedBlock>::iterator> >::iterator itInFlight = mapBlocksInFlight.find(hash);
    if (itInFlight != mapBlocksInFlight.end()) {
        CNodeState *state = State(itInFlight->second.first);
        nQueuedValidatedHeaders -= itInFlight->second.second->fValidatedHeaders;
        state->nBlocksInFlightValidHeaders -= itInFlight->second.second->fValidatedHeaders;
        state->vBlocksInFlight.erase(itInFlight->second.second);
        state->nBlocksInFlight--;
        state->nStallingSince = 0;
        mapBlocksInFlight.erase(itInFlight);
        return true;
    }
    return false;
}

// Requires cs_main.
void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, const Consensus::Params& consensusParams, CBlockIndex *pindex = NULL) {
    CNodeState *state = State(nodeid);
    assert(state != NULL);

    // Make sure it's not listed somewhere already.
    MarkBlockAsReceived(hash);

    int64_t nNow = GetTimeMicros();
    QueuedBlock newentry = {hash, pindex, nNow, pindex != NULL, GetBlockTimeout(nNow, nQueuedValidatedHeaders, consensusParams)};
    nQueuedValidatedHeaders += newentry.fValidatedHeaders;
    list<QueuedBlock>::iterator it = state->vBlocksInFlight.insert(state->vBlocksInFlight.end(), newentry);
    state->nBlocksInFlight++;
    state->nBlocksInFlightValidHeaders += newentry.fValidatedHeaders;
    mapBlocksInFlight[hash] = std::make_pair(nodeid, it);
}

/** Check whether the last unknown block a peer advertized is not yet known. */
void ProcessBlockAvailability(NodeId nodeid) {
    CNodeState *state = State(nodeid);
    assert(state != NULL);

    if (!state->hashLastUnknownBlock.IsNull()) {
        BlockMap::iterator itOld = mapBlockIndex.find(state->hashLastUnknownBlock);
        if (itOld != mapBlockIndex.end() && itOld->second->nChainWork > 0) {
            if (state->pindexBestKnownBlock == NULL || itOld->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
                state->pindexBestKnownBlock = itOld->second;
            state->hashLastUnknownBlock.SetNull();
        }
    }
}

/** Update tracking information about which blocks a peer is assumed to have. */
void UpdateBlockAvailability(NodeId nodeid, const uint256 &hash) {
    CNodeState *state = State(nodeid);
    assert(state != NULL);

    ProcessBlockAvailability(nodeid);

    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end() && it->second->nChainWork > 0) {
        // An actually better block was announced.
        if (state->pindexBestKnownBlock == NULL || it->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
            state->pindexBestKnownBlock = it->second;
    } else {
        // An unknown block was announced; just assume that the latest one is the best one.
        state->hashLastUnknownBlock = hash;
    }
}

/** Find the last common ancestor two blocks have.
 *  Both pa and pb must be non-NULL. */
CBlockIndex* LastCommonAncestor(CBlockIndex* pa, CBlockIndex* pb) {
    if (pa->nHeight > pb->nHeight) {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight) {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb) {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}

/** Update pindexLastCommonBlock and add not-in-flight missing successors to vBlocks, until it has
 *  at most count entries. */
void FindNextBlocksToDownload(NodeId nodeid, unsigned int count, std::vector<CBlockIndex*>& vBlocks, NodeId& nodeStaller) {
    if (count == 0)
    {
        LogPrint("forks", "%s():%d - peer has too many blocks in fligth\n", __func__, __LINE__);
        return;
    }

    vBlocks.reserve(vBlocks.size() + count);
    CNodeState *state = State(nodeid);
    assert(state != NULL);

    // Make sure pindexBestKnownBlock is up to date, we'll need it.
    ProcessBlockAvailability(nodeid);

    if (state->pindexBestKnownBlock == NULL || state->pindexBestKnownBlock->nChainWork < chainActive.Tip()->nChainWork) {
        // This peer has nothing interesting.
        return;
    }

    if (state->pindexLastCommonBlock == NULL) {
        // Bootstrap quickly by guessing a parent of our best tip is the forking point.
        // Guessing wrong in either direction is not a problem.
        state->pindexLastCommonBlock = chainActive[std::min(state->pindexBestKnownBlock->nHeight, chainActive.Height())];
    }

    // If the peer reorganized, our previous pindexLastCommonBlock may not be an ancestor
    // of its current tip anymore. Go back enough to fix that.
    state->pindexLastCommonBlock = LastCommonAncestor(state->pindexLastCommonBlock, state->pindexBestKnownBlock);
    if (state->pindexLastCommonBlock == state->pindexBestKnownBlock)
        return;

    std::vector<CBlockIndex*> vToFetch;
    CBlockIndex *pindexWalk = state->pindexLastCommonBlock;
    // Never fetch further than the best block we know the peer has, or more than BLOCK_DOWNLOAD_WINDOW + 1 beyond the last
    // linked block we have in common with this peer. The +1 is so we can detect stalling, namely if we would be able to
    // download that next block if the window were 1 larger.
    int nWindowEnd = state->pindexLastCommonBlock->nHeight + BLOCK_DOWNLOAD_WINDOW;
    int nMaxHeight = std::min<int>(state->pindexBestKnownBlock->nHeight, nWindowEnd + 1);
    NodeId waitingfor = -1;
    while (pindexWalk->nHeight < nMaxHeight) {
        // Read up to 128 (or more, if more blocks than that are needed) successors of pindexWalk (towards
        // pindexBestKnownBlock) into vToFetch. We fetch 128, because CBlockIndex::GetAncestor may be as expensive
        // as iterating over ~100 CBlockIndex* entries anyway.
        int nToFetch = std::min(nMaxHeight - pindexWalk->nHeight, std::max<int>(count - vBlocks.size(), 128));
        vToFetch.resize(nToFetch);
        pindexWalk = state->pindexBestKnownBlock->GetAncestor(pindexWalk->nHeight + nToFetch);
        vToFetch[nToFetch - 1] = pindexWalk;
        for (unsigned int i = nToFetch - 1; i > 0; i--) {
            vToFetch[i - 1] = vToFetch[i]->pprev;
        }

        // Iterate over those blocks in vToFetch (in forward direction), adding the ones that
        // are not yet downloaded and not in flight to vBlocks. In the meantime, update
        // pindexLastCommonBlock as long as all ancestors are already downloaded, or if it's
        // already part of our chain (and therefore don't need it even if pruned).
        BOOST_FOREACH(CBlockIndex* pindex, vToFetch) {
            if (!pindex->IsValid(BLOCK_VALID_TREE)) {
                // We consider the chain that this peer is on invalid.
                return;
            }
            if (pindex->nStatus & BLOCK_HAVE_DATA || chainActive.Contains(pindex)) {
                if (pindex->nChainTx)
                    state->pindexLastCommonBlock = pindex;
            } else if (mapBlocksInFlight.count(pindex->GetBlockHash()) == 0) {
                // The block is not already downloaded, and not yet in flight.
                if (pindex->nHeight > nWindowEnd) {
                    // We reached the end of the window.
                    if (vBlocks.size() == 0 && waitingfor != nodeid) {
                        // We aren't able to fetch anything, but we would be if the download window was one larger.
                        nodeStaller = waitingfor;
                    }
                    LogPrint("forks", "%s():%d - could not fetch [%s]\n", __func__, __LINE__, pindex->GetBlockHash().ToString() );
                    return;
                }
                vBlocks.push_back(pindex);
                if (vBlocks.size() == count) {
                    return;
                }
            } else if (waitingfor == -1) {
                // This is the first already-in-flight block.
                waitingfor = mapBlocksInFlight[pindex->GetBlockHash()].first;
            }
        }
    }
}

} // anon namespace

bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats) {
    LOCK(cs_main);
    CNodeState *state = State(nodeid);
    if (state == NULL)
        return false;
    stats.nMisbehavior = state->nMisbehavior;
    stats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
    stats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;
    BOOST_FOREACH(const QueuedBlock& queue, state->vBlocksInFlight) {
        if (queue.pindex)
            stats.vHeightInFlight.push_back(queue.pindex->nHeight);
    }
    return true;
}

void RegisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.GetHeight.connect(&GetHeight);
    nodeSignals.ProcessMessages.connect(&ProcessMessages);
    nodeSignals.SendMessages.connect(&SendMessages);
    nodeSignals.InitializeNode.connect(&InitializeNode);
    nodeSignals.FinalizeNode.connect(&FinalizeNode);
}

void UnregisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.GetHeight.disconnect(&GetHeight);
    nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
}

CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    // Find the first block the caller has in the main chain
    BOOST_FOREACH(const uint256& hash, locator.vHave) {
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end())
        {
            CBlockIndex* pindex = (*mi).second;
            if (chain.Contains(pindex))
                return pindex;
        }
    }
    return chain.Genesis();
}

CCoinsViewCache *pcoinsTip = NULL;
CBlockTreeDB *pblocktree = NULL;

//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//

bool AddOrphanTx(const CTransactionBase& txObj, NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    uint256 hash = txObj.GetHash();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 10,000 orphans, each of which is at most 5,000 bytes big is
    // at most 500 megabytes of orphans:
    unsigned int sz = txObj.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
    if (sz > 5000)
    {
        LogPrint("mempool", "ignoring large orphan tx (size: %u, hash: %s)\n", sz, hash.ToString());
        return false;
    }

    mapOrphanTransactions[hash].tx = txObj.MakeShared();
    mapOrphanTransactions[hash].fromPeer = peer;
    BOOST_FOREACH(const CTxIn& txin, txObj.GetVin())
        mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);

    LogPrint("mempool", "stored orphan tx %s (mapsz %u prevsz %u)\n", hash.ToString(),
             mapOrphanTransactions.size(), mapOrphanTransactionsByPrev.size());
    return true;
}

void static EraseOrphanTx(uint256 hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end())
        return;
    BOOST_FOREACH(const CTxIn& txin, it->second.tx->GetVin())
    {
        map<uint256, set<uint256> >::iterator itPrev = mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == mapOrphanTransactionsByPrev.end())
            continue;
        itPrev->second.erase(hash);
        if (itPrev->second.empty())
            mapOrphanTransactionsByPrev.erase(itPrev);
    }
    mapOrphanTransactions.erase(it);
}

void EraseOrphansFor(NodeId peer)
{
    int nErased = 0;
    map<uint256, COrphanTx>::iterator iter = mapOrphanTransactions.begin();
    while (iter != mapOrphanTransactions.end())
    {
        map<uint256, COrphanTx>::iterator maybeErase = iter++; // increment to avoid iterator becoming invalid
        if (maybeErase->second.fromPeer == peer)
        {
            EraseOrphanTx(maybeErase->second.tx->GetHash());
            ++nErased;
        }
    }
    if (nErased > 0) LogPrint("mempool", "Erased %d orphan tx from peer %d\n", nErased, peer);
}


unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans)
    {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}


bool IsStandardTx(const CTransactionBase& txBase, string& reason, const int nHeight)
{
    if (!txBase.IsVersionStandard(nHeight))
    {
        reason = "version";
        return false;
    }


    BOOST_FOREACH(const CTxIn& txin, txBase.GetVin())
    {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys. (remember the 520 byte limit on redeemScript size) That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20
        // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
        // considered standard)
        if (txin.scriptSig.size() > 1650) {
            reason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
    }

    unsigned int nDataOut = 0;
    txnouttype whichType;
    for(int pos = 0; pos < txBase.GetVout().size(); ++pos) {
        const CTxOut & txout = txBase.GetVout()[pos];
        ReplayProtectionAttributes rpAttributes;
        if (!::IsStandard(txout.scriptPubKey, whichType, rpAttributes)) {
            LogPrintf("%s():%d - Non standard output: scriptPubKey[%s]\n",
                __func__, __LINE__, txout.scriptPubKey.ToString());
            reason = "scriptpubkey";
            return false;
        }

        if (rpAttributes.GotValues())
        {
            if ( (nHeight - rpAttributes.referencedHeight) < getCheckBlockAtHeightMinAge())
            {
                LogPrintf("%s():%d - referenced block h[%d], chain.h[%d], minAge[%d] (tx=%s)\n",
                    __func__, __LINE__, rpAttributes.referencedHeight, nHeight, getCheckBlockAtHeightMinAge(),
                    txBase.GetHash().ToString() );
                reason = "scriptpubkey checkblockatheight: referenced block too recent";
                return false;
            }
        }

        // provide temporary replay protection for two minerconf windows during chainsplit
        if ((!txBase.IsCoinBase() && !txBase.IsBackwardTransfer(pos)) &&
            (!ForkManager::getInstance().isTransactionTypeAllowedAtHeight(chainActive.Height(), whichType))) {
            reason = "op-checkblockatheight-needed";
            return false;
        }

        if (whichType == TX_NULL_DATA || whichType == TX_NULL_DATA_REPLAY)
            nDataOut++;
        else if ((whichType == TX_MULTISIG) && (!fIsBareMultisigStd)) {
            reason = "bare-multisig";
            return false;
        } else if (txout.IsDust(::minRelayTxFee)) {
            if (Params().NetworkIDString() == "regtest" && fRegtestAllowDustOutput)
            {
                // do not reject this tx in regtest, there are py tests intentionally using zero values
                // and expecting this to be processable
                LogPrintf("%s():%d - txout is dust, ignoring it because we are in regtest\n",
                    __func__, __LINE__);
            }
            else
            {
                LogPrintf("%s():%d - ERROR: txout pos=%d, amount=%f is dust (min %f )\n",
                    __func__, __LINE__, pos, txout.nValue, txout.GetDustThreshold(::minRelayTxFee));
                reason = "dust (minimum output value is " + std::to_string(txout.GetDustThreshold(::minRelayTxFee)) + " zat)";
                return false;
            }
        }
    }

    // only one OP_RETURN txout is permitted
    if (nDataOut > 1) {
        reason = "multi-op-return";
        return false;
    }

    return true;
}

bool IsFinalTx(const CTransactionBase &tx, int nBlockHeight, int64_t nBlockTime)
{
    /* A specified locktime indicates that the transaction is only valid at the given blockheight or later.*/
    if (tx.GetLockTime() == 0)
        return true;
    if ((int64_t)tx.GetLockTime() < ((int64_t)tx.GetLockTime() < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    BOOST_FOREACH(const CTxIn& txin, tx.GetVin())
    /* According to BIP 68, setting nSequence value to 0xFFFFFFFF for every input in the transaction disables nLocktime.
       So, whatever may be the value of nLocktime above, it will have no effect on the transaction as far as nSequence
       value is 0xFFFFFFFF.*/
        if (!txin.IsFinal())
            return false;
    return true;
}

bool CheckFinalTx(const CTransactionBase &tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int nBlockHeight = chainActive.Height() + 1;

    // Timestamps on the other hand don't get any special treatment,
    // because we can't know what timestamp the next block will have,
    // and there aren't timestamp applications where it matters.
    // However this changes once median past time-locks are enforced:
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST)
                             ? chainActive.Tip()->GetMedianTimePast()
                             : GetTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

/**
 * Check transaction inputs to mitigate two
 * potential denial-of-service attacks:
 *
 * 1. scriptSigs with extra data stuffed into them,
 *    not consumed by scriptPubKey (or P2SH script)
 * 2. P2SH scripts with a crazy number of expensive
 *    CHECKSIG/CHECKMULTISIG operations
 */
bool AreInputsStandard(const CTransactionBase& txBase, const CCoinsViewCache& mapInputs)
{
    if (txBase.IsCoinBase())
        return true; // Coinbases don't use vin normally

    for (unsigned int i = 0; i < txBase.GetVin().size(); i++)
    {
        const CTxIn&  in   = txBase.GetVin().at(i);
        const CTxOut& prev = mapInputs.GetOutputFor(in);

        vector<vector<unsigned char> > vSolutions;
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions))
        {
            LogPrintf("%s():%d - Input %d: failed checking scriptpubkey %s\n",
                __func__, __LINE__, i, prevScript.ToString());
            return false;
        }
        int nArgsExpected = ScriptSigArgsExpected(whichType, vSolutions);
        if (nArgsExpected < 0)
            return false;

        // Transactions with extra stuff in their scriptSigs are
        // non-standard. Note that this EvalScript() call will
        // be quick, because if there are any operations
        // beside "push data" in the scriptSig
        // IsStandardTx() will have already returned false
        // and this method isn't called.
        vector<vector<unsigned char> > stack;
        if (!EvalScript(stack, in.scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker()))
            return false;

        if (whichType == TX_SCRIPTHASH || whichType == TX_SCRIPTHASH_REPLAY)
        {
            if (stack.empty())
                return false;
            CScript subscript(stack.back().begin(), stack.back().end());
            vector<vector<unsigned char> > vSolutions2;
            txnouttype whichType2;
            if (Solver(subscript, whichType2, vSolutions2))
            {
                int tmpExpected = ScriptSigArgsExpected(whichType2, vSolutions2);
                if (tmpExpected < 0)
                    return false;
                nArgsExpected += tmpExpected;
            }
            else
            {
                // Any other Script with less than 15 sigops OK:
                unsigned int sigops = subscript.GetSigOpCount(true);
                // ... extra data left on the stack after execution is OK, too:
                return (sigops <= MAX_P2SH_SIGOPS);
            }
        }

        if (stack.size() != (unsigned int)nArgsExpected)
            return false;
    }

    return true;
}

unsigned int GetLegacySigOpCount(const CTransactionBase& txBase)
{
    unsigned int nSigOps = 0;
    for(const CTxIn& txin: txBase.GetVin())
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    for(const CTxOut& txout: txBase.GetVout())
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }

    if(!txBase.IsCertificate())
    {
        try {
            const CTransaction& tx = dynamic_cast<const CTransaction&>(txBase);
            for(const CTxCeasedSidechainWithdrawalInput& csw: tx.GetVcswCcIn())
            {
                nSigOps += csw.scriptPubKey().GetSigOpCount(false);
                nSigOps += csw.redeemScript.GetSigOpCount(false);
            }
        } catch(const bad_cast& e) {
            LogPrintf("%s():%d - can't cast CTransactionBase (%s) to CTransaction when expected.\n",
                __func__, __LINE__, txBase.GetHash().ToString());
            assert(false);
        }
    }

    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransactionBase& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.GetVin().size(); i++)
    {
        const CTxOut &prevout = inputs.GetOutputFor(tx.GetVin()[i]);
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.GetVin()[i].scriptSig);
    }
    return nSigOps;
}

bool CheckCertificate(const CScCertificate& cert, CValidationState& state)
{
    if (!cert.IsValidVersion(state))
        return false;

    if (!cert.CheckInputsOutputsNonEmpty(state))
        return false;

    if (!cert.CheckSerializedSize(state))
        return false;

    if (!cert.CheckAmounts(state))
        return false;

    if (!cert.CheckInputsDuplication(state))
        return false;

    if (!cert.CheckInputsInteraction(state))
        return false;

    if(!Sidechain::checkCertSemanticValidity(cert, state))
        return false;

    return true;
}

std::map<uint256, uint256> HighQualityCertData(const CBlock& blockToConnect, const CCoinsViewCache& view)
{
    // The function assumes that certs of given scId are ordered by increasing quality and
    // reference all the same epoch as CheckBlock() guarantees.
    // It returns key: highQualityCert hash -> value: hash of superseeded certificate to be voided (or null hash)

    std::set<uint256> visitedScIds;
    std::map<uint256, uint256> res;
    for(auto itCert = blockToConnect.vcert.rbegin(); itCert != blockToConnect.vcert.rend(); ++itCert)
    {
        if (visitedScIds.count(itCert->GetScId()) != 0)
            continue;

        CSidechain sidechain;
        if(!view.GetSidechain(itCert->GetScId(), sidechain))
            continue;

        if (itCert->epochNumber == sidechain.lastTopQualityCertReferencedEpoch)
        {
            assert(!sidechain.isNonCeasing());
            res[itCert->GetHash()] = sidechain.lastTopQualityCertHash;
        }
        else
            res[itCert->GetHash()] = uint256();

        visitedScIds.insert(itCert->GetScId());
    }

    return res;
}

std::map<uint256, uint256> HighQualityCertData(const CBlock& blockToDisconnect, const CBlockUndo& blockUndo)
{
    // The function assumes that certs of given scId are ordered by increasing quality and
    // reference all the same epoch as CheckBlock() guarantees.
    // It returns key: highQualityCert hash -> value: hash of superseeded certificate to be restored (or null hash)

    std::set<uint256> visitedScIds;
    std::map<uint256, uint256> res;

    for (int certPos = blockToDisconnect.vcert.size() - 1; certPos >= 0; certPos--)
    {
        const CScCertificate& cert = blockToDisconnect.vcert.at(certPos);

        if (visitedScIds.count(cert.GetScId()) != 0)
            continue;

        if (cert.epochNumber == blockUndo.scUndoDatabyScId.at(cert.GetScId()).prevTopCommittedCertReferencedEpoch)
        {
            res[cert.GetHash()] = blockUndo.scUndoDatabyScId.at(cert.GetScId()).prevTopCommittedCertHash;
        }
        else
            res[cert.GetHash()] = uint256();

        visitedScIds.insert(cert.GetScId());
    }

    return res;
}

// With the introduction of non-ceasing sidechains we modified this function to perform less strict checks
// on certificate ordering.
// This functions now checks that, for every sc, certificates in a block are ordered by increasing epochs,
// and for each epoch, certificates are ordered by increasing quality. Originally, it also checked that
// a block did not contain 2 or more certs referring to different epochs (invalid only for v0/v1) and that
// for each epoch, certs were stricly ordered by quality (it was not possible to include certs with the
// same quality)
bool CheckCertificatesOrdering(const std::vector<CScCertificate>& certList, CValidationState& state)
{
    std::map<uint256, std::pair<int32_t, int64_t>> mBestCertDataByScId;

    for(const CScCertificate& cert: certList)
    {
        const uint256& scid = cert.GetScId();
        if (mBestCertDataByScId.count(scid))
        {
            const auto & bestCertData = mBestCertDataByScId.at(scid); 
            if (bestCertData.first > cert.epochNumber)
            {
                LogPrint("cert", "%s():%d - cert %s / q=%d / epoch=%d has an incorrect epoch order in block for scid = %s\n",
                    __func__, __LINE__, cert.GetHash().ToString(), cert.quality, cert.epochNumber, scid.ToString());
                return state.DoS(100, error("%s: incorrect certificate epoch order in block",
                    __func__), CValidationState::Code::INVALID, "bad-cert-epoch-ordering-in-block");
            }
            if ((bestCertData.first == cert.epochNumber) && (bestCertData.second > cert.quality))
            {
                LogPrint("cert", "%s():%d - cert %s / q=%d / epoch=%d has an incorrect quality order in block for scid = %s\n",
                    __func__, __LINE__, cert.GetHash().ToString(), cert.quality, cert.epochNumber, scid.ToString());
                return state.DoS(100, error("%s: incorrect certificate quality order in block",
                    __func__), CValidationState::Code::INVALID, "bad-cert-quality-in-block");
            }
        }
        LogPrint("cert", "%s():%d - setting cert %s / q=%d / epoch=%d as current best in block for scid = %s\n",
            __func__, __LINE__, cert.GetHash().ToString(), cert.quality, cert.epochNumber, scid.ToString());
        // As we iterate over certs, we only keep the current max combination of epoch / quality for a given sc
        mBestCertDataByScId[scid] = std::make_pair(cert.epochNumber, cert.quality);
    }

    return true;
}

bool CheckTransaction(const CTransaction& tx, CValidationState &state,
                      libzcash::ProofVerifier& verifier)
{
    // Don't count coinbase transactions because mining skews the count
    if (!tx.IsCoinBase()) {
        transactionsValidated.increment();
    }
    if (!CheckTransactionWithoutProofVerification(tx, state)) {
        return false;
    }

    // Ensure that zk-SNARKs verify
    BOOST_FOREACH(const JSDescription &joinsplit, tx.GetVjoinsplit()) {
        if (!joinsplit.Verify(*pzcashParams, verifier, tx.joinSplitPubKey)) {
            return state.DoS(100, error("CheckTransaction(): joinsplit does not verify"),
                                CValidationState::Code::INVALID, "bad-txns-joinsplit-verification-failed");
        }
    }

    if (!Sidechain::checkTxSemanticValidity(tx, state))
        return false;

    return true;
}

bool CheckTransactionWithoutProofVerification(const CTransaction& tx, CValidationState &state)
{
    if (!tx.IsValidVersion(state))
        return false;

    if (!tx.CheckInputsOutputsNonEmpty(state))
        return false;

    if (!tx.CheckSerializedSize(state))
        return false;

    if (!tx.CheckAmounts(state))
        return false;

    if (!tx.CheckInputsDuplication(state))
        return false;

    if (!tx.CheckInputsInteraction(state))
        return false;

    if (!tx.IsCoinBase())
    {
        if (tx.GetVjoinsplit().size() > 0) {
            // Empty output script.
            CScript scriptCode;
            uint256 dataToBeSigned;
            try {
                dataToBeSigned = SignatureHash(scriptCode, tx, NOT_AN_INPUT, SIGHASH_ALL);
            } catch (std::logic_error& ex) {
                return state.DoS(100, error("%s():%d error computing signature hash", __func__, __LINE__),
                                 CValidationState::Code::INVALID, "error-computing-signature-hash");
            }

            BOOST_STATIC_ASSERT(crypto_sign_PUBLICKEYBYTES == 32);

            // We rely on libsodium to check that the signature is canonical.
            // https://github.com/jedisct1/libsodium/commit/62911edb7ff2275cccd74bf1c8aefcc4d76924e0
            if (crypto_sign_verify_detached(&tx.joinSplitSig[0],
                                            dataToBeSigned.begin(), 32,
                                            tx.joinSplitPubKey.begin()
                                           ) != 0) {
                return state.DoS(100, error("%s():%d invalid joinsplit signature", __func__, __LINE__),
                                 CValidationState::Code::INVALID, "bad-txns-invalid-joinsplit-signature");
            }
        }
    }

    return true;
}

CAmount GetMinRelayFee(const CTransactionBase& tx, unsigned int nBytes, bool fAllowFree, unsigned int block_priority_size)
{
    {
        LOCK(mempool.cs);
        uint256 hash = tx.GetHash();
        double dPriorityDelta = 0;
        CAmount nFeeDelta = 0;
        mempool.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
        if (dPriorityDelta > 0 || nFeeDelta > 0)
            return 0;
    }

    CAmount nMinFee = ::minRelayTxFee.GetFee(nBytes);

    if (fAllowFree)
    {
        // There is a free transaction area in blocks created by most miners,
        // * If we are relaying we allow transactions up to DEFAULT_BLOCK_PRIORITY_SIZE - 1000
        //   to be considered to fall into this category. We don't want to encourage sending
        //   multiple transactions instead of one big transaction to avoid fees.
        if ((nBytes < (block_priority_size - 1000)) )
            nMinFee = 0;
    }

    if (!MoneyRange(nMinFee))
        nMinFee = MAX_MONEY;
    return nMinFee;
}

void RejectMemoryPoolTxBase(const CValidationState& state, const CTransactionBase& txBase, CNode* pfrom)
{
    LogPrint("mempool", "%s from peer=%d %s was not accepted into the memory pool: %s\n", txBase.GetHash().ToString(),
             pfrom->id, pfrom->cleanSubVer,
             state.GetRejectReason());
    std::string cmdString("tx");
    pfrom->PushMessage("reject", cmdString, CValidationState::CodeToChar(state.GetRejectCode()),
                        state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), txBase.GetHash());
    if (state.GetDoS() > 0)
        Misbehaving(pfrom->GetId(), state.GetDoS());
}

MempoolReturnValue AcceptCertificateToMemoryPool(CTxMemPool& pool, CValidationState &state, const CScCertificate &cert,
    LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee, MempoolProofVerificationFlag fProofVerification, CNode* pfrom)
{
    AssertLockHeld(cs_main);

    //we retrieve the current height from the pcoinsTip and not from chainActive because on DisconnectTip the Accept*ToMemoryPool
    // is called after having reverted the txs from the pcoinsTip view but before having updated the chainActive
    int nextBlockHeight = pcoinsTip->GetHeight() + 1;

    if (!cert.CheckInputsLimit())
    {
        LogPrintf("%s(): CheckInputsLimit failed\n", __func__);
        return MempoolReturnValue::INVALID;
    }

    if(!CheckCertificate(cert, state))
    {
        error("%s(): CheckCertificate failed", __func__);
        return MempoolReturnValue::INVALID;
    }

    static const int DOS_LEVEL = 10;
    if(!cert.ContextualCheck(state, nextBlockHeight, DOS_LEVEL))
    {
        LogPrintf("%s(): ContextualCheck failed\n", __func__);
        return MempoolReturnValue::INVALID;
    }

    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    string reason;
    if (getRequireStandard() &&  !IsStandardTx(cert, reason, nextBlockHeight))
    {
        LogPrintf("%s():%d - Dropping nonstandard certid %s\n", __func__, __LINE__, cert.GetHash().ToString());
        state.DoS(0, error("%s(): nonstandard certificate: %s", __func__, reason),
                            CValidationState::Code::NONSTANDARD, reason);
        return MempoolReturnValue::INVALID;
    }

    if (!pool.checkIncomingCertConflicts(cert))
    {
        LogPrintf("%s(): certificate has conflicts in mempool\n", __func__);
        return MempoolReturnValue::INVALID;
    }

    // Check if cert is already in mempool or if there are conflicts with in-memory certs
    std::pair<uint256, CAmount> conflictingCertData = pool.FindCertWithQuality(cert.GetScId(), cert.quality);

    {
        uint256 certHash = cert.GetHash();
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nFees = 0;
        {
            LOCK(pool.cs);
            CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
            view.SetBackend(viewMemPool);

            // do we already have it?
            if (view.HaveCoins(certHash))
            {
                LogPrint("mempool", "%s():%d Dropping cert %s : view already has coins\n",
                    __func__, __LINE__, certHash.ToString());
                return MempoolReturnValue::INVALID;
            }

            int nDoS = 0;

            // checking txs commitment tree validity
            if (ForkManager::getInstance().isNonCeasingSidechainActive(nextBlockHeight)) {
                SidechainTxsCommitmentGuard scCommitmentGuard;
                bool retval = scCommitmentGuard.add(cert);
                if (!retval) {
                    nDoS = 100;
                    state.DoS(nDoS,
                        error("%s():%d - ERROR: Invalid cert[%s], SidechainTxsCommitmentGuard failed\n",
                            __func__, __LINE__, certHash.ToString()),
                        CValidationState::Code::INVALID, "bad-cert-txscommitmentguard");
                    return MempoolReturnValue::INVALID;
                }
            }

            CValidationState::Code ret_code = view.IsCertApplicableToState(cert);
            if (ret_code != CValidationState::Code::OK)
            {
                if (ret_code == CValidationState::Code::INVALID_AND_BAN)
                    nDoS = 100;

                state.DoS(nDoS, error("%s():%d - certificate not applicable: ret_code[0x%x]",
                    __func__, __LINE__, CValidationState::CodeToChar(ret_code)),
                    ret_code, "bad-sc-cert-not-applicable");
                return MempoolReturnValue::INVALID;
            }

            // do all inputs exist?
            // Note that this does not check for the presence of actual outputs (see the next check for that),
            // and only helps with filling in pfMissingInputs (to determine missing vs spent).
            for(const CTxIn txin: cert.GetVin())
            {
                if (!view.HaveCoins(txin.prevout.hash))
                {
                    LogPrint("mempool", "%s(): Dropping cert %s : no coins for vin (tx=%s)\n",
                        __func__, certHash.ToString(), txin.prevout.hash.ToString());
                    return MempoolReturnValue::MISSING_INPUT;
                }
            }

            // are the actual inputs available?
            if (!view.HaveInputs(cert))
            {
                state.Invalid(
                    error("%s():%d - ERROR: cert[%s] inputs already spent\n", __func__, __LINE__, certHash.ToString()),
                    CValidationState::Code::DUPLICATED, "bad-sc-cert-inputs-spent");
                return MempoolReturnValue::INVALID;
            }

            nFees = cert.GetFeeAmount(view.GetValueIn(cert));

            CSidechain sc;
            if (!view.GetSidechain(cert.GetScId(), sc))
            {
                LogPrint("mempool", "%s():%d - ERROR: cert[%s] refers to a non existing sidechain[%s]\n", __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString());
                return MempoolReturnValue::INVALID;
            }

            if (sc.isNonCeasing() && pool.certificateExists(cert.GetScId()))
            {
                state.Invalid(
                    error("%s():%d - Dropping cert %s : conflicting with another cert in mempool for non ceasing SC\n",
                        __func__, __LINE__, certHash.ToString()),
                    CValidationState::Code::INVALID, "bad-sc-cert-conflict");
                return MempoolReturnValue::INVALID;
            }
            else if (!sc.isNonCeasing())
            {
                if (!conflictingCertData.first.IsNull() && conflictingCertData.second >= nFees)
                {
                    state.Invalid(
                        error("%s():%d - Dropping cert %s : low fee and same quality as other cert in mempool\n",
                            __func__, __LINE__, certHash.ToString()),
                        CValidationState::Code::INVALID, "bad-sc-cert-quality");
                    return MempoolReturnValue::INVALID;
                }
            }

            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            view.SetBackend(dummy);
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (getRequireStandard() && !AreInputsStandard(cert, view)) {
            LogPrintf("%s():%d - Dropping cert %s : nonstandard transaction input\n",
                    __func__, __LINE__, certHash.ToString());
            return MempoolReturnValue::INVALID;
        }

        unsigned int nSigOps = GetLegacySigOpCount(cert);
        if (nSigOps > MAX_STANDARD_TX_SIGOPS)
        {
            state.DoS(0,
                error("%s():%d - too many sigops %s, %d > %d",
                    __func__, __LINE__, certHash.ToString(), nSigOps, MAX_STANDARD_TX_SIGOPS),
                CValidationState::Code::NONSTANDARD, "bad-sc-cert-too-many-sigops");
            return MempoolReturnValue::INVALID;
        }

        // cert: this computes priority based on input amount and depth in blockchain, as transparent txes.
        // another option would be to return max prio, as shielded txes do
        double dPriority = view.GetPriority(cert, chainActive.Height());
        LogPrint("mempool", "%s():%d - Computed fee=%lld, prio[%22.8f]\n", __func__, __LINE__, nFees, dPriority);

        CCertificateMemPoolEntry entry(cert, nFees, GetTime(), dPriority, chainActive.Height());
        unsigned int nSize = entry.GetCertificateSize();

        // Don't accept it if it can't get into a block
        CAmount txMinFee = GetMinRelayFee(cert, nSize, true, DEFAULT_BLOCK_PRIORITY_SIZE);

        LogPrintf("nFees=%d, txMinFee=%d\n", nFees, txMinFee);
        if (fLimitFree == LimitFreeFlag::ON && nFees < txMinFee)
        {
            state.DoS(0, error("%s(): not enough fees %s, %d < %d",
                                    __func__, certHash.ToString(), nFees, txMinFee),
                            CValidationState::Code::INSUFFICIENT_FEE, "insufficient fee");
            return MempoolReturnValue::INVALID;
        }

        // Require that free transactions have sufficient priority to be mined in the next block.
        if (GetBoolArg("-relaypriority", false) &&
            nFees < ::minRelayTxFee.GetFee(nSize) &&
            !AllowFree(view.GetPriority(cert, chainActive.Height() + 1)))
        {
            state.DoS(0, false, CValidationState::Code::INSUFFICIENT_FEE, "insufficient priority");
            return MempoolReturnValue::INVALID;
        }

        // Continuously rate-limit free (really, very-low-fee) transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm.
        if (fLimitFree == LimitFreeFlag::ON && nFees < ::minRelayTxFee.GetFee(nSize))
        {
            static CCriticalSection csFreeLimiter;
            static double dFreeCount;
            static int64_t nLastTime;
            int64_t nNow = GetTime();

            LOCK(csFreeLimiter);

            // Use an exponentially decaying ~10-minute window:
            dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
            nLastTime = nNow;
            // -limitfreerelay unit is thousand-bytes-per-minute
            // At default rate it would take over a month to fill 1GB
            if (dFreeCount >= GetArg("-limitfreerelay", 15)*10*1000)
            {
                state.DoS(0, error("%s(): free transaction rejected by rate limiter", __func__),
                                 CValidationState::Code::INSUFFICIENT_FEE, "rate limited free transaction");
                return MempoolReturnValue::INVALID;
            }
            LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
            dFreeCount += nSize;
        }

        if (fRejectAbsurdFee == RejectAbsurdFeeFlag::ON && nFees > ::minRelayTxFee.GetFee(nSize) * 10000)
        {
            LogPrintf("%s():%d - absurdly high fees cert[%s], %d > %d\n",
                    __func__, __LINE__, certHash.ToString(), nFees, ::minRelayTxFee.GetFee(nSize) * 10000);
            return MempoolReturnValue::INVALID;
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        if (!ContextualCheckCertInputs(cert, state, view, true, chainActive, STANDARD_CONTEXTUAL_SCRIPT_VERIFY_FLAGS, true, Params().GetConsensus()))
        {
            LogPrintf("%s():%d - ERROR: ConnectInputs failed, cert[%s]\n", __func__, __LINE__, certHash.ToString());
            return MempoolReturnValue::INVALID;
        }

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        if (!ContextualCheckCertInputs(cert, state, view, true, chainActive, MANDATORY_SCRIPT_VERIFY_FLAGS, true, Params().GetConsensus()))
        {
            LogPrintf("%s():%d - BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags, cert[%s]\n",
                                __func__, __LINE__, certHash.ToString());
            return MempoolReturnValue::INVALID;
        }

        if (fProofVerification == MempoolProofVerificationFlag::ASYNC)
        {
            CScAsyncProofVerifier::GetInstance().LoadDataForCertVerification(view, cert, pfrom);
            return MempoolReturnValue::PARTIALLY_VALIDATED;
        }
        else if (fProofVerification == MempoolProofVerificationFlag::SYNC)
        {
            CScProofVerifier scVerifier{CScProofVerifier::Verification::Strict, CScProofVerifier::Priority::Low};
            scVerifier.LoadDataForCertVerification(view, cert);

            LogPrint("sc", "%s():%d - calling scVerifier.BatchVerify()\n", __func__, __LINE__);
            if (!scVerifier.BatchVerify())
            {
                state.DoS(100, error("%s():%d - cert proof failed to verify",
                    __func__, __LINE__),  CValidationState::Code::INVALID_PROOF, "bad-sc-cert-proof");
                return MempoolReturnValue::INVALID;
            }
        }

        if (!pool.RemoveCertAndSync(conflictingCertData.first))
        {
            state.Invalid(
                error("%s():%d - Dropping cert %s : depends on some conflicting quality certs\n",
                    __func__, __LINE__, certHash.ToString()),
                CValidationState::Code::INVALID, "bad-sc-cert-quality");
            return MempoolReturnValue::INVALID;
        }

        // Store transaction in memory
        pool.addUnchecked(certHash, entry, !IsInitialBlockDownload());

        // Add memory address index
        if (fAddressIndex) {
            pool.addAddressIndex(entry.GetCertificate(), entry.GetTime(), view);
        }

        // Add memory spent index
        if (fSpentIndex) {
            pool.addSpentIndex(entry.GetCertificate(), view);
        }

    }
    return MempoolReturnValue::VALID;
}

MempoolReturnValue AcceptTxToMemoryPool(CTxMemPool& pool, CValidationState &state, const CTransaction &tx, LimitFreeFlag fLimitFree,
                        RejectAbsurdFeeFlag fRejectAbsurdFee, MempoolProofVerificationFlag fProofVerification, CNode* pfrom)
{
    AssertLockHeld(cs_main);

    //we retrieve the current height from the pcoinsTip and not from chainActive because on DisconnectTip the Accept*ToMemoryPool
    // is called after having reverted the txs from the pcoinsTip view but before having updated the chainActive
    int nextBlockHeight = pcoinsTip->GetHeight() + 1;

    if (!tx.CheckInputsLimit())
    {
        LogPrintf("%s():%d - CheckInputsLimit failed\n", __func__, __LINE__);
        return MempoolReturnValue::INVALID;
    }

    auto verifier = libzcash::ProofVerifier::Strict();
    if (!CheckTransaction(tx, state, verifier))
    {
        error("%s(): CheckTransaction failed", __func__);
        return MempoolReturnValue::INVALID;
    }

    // DoS level set to 10 to be more forgiving.
    // Check transaction contextually against the set of consensus rules which apply in the next block to be mined.
    if (!tx.ContextualCheck(state, nextBlockHeight, 10))
    {
        error("%s(): ContextualCheck() failed", __func__);
        return MempoolReturnValue::INVALID;
    }

    // Silently drop pre-chainsplit transactions
    if (!ForkManager::getInstance().isAfterChainsplit(nextBlockHeight))
    {
        LogPrint("mempool", "%s():%d - Dropping txid[%s]: chain height[%d] is before chain split\n",
            __func__, __LINE__, tx.GetHash().ToString(), nextBlockHeight);
        return MempoolReturnValue::INVALID;
    }

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
    {
        state.DoS(100, error("%s(): coinbase as individual tx", __func__),
                  CValidationState::Code::INVALID, "coinbase");
        return MempoolReturnValue::INVALID;
    }


    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    string reason;
    if (getRequireStandard() && !IsStandardTx(tx, reason, nextBlockHeight))
    {
        state.DoS(0, error("%s(): nonstandard transaction: %s", __func__, reason),
                  CValidationState::Code::NONSTANDARD, reason);
        return MempoolReturnValue::INVALID;
    }

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
    {
        state.DoS(0, false, CValidationState::Code::NONSTANDARD, "non-final");
        return MempoolReturnValue::INVALID;
    }

    if (!pool.checkCswInputsPerScLimit(tx))
    {
        state.Invalid(error("%s():%d: tx[%s] would exceed limit of csw inputs for sc in mempool\n",
            __func__, __LINE__, tx.GetHash().ToString()),
            CValidationState::Code::TOO_MANY_CSW_INPUTS_FOR_SC, "bad-txns-too-many-csw-inputs-for-sc");
        return MempoolReturnValue::INVALID;
    }

    if (!pool.checkIncomingTxConflicts(tx))
    {
        LogPrintf("%s():%d: tx[%s] has conflicts in mempool\n", __func__, __LINE__, tx.GetHash().ToString());
        return MempoolReturnValue::INVALID;
    }

    {
        uint256 hash = tx.GetHash();
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nFees = 0;
        {
            LOCK(pool.cs);
            CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
            view.SetBackend(viewMemPool);

            // do we already have it?
            if (view.HaveCoins(hash))
            {
                LogPrint("mempool", "%s():%d Dropping tx %s : view already has coins\n",
                    __func__, __LINE__, tx.GetHash().ToString());
                return MempoolReturnValue::INVALID;
            }

            // do all inputs exist?
            // Note that this does not check for the presence of actual outputs (see the next check for that),
            // and only helps with filling in pfMissingInputs (to determine missing vs spent).
            for(const CTxIn txin: tx.GetVin())
            {
                if (!view.HaveCoins(txin.prevout.hash))
                {
                    LogPrint("mempool", "%s():%d - Dropping tx %s : no coins for vin (tx=%s)\n",
                        __func__, __LINE__, tx.GetHash().ToString(), txin.prevout.hash.ToString());
                    return MempoolReturnValue::MISSING_INPUT;
                }
            }

            // are the actual inputs available?
            if (!view.HaveInputs(tx))
            {
                LogPrintf("%s():%d - ERROR: tx[%s]\n", __func__, __LINE__, hash.ToString());
                state.Invalid(error("%s(): inputs already spent", __func__),
                                     CValidationState::Code::DUPLICATED, "bad-txns-inputs-spent");
                return MempoolReturnValue::INVALID;
            }

            int nDoS = 0;

            // checking txs commitment tree validity
            if (ForkManager::getInstance().isNonCeasingSidechainActive(pcoinsTip->GetHeight())) {
                SidechainTxsCommitmentGuard scCommitmentGuard;
                bool retval = scCommitmentGuard.add(tx);
                if (!retval) {
                    nDoS = 100;
                    state.DoS(nDoS,
                        error("%s():%d - ERROR: Invalid tx[%s], SidechainTxsCommitmentGuard failed\n",
                            __func__, __LINE__, tx.GetHash().ToString()),
                        CValidationState::Code::INVALID, "sidechain-tx-txscommitmentguard");
                    return MempoolReturnValue::INVALID;
                }
            }

            // We pass pcoinsTip to IsScTxApplicableToState because we want to validate the fees against the last certificate
            // in the blockchain, and not against certificates in the mempool.
            CValidationState::Code ret_code = view.IsScTxApplicableToState(tx, Sidechain::ScFeeCheckFlag::LATEST_VALUE, pcoinsTip);
            if (ret_code != CValidationState::Code::OK)
            {
                if (ret_code == CValidationState::Code::INVALID_AND_BAN)
                    nDoS = 100;

                state.DoS(nDoS,
                    error("%s():%d - ERROR: sc-related tx [%s] is not applicable: ret_code[0x%x]\n",
                        __func__, __LINE__, hash.ToString(), CValidationState::CodeToChar(ret_code)),
                    ret_code, "bad-sc-tx-not-applicable");
                return MempoolReturnValue::INVALID;
            }

            // are the joinsplit's requirements met?
            if (!view.HaveJoinSplitRequirements(tx))
            {
                state.Invalid(error("%s():%d - joinsplit requirements not met", __func__, __LINE__),
                              CValidationState::Code::DUPLICATED, "bad-txns-joinsplit-requirements-not-met");
                return MempoolReturnValue::INVALID;
            }

            // Bring the best block into scope
            view.GetBestBlock();

            nFees = tx.GetFeeAmount(view.GetValueIn(tx));

            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            view.SetBackend(dummy);
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (getRequireStandard() && !AreInputsStandard(tx, view))
        {
            LogPrintf("%s():%d - Dropping tx %s : nonstandard transaction input\n",
                    __func__, __LINE__, tx.GetHash().ToString());
            return MempoolReturnValue::INVALID;
        }

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_STANDARD_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        unsigned int nSigOps = GetLegacySigOpCount(tx);
        nSigOps += GetP2SHSigOpCount(tx, view);
        if (nSigOps > MAX_STANDARD_TX_SIGOPS)
        {
            state.Invalid(
                error("%s():%d - too many sigops %s, %d > %d",
                    __func__, __LINE__, hash.ToString(), nSigOps, MAX_STANDARD_TX_SIGOPS),
                CValidationState::Code::NONSTANDARD, "bad-txns-too-many-sigops");
            return MempoolReturnValue::INVALID;
        }

        double dPriority = view.GetPriority(tx, chainActive.Height());
        LogPrint("mempool", "%s():%d - tx[%s], Computed fee=%lld, prio[%22.8f]\n", __func__, __LINE__, hash.ToString(), nFees, dPriority);

        CTxMemPoolEntry entry(tx, nFees, GetTime(), dPriority, chainActive.Height(), mempool.HasNoInputsOf(tx));
        unsigned int nSize = entry.GetTxSize();

        // Accept a tx if it contains joinsplits and has at least the default fee specified by z_sendmany.
        if (tx.GetVjoinsplit().size() > 0 && nFees >= ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE) {
            // In future we will we have more accurate and dynamic computation of fees for tx with joinsplits.
        } else {
            unsigned int block_priority_size = DEFAULT_BLOCK_PRIORITY_SIZE;
            if (!ForkManager::getInstance().areSidechainsSupported(nextBlockHeight))
                block_priority_size = DEFAULT_BLOCK_PRIORITY_SIZE_BEFORE_SC;

            // Don't accept it if it can't get into a block
            CAmount txMinFee = GetMinRelayFee(tx, nSize, true, block_priority_size);

            LogPrintf("nFees=%d, txMinFee=%d\n", nFees, txMinFee);
            if (fLimitFree == LimitFreeFlag::ON && nFees < txMinFee)
            {
                state.DoS(0, error("%s():%d - not enough fees %s, %d < %d",
                          __func__, __LINE__, hash.ToString(), nFees, txMinFee),
                          CValidationState::Code::INSUFFICIENT_FEE, "insufficient fee");
                return MempoolReturnValue::INVALID;
            }
        }

        // Require that free transactions have sufficient priority to be mined in the next block.
        if (GetBoolArg("-relaypriority", false) &&
            nFees < ::minRelayTxFee.GetFee(nSize) &&
            !AllowFree(view.GetPriority(tx, chainActive.Height() + 1)))
        {
            state.DoS(0, false, CValidationState::Code::INSUFFICIENT_FEE, "insufficient priority");
            return MempoolReturnValue::INVALID;
        }

        // Continuously rate-limit free (really, very-low-fee) transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm.
        if (fLimitFree == LimitFreeFlag::ON && nFees < ::minRelayTxFee.GetFee(nSize))
        {
            static CCriticalSection csFreeLimiter;
            static double dFreeCount;
            static int64_t nLastTime;
            int64_t nNow = GetTime();

            LOCK(csFreeLimiter);

            // Use an exponentially decaying ~10-minute window:
            dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
            nLastTime = nNow;
            // -limitfreerelay unit is thousand-bytes-per-minute
            // At default rate it would take over a month to fill 1GB
            if (dFreeCount >= GetArg("-limitfreerelay", 15)*10*1000)
            {
                state.DoS(0, error("%s():%d - free transaction rejected by rate limiter", __func__, __LINE__),
                          CValidationState::Code::INSUFFICIENT_FEE, "rate limited free transaction");
                return MempoolReturnValue::INVALID;
            }
            LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
            dFreeCount += nSize;
        }

        if (fRejectAbsurdFee == RejectAbsurdFeeFlag::ON && nFees > ::minRelayTxFee.GetFee(nSize) * 10000)
        {
            LogPrintf("%s():%d - absurdly high fees tx[%s], %d > %d\n",
                    __func__, __LINE__, hash.ToString(), nFees, ::minRelayTxFee.GetFee(nSize) * 10000);
            return MempoolReturnValue::INVALID;
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        if (!ContextualCheckTxInputs(tx, state, view, true, chainActive, STANDARD_CONTEXTUAL_SCRIPT_VERIFY_FLAGS, true, Params().GetConsensus()))
        {
            error("%s(): ConnectInputs failed %s", __func__, hash.ToString());
            return MempoolReturnValue::INVALID;
        }

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        if (!ContextualCheckTxInputs(tx, state, view, true, chainActive, MANDATORY_SCRIPT_VERIFY_FLAGS, true, Params().GetConsensus()))
        {
            error("%s(): BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s", __func__,  hash.ToString());
            return MempoolReturnValue::INVALID;
        }

        // Run the proof verification only if there is at least one CSW input.
        if (tx.GetVcswCcIn().size() > 0)
        {
            if (fProofVerification == MempoolProofVerificationFlag::ASYNC)
            {
                CScAsyncProofVerifier::GetInstance().LoadDataForCswVerification(view, tx, pfrom);
                return MempoolReturnValue::PARTIALLY_VALIDATED;
            }
            else if (fProofVerification == MempoolProofVerificationFlag::SYNC)
            {
                CScProofVerifier scVerifier{CScProofVerifier::Verification::Strict, CScProofVerifier::Priority::Low};
                scVerifier.LoadDataForCswVerification(view, tx);

                LogPrint("sc", "%s():%d - calling scVerifier.BatchVerify()\n", __func__, __LINE__);
                if (!scVerifier.BatchVerify())
                {
                    state.DoS(100,
                        error("%s():%d - ERROR: sc-related tx [%s] proof failed",
                            __func__, __LINE__, hash.ToString()),
                            CValidationState::Code::INVALID_PROOF, "bad-sc-tx-proof");
                    return MempoolReturnValue::INVALID;
                }
            }
        }

        pool.addUnchecked(hash, entry, !IsInitialBlockDownload());

        // Add memory address index
        if (fAddressIndex) {
            pool.addAddressIndex(entry.GetTx(), entry.GetTime(), view);
        }

        // Add memory spent index
        if (fSpentIndex) {
            pool.addSpentIndex(entry.GetTx(), view);
        }

    }

    return MempoolReturnValue::VALID;
}

MempoolReturnValue AcceptTxBaseToMemoryPool(CTxMemPool& pool, CValidationState &state, const CTransactionBase &txBase,
    LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee, MempoolProofVerificationFlag fProofVerification, CNode* pfrom)
{
    try
    {
        if (txBase.IsCertificate())
        {
            return AcceptCertificateToMemoryPool(pool, state, dynamic_cast<const CScCertificate&>(txBase), fLimitFree,
                                                 fRejectAbsurdFee, fProofVerification, pfrom);
        }
        else
        {
            return AcceptTxToMemoryPool(pool, state, dynamic_cast<const CTransaction&>(txBase), fLimitFree,
                                        fRejectAbsurdFee, fProofVerification, pfrom);
        }
    }
    catch (...)
    {
        LogPrintf("%s():%d - ERROR: txBase[%s] cast error\n", __func__, __LINE__, txBase.GetHash().ToString());
    }

    return MempoolReturnValue::INVALID;
}

bool GetTimestampIndex(const unsigned int &high, const unsigned int &low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &hashes)
{
    if (!fTimestampIndex)
        return error("Timestamp index not enabled");

    if (!pblocktree->ReadTimestampIndex(high, low, fActiveOnly, hashes))
        return error("Unable to get hashes for timestamps");

    return true;
}

bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value)
{
    if (!fSpentIndex)
        return false;

    if (mempool.getSpentIndex(key, value))
        return true;

    if (!pblocktree->ReadSpentIndex(key, value))
        return false;

    return true;
}

bool GetAddressIndex(uint160 addressHash, AddressType type,
                     std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> > &addressIndex, int start, int end)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressIndex(addressHash, type, addressIndex, start, end))
        return error("unable to get txids for address");

    return true;
}

bool GetAddressUnspent(uint160 addressHash, AddressType type,
                       std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressUnspentIndex(addressHash, type, unspentOutputs))
        return error("unable to get txids for address");

    return true;
}

/** Return transaction in tx, and if it was found inside a block, its hash is placed in hashBlock */
bool GetTransaction(const uint256 &hash, CTransaction &txOut, uint256 &hashBlock, bool fAllowSlow)
{
    LOCK(cs_main);

    if (mempool.lookup(hash, txOut))
        return true;

    if (fTxIndex)
    {
        CTxIndexValue txIndexValue;
        if (pblocktree->ReadTxIndex(hash, txIndexValue))
        {
            CAutoFile file(OpenBlockFile(txIndexValue.txPosition, true), SER_DISK, CLIENT_VERSION);
            if (file.IsNull())
                return error("%s: OpenBlockFile failed", __func__);
            CBlockHeader header;
            try
            {
                file >> header;
                fseek(file.Get(), txIndexValue.txPosition.nTxOffset, SEEK_CUR);
                file >> txOut;
            } catch (const std::exception& e)
            {
                return error("%s: Attempt to deserialize tx from disk failed or I/O error - %s", __func__, e.what());
            }
            hashBlock = header.GetHash();
            if (txOut.GetHash() != hash)
                return error("%s: txid mismatch", __func__);
            return true;
        }
    }

    if (fAllowSlow) // use coin database to locate block that contains transaction, and scan it
    {
        CBlockIndex *pindexSlow = nullptr;
        int nHeight = -1;
        {
            CCoinsViewCache &view = *pcoinsTip;
            const CCoins* coins = view.AccessCoins(hash);
            if (coins)
                nHeight = coins->nHeight;
        }
        if (nHeight > 0)
            pindexSlow = chainActive[nHeight];

        if (pindexSlow)
        {
            CBlock block;
            if (ReadBlockFromDisk(block, pindexSlow))
            {
                for(const CTransaction &tx: block.vtx)
                {
                    if (tx.GetHash() == hash)
                    {
                        txOut = tx;
                        hashBlock = pindexSlow->GetBlockHash();
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

/** Return certificate in certOut, and if it was found inside a block, its hash is placed in hashBlock */
bool GetCertificate(const uint256 &hash, CScCertificate &certOut, uint256 &hashBlock, bool fAllowSlow)
{
    LOCK(cs_main);

    if (mempool.lookup(hash, certOut))
        return true;

    if (fTxIndex)
    {
        CTxIndexValue txIndexValue;
        if (pblocktree->ReadTxIndex(hash, txIndexValue))
        {
            CAutoFile file(OpenBlockFile(txIndexValue.txPosition, true), SER_DISK, CLIENT_VERSION);
            if (file.IsNull())
                return error("%s: OpenBlockFile failed", __func__);
            CBlockHeader header;
            try
            {
                file >> header;
                fseek(file.Get(), txIndexValue.txPosition.nTxOffset, SEEK_CUR);
                file >> certOut;
            } catch (const std::exception& e)
            {
                return error("%s: Attempt to deserialize cert from disk failed or I/O error - %s", __func__, e.what());
            }
            hashBlock = header.GetHash();
            if (certOut.GetHash() != hash)
                return error("%s: txid mismatch", __func__);
            return true;
        }
    }

    if (fAllowSlow) // use coin database to locate block that contains cert, and scan it
    {
        int nHeight = -1;
        CBlockIndex *pindexSlow = nullptr;
        {
            CCoinsViewCache &view = *pcoinsTip;
            const CCoins* coins = view.AccessCoins(hash);
            if (coins)
                nHeight = coins->nHeight;
        }
        if (nHeight > 0)
            pindexSlow = chainActive[nHeight];

        if (pindexSlow)
        {
            CBlock block;
            if (ReadBlockFromDisk(block, pindexSlow))
            {
                for(const CScCertificate &cert: block.vcert)
                {
                    if (cert.GetHash() == hash)
                    {
                        certOut = cert;
                        hashBlock = pindexSlow->GetBlockHash();
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool GetTxBaseObj(const uint256 &hash, std::unique_ptr<CTransactionBase>& pTxBase, uint256 &hashBlock, bool fAllowSlow)
{
    CTransaction txAttempt;
    if (GetTransaction(hash, txAttempt, hashBlock, fAllowSlow))
    {
        pTxBase.reset(new CTransaction(txAttempt));
        return true;
    }

    CScCertificate certAttempt;
    if (GetCertificate(hash, certAttempt, hashBlock, fAllowSlow))
    {
        pTxBase.reset(new CScCertificate(certAttempt));
        return true;
    }

    return false;
}

//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool WriteBlockToDisk(CBlock& block, CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("WriteBlockToDisk: OpenBlockFile failed");

    // Write index header
    unsigned int nSize = fileout.GetSerializeSize(block);
    fileout << FLATDATA(messageStart) << nSize;

    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("WriteBlockToDisk: ftell failed");
    pos.nPos = (unsigned int)fileOutPos;
    fileout << block;

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos)
{
    block.SetNull();

    // Open history file to read
    CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("ReadBlockFromDisk: OpenBlockFile failed for %s", pos.ToString());

    // Read block
    try {
        filein >> block;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s at %s", __func__, e.what(), pos.ToString());
    }

    // Check the header
    if (!(CheckEquihashSolution(&block, Params()) &&
          CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus())))
        return error("ReadBlockFromDisk: Errors in block header at %s", pos.ToString());

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex)
{
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos()))
        return false;
    if (block.GetHash() != pindex->GetBlockHash())
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s",
                pindex->ToString(), pindex->GetBlockPos().ToString());
    return true;
}

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams)
{
    CAmount nSubsidy = 12.5 * COIN;
    if (nHeight == 0)
        return 0;

    // Mining slow start
    // The subsidy is ramped up linearly, skipping the middle payout of
    // MAX_SUBSIDY/2 to keep the monetary curve consistent with no slow start.
    if (nHeight < consensusParams.nSubsidySlowStartInterval / 2) {
        nSubsidy /= consensusParams.nSubsidySlowStartInterval;
        nSubsidy *= nHeight;
        return nSubsidy;
    } else if (nHeight < consensusParams.nSubsidySlowStartInterval) {
        nSubsidy /= consensusParams.nSubsidySlowStartInterval;
        nSubsidy *= (nHeight+1);
        return nSubsidy;
    }

    assert(nHeight > consensusParams.SubsidySlowStartShift());
    int halvings = (nHeight - consensusParams.SubsidySlowStartShift()) / consensusParams.nSubsidyHalvingInterval;
    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64)
        return 0;

    // Subsidy is cut in half every 840,000 blocks which will occur approximately every 4 years.
    nSubsidy >>= halvings;
    return nSubsidy;
}

bool IsInitialBlockDownload()
{
    static std::atomic<bool> lockIBDState{false};
    // Once this function has returned false, it must remain false.
    // Optimization: pre-test latch before taking the lock.
    if (lockIBDState.load(std::memory_order_relaxed))
        return false;

    const CChainParams& chainParams = Params();
    LOCK(cs_main);
    if (lockIBDState.load(std::memory_order_relaxed))
        return false;
    if (fImporting || fReindex || fReindexFast)
        return true;
    if (fCheckpointsEnabled && chainActive.Height() < Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()))
        return true;
    if (pindexBestHeader == nullptr)
        return true;
    if (chainActive.Tip() == nullptr)
        return true;
    if ((chainActive.Height() < pindexBestHeader->nHeight - 24 * 6 || pindexBestHeader->GetBlockTime() < GetTime() - chainParams.MaxTipAge()))
        return true;
    LogPrintf("Leaving InitialBlockDownload (latching to false)\n");
    lockIBDState.store(true, std::memory_order_relaxed);
    return false;
}

bool fLargeWorkForkFound = false;
bool fLargeWorkInvalidChainFound = false;
CBlockIndex *pindexBestForkTip = NULL, *pindexBestForkBase = NULL;

void alertNotify(const std::string& strMessage, bool fThread) {
    std::string strCmd = GetArg("-alertnotify", "");
    if (strCmd.empty()) return;

    // Alert text should be plain ascii coming from a trusted source, but to
    // be safe we first strip anything not in safeChars, then add single quotes around
    // the whole string before passing it to the shell:
    std::string singleQuote("'");
    std::string safeStatus = SanitizeString(strMessage);
    safeStatus = singleQuote+safeStatus+singleQuote;
    strCmd = std::regex_replace(strCmd, std::regex("%s"), safeStatus); 

    if (fThread) {
        std::thread t(runCommand, strCmd);
        t.detach();
    }
    else
        runCommand(strCmd);
}

void CheckForkWarningConditions()
{
    AssertLockHeld(cs_main);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before the last checkpoint)
    if (IsInitialBlockDownload())
        return;

    // If our best fork is no longer within 288 blocks (+/- 12 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && chainActive.Height() - pindexBestForkTip->nHeight >= 288)
        pindexBestForkTip = NULL;

    if (pindexBestForkTip || (pindexBestInvalid && pindexBestInvalid->nChainWork > chainActive.Tip()->nChainWork + (GetBlockProof(*chainActive.Tip()) * 6)))
    {
        if (!fLargeWorkForkFound && pindexBestForkBase)
        {
            std::string warning = std::string("'Warning: Large-work fork detected, forking after block ") +
                pindexBestForkBase->phashBlock->ToString() + std::string("'");
            alertNotify(warning, true);
        }
        if (pindexBestForkTip && pindexBestForkBase)
        {
            LogPrintf("%s: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n", __func__,
                   pindexBestForkBase->nHeight, pindexBestForkBase->phashBlock->ToString(),
                   pindexBestForkTip->nHeight, pindexBestForkTip->phashBlock->ToString());
            fLargeWorkForkFound = true;
        }
        else
        {
            std::string warning = std::string("Warning: Found invalid chain at least ~6 blocks longer than our best chain.\nChain state database corruption likely.");
            LogPrintf("%s: %s\n", warning.c_str(), __func__);
            alertNotify(warning, true);
            fLargeWorkInvalidChainFound = true;
        }
    }
    else
    {
        fLargeWorkForkFound = false;
        fLargeWorkInvalidChainFound = false;
    }
}

void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip)
{
    AssertLockHeld(cs_main);
    // If we are on a fork that is sufficiently large, set a warning flag
    CBlockIndex* pfork = pindexNewForkTip;
    CBlockIndex* plonger = chainActive.Tip();
    while (pfork && pfork != plonger)
    {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition where we should warn the user about as a fork of at least 7 blocks
    // with a tip within 72 blocks (+/- 3 hours if no one mines it) of ours
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork && (!pindexBestForkTip || (pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight)) &&
            pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
            chainActive.Height() - pindexNewForkTip->nHeight < 72)
    {
        pindexBestForkTip = pindexNewForkTip;
        pindexBestForkBase = pfork;
    }

    CheckForkWarningConditions();
}

// Requires cs_main.
void Misbehaving(NodeId pnode, int howmuch)
{
    if (howmuch == 0)
        return;

    CNodeState *state = State(pnode);
    if (state == NULL)
        return;

    state->nMisbehavior += howmuch;
    int banscore = GetArg("-banscore", 100);
    if (state->nMisbehavior >= banscore && state->nMisbehavior - howmuch < banscore)
    {
        LogPrintf("%s: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", __func__, state->name, state->nMisbehavior-howmuch, state->nMisbehavior);
        state->fShouldBan = true;
    } else
        LogPrintf("%s: %s (%d -> %d)\n", __func__, state->name, state->nMisbehavior-howmuch, state->nMisbehavior);
}

void static InvalidChainFound(CBlockIndex* pindexNew)
{
    if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork)
        pindexBestInvalid = pindexNew;

    LogPrintf("%s: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
      pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
      log(pindexNew->nChainWork.getdouble())/log(2.0), DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
      pindexNew->GetBlockTime()));
    CBlockIndex *tip = chainActive.Tip();
    assert (tip);
    LogPrintf("%s:  current best=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
      tip->GetBlockHash().ToString(), chainActive.Height(), log(tip->nChainWork.getdouble())/log(2.0),
      DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    CheckForkWarningConditions();
}

void static InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state)
{
    if (state.IsInvalid())
    {
        std::map<uint256, NodeId>::iterator it = mapBlockSource.find(pindex->GetBlockHash());
        if (it != mapBlockSource.end() && State(it->second))
        {
            CBlockReject reject = {state.GetRejectCode(), state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), pindex->GetBlockHash()};
            State(it->second)->rejects.push_back(reject);
            if (state.GetDoS() > 0)
                Misbehaving(it->second, state.GetDoS());
        }
    }

    if (!state.CorruptionPossible())
    {
        pindex->nStatus |= BLOCK_FAILED_VALID;
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);
    }
}

/**
 * Apply the undo operation of a CTxInUndo to the given chain state.
 * @param undo The undo object.
 * @param view The coins view to which to apply the changes.
 * @param out The out point that corresponds to the tx input.
 * @return True on success.
 */
bool ApplyTxInUndo(const CTxInUndo& undo, CCoinsViewCache& view, const COutPoint& out)
{
    bool fClean = true;

    CCoinsModifier coins = view.ModifyCoins(out.hash);
    if (undo.nHeight != 0)
    {
        coins->fCoinBase          = undo.fCoinBase;
        coins->nHeight            = undo.nHeight;
        coins->nVersion           = undo.nVersion;
        coins->nFirstBwtPos       = undo.nFirstBwtPos;
        coins->nBwtMaturityHeight = undo.nBwtMaturityHeight;
    } else
    {
        if (coins->IsPruned())
            fClean = fClean && error("%s: undo data adding output to missing transaction", __func__);
    }

    if (coins->IsAvailable(out.n))
        fClean = fClean && error("%s: undo data overwriting existing output", __func__);
    if (coins->vout.size() < out.n+1)
        coins->vout.resize(out.n+1);

    coins->vout[out.n] = undo.txout;

    return fClean;
}

void UpdateCoins(const CTransaction& tx, CCoinsViewCache &inputs, CTxUndo &txundo, int nHeight)
{
    // mark inputs spent
    if (!tx.IsCoinBase())
    {
        txundo.vprevout.reserve(tx.GetVin().size());
        for(const CTxIn &txin: tx.GetVin())
        {
            CCoinsModifier coins = inputs.ModifyCoins(txin.prevout.hash);
            unsigned nPos = txin.prevout.n;
            assert(coins->IsAvailable(nPos));

            // mark an outpoint spent, and construct undo information
            txundo.vprevout.push_back(CTxInUndo(coins->vout[nPos]));
            coins->Spend(nPos);
            if (coins->vout.size() == 0)
            {
                CTxInUndo& undo         = txundo.vprevout.back();
                undo.nHeight            = coins->nHeight;
                undo.fCoinBase          = coins->fCoinBase;
                undo.nVersion           = coins->nVersion;
                undo.nFirstBwtPos       = coins->nFirstBwtPos;
                undo.nBwtMaturityHeight = coins->nBwtMaturityHeight;
            }
        }
    }

    // spend nullifiers
    for(const JSDescription &joinsplit: tx.GetVjoinsplit()) {
        for(const uint256 &nf: joinsplit.nullifiers) {
            inputs.SetNullifier(nf, true);
        }
    }

    // add outputs
    inputs.ModifyCoins(tx.GetHash())->From(tx, nHeight);
}

void UpdateCoins(const CScCertificate& cert, CCoinsViewCache &inputs, CTxUndo &txundo, int nHeight, bool isBlockTopQualityCert)
{
    // mark inputs spent
    txundo.vprevout.reserve(cert.GetVin().size());
    for(const CTxIn &txin: cert.GetVin())
    {
        CCoinsModifier coins = inputs.ModifyCoins(txin.prevout.hash);
        unsigned nPos = txin.prevout.n;
        assert(coins->IsAvailable(nPos));

        // mark an outpoint spent, and construct undo information
        txundo.vprevout.push_back(CTxInUndo(coins->vout[nPos]));
        coins->Spend(nPos);
        if (coins->vout.size() == 0)
        {
            CTxInUndo& undo = txundo.vprevout.back();
            undo.nHeight            = coins->nHeight;
            undo.fCoinBase          = coins->fCoinBase;
            undo.nVersion           = coins->nVersion;
            undo.nFirstBwtPos       = coins->nFirstBwtPos;
            undo.nBwtMaturityHeight = coins->nBwtMaturityHeight;
        }
    }

    // add outputs
    CSidechain sidechain;
    assert(inputs.GetSidechain(cert.GetScId(), sidechain));
    int bwtMaturityHeight = sidechain.GetCertMaturityHeight(cert.epochNumber, nHeight);
    inputs.ModifyCoins(cert.GetHash())->From(cert, nHeight, bwtMaturityHeight, isBlockTopQualityCert);
    return;
}

CScriptCheck::CScriptCheck(): ptxTo(0), nIn(0), chain(nullptr),
                              nFlags(0), cacheStore(false),
                              error(SCRIPT_ERR_UNKNOWN_ERROR) {}
CScriptCheck::CScriptCheck(const CCoins& txFromIn, const CTransactionBase& txToIn,
                           unsigned int nInIn, const CChain* chainIn,
                           unsigned int nFlagsIn, bool cacheIn):
                            scriptPubKey(txFromIn.vout[txToIn.GetVin()[nInIn].prevout.n].scriptPubKey),
                            ptxTo(&txToIn), nIn(nInIn), chain(chainIn), nFlags(nFlagsIn),
                            cacheStore(cacheIn), error(SCRIPT_ERR_UNKNOWN_ERROR) { }

CScriptCheck::CScriptCheck(const CScript& scriptPubKeyIn, const CTransactionBase& txToIn,
                           unsigned int nInIn, const CChain* chainIn,
                           unsigned int nFlagsIn, bool cacheIn):
                            scriptPubKey(scriptPubKeyIn), ptxTo(&txToIn), nIn(nInIn), chain(chainIn),
                            nFlags(nFlagsIn), cacheStore(cacheIn), error(SCRIPT_ERR_UNKNOWN_ERROR) { }

bool CScriptCheck::operator()() {
    return ptxTo->VerifyScript(scriptPubKey, nFlags, nIn, chain, cacheStore, &error);
}

void CScriptCheck::swap(CScriptCheck &check) {
    scriptPubKey.swap(check.scriptPubKey);
    std::swap(ptxTo, check.ptxTo);
    std::swap(nIn, check.nIn);
    std::swap(chain, check.chain);
    std::swap(nFlags, check.nFlags);
    std::swap(cacheStore, check.cacheStore);
    std::swap(error, check.error);
}

ScriptError CScriptCheck::GetScriptError() const { return error; }

bool IsCommunityFund(const CCoins *coins, int nIn)
{
    if(coins != NULL &&
       coins->IsCoinBase() &&
       ForkManager::getInstance().isAfterChainsplit(coins->nHeight) &&
       static_cast<int>(coins->vout.size()) > nIn)
    {
        const Consensus::Params& consensusParams = Params().GetConsensus();
        CAmount reward = GetBlockSubsidy(coins->nHeight, consensusParams);

        for (Fork::CommunityFundType cfType=Fork::CommunityFundType::FOUNDATION; cfType < Fork::CommunityFundType::ENDTYPE; cfType = Fork::CommunityFundType(cfType + 1)) {
            if (ForkManager::getInstance().getCommunityFundReward(coins->nHeight, reward, cfType) > 0) {
                CScript communityScriptPubKey = Params().GetCommunityFundScriptAtHeight(coins->nHeight, cfType);
                if (coins->vout[nIn].scriptPubKey == communityScriptPubKey)
                    return true;
            }
        }
    }

    return false;
}

namespace Consensus {
bool CheckTxInputs(const CTransactionBase& txBase, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, const Consensus::Params& consensusParams)
{
    // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
    // for an attacker to attempt to split the network.
    if (!inputs.HaveInputs(txBase))
        return state.Invalid(error("CheckInputs(): %s inputs unavailable", txBase.GetHash().ToString()));

    // are the JoinSplit's requirements met?
    if (!inputs.HaveJoinSplitRequirements(txBase))
        return state.Invalid(error("CheckInputs(): %s JoinSplit requirements not met", txBase.GetHash().ToString()));

    CAmount nValueIn = 0;
    for (unsigned int i = 0; i < txBase.GetVin().size(); i++)
    {
        const CTxIn& in = txBase.GetVin().at(i);
        const COutPoint &prevout = in.prevout;
        const CCoins *coins = inputs.AccessCoins(prevout.hash);
        assert(coins);

        // Ensure that coinbases and certificates outputs are matured
        if (coins->IsCoinBase() || coins->IsFromCert() )
        {
            if (!coins->isOutputMature(in.prevout.n, nSpendHeight) )
            {
                LogPrintf("%s():%d - Error: txBase [%s] attempts to spend immature output [%d] of tx [%s]\n",
                        __func__, __LINE__, txBase.GetHash().ToString(), in.prevout.n, in.prevout.hash.ToString());
                LogPrintf("%s():%d - Error: Immature coin info: coin creation height [%d], output maturity height [%d], spend height [%d]\n",
                        __func__, __LINE__, coins->nHeight, coins->nBwtMaturityHeight, nSpendHeight);
                if (coins->IsCoinBase())
                    return state.Invalid(
                        error("%s(): tried to spend coinbase at depth %d", __func__, nSpendHeight - coins->nHeight),
                        CValidationState::Code::INVALID, "bad-txns-premature-spend-of-coinbase");
                if (coins->IsFromCert())
                    return state.Invalid(
                        error("%s(): tried to spend certificate before next epoch certificate is received", __func__),
                        CValidationState::Code::INVALID, "bad-txns-premature-spend-of-certificate");
            }
        }

        if (coins->IsCoinBase())
        {
            // Ensure that coinbases cannot be spent to transparent outputs
            // Disabled on regtest
            if (ForkManager::getInstance().mustCoinBaseBeShielded(nSpendHeight) &&
                !txBase.GetVout().empty())
            {
                // Since HARD_FORK_HEIGHT there is an exemption for community fund coinbase coins, so it is allowed
                // to send them to the transparent addr.
                bool fMustShieldCommunityFund = !ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(nSpendHeight);
                if (fMustShieldCommunityFund || !IsCommunityFund(coins, prevout.n))
                {
                    return state.Invalid(
                        error("%s(): tried to spend coinbase with transparent outputs", __func__),
                        CValidationState::Code::INVALID, "bad-txns-coinbase-spend-has-transparent-outputs");
                }
            }
        }
        else
        {
            ReplayProtectionLevel rpLevel = ForkManager::getInstance().getReplayProtectionLevel(nSpendHeight);

            if (rpLevel >= RPLEVEL_FIXED_2)
            {
                // check for invalid OP_CHECKBLOCKATHEIGHT in order to catch it before signature verifications are performed
                std::string reason;
                CScript scriptPubKey(coins->vout[prevout.n].scriptPubKey);

                if (!CheckReplayProtectionAttributes(scriptPubKey, reason) )
                {
                    return state.Invalid(
                        error("%s(): input %d has an invalid scriptPubKey %s (reason=%s)",
                            __func__, i, scriptPubKey.ToString(), reason),
                        CValidationState::Code::INVALID, "bad-txns-output-scriptpubkey");
                }
            }
        }

        // Check for negative or overflow input values
        nValueIn += coins->vout[prevout.n].nValue;
        if (!MoneyRange(coins->vout[prevout.n].nValue) || !MoneyRange(nValueIn))
            return state.DoS(100, error("%s(): txin values out of range", __func__),
                CValidationState::Code::INVALID, "bad-txns-inputvalues-outofrange");
    }

    try {
        nValueIn += txBase.GetCSWValueIn();
        if (!MoneyRange(nValueIn))
            return state.DoS(100, error("CheckInputs(): Total inputs value out of range."),
                CValidationState::Code::INVALID, "bad-txns-inputvalues-outofrange");
    } catch (const std::runtime_error& e) {
        return state.DoS(100, error("CheckInputs(): tx csw input values out of range"),
            CValidationState::Code::INVALID, "bad-txns-inputvalues-outofrange");
    }

    nValueIn += txBase.GetJoinSplitValueIn();
    if (!MoneyRange(nValueIn))
        return state.DoS(100, error("%s(): vpub_old values out of range", __func__),
                         CValidationState::Code::INVALID, "bad-txns-inputvalues-outofrange");

    if (!txBase.CheckFeeAmount(nValueIn, state))
        return false;

    return true;
}
}// namespace Consensus

bool InputScriptCheck(const CScript& scriptPubKey, const CTransactionBase& tx, unsigned int nIn,
                      const CChain& chain, unsigned int flags, bool cacheStore,  CValidationState &state, std::vector<CScriptCheck> *pvChecks)
{
    // Verify signature
    CScriptCheck check(scriptPubKey, tx, nIn, &chain, flags, cacheStore);
    if (pvChecks) {
        pvChecks->push_back(CScriptCheck());
        check.swap(pvChecks->back());
    } else if (!check()) {
        if (check.GetScriptError() == SCRIPT_ERR_NOT_FINAL) {
            return state.DoS(0, false, CValidationState::Code::NONSTANDARD, "non-final");
        }
        if (flags & STANDARD_CONTEXTUAL_NOT_MANDATORY_VERIFY_FLAGS) {
            // Check whether the failure was caused by a
            // non-mandatory script verification check, such as
            // non-standard DER encodings or non-null dummy
            // arguments; if so, don't trigger DoS protection to
            // avoid splitting the network between upgraded and
            // non-upgraded nodes.
            CScriptCheck check(scriptPubKey, tx, nIn, &chain,
                    flags & ~STANDARD_CONTEXTUAL_NOT_MANDATORY_VERIFY_FLAGS, cacheStore);
            if (check())
                return state.Invalid(false, CValidationState::Code::NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
        }
        // Failures of other flags indicate a transaction that is
        // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
        // such nodes as they are not following the protocol. That
        // said during an upgrade careful thought should be taken
        // as to the correct behavior - we may want to continue
        // peering with non-upgraded nodes even after a soft-fork
        // super-majority vote has passed.
        return state.DoS(100, false, CValidationState::Code::INVALID, strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())));
    }

    return true;
}

bool ContextualCheckTxInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, const CChain& chain, unsigned int flags, bool cacheStore, const Consensus::Params& consensusParams, std::vector<CScriptCheck> *pvChecks)
{
    if (!tx.IsCoinBase())
    {
        // While checking, GetHeight() is the height of the parent block.
        // This is also true for mempool checks.
        int spendHeight = inputs.GetHeight() + 1;
        if (!Consensus::CheckTxInputs(tx, state, inputs, spendHeight, consensusParams)) {
            return false;
        }

        if (pvChecks)
            pvChecks->reserve(tx.GetVin().size() + tx.GetVcswCcIn().size());

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip ECDSA signature verification when connecting blocks
        // before the last block chain checkpoint. This is safe because block merkle hashes are
        // still computed and checked, and any change will be caught at the next checkpoint.
        if (fScriptChecks) {
            for (unsigned int i = 0; i < tx.GetVin().size(); i++) {
                const COutPoint &prevout = tx.GetVin()[i].prevout;
                const CCoins* coins = inputs.AccessCoins(prevout.hash);
                assert(coins);

                const CScript& scriptPubKey = coins->vout[tx.GetVin()[i].prevout.n].scriptPubKey;
                if(!InputScriptCheck(scriptPubKey, tx, i, chain, flags, cacheStore, state, pvChecks)) {
                    return false;
                }
            }

            unsigned int vinSize = tx.GetVin().size();
            for (unsigned int i = 0; i < tx.GetVcswCcIn().size(); i++) {
                const CScript& scriptPubKey = tx.GetVcswCcIn()[i].scriptPubKey();
                if(!InputScriptCheck(scriptPubKey, tx, i + vinSize, chain, flags, cacheStore, state, pvChecks)) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool ContextualCheckCertInputs(const CScCertificate& cert, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, const CChain& chain, unsigned int flags, bool cacheStore, const Consensus::Params& consensusParams, std::vector<CScriptCheck> *pvChecks)
{
    // While checking, GetHeight() is the height of the parent block.
    // This is also true for mempool checks.
    int spendHeight = inputs.GetHeight() + 1;
    if (!Consensus::CheckTxInputs(cert, state, inputs, spendHeight, consensusParams)) {
        return false;
    }

    if (pvChecks)
        pvChecks->reserve(cert.GetVin().size());

    // The first loop above does all the inexpensive checks.
    // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
    // Helps prevent CPU exhaustion attacks.

    // Skip ECDSA signature verification when connecting blocks
    // before the last block chain checkpoint. This is safe because block merkle hashes are
    // still computed and checked, and any change will be caught at the next checkpoint.
    if (fScriptChecks) {
        for (unsigned int i = 0; i < cert.GetVin().size(); i++) {
            const COutPoint &prevout = cert.GetVin()[i].prevout;
            const CCoins* coins = inputs.AccessCoins(prevout.hash);
            assert(coins);

            const CScript& scriptPubKey = coins->vout[cert.GetVin()[i].prevout.n].scriptPubKey;
            if(!InputScriptCheck(scriptPubKey, cert, i, chain, flags, cacheStore, state, pvChecks)) {
                return false;
            }
        }
    }

    return true;
}

namespace {

bool UndoWriteToDisk(const CBlockUndo& blockundo, CDiskBlockPos& pos, const uint256& hashBlock, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s: OpenUndoFile failed", __func__);

    // Write index header
    unsigned int nSize = fileout.GetSerializeSize(blockundo);
    fileout << FLATDATA(messageStart) << nSize;

    // Write undo data
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("%s: ftell failed", __func__);
    pos.nPos = (unsigned int)fileOutPos;
    fileout << blockundo;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    fileout << hasher.GetHash();

    return true;
}

bool UndoReadFromDisk(CBlockUndo& blockundo, const CDiskBlockPos& pos, const uint256& hashBlock)
{
    // Open history file to read
    CAutoFile filein(OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("%s: OpenBlockFile failed", __func__);

    // Read block
    uint256 hashChecksum;
    try {
        filein >> blockundo;
        filein >> hashChecksum;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    // Verify checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;

    LogPrint("sc", "%s\n", blockundo.ToString());

    if (hashChecksum != hasher.GetHash())
        return error("%s: Checksum mismatch", __func__);

    return true;
}

/** Abort with a message */
bool AbortNode(const std::string& strMessage, const std::string& userMessage="")
{
    strMiscWarning = strMessage;
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occurred, see debug.log for details") : userMessage,
        "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

bool AbortNode(CValidationState& state, const std::string& strMessage, const std::string& userMessage="")
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

} // anon namespace

bool DisconnectBlock(CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, flagLevelDBIndexesWrite explorerIndexesWrite,
                     bool* pfClean, std::vector<CScCertificateStatusUpdateInfo>* pCertsStateInfo)
{
    std::vector<std::pair<CAddressIndexKey, CAddressIndexValue>> addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue>> spentIndex;

    std::vector<std::pair<uint256, CTxIndexValue> > vTxIndexValues;
    std::vector<std::pair<CMaturityHeightKey, CMaturityHeightValue> > maturityHeightValues;

    assert(pindex->GetBlockHash() == view.GetBestBlock());

    if (pfClean)
        *pfClean = false;

    bool fClean = true;

    IncludeScAttributes includeSc = IncludeScAttributes::ON;

    if (block.nVersion != BLOCK_VERSION_SC_SUPPORT)
        includeSc = IncludeScAttributes::OFF;

    CBlockUndo blockUndo(includeSc);

    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
        return error("DisconnectBlock(): no undo data available");
    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockHash()))
        return error("DisconnectBlock(): failure reading undo data");

    if (blockUndo.vtxundo.size() != (block.vtx.size() - 1 + block.vcert.size()))
        return error("DisconnectBlock(): block and undo data inconsistent");

    if (!view.RevertSidechainEvents(blockUndo, pindex->nHeight, pCertsStateInfo))
    {
        LogPrint("cert", "%s():%d - SIDECHAIN-EVENT: failed reverting scheduled event\n", __func__, __LINE__);
        return error("DisconnectBlock(): cannot revert sidechains scheduled events");
    }

    if (explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
    {
        if (fTxIndex)
        {
            view.RevertTxIndexSidechainEvents(pindex->nHeight, blockUndo, pblocktree, vTxIndexValues);
        }

        if (fMaturityHeightIndex) {
            //Restore the previous ceased sidechain
            view.RevertMaturityHeightIndexSidechainEvents(pindex->nHeight, blockUndo, pblocktree, maturityHeightValues);
        }

        if (fAddressIndex) {
            view.RevertIndexesSidechainEvents(pindex->nHeight, blockUndo, pblocktree, addressIndex, addressUnspentIndex);
        }
    }

    // not including coinbase
    const int certOffset = block.vtx.size() - 1;
    std::map<uint256, uint256> highQualityCertData = HighQualityCertData(block, blockUndo);
    // key: current block top quality cert for given sc --> value: prev block superseeded cert hash (possibly null)

    // undo certificates in reverse order
    for (int i = block.vcert.size() - 1; i >= 0; i--) {
        const CScCertificate& cert = block.vcert[i];
        uint256 hash = cert.GetHash();
        bool isBlockTopQualityCert = highQualityCertData.count(cert.GetHash()) != 0;

        CSidechain sidechain;
        assert(view.GetSidechain(cert.GetScId(), sidechain));
        if (sidechain.isNonCeasing())   // For non-ceasing SC cert should always be top quality
        {
            assert(isBlockTopQualityCert);
        }

        LogPrint("cert", "%s():%d - reverting outs of cert[%s]\n", __func__, __LINE__, hash.ToString());

        if (explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
        {
            if (fTxIndex)
            {
                // Set the disconnected certificate as invalid with maturityHeight -1
                CTxIndexValue txIndexVal;
                assert(pblocktree->ReadTxIndex(hash, txIndexVal));
                txIndexVal.maturityHeight = CTxIndexValue::INVALID_MATURITY_HEIGHT;
                vTxIndexValues.push_back(std::make_pair(hash, txIndexVal));
            }


            // Update the explorer indexes according to the removed outputs
            if (fAddressIndex)
            {
                for (unsigned int k = cert.GetVout().size(); k-- > 0;)
                {
                    const CTxOut &out = cert.GetVout()[k];
                    CScript::ScriptType scriptType = out.scriptPubKey.GetType();

                    if (scriptType != CScript::ScriptType::UNKNOWN)
                    {
                        const uint160 addrHash = out.scriptPubKey.AddressHash();
                        const AddressType addressType = fromScriptTypeToAddressType(scriptType);

                        // undo receiving activity
                        addressIndex.push_back(make_pair(CAddressIndexKey(addressType, addrHash, pindex->nHeight, i, hash, k, false),
                                                         CAddressIndexValue()));

                        // undo unspent index
                        addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(addressType, addrHash, hash, k), CAddressUnspentValue()));
                    }
                }
            }
        }

        // Check that all outputs are available and match the outputs in the block itself
        // exactly.
        {
            CCoinsModifier outs = view.ModifyCoins(hash);
            outs->ClearUnspendable();

            int bwtMaturityHeight = sidechain.GetCertMaturityHeight(cert.epochNumber, pindex->nHeight);
            CCoins outsBlock(cert, pindex->nHeight, bwtMaturityHeight, isBlockTopQualityCert);

            // The CCoins serialization does not serialize negative numbers.
            // No network rules currently depend on the version here, so an inconsistency is harmless
            // but it must be corrected before txout nversion ever influences a network rule.
            if (outsBlock.nVersion < 0)
                outs->nVersion = outsBlock.nVersion;


            if (*outs != outsBlock) {

                LogPrint("cert", "%s():%d - outs     :%s\n", __func__, __LINE__, outs->ToString());
                LogPrint("cert", "%s():%d - outsBlock:%s\n", __func__, __LINE__, outsBlock.ToString());

                fClean = fClean && error("DisconnectBlock(): added certificate mismatch? database corrupted");
                //LogPrint("cert", "%s():%d - mismatched cert hash [%s]\n", __func__, __LINE__, hash.ToString());
            }

            // remove outputs
            LogPrint("cert", "%s():%d - clearing outs of cert[%s]\n", __func__, __LINE__, hash.ToString());
            outs->Clear();
        }

        const CTxUndo &certUndo = blockUndo.vtxundo[certOffset + i];
        if (isBlockTopQualityCert)
        {
            const uint256& prevBlockTopQualityCertHash = highQualityCertData.at(cert.GetHash());
            //Used only if fMaturityHeightIndex == true
            int certMaturityHeight = -1;

            // prevBlockTopQualityCertHash should always be null in v2 non-ceasing sc
            if (!prevBlockTopQualityCertHash.IsNull()) {
                assert(!sidechain.isNonCeasing());
            }

            //Remove the current certificate from the MaturityHeight DB
            if (fMaturityHeightIndex && explorerIndexesWrite == flagLevelDBIndexesWrite::ON) {
                certMaturityHeight = sidechain.GetCertMaturityHeight(cert.epochNumber, pindex->nHeight);
                CMaturityHeightKey maturityHeightKey = CMaturityHeightKey(certMaturityHeight, cert.GetHash());
                maturityHeightValues.push_back(make_pair(maturityHeightKey, CMaturityHeightValue()));
            }

            // cancels scEvents only if cert is first in its epoch, i.e. if it won't restore any other cert
            if (!prevBlockTopQualityCertHash.IsNull())
            {
                // resurrect prevBlockTopQualityCertHash bwts
                assert(blockUndo.scUndoDatabyScId.at(cert.GetScId()).contentBitMask & CSidechainUndoData::AvailableSections::SUPERSEDED_CERT_DATA);
                view.RestoreBackwardTransfers(prevBlockTopQualityCertHash, blockUndo.scUndoDatabyScId.at(cert.GetScId()).lowQualityBwts);

                if (explorerIndexesWrite == flagLevelDBIndexesWrite::ON) {
                    //Restore the previous top certificate in the MaturityHeight DB
                    if (fMaturityHeightIndex) {
                        assert(certMaturityHeight != -1);
                        const CMaturityHeightKey maturityHeightKey = CMaturityHeightKey(certMaturityHeight, prevBlockTopQualityCertHash);
                        maturityHeightValues.push_back(std::make_pair(maturityHeightKey, CMaturityHeightValue(static_cast<char>(1))));
                    }

                    // Set the lower quality BTs as top quality
                    if (fAddressIndex) {
                        CTxIndexValue txIndexVal;
                        assert(pblocktree->ReadTxIndex(prevBlockTopQualityCertHash, txIndexVal));

                        view.UpdateBackwardTransferIndexes(prevBlockTopQualityCertHash, txIndexVal.txIndex, addressIndex, addressUnspentIndex,
                                                           CCoinsViewCache::flagIndexesUpdateType::RESTORE_CERTIFICATE);
                    }
                }
            }

            // Refresh previous certificate in wallet, whether it has been just restored or it is from previous epoch
            // On the contrary, cert will have BWT_OFF status since it will end up off blockchain anyhow.
            if (pCertsStateInfo!= nullptr)
            {
                pCertsStateInfo->push_back(CScCertificateStatusUpdateInfo(cert.GetScId(),
                                           blockUndo.scUndoDatabyScId.at(cert.GetScId()).prevTopCommittedCertHash,
                                           blockUndo.scUndoDatabyScId.at(cert.GetScId()).prevTopCommittedCertReferencedEpoch,
                                           blockUndo.scUndoDatabyScId.at(cert.GetScId()).prevTopCommittedCertQuality,
                                           CScCertificateStatusUpdateInfo::BwtState::BWT_ON));
            }

            if (!view.RestoreSidechain(cert, blockUndo.scUndoDatabyScId.at(cert.GetScId())))
            {
                LogPrint("sc", "%s():%d - ERROR undoing certificate\n", __func__, __LINE__);
                return error("DisconnectBlock(): certificate can not be reverted: data inconsistent");
            }
        }

        // restore inputs
        if (certUndo.vprevout.size() != cert.GetVin().size())
            return error("DisconnectBlock(): certificate and undo data inconsistent");
        for (unsigned int j = cert.GetVin().size(); j-- > 0;) {
            const COutPoint &out = cert.GetVin()[j].prevout;
            const CTxInUndo &undo = certUndo.vprevout[j];
            if (!ApplyTxInUndo(undo, view, out)) {
                LogPrint("cert", "%s():%d ApplyTxInUndo returned FALSE on cert [%s] \n", __func__, __LINE__, cert.GetHash().ToString());
                fClean = false;
            }

            if (explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
            {
                // Update the explorer indexes according to the removed inputs
                if (fAddressIndex)
                {
                    CScript::ScriptType scriptType = undo.txout.scriptPubKey.GetType();
                    if (scriptType != CScript::ScriptType::UNKNOWN)
                    {
                        const uint160 addrHash = undo.txout.scriptPubKey.AddressHash();
                        const AddressType addressType = fromScriptTypeToAddressType(scriptType);
                        
                        // undo spending activity
                        addressIndex.push_back(make_pair(CAddressIndexKey(addressType, addrHash, pindex->nHeight, i, hash, j, true),
                                                        CAddressIndexValue()));
                        // restore unspent index
                        addressUnspentIndex.push_back(make_pair(
                            CAddressUnspentKey(addressType, addrHash, undo.txout.GetHash(), out.n),
                            CAddressUnspentValue(undo.txout.nValue, undo.txout.scriptPubKey, undo.nHeight, 0)));
                    }
                }
                if (fSpentIndex) {
                    // undo and delete the spent index
                    spentIndex.push_back(make_pair(CSpentIndexKey(out.hash, out.n), CSpentIndexValue()));
                }
            }

        }
    }

    // undo transactions in reverse order
    for (int i = block.vtx.size() - 1; i >= 0; i--) {
        const CTransaction &tx = block.vtx[i];
        uint256 hash = tx.GetHash();

        if (fAddressIndex && explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
        {
            for (unsigned int k = tx.GetVout().size(); k-- > 0;)
            {
                const CTxOut &out = tx.GetVout()[k];
                CScript::ScriptType scriptType = out.scriptPubKey.GetType();

                if (scriptType != CScript::ScriptType::UNKNOWN)
                {
                    const uint160 addrHash = out.scriptPubKey.AddressHash();
                    const AddressType addressType = fromScriptTypeToAddressType(scriptType);

                    // undo receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(addressType, addrHash, pindex->nHeight, i, hash, k, false),
                                                     CAddressIndexValue()));

                    // undo unspent index
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(addressType, addrHash, hash, k),
                                                            CAddressUnspentValue()));
                }
            }
        }

        // Check that all outputs are available and match the outputs in the block itself
        // exactly.
        {
            CCoinsModifier outs = view.ModifyCoins(hash);
            outs->ClearUnspendable();

            CCoins outsBlock(tx, pindex->nHeight);
            // The CCoins serialization does not serialize negative numbers.
            // No network rules currently depend on the version here, so an inconsistency is harmless
            // but it must be corrected before txout nversion ever influences a network rule.
            if (outsBlock.nVersion < 0)
                outs->nVersion = outsBlock.nVersion;
            if (*outs != outsBlock) {
                fClean = fClean && error("DisconnectBlock(): added transaction mismatch? database corrupted");
                LogPrint("cert", "%s():%d - tx[%s]\n", __func__, __LINE__, hash.ToString());
            }

            // remove outputs
            LogPrint("cert", "%s():%d - clearing outs of tx[%s]\n", __func__, __LINE__, hash.ToString());
            outs->Clear();
        }

        // unspend nullifiers
        BOOST_FOREACH(const JSDescription &joinsplit, tx.GetVjoinsplit()) {
            BOOST_FOREACH(const uint256 &nf, joinsplit.nullifiers) {
                view.SetNullifier(nf, false);
            }
        }

        for (const CTxCeasedSidechainWithdrawalInput& cswIn:tx.GetVcswCcIn()) {
            if (!view.RemoveCswNullifier(cswIn.scId, cswIn.nullifier)) {
                LogPrint("sc", "%s():%d - ERROR removing csw nullifier\n", __func__, __LINE__);
                return error("DisconnectBlock(): nullifiers cannot be reverted: data inconsistent");
            }
        }

        LogPrint("sc", "%s():%d - undo sc outputs if any\n", __func__, __LINE__);
        if (!view.RevertTxOutputs(tx, pindex->nHeight) )
        {
            LogPrint("sc", "%s():%d - ERROR undoing sc creation\n", __func__, __LINE__);
            return error("DisconnectBlock(): sc creation can not be reverted: data inconsistent");
        }

        // restore inputs
        if (i > 0) { // not coinbases
            const CTxUndo &txundo = blockUndo.vtxundo[i-1];
            if (txundo.vprevout.size() != tx.GetVin().size())
                return error("DisconnectBlock(): transaction and undo data inconsistent");
            for (unsigned int j = tx.GetVin().size(); j-- > 0;) {
                const COutPoint &out = tx.GetVin()[j].prevout;
                const CTxInUndo &undo = txundo.vprevout[j];
                if (!ApplyTxInUndo(undo, view, out)) {
                    LogPrint("cert", "%s():%d ApplyTxInUndo returned FALSE on tx [%s] \n", __func__, __LINE__, tx.GetHash().ToString());
                    fClean = false;
                }

                if (explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
                {
                    const CTxIn input = tx.GetVin()[j];

                    if (fAddressIndex)
                    {
                        const CTxOut &prevout = view.GetOutputFor(tx.GetVin()[j]);
                        CScript::ScriptType scriptType = prevout.scriptPubKey.GetType();

                        if (scriptType != CScript::ScriptType::UNKNOWN)
                        {
                            const uint160 addrHash = prevout.scriptPubKey.AddressHash();
                            const AddressType addressType = fromScriptTypeToAddressType(scriptType);

                            // undo spending activity
                            addressIndex.push_back(make_pair(CAddressIndexKey(addressType, addrHash, pindex->nHeight, i, hash, j, true),
                                                             CAddressIndexValue()));

                            // restore unspent index
                            addressUnspentIndex.push_back(make_pair(
                                CAddressUnspentKey(addressType, addrHash, input.prevout.hash, input.prevout.n),
                                CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight, 0)));
                        }
                    }

                    if (fSpentIndex)
                    {
                        // undo and delete the spent index
                        spentIndex.push_back(make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue()));
                    }
                }

            }
        }
    }

    // set the old best anchor back
    view.PopAnchor(blockUndo.old_tree_root);

    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());

    if (pfClean) {
        *pfClean = fClean;
        return true;
    }

    if (explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
    {
        if (fTxIndex) {
            if (!pblocktree->WriteTxIndex(vTxIndexValues))
                return AbortNode(state, "Failed to write transaction index");
        }
        if (fMaturityHeightIndex) {
            if (!pblocktree->UpdateMaturityHeightIndex(maturityHeightValues)) {
                return AbortNode(state, "Failed to write maturity height index");
            }
        }

        if (fAddressIndex)
        {
            if (!pblocktree->UpdateAddressIndex(addressIndex))
            {
                return AbortNode(state, "Failed to update address index");
            }
            if (!pblocktree->UpdateAddressUnspentIndex(addressUnspentIndex))
            {
                return AbortNode(state, "Failed to update address unspent index");
            }
        }

        if (fSpentIndex)
        {
            if (!pblocktree->UpdateSpentIndex(spentIndex))
            {
                return AbortNode(state, "Failed to update address spent index");
            }
        }
    }

    return fClean;
}

void static FlushBlockFile(bool fFinalize = false)
{
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE *fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize);

static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void ThreadScriptCheck() {
    RenameThread("horizen-scriptch");
    scriptcheckqueue.Thread();
}

//
// Called periodically asynchronously; alerts if it smells like
// we're being fed a bad chain (blocks being generated much
// too slowly or too quickly).
//
void PartitionCheck(bool (*initialDownloadCheck)(), CCriticalSection& cs, const CBlockIndex *const &bestHeader,
                    int64_t nPowTargetSpacing)
{
    if (bestHeader == NULL || initialDownloadCheck()) return;

    static int64_t lastAlertTime = 0;
    int64_t now = GetTime();
    if (lastAlertTime > now-60*60*24) return; // Alert at most once per day

    const int SPAN_HOURS=4;
    const int SPAN_SECONDS=SPAN_HOURS*60*60;
    int BLOCKS_EXPECTED = SPAN_SECONDS / nPowTargetSpacing;

    boost::math::poisson_distribution<double> poisson(BLOCKS_EXPECTED);

    std::string strWarning;
    int64_t startTime = GetTime()-SPAN_SECONDS;

    LOCK(cs);
    const CBlockIndex* i = bestHeader;
    int nBlocks = 0;
    while (i->GetBlockTime() >= startTime) {
        ++nBlocks;
        i = i->pprev;
        if (i == NULL) return; // Ran out of chain, we must not be fully sync'ed
    }

    // How likely is it to find that many by chance?
    double p = boost::math::pdf(poisson, nBlocks);

    LogPrint("partitioncheck", "%s : Found %d blocks in the last %d hours\n", __func__, nBlocks, SPAN_HOURS);
    LogPrint("partitioncheck", "%s : likelihood: %g\n", __func__, p);

    // Aim for one false-positive about every fifty years of normal running:
    const int FIFTY_YEARS = 50*365*24*60*60;
    double alertThreshold = 1.0 / (FIFTY_YEARS / SPAN_SECONDS);

    if (p <= alertThreshold && nBlocks < BLOCKS_EXPECTED)
    {
        // Many fewer blocks than expected: alert!
        strWarning = strprintf(_("WARNING: check your network connection, %d blocks received in the last %d hours (%d expected)"),
                               nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
    }
    else if (p <= alertThreshold && nBlocks > BLOCKS_EXPECTED)
    {
        // Many more blocks than expected: alert!
        strWarning = strprintf(_("WARNING: abnormally high number of blocks generated, %d blocks received in the last %d hours (%d expected)"),
                               nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
    }
    if (!strWarning.empty())
    {
        strMiscWarning = strWarning;
        alertNotify(strWarning, true);
        lastAlertTime = now;
    }
}


static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;

bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view,
    const CChain& chain, flagBlockProcessingType processingType, flagScRelatedChecks fScRelatedChecks,
    flagScProofVerification fScProofVerification, flagLevelDBIndexesWrite explorerIndexesWrite,
    std::vector<CScCertificateStatusUpdateInfo>* pCertsStateInfo)
{
    /**
     * When using CHECK_ONLY there is no need to write explorer indexes.
     * When ConnectBlock() is called from VerifyDB() the type of block processing
     * is COMPLETE but writing on the Level DB of explorer indexes must be disabled.
     */
    if (processingType == flagBlockProcessingType::CHECK_ONLY)
    {
        assert(explorerIndexesWrite == flagLevelDBIndexesWrite::OFF);
    }

    int64_t nTime0 = GetTimeMicros();

    const CChainParams& chainparams = Params();
    AssertLockHeld(cs_main);

    if(block.nVersion != BLOCK_VERSION_SC_SUPPORT)
    {
        fScRelatedChecks = flagScRelatedChecks::OFF;
    }

    bool fExpensiveChecks = true;
    if (fCheckpointsEnabled) {
        CBlockIndex *pindexLastCheckpoint = Checkpoints::GetLastCheckpoint(chainparams.Checkpoints());
        if (pindexLastCheckpoint && pindexLastCheckpoint->GetAncestor(pindex->nHeight) == pindex) {
            // This block is an ancestor of a checkpoint: disable script checks
            fExpensiveChecks = false;
        }
    }

    bool pauseLowPrioZendooThread = (
        fExpensiveChecks &&
        fScRelatedChecks == flagScRelatedChecks::ON &&
        fScProofVerification == flagScProofVerification::ON &&
        SidechainTxsCommitmentBuilder::getEmptyCommitment() != block.hashScTxsCommitment // no sc related tx/certs
    );

    // if necessary pause rust low priority threads in order to speed up times
    // Note: it works even if the same code was executed for the high priority proof verifier
    CZendooLowPrioThreadGuard lowPrioThreadGuard(pauseLowPrioZendooThread);

    auto verifier = libzcash::ProofVerifier::Strict();
    auto disabledVerifier = libzcash::ProofVerifier::Disabled();

    // Check it again to verify JoinSplit proofs, and in case a previous version let a bad block in
    if (!CheckBlock(block, state, fExpensiveChecks ? verifier : disabledVerifier,
                    processingType == flagBlockProcessingType::COMPLETE ? flagCheckPow::ON : flagCheckPow::OFF,
                    processingType == flagBlockProcessingType::COMPLETE ? flagCheckMerkleRoot::ON: flagCheckMerkleRoot::OFF))
        return false;

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == NULL ? uint256() : pindex->pprev->GetBlockHash();
    assert(hashPrevBlock == view.GetBestBlock());

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (block.GetHash() == chainparams.GetConsensus().hashGenesisBlock) {
        if (processingType == flagBlockProcessingType::COMPLETE)
        {
            view.SetBestBlock(pindex->GetBlockHash());
            // Before the genesis block, there was an empty tree
            ZCIncrementalMerkleTree tree;
            pindex->hashAnchor = tree.root();
            // The genesis block contained no JoinSplits
            pindex->hashAnchorEnd = pindex->hashAnchor;
        }
        return true;
    }

    // Do not allow blocks that contain transactions which 'overwrite' older transactions,
    // unless those are already completely spent.
    for(const CTransaction& tx: block.vtx)
    {
        const CCoins* coins = view.AccessCoins(tx.GetHash());
        if (coins && !coins->IsPruned())
            return state.DoS(100, error("%s():%d: tried to overwrite transaction",__func__, __LINE__),
                             CValidationState::Code::INVALID, "bad-txns-BIP30");
    }
    for(const CScCertificate& cert: block.vcert)
    {
        const CCoins* coins = view.AccessCoins(cert.GetHash());
        if (coins && !coins->IsPruned())
            return state.DoS(100, error("%s():%d: tried to overwrite certificate",__func__, __LINE__),
                             CValidationState::Code::INVALID, "bad-txns-BIP30");
    }


    // Started enforcing CHECKBLOCKATHEIGHT from block.nVersion=4, that means for all the blocks
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY | SCRIPT_VERIFY_CHECKBLOCKATHEIGHT;

    // DERSIG (BIP66) is also always enforced, but does not have a flag.

    IncludeScAttributes includeSc = IncludeScAttributes::ON;

    if (block.nVersion != BLOCK_VERSION_SC_SUPPORT)
        includeSc = IncludeScAttributes::OFF;

    CBlockUndo blockundo(includeSc);

    CCheckQueueControl<CScriptCheck> control(fExpensiveChecks && nScriptCheckThreads ? &scriptcheckqueue : NULL);

    int64_t deltaPreProcTime = GetTimeMicros() - nTime0;
    LogPrint("bench", "    - block preproc: %.2fms\n", 0.001 * deltaPreProcTime);

    int64_t nTimeStart = GetTimeMicros();
    CAmount nFees = 0;
    int nInputs = 0;
    unsigned int nSigOps = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CTxIndexValue> > vTxIndexValues;
    vTxIndexValues.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1 + block.vcert.size());
    std::vector<std::pair<CMaturityHeightKey,CMaturityHeightValue>> maturityHeightValues;

    std::vector<std::pair<CAddressIndexKey, CAddressIndexValue>> addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue>> spentIndex;

    // Construct the incremental merkle tree at the current
    // block position,
    auto old_tree_root = view.GetBestAnchor();
    // saving the top anchor in the block index as we go.
    if (processingType == flagBlockProcessingType::COMPLETE)
    {
        pindex->hashAnchor = old_tree_root;
    }
    ZCIncrementalMerkleTree tree;
    // This should never fail: we should always be able to get the root
    // that is on the tip of our chain
    assert(view.GetAnchorAt(old_tree_root, tree));

    {
        // Consistency check: the root of the tree we're given should
        // match what we asked for.
        assert(tree.root() == old_tree_root);
    }

    // Check sidechain txs commitment tree limits now. This is less expensive than populating a txsCommitmentBuilder
    if (ForkManager::getInstance().isNonCeasingSidechainActive(pindex->nHeight)) {
        SidechainTxsCommitmentGuard scCommGuard;
        for (unsigned int txIdx = 0; txIdx < block.vtx.size(); ++txIdx) {
            const CTransaction &tx = block.vtx[txIdx];
            if (!scCommGuard.add(tx))
                return state.DoS(100, error("%s():%d: cannot add tx to scTxsCommitment guard", __func__, __LINE__),
                    CValidationState::Code::INVALID, "bad-blk-tx-commitguard");
        }
        for (unsigned int certIdx = 0; certIdx < block.vcert.size(); certIdx++) {
            const CScCertificate &cert = block.vcert[certIdx];
            if (!scCommGuard.add(cert))
                return state.DoS(100, error("%s():%d: cannot add cert to scTxsCommitmentBuilder", __func__, __LINE__),
                    CValidationState::Code::INVALID, "bad-blk-cert-commitguard");
        }
    }

    const auto scVerifierMode = fExpensiveChecks ?
                CScProofVerifier::Verification::Strict : CScProofVerifier::Verification::Loose;
    // Set high priority to verify the proofs as soon as possible (pausing mempool verification operations if any.)
    CScProofVerifier scVerifier{scVerifierMode, CScProofVerifier::Priority::High};
    // We check scCommitmentBuilder's status after adding each tx or cert to avoid accepting blocks
    // having a total number of sc or ft / bwt / csw / cert per sidechain greater than currently
    // supported by CCTPlib.
    // We also check that the on-the-fly calculated scTxsCommitment is equal to that included in the block.
    SidechainTxsCommitmentBuilder scCommitmentBuilder;

    for (unsigned int txIdx = 0; txIdx < block.vtx.size(); ++txIdx) // Processing transactions loop
    {
        const CTransaction &tx = block.vtx[txIdx];

        nInputs += tx.GetVin().size() + tx.GetVcswCcIn().size();
        nSigOps += GetLegacySigOpCount(tx);
        if (nSigOps > MAX_BLOCK_SIGOPS)
            return state.DoS(100, error("%s():%d: too many sigops",__func__, __LINE__),
                             CValidationState::Code::INVALID, "bad-blk-sigops");

        if (!tx.IsCoinBase())
        {
            if (!view.HaveInputs(tx))
                return state.DoS(100, error("%s():%d: tx inputs missing/spent",__func__, __LINE__),
                                     CValidationState::Code::INVALID, "bad-txns-inputs-missingorspent");

            CValidationState::Code ret_code = view.IsScTxApplicableToState(tx, Sidechain::ScFeeCheckFlag::MINIMUM_IN_A_RANGE);
            if (ret_code != CValidationState::Code::OK)
            {
                return state.DoS(100,
                    error("%s():%d - invalid tx[%s], ret_code[0x%x]",
                        __func__, __LINE__, tx.GetHash().ToString(), CValidationState::CodeToChar(ret_code)),
                    ret_code, "bad-sc-tx-not-applicable");
            }

            // Add the transaction proofs (if any) to the sidechain proof verifier.
            if (fScProofVerification == flagScProofVerification::ON)
            {
                scVerifier.LoadDataForCswVerification(view, tx);
            }

            // are the JoinSplit's requirements met?
            if (!view.HaveJoinSplitRequirements(tx))
                return state.DoS(100, error("%s():%d: JoinSplit requirements not met",__func__, __LINE__),
                                 CValidationState::Code::INVALID, "bad-txns-joinsplit-requirements-not-met");

            if ((fAddressIndex || fSpentIndex) && explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
            {
                for (size_t j = 0; j < tx.GetVin().size(); j++)
                {
                    const CTxIn input = tx.GetVin()[j];
                    const CTxOut &prevout = view.GetOutputFor(tx.GetVin()[j]);
                    CScript::ScriptType scriptType = prevout.scriptPubKey.GetType();
                    const uint160 addrHash = prevout.scriptPubKey.AddressHash();
                    const AddressType addressType = fromScriptTypeToAddressType(scriptType);

                    if (fAddressIndex && scriptType != CScript::ScriptType::UNKNOWN)
                    {
                        // record spending activity
                        addressIndex.push_back(make_pair(
                            CAddressIndexKey(addressType, addrHash, pindex->nHeight, txIdx, tx.GetHash(), j, true),
                            CAddressIndexValue(prevout.nValue * -1, 0)));

                        // remove address from unspent index
                        addressUnspentIndex.push_back(make_pair(
                            CAddressUnspentKey(addressType, addrHash, input.prevout.hash, input.prevout.n),
                            CAddressUnspentValue()));
                    }

                    if (fSpentIndex)
                    {
                        // Add the spent index to determine the txid and input that spent an output
                        // and to find the amount and address from an input.
                        // If we do not recognize the script type, we still add an entry to the
                        // spentindex db, with a script type of 0 and addrhash of all zeroes.
                        spentIndex.push_back(make_pair(
                            CSpentIndexKey(input.prevout.hash, input.prevout.n),
                            CSpentIndexValue(tx.GetHash(), j, pindex->nHeight, prevout.nValue, addressType, addrHash)));
                    }
                }
            }

            // Add in sigops done by pay-to-script-hash inputs;
            // this is to prevent a "rogue miner" from creating
            // an incredibly-expensive-to-validate block.
            nSigOps += GetP2SHSigOpCount(tx, view);
            if (nSigOps > MAX_BLOCK_SIGOPS)
                return state.DoS(100, error("%s():%d: too many sigops",__func__, __LINE__),
                                 CValidationState::Code::INVALID, "bad-blk-sigops");

            nFees += tx.GetFeeAmount(view.GetValueIn(tx));

            std::vector<CScriptCheck> vChecks;
            if (!ContextualCheckTxInputs(tx, state, view, fExpensiveChecks, chain, flags, false, chainparams.GetConsensus(), nScriptCheckThreads ? &vChecks : NULL))
                return false;

            control.Add(vChecks);
        }

        if (fAddressIndex && explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
        {
            for (unsigned int k = 0; k < tx.GetVout().size(); k++)
            {
                const CTxOut &out = tx.GetVout()[k];
                CScript::ScriptType scriptType = out.scriptPubKey.GetType();

                if (scriptType != CScript::ScriptType::UNKNOWN)
                {
                    const uint160 addrHash = out.scriptPubKey.AddressHash();
                    const AddressType addressType = fromScriptTypeToAddressType(scriptType);

                    // record receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(addressType, addrHash, pindex->nHeight, txIdx, tx.GetHash(), k, false),
                                                     CAddressIndexValue(out.nValue, 0)));

                    // record unspent output
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(addressType, addrHash, tx.GetHash(), k),
                                                            CAddressUnspentValue(out.nValue, out.scriptPubKey, pindex->nHeight, 0)));
                }
            }
        }

        CTxUndo undoDummy;
        if (txIdx > 0) {
            blockundo.vtxundo.push_back(CTxUndo());
        }
        UpdateCoins(tx, view, txIdx == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);

        if (txIdx > 0)
        {
            if (!view.UpdateSidechain(tx, block, pindex->nHeight) )
            {
                return state.DoS(100, error("%s():%d: could not add sidechain in view: tx[%s]",
                                            __func__, __LINE__, tx.GetHash().ToString()),
                                 CValidationState::Code::INVALID, "bad-sc-tx");
            }

            for (const CTxCeasedSidechainWithdrawalInput& cswIn:tx.GetVcswCcIn()) {
                if (!view.AddCswNullifier(cswIn.scId, cswIn.nullifier)) {
                    return state.DoS(100, error("ConnectBlock(): try to use existed nullifier Tx [%s]", tx.GetHash().ToString()),
                             CValidationState::Code::INVALID, "bad-txns-csw-input-nullifier");
                }
            }
        }

        BOOST_FOREACH(const JSDescription &joinsplit, tx.GetVjoinsplit()) {
            BOOST_FOREACH(const uint256 &note_commitment, joinsplit.commitments) {
                // Insert the note commitments into our temporary tree.
                tree.append(note_commitment);
            }
        }

        vTxIndexValues.push_back(std::make_pair(tx.GetHash(), CTxIndexValue(pos, txIdx, 0)));
        pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);

        if (fScRelatedChecks == flagScRelatedChecks::ON) {
            bool retBuilder = scCommitmentBuilder.add(tx);
            if (!retBuilder && ForkManager::getInstance().isNonCeasingSidechainActive(pindex->nHeight))
                return state.DoS(100, error("%s():%d: cannot add tx to scTxsCommitmentBuilder", __func__, __LINE__),
                    CValidationState::Code::INVALID, "bad-blk-tx-commitbuild");
        }
    }  //end of Processing transactions loop


    std::map<uint256, uint256> highQualityCertData = HighQualityCertData(block, view);
    // key: current block top quality cert for given sc --> value: prev block superseeded cert hash (possibly null)

    for (unsigned int certIdx = 0; certIdx < block.vcert.size(); certIdx++) // Processing certificates loop
    {
        const CScCertificate &cert = block.vcert[certIdx];
        nInputs += cert.GetVin().size();
        nSigOps += GetLegacySigOpCount(cert);
        if (nSigOps > MAX_BLOCK_SIGOPS)
            return state.DoS(100, error("%s():%d: too many sigops",__func__, __LINE__),
                             CValidationState::Code::INVALID, "bad-blk-sigops");

        if (!view.HaveInputs(cert))
            return state.DoS(100, error("%s():%d: certificate inputs missing/spent",__func__, __LINE__),
                                 CValidationState::Code::INVALID, "bad-cert-inputs-missingorspent");

        // Update the explorer indexes with the inputs
        if ((fAddressIndex || fSpentIndex) && explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
        {
            for (size_t j = 0; j < cert.GetVin().size(); j++)
            {
                const CTxIn input = cert.GetVin()[j];
                const CTxOut &prevout = view.GetOutputFor(cert.GetVin()[j]);
                CScript::ScriptType scriptType = prevout.scriptPubKey.GetType();
                const uint160 addrHash = prevout.scriptPubKey.AddressHash();
                const AddressType addressType = fromScriptTypeToAddressType(scriptType);

                if (fAddressIndex && scriptType != CScript::ScriptType::UNKNOWN)
                {
                    // record spending activity
                    addressIndex.push_back(make_pair(
                        CAddressIndexKey(addressType, addrHash, pindex->nHeight, certIdx, cert.GetHash(), j, true),
                        CAddressIndexValue(prevout.nValue * -1, 0)));

                    // remove address from unspent index
                    addressUnspentIndex.push_back(make_pair(
                        CAddressUnspentKey(addressType, addrHash, input.prevout.hash, input.prevout.n),
                        CAddressUnspentValue()));
                }

                if (fSpentIndex)
                {
                    // Add the spent index to determine the txid and input that spent an output
                    // and to find the amount and address from an input.
                    // If we do not recognize the script type, we still add an entry to the
                    // spentindex db, with a script type of 0 and addrhash of all zeroes.
                    spentIndex.push_back(make_pair(
                        CSpentIndexKey(input.prevout.hash, input.prevout.n),
                        CSpentIndexValue(cert.GetHash(), j, pindex->nHeight, prevout.nValue, addressType, addrHash)));
                }
            }
        }

        // Add in sigops done by pay-to-script-hash inputs;
        // this is to prevent a "rogue miner" from creating
        // an incredibly-expensive-to-validate block.
        nSigOps += GetP2SHSigOpCount(cert, view);
        if (nSigOps > MAX_BLOCK_SIGOPS)
            return state.DoS(100, error("%s():%d: too many sigops",__func__, __LINE__),
                             CValidationState::Code::INVALID, "bad-blk-sigops");

        nFees += cert.GetFeeAmount(view.GetValueIn(cert));

        std::vector<CScriptCheck> vChecks;
        if (!ContextualCheckCertInputs(cert, state, view, fExpensiveChecks, chain, flags, false, chainparams.GetConsensus(), nScriptCheckThreads ? &vChecks : NULL))
            return false;

        control.Add(vChecks);

        CValidationState::Code ret_code = view.IsCertApplicableToState(cert);
        if (ret_code != CValidationState::Code::OK)
        {
            return state.DoS(100, error("%s():%d: invalid sc certificate [%s], ret_code[0x%x]",
                __func__, __LINE__, cert.GetHash().ToString(), CValidationState::CodeToChar(ret_code)),
                ret_code, "bad-sc-cert-not-applicable");
        }

        if (fScProofVerification == flagScProofVerification::ON)
        {
            scVerifier.LoadDataForCertVerification(view, cert);
        }

        // Update the explorer indexes with the "normal" outputs
        if (fAddressIndex && explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
        {
            for (unsigned int k = 0; k < cert.nFirstBwtPos; k++)
            {
                const CTxOut &out = cert.GetVout()[k];
                CScript::ScriptType scriptType = out.scriptPubKey.GetType();

                if (scriptType != CScript::ScriptType::UNKNOWN)
                {
                    const uint160 addrHash = out.scriptPubKey.AddressHash();
                    const AddressType addressType = fromScriptTypeToAddressType(scriptType);

                    // record receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(addressType, addrHash, pindex->nHeight, certIdx, cert.GetHash(), k, false),
                                                     CAddressIndexValue(out.nValue, 0)));

                    // record unspent output
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(addressType, addrHash, cert.GetHash(), k),
                                                            CAddressUnspentValue(out.nValue, out.scriptPubKey, pindex->nHeight, 0)));
                }
            }
        }

        CSidechain sidechain;
        assert(view.GetSidechain(cert.GetScId(), sidechain));

        blockundo.vtxundo.push_back(CTxUndo());
        bool isBlockTopQualityCert = highQualityCertData.count(cert.GetHash()) != 0;
        if (sidechain.isNonCeasing())   // For non-ceasing SC cert should always be top quality
        {
            assert(isBlockTopQualityCert);
        }
        UpdateCoins(cert, view, blockundo.vtxundo.back(), pindex->nHeight, isBlockTopQualityCert);
        
        int certMaturityHeight = sidechain.GetCertMaturityHeight(cert.epochNumber, pindex->nHeight);

        if (!isBlockTopQualityCert) {
            certMaturityHeight *= -1;   // A negative maturity height indicates that the certificate is superseded
        }

        if (isBlockTopQualityCert)
        {
            //Add the new certificate in the MaturityHeight collection
            if (fMaturityHeightIndex && explorerIndexesWrite == flagLevelDBIndexesWrite::ON) {
                const CMaturityHeightKey maturityHeightKey = CMaturityHeightKey(certMaturityHeight, cert.GetHash());
                maturityHeightValues.push_back(std::make_pair(maturityHeightKey, CMaturityHeightValue(static_cast<char>(1))));
            }

            if (!view.UpdateSidechain(cert, blockundo, pindex->nHeight))
            {
                return state.DoS(100, error("%s():%d: could not add in scView: cert[%s]",__func__, __LINE__, cert.GetHash().ToString()),
                                 CValidationState::Code::INVALID, "bad-sc-cert-not-updated");
            }

            const uint256& prevBlockTopQualityCertHash = highQualityCertData.at(cert.GetHash());
            if (!prevBlockTopQualityCertHash.IsNull())
            {
                // prevBlockTopQualityCertHash should always be null in v2 non-ceasing sc
                assert(!sidechain.isNonCeasing());

                // if prevBlockTopQualityCertHash is not null, it has same scId/epochNumber as cert
                if (explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
                {
                    if (fTxIndex)
                    {
                        // Update the prevBlockTopQualityCert maturity inside the txIndex DB to appear as superseded
                        CTxIndexValue txIndexVal;
                        assert(pblocktree->ReadTxIndex(prevBlockTopQualityCertHash, txIndexVal));
                        txIndexVal.maturityHeight *= -1;
                        vTxIndexValues.push_back(std::make_pair(prevBlockTopQualityCertHash, txIndexVal));

                        // Set any lower quality BT as superseded on the explorer indexes
                        if (fAddressIndex)
                        {
                            // Set the lower quality BTs as superseded
                            view.UpdateBackwardTransferIndexes(prevBlockTopQualityCertHash, txIndexVal.txIndex, addressIndex, addressUnspentIndex,
                                                               CCoinsViewCache::flagIndexesUpdateType::SUPERSEDE_CERTIFICATE);
                        }

                    }
                    if (fMaturityHeightIndex) {
                        //Remove the superseded certificate from the MaturityHeight DB
                        const CMaturityHeightKey maturityHeightKey = CMaturityHeightKey(certMaturityHeight, prevBlockTopQualityCertHash);
                        maturityHeightValues.push_back(std::make_pair(maturityHeightKey, CMaturityHeightValue()));
                    }
                }

                view.NullifyBackwardTransfers(prevBlockTopQualityCertHash, blockundo.scUndoDatabyScId.at(cert.GetScId()).lowQualityBwts);
                blockundo.scUndoDatabyScId.at(cert.GetScId()).contentBitMask |= CSidechainUndoData::AvailableSections::SUPERSEDED_CERT_DATA;
                if (pCertsStateInfo != nullptr)
                    pCertsStateInfo->push_back(CScCertificateStatusUpdateInfo(cert.GetScId(), prevBlockTopQualityCertHash,
                                            cert.epochNumber,
                                            blockundo.scUndoDatabyScId.at(cert.GetScId()).prevTopCommittedCertQuality,
                                            CScCertificateStatusUpdateInfo::BwtState::BWT_OFF));
            }

            if (pCertsStateInfo != nullptr)
                pCertsStateInfo->push_back(CScCertificateStatusUpdateInfo(cert.GetScId(), cert.GetHash(),
                                        cert.epochNumber, cert.quality, CScCertificateStatusUpdateInfo::BwtState::BWT_ON));
        } else {
            if (pCertsStateInfo != nullptr)
                pCertsStateInfo->push_back(CScCertificateStatusUpdateInfo(cert.GetScId(), cert.GetHash(),
                                        cert.epochNumber, cert.quality, CScCertificateStatusUpdateInfo::BwtState::BWT_OFF));
        }

        if (certIdx == 0) {
            // we are processing the first certificate, add the size of the vcert to the offset
            int sz = GetSizeOfCompactSize(block.vcert.size());
            LogPrint("cert", "%s():%d - adding %d to nTxOffset\n", __func__, __LINE__, sz );
            pos.nTxOffset += sz;
            LogPrint("cert", "%s():%d - nTxOffset=%d\n", __func__, __LINE__, pos.nTxOffset );
        }

        vTxIndexValues.push_back(std::make_pair(cert.GetHash(), CTxIndexValue(pos, certIdx, certMaturityHeight)));
        pos.nTxOffset += cert.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);

        if (fScRelatedChecks == flagScRelatedChecks::ON)
        {
            bool retBuilder = scCommitmentBuilder.add(cert, view);
            if (!retBuilder && ForkManager::getInstance().isNonCeasingSidechainActive(pindex->nHeight))
                return state.DoS(100, error("%s():%d: cannot add cert to scTxsCommitmentBuilder", __func__, __LINE__),
                    CValidationState::Code::INVALID, "bad-blk-cert-commitbuild");
        }

        // Update the explorer indexes according to the Backward Transfer outputs
        if (fAddressIndex && explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
        {
            for (unsigned int k = cert.nFirstBwtPos; k < cert.GetVout().size(); k++)
            {
                const CTxOut &out = cert.GetVout()[k];
                CScript::ScriptType scriptType = out.scriptPubKey.GetType();

                if (scriptType != CScript::ScriptType::UNKNOWN)
                {
                    const uint160 addrHash = out.scriptPubKey.AddressHash();
                    const AddressType addressType = fromScriptTypeToAddressType(scriptType);

                    // record receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(addressType, addrHash, pindex->nHeight, certIdx, cert.GetHash(), k, false),
                                                     CAddressIndexValue(out.nValue, certMaturityHeight)));

                    // record unspent output
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(addressType, addrHash, cert.GetHash(), k),
                                                            CAddressUnspentValue(out.nValue, out.scriptPubKey, pindex->nHeight, certMaturityHeight)));
                }
            }
        }

        LogPrint("cert", "%s():%d - nTxOffset=%d\n", __func__, __LINE__, pos.nTxOffset );
    } //end of Processing certificates loop

    if (explorerIndexesWrite == flagLevelDBIndexesWrite::ON)
    {

        if (fAddressIndex)
        {
            view.HandleIndexesSidechainEvents(pindex->nHeight, pblocktree, addressIndex, addressUnspentIndex);
        }

        if (fMaturityHeightIndex)
        {
            //Remove the certificates from the MaturityHeight DB related to the ceased sidechains
            view.HandleMaturityHeightIndexSidechainEvents(pindex->nHeight, pblocktree, maturityHeightValues);
        }
        if (fTxIndex)
        {
            view.HandleTxIndexSidechainEvents(pindex->nHeight, pblocktree, vTxIndexValues);
        }
    }

    if (!view.HandleSidechainEvents(pindex->nHeight, blockundo, pCertsStateInfo))
    {
        return state.DoS(100, error("%s():%d - SIDECHAIN-EVENT: could not handle scheduled event",__func__, __LINE__),
                                 CValidationState::Code::INVALID, "bad-sc-events-handling");
    }

    view.PushAnchor(tree);

    if (processingType == flagBlockProcessingType::COMPLETE)
    {
        pindex->hashAnchorEnd = tree.root();
    }

    blockundo.old_tree_root = old_tree_root;

    int64_t nTime1 = GetTimeMicros();

    int64_t deltaConnectTime = nTime1 - nTimeStart;
    nTimeConnect += deltaConnectTime;

    LogPrint("bench", "      - Connect %u txes, %u certs: %.2fms (%.3fms/(tx+cert), %.3fms/(tx+cert inputs)) [%.2fs]\n",
        (unsigned)block.vtx.size(), (unsigned)block.vcert.size(),
         0.001 * deltaConnectTime, 0.001 * deltaConnectTime / (block.vtx.size() + block.vcert.size()),
         nInputs <= 1 ? 0 : 0.001 * deltaConnectTime / (nInputs-1), nTimeConnect * 0.000001);

    CAmount blockReward = nFees + GetBlockSubsidy(pindex->nHeight, chainparams.GetConsensus());
    if (block.vtx[0].GetValueOut() > blockReward)
        return state.DoS(100,
                         error("%s():%d: coinbase pays too much (actual=%d vs limit=%d)",
                                 __func__, __LINE__, block.vtx[0].GetValueOut(), blockReward),
                        CValidationState::Code::INVALID, "bad-cb-amount");

    if (!control.Wait())
        return state.DoS(100, false);

    int64_t nTime2 = GetTimeMicros();
    int64_t deltaVerifyTime = nTime2 - nTimeStart;

    nTimeVerify += deltaVerifyTime;
    LogPrint("bench", "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs] (nScriptCheckThreads=%d)\n", nInputs - 1, 0.001 * deltaVerifyTime, nInputs <= 1 ? 0 : 0.001 * deltaVerifyTime / (nInputs-1), nTimeVerify * 0.000001, nScriptCheckThreads);

    if (fScRelatedChecks == flagScRelatedChecks::ON)
    {
        int64_t nCommTreeStartTime = GetTimeMicros();
        const uint256& scTxsCommitment = scCommitmentBuilder.getCommitment();
        int64_t deltaCommTreeTime = GetTimeMicros() - nCommTreeStartTime;
        LogPrint("bench", "    - txsCommTree: %.2fms\n", deltaCommTreeTime * 0.001);

        if (block.hashScTxsCommitment != scTxsCommitment)
        {
            // If this check fails, we return validation state obj with a state.corruptionPossible=false attribute,
            // which will mark this header as failed. This is because the previous check on merkel root was successful,
            // that means sc txes/cert are verified, and yet their contribution to scTxsCommitment is not
            return state.DoS(100, error("%s():%d: SCTxsCommitment verification failed; block[%s] vs computed[%s]",__func__, __LINE__,
                                        block.hashScTxsCommitment.ToString(), scTxsCommitment.ToString()),
                                        CValidationState::Code::INVALID, "bad-sc-txs-commitment");
        }
        LogPrint("cert", "%s():%d - Successfully verified SCTxsCommitment %s\n",
            __func__, __LINE__, block.hashScTxsCommitment.ToString());
    }

    if (fScProofVerification == flagScProofVerification::ON)
    {
        LogPrint("sc", "%s():%d - calling scVerifier.BatchVerify()\n", __func__, __LINE__);
        int64_t nBatchVerifyStartTime = GetTimeMicros();
        if (!scVerifier.BatchVerify())
        {
            return state.DoS(100, error("%s():%d - ERROR: sc-related batch proof verification failed", __func__, __LINE__),
                            CValidationState::Code::INVALID_PROOF, "bad-sc-proof");
        }
        int64_t deltaBatchVerifyTime = GetTimeMicros() - nBatchVerifyStartTime;
        LogPrint("bench", "    - scBatchVerify: %.2fms\n", deltaBatchVerifyTime * 0.001);
    }

    int64_t nTime2b = GetTimeMicros();

    if (processingType == flagBlockProcessingType::CHECK_ONLY)
        return true;

    LogPrint("sc", "%s():%d Writing CBlockUndo into DB:\n%s\n",
        __func__, __LINE__, blockundo.ToString());

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS))
    {
        if (pindex->GetUndoPos().IsNull()) {
            CDiskBlockPos pos;
            if (!FindUndoPos(state, pindex->nFile, pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                return error("%s():%d: FindUndoPos failed",__func__, __LINE__);
            if (!UndoWriteToDisk(blockundo, pos, pindex->pprev->GetBlockHash(), chainparams.MessageStart()))
                return AbortNode(state, "Failed to write undo data");

            LogPrint("sc", "%s():%d - undo info written on disk\n", __func__, __LINE__);
            // update nUndoPos in block index
            pindex->nUndoPos = pos.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        setDirtyBlockIndex.insert(pindex);
    }

    if (explorerIndexesWrite == flagLevelDBIndexesWrite::ON) {
        if (fTxIndex) {
            if (!pblocktree->WriteTxIndex(vTxIndexValues))
                return AbortNode(state, "Failed to write transaction index");
        }
        if (fMaturityHeightIndex) {
            if (!pblocktree->UpdateMaturityHeightIndex(maturityHeightValues))
                return AbortNode(state, "Failed to write maturity height index");
        }

        if (fAddressIndex) {
            if (!pblocktree->WriteAddressIndex(addressIndex)) {
                return AbortNode(state, "Failed to write address index");
            }

            if (!pblocktree->UpdateAddressUnspentIndex(addressUnspentIndex))
            {
                return AbortNode(state, "Failed to update address unspent index");
            }
        }

        if (fSpentIndex)
        {
            if (!pblocktree->UpdateSpentIndex(spentIndex))
            {
                return AbortNode(state, "Failed to update address spent index");
            }
        }

        if (fTimestampIndex) {
            unsigned int logicalTS = pindex->nTime;
            unsigned int prevLogicalTS = 0;

            // retrieve logical timestamp of the previous block
            if (pindex->pprev)
                if (!pblocktree->ReadTimestampBlockIndex(pindex->pprev->GetBlockHash(), prevLogicalTS))
                    LogPrintf("%s: Failed to read previous block's logical timestamp\n", __func__);

            if (logicalTS <= prevLogicalTS) {
                logicalTS = prevLogicalTS + 1;
                LogPrintf("%s: Previous logical timestamp is newer Actual[%d] prevLogical[%d] Logical[%d]\n", __func__, pindex->nTime, prevLogicalTS, logicalTS);
            }

            if (!pblocktree->WriteTimestampIndex(CTimestampIndexKey(logicalTS, pindex->GetBlockHash())))
                return AbortNode(state, "Failed to write timestamp index");

            if (!pblocktree->WriteTimestampBlockIndex(CTimestampBlockIndexKey(pindex->GetBlockHash()), CTimestampBlockIndexValue(logicalTS)))
                return AbortNode(state, "Failed to write blockhash index");
        }
    }

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime3 = GetTimeMicros(); nTimeIndex += nTime3 - nTime2b;
    LogPrint("bench", "    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime3 - nTime2b), nTimeIndex * 0.000001);

    // Watch for changes to the previous coinbase transaction.
    static uint256 hashPrevBestCoinBase;
    GetMainSignals().UpdatedTransaction(hashPrevBestCoinBase);
    hashPrevBestCoinBase = block.vtx[0].GetHash();

    int64_t nTime4 = GetTimeMicros(); nTimeCallbacks += nTime4 - nTime3;
    LogPrint("bench", "    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime4 - nTime3), nTimeCallbacks * 0.000001);

    return true;
}

enum FlushStateMode {
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed depending on the mode we're called with
 * if they're too large, if it's been a while since the last write,
 * or always and in all cases if we're in prune mode and are deleting files.
 */
bool static FlushStateToDisk(CValidationState &state, FlushStateMode mode) {
    LogPrint("sc", "%s():%d - called\n", __func__, __LINE__);
    LOCK2(cs_main, cs_LastBlockFile);
    static int64_t nLastWrite = 0;
    static int64_t nLastFlush = 0;
    static int64_t nLastSetChain = 0;
    std::set<int> setFilesToPrune;
    bool fFlushForPrune = false;
    try {
    if (fPruneMode && fCheckForPruning && !fReindex && !fReindexFast)
    {
        FindFilesToPrune(setFilesToPrune);
        fCheckForPruning = false;
        if (!setFilesToPrune.empty()) {
            fFlushForPrune = true;
            if (!fHavePruned) {
                pblocktree->WriteFlag("prunedblockfiles", true);
                fHavePruned = true;
            }
        }
    }
    int64_t nNow = GetTimeMicros();
    // Avoid writing/flushing immediately after startup.
    if (nLastWrite == 0) {
        nLastWrite = nNow;
    }
    if (nLastFlush == 0) {
        nLastFlush = nNow;
    }
    if (nLastSetChain == 0) {
        nLastSetChain = nNow;
    }
    size_t cacheSize = pcoinsTip->DynamicMemoryUsage();
    // The cache is large and close to the limit, but we have time now (not in the middle of a block processing).
    bool fCacheLarge = mode == FLUSH_STATE_PERIODIC && cacheSize * (10.0/9) > nCoinCacheUsage;
    // The cache is over the limit, we have to write now.
    bool fCacheCritical = mode == FLUSH_STATE_IF_NEEDED && cacheSize > nCoinCacheUsage;
    // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload after a crash.
    bool fPeriodicWrite = mode == FLUSH_STATE_PERIODIC && nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
    // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
    bool fPeriodicFlush = mode == FLUSH_STATE_PERIODIC && nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
    // Combine all conditions that result in a full cache flush.
    bool fDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || fCacheLarge || fCacheCritical || fPeriodicFlush || fFlushForPrune;
    // Write blocks and block index to disk.
    if (fDoFullFlush || fPeriodicWrite) {
        // Depend on nMinDiskSpace to ensure we can write block index
        if (!CheckDiskSpace(0))
            return state.Error("out of disk space");
        // First make sure all block and undo data is flushed to disk.
        FlushBlockFile();
        // Then update all block file information (which may refer to block and undo files).
        {
            std::vector<std::pair<int, const CBlockFileInfo*> > vFiles;
            vFiles.reserve(setDirtyFileInfo.size());
            for (set<int>::iterator it = setDirtyFileInfo.begin(); it != setDirtyFileInfo.end(); ) {
                vFiles.push_back(make_pair(*it, &vinfoBlockFile[*it]));
                setDirtyFileInfo.erase(it++);
            }
            std::vector<const CBlockIndex*> vBlocks;
            vBlocks.reserve(setDirtyBlockIndex.size());
            for (set<CBlockIndex*>::iterator it = setDirtyBlockIndex.begin(); it != setDirtyBlockIndex.end(); ) {
                vBlocks.push_back(*it);
                setDirtyBlockIndex.erase(it++);
            }
            if (!pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks)) {
                return AbortNode(state, "Files to write to block index database");
            }
        }
        // Finally remove any pruned files
        if (fFlushForPrune)
            UnlinkPrunedFiles(setFilesToPrune);
        nLastWrite = nNow;
    }
    // Flush best chain related state. This can only be done if the blocks / block index write was also done.
    if (fDoFullFlush) {
        // Typical CCoins structures on disk are around 128 bytes in size.
        // Pushing a new one to the database can cause it to be written
        // twice (once in the log, and once in the tables). This is already
        // an overestimation, as most will delete an existing entry or
        // overwrite one. Still, use a conservative safety factor of 2.
        if (!CheckDiskSpace(128 * 2 * 2 * pcoinsTip->GetCacheSize()))
            return state.Error("out of disk space");
        // Flush the chainstate (which may refer to block index entries).
        if (!pcoinsTip->Flush())
            return AbortNode(state, "Failed to write to coin database");
        nLastFlush = nNow;
    }
    if ((mode == FLUSH_STATE_ALWAYS || mode == FLUSH_STATE_PERIODIC) && nNow > nLastSetChain + (int64_t)DATABASE_WRITE_INTERVAL * 1000000) {
        // Update best block in wallet (so we can detect restored wallets).
        GetMainSignals().SetBestChain(chainActive.GetLocator());
        nLastSetChain = nNow;
    }
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error while flushing: ") + e.what());
    }
    return true;
}

void FlushStateToDisk() {
    CValidationState state;
    FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
}

void PruneAndFlush() {
    CValidationState state;
    fCheckForPruning = true;
    FlushStateToDisk(state, FLUSH_STATE_NONE);
}

/** Update chainActive and related internal data structures. */
void static UpdateTip(CBlockIndex *pindexNew) {
    const CChainParams& chainParams = Params();
    chainActive.SetTip(pindexNew);

    // New best block
    nTimeBestReceived = GetTime();
    mempool.AddTransactionsUpdated(1);

    double syncProgress = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip());
    if(fIsStartupSyncing && std::abs(1.0 - syncProgress) < 0.000001) {
        LogPrintf("Fully synchronized at block height %d\n", chainActive.Height());
        fIsStartupSyncing = false;
    }

    LogPrintf("%s: new best=%s  height=%d  log2_work=%.8g  tx=%lu  date=%s progress=%f  cache=%.1fMiB(%utx)\n", __func__,
      chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(), log(chainActive.Tip()->nChainWork.getdouble())/log(2.0), (unsigned long)chainActive.Tip()->nChainTx,
      DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
      syncProgress, pcoinsTip->DynamicMemoryUsage() * (1.0 / (1<<20)), pcoinsTip->GetCacheSize());

    cvBlockChange.notify_all();
}

/** Disconnect chainActive's tip. */
bool static DisconnectTip(CValidationState &state) {
    CBlockIndex *pindexDelete = chainActive.Tip();
    assert(pindexDelete);
    mempool.check(pcoinsTip);
    // Read block from disk.
    CBlock block;
    if (!ReadBlockFromDisk(block, pindexDelete))
        return AbortNode(state, "Failed to read block");
    // Apply the block atomically to the chain state.
    uint256 anchorBeforeDisconnect = pcoinsTip->GetBestAnchor();
    int64_t nStart = GetTimeMicros();
    std::vector<CScCertificateStatusUpdateInfo> certsStateInfo;
    {
        CCoinsViewCache view(pcoinsTip);
        if (!DisconnectBlock(block, state, pindexDelete, view, flagLevelDBIndexesWrite::ON, nullptr, &certsStateInfo))
            return error("DisconnectTip(): DisconnectBlock %s failed", pindexDelete->GetBlockHash().ToString());
        assert(view.Flush());
    }
    LogPrint("bench", "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);

    std::list<CTransaction> dummyTxs;
    std::list<CScCertificate> dummyCerts;

    size_t erased = mapCumtreeHeight.erase(pindexDelete->scCumTreeHash.GetLegacyHash());
    if (erased) {
        LogPrint("sc", "- Removed %zu entries from mapCumtreeHeight\n", erased);
        mempool.removeCertificatesWithoutRef(pcoinsTip, dummyCerts);
    }
    dummyTxs.clear();
    dummyCerts.clear();

    uint256 anchorAfterDisconnect = pcoinsTip->GetBestAnchor();
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
        return false;

    // Resurrect mempool transactions and certificates from the disconnected block.
    for(const CTransaction &tx: block.vtx) {
        // ignore validation errors in resurrected transactions
        CValidationState stateDummy;
        if (tx.IsScVersion()) {
            LogPrint("sc", "%s():%d - resurrecting tx [%s] to mempool\n", __func__, __LINE__, tx.GetHash().ToString());
        }

        if (tx.IsCoinBase() ||
            MempoolReturnValue::VALID != AcceptTxToMemoryPool(mempool, stateDummy, tx,
                    LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF, MempoolProofVerificationFlag::DISABLED))
        {
            LogPrint("sc", "%s():%d - removing tx [%s] from mempool\n[%s]\n",
                __func__, __LINE__, tx.GetHash().ToString(), tx.ToString());
            mempool.remove(tx, dummyTxs, dummyCerts, true);
        }
    }

    dummyTxs.clear();
    dummyCerts.clear();
    for (const CScCertificate& cert : block.vcert) {
        // ignore validation errors in resurrected certificates
        LogPrint("sc", "%s():%d - resurrecting certificate [%s] to mempool\n", __func__, __LINE__, cert.GetHash().ToString());
        CValidationState stateDummy;
        if (MempoolReturnValue::VALID != AcceptCertificateToMemoryPool(mempool, stateDummy, cert,
                LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF, MempoolProofVerificationFlag::DISABLED, nullptr))
        {
            LogPrint("sc", "%s():%d - removing certificate [%s] from mempool\n[%s]\n",
                __func__, __LINE__, cert.GetHash().ToString(), cert.ToString());

            mempool.remove(cert, dummyTxs, dummyCerts, true);
        }
    }

    if (anchorBeforeDisconnect != anchorAfterDisconnect) {
        // The anchor may not change between block disconnects,
        // in which case we don't want to evict from the mempool yet!
        mempool.removeWithAnchor(anchorBeforeDisconnect);
    }

    mempool.removeStaleTransactions(pcoinsTip, dummyTxs, dummyCerts);
    mempool.removeStaleCertificates(pcoinsTip, dummyCerts);

    mempool.check(pcoinsTip);
    // Update chainActive and related variables.
    UpdateTip(pindexDelete->pprev);
    // Get the current commitment tree
    ZCIncrementalMerkleTree newTree;
    assert(pcoinsTip->GetAnchorAt(pcoinsTip->GetBestAnchor(), newTree));

    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    for(const CTransaction &tx: block.vtx) {
        SyncWithWallets(tx, nullptr);
    }

    for(const CScCertificate &cert: block.vcert) {
        LogPrint("cert", "%s():%d - sync with wallet from block to unconfirmed cert[%s]\n", __func__, __LINE__, cert.GetHash().ToString());
        SyncWithWallets(cert, nullptr);
    }

    for(const auto& item : certsStateInfo) {
        LogPrint("cert", "%s():%d - updating cert state in wallet:\n[%s]\n", __func__, __LINE__, item.ToString());
        SyncCertStatusUpdate(item);
    }

    // Update cached incremental witnesses
    GetMainSignals().ChainTip(pindexDelete, &block, newTree, false);
    return true;
}

static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

/**
 * Connect a new block to chainActive. pblock is either NULL or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 */
bool static ConnectTip(CValidationState &state, CBlockIndex *pindexNew, CBlock *pblock) {
    assert(pindexNew->pprev == chainActive.Tip());
    mempool.check(pcoinsTip);
    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    CBlock block;
    if (!pblock) {
        if (!ReadBlockFromDisk(block, pindexNew))
            return AbortNode(state, "Failed to read block");
        pblock = &block;
    }
    // Get the current commitment tree
    ZCIncrementalMerkleTree oldTree;
    assert(pcoinsTip->GetAnchorAt(pcoinsTip->GetBestAnchor(), oldTree));
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros(); nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LogPrint("bench", "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    std::vector<CScCertificateStatusUpdateInfo> certsStateInfo;
    {
        CCoinsViewCache view(pcoinsTip);
        bool rv = ConnectBlock(*pblock, state, pindexNew, view, chainActive, flagBlockProcessingType::COMPLETE,
                               flagScRelatedChecks::ON, flagScProofVerification::ON, flagLevelDBIndexesWrite::ON, &certsStateInfo);
        GetMainSignals().BlockChecked(*pblock, state);
        if (!rv) {
            if (state.IsInvalid())
                InvalidBlockFound(pindexNew, state);
            return error("ConnectTip(): ConnectBlock %s failed", pindexNew->GetBlockHash().ToString());
        }
        mapBlockSource.erase(pindexNew->GetBlockHash());
        nTime3 = GetTimeMicros(); nTimeConnectTotal += nTime3 - nTime2;
        LogPrint("bench", "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
        assert(view.Flush());
    }
    mapCumtreeHeight.insert(std::make_pair(pindexNew->scCumTreeHash.GetLegacyHash(), pindexNew->nHeight));
    int64_t nTime4 = GetTimeMicros(); nTimeFlush += nTime4 - nTime3;
    LogPrint("bench", "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
        return false;
    int64_t nTime5 = GetTimeMicros(); nTimeChainState += nTime5 - nTime4;
    LogPrint("bench", "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);

    // Remove conflicting transactions from the mempool.
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.removeForBlock(pblock->vtx, pindexNew->nHeight, removedTxs,  removedCerts, !IsInitialBlockDownload());
    mempool.removeForBlock(pblock->vcert, pindexNew->nHeight, removedTxs, removedCerts);

    mempool.removeStaleTransactions(pcoinsTip, removedTxs, removedCerts);
    mempool.removeStaleCertificates(pcoinsTip, removedCerts);

    mempool.check(pcoinsTip);

    UpdateTip(pindexNew); // Update chainActive & related variables.

    // Tell wallet about transactions and certificates that went from mempool to conflicted:
    for(const CTransaction &tx: removedTxs) {
        SyncWithWallets(tx, nullptr);
    }
    for(const CScCertificate &cert: removedCerts) {
        LogPrint("cert", "%s():%d - sync with wallet removed cert[%s]\n", __func__, __LINE__, cert.GetHash().ToString());
        SyncWithWallets(cert, nullptr);
    }

    // ... and about ones that got confirmed:
    for(const CTransaction &tx: pblock->vtx) {
        LogPrint("cert", "%s():%d - sync with wallet tx[%s]\n", __func__, __LINE__, tx.GetHash().ToString());
        SyncWithWallets(tx, pblock);
    }

    for(const CScCertificate &cert: pblock->vcert) {
        CSidechain sidechain;
        assert(pcoinsTip->GetSidechain(cert.GetScId(), sidechain));
        int bwtMaturityDepth = sidechain.GetCertMaturityHeight(cert.epochNumber, pindexNew->nHeight) - chainActive.Height();
        LogPrint("cert", "%s():%d - sync with wallet confirmed cert[%s], bwtMaturityDepth[%d]\n",
            __func__, __LINE__, cert.GetHash().ToString(), bwtMaturityDepth);
        SyncWithWallets(cert, pblock, bwtMaturityDepth);
    }

    for(const auto& item : certsStateInfo) {
        LogPrint("cert", "%s():%d - updating cert state in wallet:\n[%s]\n", __func__, __LINE__, item.ToString());
        SyncCertStatusUpdate(item);
    }

    // Update cached incremental witnesses
    GetMainSignals().ChainTip(pindexNew, pblock, oldTree, true);

    EnforceNodeDeprecation(pindexNew->nHeight);

    int64_t nTime6 = GetTimeMicros(); nTimePostConnect += nTime6 - nTime5; nTimeTotal += nTime6 - nTime1;
    LogPrint("bench", "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogPrint("bench", "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
    return true;
}

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
static CBlockIndex* FindMostWorkChain() {
    do {
        CBlockIndex *pindexNew = NULL;

        // Find the best candidate header.
        {
            std::set<CBlockIndex*, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return NULL;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex *pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chainActive.Contains(pindexTest)) {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK;
            bool fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (fFailedChain || fMissingData) {
                // Candidate chain is not usable (either invalid or missing data)
                if (fFailedChain && (pindexBestInvalid == NULL || pindexNew->nChainWork > pindexBestInvalid->nChainWork))
                    pindexBestInvalid = pindexNew;
                CBlockIndex *pindexFailed = pindexNew;
                // Remove the entire chain from the set.
                while (pindexTest != pindexFailed) {
                    if (fFailedChain) {
                        LogPrint("forks", "%s():%d - marking FAILED candidate idx [%s]\n", __func__, __LINE__,
                            pindexFailed->GetBlockHash().ToString());
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    } else if (fMissingData) {
                        // If we're missing data, then add back to mapBlocksUnlinked,
                        // so that if the block arrives in the future we can try adding
                        // to setBlockIndexCandidates again.
                        mapBlocksUnlinked.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    setBlockIndexCandidates.erase(pindexFailed);
                    pindexFailed = pindexFailed->pprev;
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while(true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
static void PruneBlockIndexCandidates() {
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex*, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chainActive.Tip())) {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either NULL or a pointer to a CBlock corresponding to pindexMostWork.
 */
static bool ActivateBestChainStep(CValidationState &state, CBlockIndex *pindexMostWork, CBlock *pblock, bool &postponeRelay) {

    AssertLockHeld(cs_main);
    bool fInvalidFound = false;
    postponeRelay = false;
    const CBlockIndex *pindexOldTip = chainActive.Tip();
    const CBlockIndex *pindexFork = chainActive.FindFork(pindexMostWork);

    // Disconnect active blocks which are no longer in the best chain.
    while (chainActive.Tip() && chainActive.Tip() != pindexFork)
    {
        if (!DisconnectTip(state))
        {
            return false;
        }
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex*> vpindexToConnect;
    bool fContinue = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;

    while (fContinue && nHeight != pindexMostWork->nHeight)
    {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);
        vpindexToConnect.clear();
        vpindexToConnect.reserve(nTargetHeight - nHeight);
        CBlockIndex *pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
        while (pindexIter && pindexIter->nHeight != nHeight) {
            vpindexToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
        nHeight = nTargetHeight;

        // Connect new blocks.
        BOOST_REVERSE_FOREACH(CBlockIndex *pindexConnect, vpindexToConnect) {
            if (!ConnectTip(state, pindexConnect, pindexConnect == pindexMostWork ? pblock : NULL)) {
                if (state.IsInvalid()) {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                        InvalidChainFound(vpindexToConnect.back());
                    state = CValidationState();
                    fInvalidFound = true;
                    postponeRelay = true;
                    fContinue = false;
                    break;
                } else {
                    // A system error occurred (disk space, database error, ...).
                    return false;
                }
            } else {
                PruneBlockIndexCandidates();
                if (!pindexOldTip || chainActive.Tip()->nChainWork > pindexOldTip->nChainWork) {
                    // We're in a better position than we were. Return temporarily to release the lock.
                    fContinue = false;
                    break;
                }
            }
        }
    }

    // Callbacks/notifications for a new best chain.
    if (fInvalidFound)
        CheckForkWarningConditionsOnNewFork(vpindexToConnect.back());
    else
        CheckForkWarningConditions();

    return true;
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either NULL or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool ActivateBestChain(CValidationState &state, CBlock *pblock, bool &postponeRelay) {
    CBlockIndex *pindexNewTip = NULL;
    CBlockIndex *pindexMostWork = NULL;
    const CChainParams& chainParams = Params();
    do {
        boost::this_thread::interruption_point();

        bool fInitialDownload;
        {
            LOCK(cs_main);
            pindexMostWork = FindMostWorkChain();

            // Whether we have anything to do at all.
            if (pindexMostWork == NULL || pindexMostWork == chainActive.Tip())
                return true;

            bool postponeRelayTmp = false;

            if (!ActivateBestChainStep(state, pindexMostWork, pblock && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : NULL, postponeRelayTmp))
                return false;

            postponeRelay |= postponeRelayTmp;

            pindexNewTip = chainActive.Tip();
            fInitialDownload = IsInitialBlockDownload();
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip).

        // Notifications/callbacks that can run without cs_main
        if (!fInitialDownload) {
            uint256 hashNewTip = pindexNewTip->GetBlockHash();
            // Relay inventory, but don't relay old inventory during initial block download.
            int nBlockEstimate = 0;
            if (fCheckpointsEnabled)
                nBlockEstimate = Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints());
            // Don't relay blocks if pruning -- could cause a peer to try to download, resulting
            // in a stalled download if the block file is pruned before the request.
            if (nLocalServices & NODE_NETWORK) {
                LOCK(cs_vNodes);
                BOOST_FOREACH(CNode* pnode, vNodes)
                {
                    if (chainActive.Height() > (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate))
                    {
                        pnode->PushInventory(CInv(MSG_BLOCK, hashNewTip));
                    }
                    else
                    {
                        LogPrint("forks", "%s():%d - Node [%s] (peer=%d) NOT pushing inv [%s] - hM[%d], hS[%d], nB[%d]\n",
                            __func__, __LINE__, pnode->addrName, pnode->GetId(), hashNewTip.ToString(),
                            chainActive.Height(), pnode->nStartingHeight, nBlockEstimate);
                    }
                }
            }
            else
            {
                LogPrint("forks", "%s():%d - NOT pushing inv [%s]\n", __func__, __LINE__,
                    hashNewTip.ToString());
            }

            // Notify external listeners about the new tip.
            GetMainSignals().UpdatedBlockTip(pindexNewTip);
            uiInterface.NotifyBlockTip(hashNewTip);
        }
        else
        {
            LogPrint("forks", "%s():%d - InitialDownload in progress: NOT pushing any inv\n", __func__, __LINE__);
        }

    } while(pindexMostWork != chainActive.Tip());
    CheckBlockIndex();

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC)) {
        return false;
    }

    return true;
}

bool InvalidateBlock(CValidationState& state, CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    // Mark the block itself as invalid.
    pindex->nStatus |= BLOCK_FAILED_VALID;
    setDirtyBlockIndex.insert(pindex);
    setBlockIndexCandidates.erase(pindex);

    while (chainActive.Contains(pindex)) {
        CBlockIndex *pindexWalk = chainActive.Tip();
        pindexWalk->nStatus |= BLOCK_FAILED_CHILD;
        setDirtyBlockIndex.insert(pindexWalk);
        setBlockIndexCandidates.erase(pindexWalk);
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state)) {
            return false;
        }
    }

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so
    // add it again.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && !setBlockIndexCandidates.value_comp()(it->second, chainActive.Tip())) {
            setBlockIndexCandidates.insert(it->second);
        }
        it++;
    }

    InvalidChainFound(pindex);
    return true;
}

bool ReconsiderBlock(CValidationState& state, CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (!it->second->IsValid() && it->second->GetAncestor(nHeight) == pindex) {
            it->second->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(it->second);
            if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && setBlockIndexCandidates.value_comp()(chainActive.Tip(), it->second)) {
                setBlockIndexCandidates.insert(it->second);
            }
            if (it->second == pindexBestInvalid) {
                // Reset invalid block marker if it was pointing to one of those.
                pindexBestInvalid = NULL;
            }
        }
        it++;
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != NULL) {
        if (pindex->nStatus & BLOCK_FAILED_MASK) {
            pindex->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(pindex);
        }
        pindex = pindex->pprev;
    }
    return true;
}

bool addToGlobalForkTips(const CBlockIndex* pindex)
{
    if (!pindex)
        return false;

    unsigned int erased = 0;
    if (pindex->pprev)
    {
        // remove its parent if any
        erased = mGlobalForkTips.erase(pindex->pprev);
    }

    if (erased == 0)
    {
        LogPrint("forks", "%s():%d - adding first fork tip in global map: h(%d) [%s]\n",
            __func__, __LINE__, pindex->nHeight, pindex->GetBlockHash().ToString());
    }

    return mGlobalForkTips.insert(std::make_pair( pindex, (int)GetTime() )).second;
}

bool updateGlobalForkTips(const CBlockIndex* pindex, bool lookForwardTips)
{
    if (!pindex)
        return false;

    LogPrint("forks", "%s():%d - Entering: lookFwd[%d], h(%d) [%s]\n",
        __func__, __LINE__, lookForwardTips, pindex->nHeight, pindex->GetBlockHash().ToString());

    if (chainActive.Contains(pindex))
    {
        LogPrint("forks", "%s():%d - Exiting: header is on main chain h(%d) [%s]\n",
            __func__, __LINE__, pindex->nHeight, pindex->GetBlockHash().ToString());
        return false;
    }

    if (mGlobalForkTips.count(pindex) )
    {
        LogPrint("forks", "%s():%d - updating tip in global set: h(%d) [%s]\n",
            __func__, __LINE__, pindex->nHeight, pindex->GetBlockHash().ToString());
        mGlobalForkTips[pindex] = (int)GetTime();
        return true;
    }
    else
    {
        // check from tips downward if we connect to this index and in this case
        // update the tip instead (for coping with very old tips not in the most recent set)
        if (lookForwardTips)
        {
            int h = pindex->nHeight;
            bool done = false;

            BOOST_FOREACH(auto mapPair, mGlobalForkTips)
            {
                const CBlockIndex* tipIndex = mapPair.first;
                if (!tipIndex)
                    continue;

                LogPrint("forks", "%s():%d - tip %s h(%d)\n",
                    __func__, __LINE__, tipIndex->GetBlockHash().ToString(), tipIndex->nHeight);

                if (tipIndex == chainActive.Tip() || tipIndex == pindexBestHeader )
                {
                    LogPrint("forks", "%s():%d - skipping main chain tip\n", __func__, __LINE__);
                    continue;
                }

                const CBlockIndex* dum = tipIndex;
                while ( dum != pindex && dum->nHeight >= h)
                {
                    dum = dum->pprev;
                }

                if (dum == pindex)
                {
                    LogPrint("forks", "%s():%d - updating tip access time in global set: h(%d) [%s]\n",
                        __func__, __LINE__, tipIndex->nHeight, tipIndex->GetBlockHash().ToString());
                    mGlobalForkTips[tipIndex] = (int)GetTime();
                    done |= true;
                }
                else
                {
                    // we must neglect this branch since not linked to the pindex
                    LogPrint("forks", "%s():%d - stopped at %s h(%d)\n",
                        __func__, __LINE__, dum->GetBlockHash().ToString(), dum->nHeight);
                }
            }

            LogPrint("forks", "%s():%d - exiting done[%d]\n", __func__, __LINE__, done);
            return done;
        }

        // nothing to do, this is not a tip at all
        LogPrint("forks", "%s():%d - not a tip: h(%d) [%s]\n",
            __func__, __LINE__, pindex->nHeight, pindex->GetBlockHash().ToString());
        return false;
    }
}

int getMostRecentGlobalForkTips(std::vector<uint256>& output)
{
    using map_pair = pair<const CBlockIndex*, int>;

    std::vector<map_pair> vTemp(begin(mGlobalForkTips), end(mGlobalForkTips));

    sort(begin(vTemp), end(vTemp), [](const map_pair& a, const map_pair& b) { return a.second < b.second; });

    int count = MAX_NUM_GLOBAL_FORKS;
    BOOST_REVERSE_FOREACH(auto const &p, vTemp)
    {
        output.push_back(p.first->GetBlockHash() );
        if (--count <= 0)
            break;
    }

    return output.size();
}

CBlockIndex* AddToBlockIndex(const CBlockHeader& block)
{
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end())
        return it->second;

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    assert(pindexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = mapBlockIndex.find(block.hashPrevBlock);
    if (miPrev != mapBlockIndex.end())
    {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();
    }
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    if (pindexNew->pprev){
        pindexNew->nChainDelay = pindexNew->pprev->nChainDelay + GetBlockDelay(*pindexNew,*(pindexNew->pprev), chainActive.Height(), fIsStartupSyncing);
    } else {
        pindexNew->nChainDelay = 0 ;
    }
    if(pindexNew->nChainDelay != 0) {
        LogPrintf("%s: Block belong to a chain under punishment Delay VAL: %i BLOCKHEIGHT: %d\n",__func__, pindexNew->nChainDelay, pindexNew->nHeight);
    }

    if (pindexNew->pprev && pindexNew->nVersion == BLOCK_VERSION_SC_SUPPORT)
    {
        const CFieldElement& prevScCumTreeHash =
                (pindexNew->pprev->nVersion == BLOCK_VERSION_SC_SUPPORT) ?
                        pindexNew->pprev->scCumTreeHash : CFieldElement::GetZeroHash();
        pindexNew->scCumTreeHash = CFieldElement::ComputeHash(prevScCumTreeHash, CFieldElement{block.hashScTxsCommitment});
        mapCumtreeHeight.insert(std::make_pair(pindexNew->scCumTreeHash.GetLegacyHash(), pindexNew->nHeight));
    }

    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    if (pindexBestHeader == NULL || (pindexBestHeader->nChainWork < pindexNew->nChainWork && pindexNew->nChainDelay==0))
        pindexBestHeader = pindexNew;

    setDirtyBlockIndex.insert(pindexNew);

    addToGlobalForkTips(pindexNew);

    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock &block, CValidationState& state, CBlockIndex *pindexNew, const CDiskBlockPos& pos, BlockSet* sForkTips)
{
    pindexNew->nTx = block.vtx.size() + block.vcert.size();
    pindexNew->nChainTx = 0;
    CAmount sproutValue = 0;
    for (auto tx : block.vtx) {
        for (auto js : tx.GetVjoinsplit()) {
            sproutValue += js.vpub_old;
            sproutValue -= js.vpub_new;
        }
    }
    pindexNew->nSproutValue = sproutValue;
    pindexNew->nChainSproutValue = std::nullopt;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    setDirtyBlockIndex.insert(pindexNew);

    if (pindexNew->pprev == NULL || pindexNew->pprev->nChainTx) {
        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        deque<CBlockIndex*> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty()) {
            CBlockIndex *pindex = queue.front();
            queue.pop_front();
            pindex->nChainTx = (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;
            if (pindex->pprev) {
                if (pindex->pprev->nChainSproutValue && pindex->nSproutValue) {
                    pindex->nChainSproutValue = *pindex->pprev->nChainSproutValue + *pindex->nSproutValue;
                } else {
                    pindex->nChainSproutValue = std::nullopt;
                }
            } else {
                pindex->nChainSproutValue = pindex->nSproutValue;
            }
            {
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (chainActive.Tip() == NULL || !setBlockIndexCandidates.value_comp()(pindex, chainActive.Tip())) {
                setBlockIndexCandidates.insert(pindex);
            }
            // we must not take 'delay' into account, otherwise when we do the relay of a block we might miss a higher tip
            // on a fork because we will look into this container
            if (chainActive.Tip() == NULL || !CBlockIndexRealWorkComparator()(pindex, chainActive.Tip()))
            {
                if (sForkTips)
                {
                    int num = sForkTips->erase(pindex->pprev);
                    LogPrint("forks", "%s():%d - Adding idx to sForkTips: h(%d) [%s], nChainTx=%d, delay=%d, prev[%d]\n",
                        __func__, __LINE__, pindex->nHeight, pindex->GetBlockHash().ToString(),
                        pindex->nChainTx, pindex->nChainDelay, num);
                    sForkTips->insert(pindex);
                }
            }

            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second) {
                std::multimap<CBlockIndex*, CBlockIndex*>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE)) {
            mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}

bool FindBlockPos(CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown)
{
    // Currently fKnown is false for blocks coming from network, true for blocks loaded from files upon reindexing
    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    vinfoBlockFile.resize(std::max<unsigned int>(vinfoBlockFile.size(), nFile + 1));

    if (!fKnown)
    {
        while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE)
        {
            nFile++;
            vinfoBlockFile.resize(nFile + 1);
        }

        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    if (nFile != nLastBlockFile) {
        if (!fKnown) {
            LogPrintf("Leaving block file %i: %s\n", nLastBlockFile, vinfoBlockFile.at(nLastBlockFile).ToString());
        }
        FlushBlockFile(!fKnown);
        nLastBlockFile = nFile;
    }

    vinfoBlockFile.at(nFile).AddBlock(nHeight, nTime);
    if (fKnown)
        vinfoBlockFile.at(nFile).nSize = std::max(pos.nPos + nAddSize, vinfoBlockFile.at(nFile).nSize);
    else
        vinfoBlockFile.at(nFile).nSize += nAddSize;

    if (!fKnown) {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = (vinfoBlockFile.at(nFile).nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks) {
            if (fPruneMode)
                fCheckForPruning = true;
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos)) {
                FILE *file = OpenBlockFile(pos);
                if (file) {
                    LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            }
            else
                return state.Error("out of disk space");
        }
    }

    setDirtyFileInfo.insert(nFile);
    return true;
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize)
{
    pos.nFile = nFile;

    LOCK(cs_LastBlockFile);

    unsigned int nNewSize;
    pos.nPos = vinfoBlockFile[nFile].nUndoSize;
    nNewSize = vinfoBlockFile[nFile].nUndoSize += nAddSize;
    setDirtyFileInfo.insert(nFile);

    unsigned int nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (fPruneMode)
            fCheckForPruning = true;
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos)) {
            FILE *file = OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else
            return state.Error("out of disk space");
    }

    return true;
}

bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, flagCheckPow fCheckPOW)
{
    // Check block version
    if (block.nVersion < MIN_BLOCK_VERSION)
        return state.DoS(100, error("CheckBlockHeader(): block version not valid"),
                         CValidationState::Code::INVALID, "version-invalid");

    // Check Equihash solution is valid
    if (fCheckPOW == flagCheckPow::ON && !CheckEquihashSolution(&block, Params()))
        return state.DoS(100, error("CheckBlockHeader(): Equihash solution invalid"),
                         CValidationState::Code::INVALID, "invalid-solution");

    // Check proof of work matches claimed amount
    if (fCheckPOW == flagCheckPow::ON && !CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus()))
        return state.DoS(50, error("CheckBlockHeader(): proof of work failed"),
                         CValidationState::Code::INVALID, "high-hash");

    return true;
}

bool CheckBlock(const CBlock& block, CValidationState& state,
                libzcash::ProofVerifier& verifier,
                flagCheckPow fCheckPOW, flagCheckMerkleRoot fCheckMerkleRoot)
{
    // These are checks that are independent of context.

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, fCheckPOW))
        return false;

    // Check the merkle root.
    if (fCheckMerkleRoot == flagCheckMerkleRoot::ON) {
        bool mutated;
        uint256 hashMerkleRoot2 = block.BuildMerkleTree(&mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(100, error("CheckBlock(): hashMerkleRoot mismatch"),
                             CValidationState::Code::INVALID, "bad-txnmrklroot", true);

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS(100, error("CheckBlock(): duplicate transaction"),
                             CValidationState::Code::INVALID, "bad-txns-duplicate", true);
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Size limits
    // - From the sidechains fork point on, the block size has been increased
    unsigned int block_size_limit = MAX_BLOCK_SIZE;
    if (block.nVersion != BLOCK_VERSION_SC_SUPPORT)
        block_size_limit = MAX_BLOCK_SIZE_BEFORE_SC;

    size_t headerSize = 0;
    size_t totTxSize = 0;
    size_t totCertSize = 0;
    size_t blockSize = block.GetSerializeComponentsSize(headerSize, totTxSize, totCertSize);

    if (block.vtx.empty() || blockSize > block_size_limit)
        return state.DoS(100, error("CheckBlock(): size limits failed"),
                         CValidationState::Code::INVALID, "bad-blk-length");

    if (block.nVersion == BLOCK_VERSION_SC_SUPPORT)
    {
        if (totTxSize > BLOCK_TX_PARTITION_SIZE)
        {
            return error("CheckBlock(): block tx partition size exceeded %d > %d", totTxSize, BLOCK_TX_PARTITION_SIZE);
        }
    }

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0].IsCoinBase())
        return state.DoS(100, error("CheckBlock(): first tx is not coinbase"),
                         CValidationState::Code::INVALID, "bad-cb-missing");
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i].IsCoinBase())
            return state.DoS(100, error("CheckBlock(): more than one coinbase"),
                             CValidationState::Code::INVALID, "bad-cb-multiple");

    // Check transactions and certificates
    for(const CTransaction& tx: block.vtx) {
        if (!CheckTransaction(tx, state, verifier)) {
            return error("CheckBlock(): CheckTransaction failed");
        }
    }

    if(!CheckCertificatesOrdering(block.vcert, state))
        return error("CheckBlock(): Certificate quality ordering check failed");

    for(const CScCertificate& cert: block.vcert)
    {
        if (!CheckCertificate(cert, state)) {
            return error("CheckBlock(): Certificate check failed");
        }
    }

    unsigned int nSigOps = 0;
    for(const CTransaction& tx: block.vtx) {
        nSigOps += GetLegacySigOpCount(tx);
    }

    for(const CScCertificate& cert: block.vcert) {
        nSigOps += GetLegacySigOpCount(cert);
    }

    if (nSigOps > MAX_BLOCK_SIGOPS)
        return state.DoS(100, error("CheckBlock(): out-of-bounds SigOpCount"),
                         CValidationState::Code::INVALID, "bad-blk-sigops", true);

    return true;
}

bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex * const pindexPrev)
{
    const CChainParams& chainParams = Params();
    const Consensus::Params& consensusParams = chainParams.GetConsensus();
    uint256 hash = block.GetHash();
    if (hash == consensusParams.hashGenesisBlock)
        return true;

    assert(pindexPrev);

    int nHeight = pindexPrev->nHeight+1;

    // Check proof of work
    if (block.nBits != GetNextWorkRequired(pindexPrev, &block, consensusParams))
        return state.DoS(100, error("%s: incorrect proof of work", __func__),
                         CValidationState::Code::INVALID, "bad-diffbits");


    // Check timestamp against prev
    auto medianTimePast = pindexPrev->GetMedianTimePast();
    if (block.GetBlockTime() <= medianTimePast) {
        return state.Invalid(error("%s: block at height %d, timestamp %d is not later than median-time-past %d",
                __func__, nHeight, block.GetBlockTime(), medianTimePast),
                CValidationState::Code::INVALID, "time-too-old");
    }


    if (ForkManager::getInstance().isFutureTimeStampActive(nHeight) &&
            block.GetBlockTime() > medianTimePast + MAX_FUTURE_BLOCK_TIME_MTP) {
        return state.Invalid(error("%s: block at height %d, timestamp %d is too far ahead of median-time-past, limit is %d",
                __func__, nHeight, block.GetBlockTime(), medianTimePast + MAX_FUTURE_BLOCK_TIME_MTP),
                CValidationState::Code::INVALID, "time-too-far-ahead-of-mtp");
    }


    // Check timestamp
    auto nTimeLimit = GetTime() + MAX_FUTURE_BLOCK_TIME_LOCAL;
    if (block.GetBlockTime() > nTimeLimit) {
        return state.Invalid(error("%s: block at height %d, timestamp %d is too far ahead of local time, limit is %d",
                __func__, nHeight, block.GetBlockTime(), nTimeLimit),
                CValidationState::Code::INVALID, "time-too-new");
    }

    if (fCheckpointsEnabled)
    {
        // Don't accept any forks from the main chain prior to last checkpoint
        CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint(chainParams.Checkpoints());
        if (pcheckpoint && nHeight < pcheckpoint->nHeight)
            return state.DoS(100, error("%s: forked chain older than last checkpoint (height %d)", __func__, nHeight));
    }

    if (!ForkManager::getInstance().isValidBlockVersion(nHeight, block.nVersion) )
    {
        return state.Invalid(error("%s : rejected nVersion block %d not supported at height %d", __func__, block.nVersion, nHeight),
            CValidationState::Code::INVALID, "bad-version");
    }

    if (block.nVersion == BLOCK_VERSION_SC_SUPPORT)
    {
        CFieldElement fieldToValidate{block.hashScTxsCommitment};
        if (!fieldToValidate.IsValid())
            return state.DoS(100, error("%s: incorrect hashScTxsCommitment", __func__),
                             CValidationState::Code::INVALID, "invalid-sc-txs-commitment");
    }

    return true;
}

bool ContextualCheckBlock(const CBlock& block, CValidationState& state, CBlockIndex * const pindexPrev)
{
    const int nHeight = pindexPrev == NULL ? 0 : pindexPrev->nHeight + 1;
    const Consensus::Params& consensusParams = Params().GetConsensus();

    // Check that all transactions are finalized
    BOOST_FOREACH(const CTransaction& tx, block.vtx) {

        // Check transaction contextually against consensus rules at block height
        if (!tx.ContextualCheck(state, nHeight, 100)) {
            return false; // Failure reason has been set in validation state object
        }

        int nLockTimeFlags = 0;
        int64_t nLockTimeCutoff = (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST)
                                ? pindexPrev->GetMedianTimePast()
                                : block.GetBlockTime();
        if (!IsFinalTx(tx, nHeight, nLockTimeCutoff)) {
            return state.DoS(10, error("%s: contains a non-final transaction", __func__), CValidationState::Code::INVALID, "bad-txns-nonfinal");
        }
    }

    for(const auto& cert: block.vcert) {
        // Check certificate contextually against consensus rules at block height
        if (!cert.ContextualCheck(state, nHeight, 100)) {
            return false; // Failure reason has been set in validation state object
        }
    }

    // Enforce BIP 34 rule that the coinbase starts with serialized block height.
    // In Zcash this has been enforced since launch, except that the genesis
    // block didn't include the height in the coinbase (see Zcash protocol spec
    // section '6.8 Bitcoin Improvement Proposals').
    if (nHeight > 0)
    {
        CScript expect = CScript() << nHeight;
        if (block.vtx[0].GetVin()[0].scriptSig.size() < expect.size() ||
            !std::equal(expect.begin(), expect.end(), block.vtx[0].GetVin()[0].scriptSig.begin())) {
            return state.DoS(100, error("%s: block height mismatch in coinbase", __func__), CValidationState::Code::INVALID, "bad-cb-height");
        }
    }

    // Reject the post-chainsplit block until a specific time is reached
    if (ForkManager::getInstance().isAfterChainsplit(nHeight) && !ForkManager::getInstance().isAfterChainsplit(nHeight-1)  && block.GetBlockTime() < ForkManager::getInstance().getMinimumTime(nHeight))
    {
        return state.DoS(10, error("%s: post-chainsplit block received prior to scheduled time", __func__), CValidationState::Code::INVALID, "bad-cs-time");
    }

    CAmount reward = GetBlockSubsidy(nHeight, consensusParams);
    // Coinbase transaction must include an output sending x.x% of
    // the block reward to a community fund script

    for (Fork::CommunityFundType cfType=Fork::CommunityFundType::FOUNDATION; cfType < Fork::CommunityFundType::ENDTYPE; cfType = Fork::CommunityFundType(cfType + 1)) {
        CAmount communityReward = ForkManager::getInstance().getCommunityFundReward(nHeight, reward, cfType);
        if (communityReward > 0) {
            const CScript& refScript = Params().GetCommunityFundScriptAtHeight(nHeight, cfType);

            bool found = false;
            for(const CTxOut& output: block.vtx[0].GetVout())
            {
                if ((output.scriptPubKey == refScript) && (output.nValue == communityReward))
                {
                    found = true;
                    break;
                }
            }

            if (!found) {
                LogPrintf("%s():%d - ERROR: subsidy quota incorrect or missing: refScript[%s], commReward=%d, type=%d\n",
                    __func__, __LINE__, refScript.ToString(), communityReward, cfType);
                return state.DoS(100, error("%s: community fund missing block %d", __func__, nHeight), CValidationState::Code::INVALID, "cb-no-community-fund");
            }
        }
    }

    return true;
}

bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex** ppindex, bool lookForwardTips)
{
    dump_global_tips(10);

    const CChainParams& chainparams = Params();
    AssertLockHeld(cs_main);
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator miSelf = mapBlockIndex.find(hash);
    CBlockIndex *pindex = NULL;
    if (miSelf != mapBlockIndex.end()) {
        // Block header is already known.
        pindex = miSelf->second;

        // update it because if it is a tip, its timestamp is most probably changed
        updateGlobalForkTips(pindex, lookForwardTips);

        if (ppindex)
            *ppindex = pindex;
        if (pindex->nStatus & BLOCK_FAILED_MASK)
            return state.Invalid(error("%s: block is marked invalid", __func__), CValidationState::Code::OK, "duplicate");
        return true;
    }

    if (!CheckBlockHeader(block, state))
        return false;

    // Get prev block index
    CBlockIndex* pindexPrev = NULL;
    if (hash != chainparams.GetConsensus().hashGenesisBlock) {
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi == mapBlockIndex.end())
        {
            LogPrint("forks", "%s():%d - prev block not found: [%s]\n",
                __func__, __LINE__, block.hashPrevBlock.ToString());
            return state.DoS(10, error("%s: prev block not found", __func__), CValidationState::Code::OK, "bad-prevblk");
        }
        pindexPrev = (*mi).second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK)
            return state.DoS(100, error("%s: prev block invalid", __func__), CValidationState::Code::INVALID, "bad-prevblk");
    }

    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
        return false;

    if (pindex == NULL)
        pindex = AddToBlockIndex(block);

    if (ppindex)
        *ppindex = pindex;

    return true;
}

bool AcceptBlock(CBlock& block, CValidationState& state, CBlockIndex** ppindex, bool fRequested, CDiskBlockPos* dbp, BlockSet* sForkTips)
{
    const CChainParams& chainparams = Params();
    AssertLockHeld(cs_main);

    CBlockIndex *&pindex = *ppindex;

    if (!AcceptBlockHeader(block, state, &pindex))
        return false;

    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new and has enough work to
    // advance our tip, and isn't too many blocks ahead.
    bool fAlreadyHave = pindex->nStatus & BLOCK_HAVE_DATA;
    bool fHasMoreWork = (chainActive.Tip() ? pindex->nChainWork > chainActive.Tip()->nChainWork : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    bool fTooFarAhead = (pindex->nHeight > int(chainActive.Height() + MIN_BLOCKS_TO_KEEP));

    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks.
    if (fAlreadyHave) return true;
    if (!fRequested) {  // If we didn't ask for it:
        if (pindex->nTx != 0) return true;  // This is a previously-processed block that was pruned
        if (!fHasMoreWork) return true;     // Don't process less-work chains
        if (fTooFarAhead) return true;      // Block height is too high
    }

    // See method docstring for why this is always disabled
    auto verifier = libzcash::ProofVerifier::Disabled();
    if ((!CheckBlock(block, state, verifier)) || !ContextualCheckBlock(block, state, pindex->pprev)) {
        if (state.IsInvalid() && !state.CorruptionPossible()) {
            pindex->nStatus |= BLOCK_FAILED_VALID;
            setDirtyBlockIndex.insert(pindex);
        }
        return false;
    }

    int nHeight = pindex->nHeight;

    // Write block to history file
    try {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != NULL)
            blockPos = *dbp;
        if (!FindBlockPos(state, blockPos, nBlockSize+8, nHeight, block.GetBlockTime(), dbp != NULL))
            return error("AcceptBlock(): FindBlockPos failed");
        if (dbp == NULL)
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                AbortNode(state, "Failed to write block");
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos, sForkTips))
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error: ") + e.what());
    }

    if (fCheckForPruning)
        FlushStateToDisk(state, FLUSH_STATE_NONE); // we just allocated more disk space for block files

    return true;
}

bool ProcessNewBlock(CValidationState &state, CNode* pfrom, CBlock* pblock, bool fForceProcessing, CDiskBlockPos *dbp)
{
    // Preliminary checks
    auto verifier = libzcash::ProofVerifier::Disabled();
    bool checked = CheckBlock(*pblock, state, verifier);

    BlockSet sForkTips;

    {
        LOCK(cs_main);

        bool fRequested = MarkBlockAsReceived(pblock->GetHash());
        fRequested |= fForceProcessing;

        if (!checked)
        {
            return error("%s: CheckBlock FAILED", __func__);
        }

        // Store to disk
        CBlockIndex *pindex = NULL;

        bool ret = AcceptBlock(*pblock, state, &pindex, fRequested, dbp, &sForkTips);

        if (pindex && pfrom)
        {
            mapBlockSource[pindex->GetBlockHash()] = pfrom->GetId();
        }

        CheckBlockIndex();

        if (!ret)
        {
            return error("%s: AcceptBlock FAILED", __func__);
        }
    }

    bool postponeRelay = false;

    if (!ActivateBestChain(state, pblock, postponeRelay))
    {
        return error("%s: ActivateBestChain failed", __func__);
    }

    if (!postponeRelay)
    {
        if (!RelayAlternativeChain(state, pblock, &sForkTips))
        {
            return error("%s: RelayAlternativeChain failed", __func__);
        }
    }
    else
    {
        LogPrint("net", "%s: Not relaying block %s\n", __func__, pblock->GetHash().ToString());
    }

    return true;
}

bool TestBlockValidity(CValidationState &state, const CBlock& block, CBlockIndex * const pindexPrev,
        flagCheckPow fCheckPOW, flagCheckMerkleRoot fCheckMerkleRoot, flagScRelatedChecks fScRelatedChecks)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev == chainActive.Tip());

    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;
    // JoinSplit and Sidechains proofs are verified in ConnectBlock
    auto verifier = libzcash::ProofVerifier::Disabled();

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
        return false;
    if (!CheckBlock(block, state, verifier, fCheckPOW, fCheckMerkleRoot))
        return false;
    if (!ContextualCheckBlock(block, state, pindexPrev))
        return false;

    if (!ConnectBlock(block, state, &indexDummy, viewNew, chainActive, flagBlockProcessingType::CHECK_ONLY,
                      fScRelatedChecks, flagScProofVerification::OFF, flagLevelDBIndexesWrite::OFF))
        return false;
    assert(state.IsValid());

    return true;
}

/**
 * BLOCK PRUNING CODE
 */

/* Calculate the amount of disk space the block & undo files currently use */
uint64_t CalculateCurrentUsage()
{
    uint64_t retval = 0;
    BOOST_FOREACH(const CBlockFileInfo &file, vinfoBlockFile) {
        retval += file.nSize + file.nUndoSize;
    }
    return retval;
}

/* Prune a block file (modify associated database entries)*/
void PruneOneBlockFile(const int fileNumber)
{
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); ++it) {
        CBlockIndex* pindex = it->second;
        if (pindex->nFile == fileNumber) {
            pindex->nStatus &= ~BLOCK_HAVE_DATA;
            pindex->nStatus &= ~BLOCK_HAVE_UNDO;
            pindex->nFile = 0;
            pindex->nDataPos = 0;
            pindex->nUndoPos = 0;
            setDirtyBlockIndex.insert(pindex);

            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setBlockIndexCandidates.
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex->pprev);
            while (range.first != range.second) {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator it = range.first;
                range.first++;
                if (it->second == pindex) {
                    mapBlocksUnlinked.erase(it);
                }
            }
        }
    }

    vinfoBlockFile[fileNumber].SetNull();
    setDirtyFileInfo.insert(fileNumber);
}


void UnlinkPrunedFiles(std::set<int>& setFilesToPrune)
{
    for (set<int>::iterator it = setFilesToPrune.begin(); it != setFilesToPrune.end(); ++it) {
        CDiskBlockPos pos(*it, 0);
        boost::filesystem::remove(GetBlockPosFilename(pos, "blk"));
        boost::filesystem::remove(GetBlockPosFilename(pos, "rev"));
        LogPrintf("Prune: %s deleted blk/rev (%05u)\n", __func__, *it);
    }
}

/* Calculate the block/rev files that should be deleted to remain under target*/
void FindFilesToPrune(std::set<int>& setFilesToPrune)
{
    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == NULL || nPruneTarget == 0) {
        return;
    }
    if (chainActive.Tip()->nHeight <= Params().PruneAfterHeight()) {
        return;
    }

    unsigned int nLastBlockWeCanPrune = chainActive.Tip()->nHeight - MIN_BLOCKS_TO_KEEP;
    uint64_t nCurrentUsage = CalculateCurrentUsage();
    // We don't check to prune until after we've allocated new space for files
    // So we should leave a buffer under our target to account for another allocation
    // before the next pruning.
    uint64_t nBuffer = BLOCKFILE_CHUNK_SIZE + UNDOFILE_CHUNK_SIZE;
    uint64_t nBytesToPrune;
    int count=0;

    if (nCurrentUsage + nBuffer >= nPruneTarget) {
        for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
            nBytesToPrune = vinfoBlockFile[fileNumber].nSize + vinfoBlockFile[fileNumber].nUndoSize;

            if (vinfoBlockFile[fileNumber].nSize == 0)
                continue;

            if (nCurrentUsage + nBuffer < nPruneTarget)  // are we below our target?
                break;

            // don't prune files that could have a block within MIN_BLOCKS_TO_KEEP of the main chain's tip but keep scanning
            if (vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
                continue;

            PruneOneBlockFile(fileNumber);
            // Queue up the files for removal
            setFilesToPrune.insert(fileNumber);
            nCurrentUsage -= nBytesToPrune;
            count++;
        }
    }

    LogPrint("prune", "Prune: target=%dMiB actual=%dMiB diff=%dMiB max_prune_height=%d removed %d blk/rev pairs\n",
           nPruneTarget/1024/1024, nCurrentUsage/1024/1024,
           ((int64_t)nPruneTarget - (int64_t)nCurrentUsage)/1024/1024,
           nLastBlockWeCanPrune, count);
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = boost::filesystem::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

FILE* OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
{
    if (pos.IsNull())
        return NULL;
    boost::filesystem::path path = GetBlockPosFilename(pos, prefix);
    boost::filesystem::create_directories(path.parent_path());
    FILE* file = fopen(path.string().c_str(), "rb+");
    if (!file && !fReadOnly)
        file = fopen(path.string().c_str(), "wb+");
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return NULL;
    }
    if (pos.nPos) {
        if (fseek(file, pos.nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return NULL;
        }
    }
    return file;
}

FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "rev", fReadOnly);
}

boost::filesystem::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix)
{
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

CBlockIndex * InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return NULL;

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw runtime_error("LoadBlockIndex(): new CBlockIndex failed");
    mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool static LoadBlockIndexDB()
{
    const CChainParams& chainparams = Params();
    if (!pblocktree->LoadBlockIndexGuts())
        return false;

    boost::this_thread::interruption_point();

    // Calculate nChainWork
    vector<pair<int, CBlockIndex*> > vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    BOOST_FOREACH(const PAIRTYPE(uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        vSortedByHeight.push_back(make_pair(pindex->nHeight, pindex));
    }
    sort(vSortedByHeight.begin(), vSortedByHeight.end());
    BOOST_FOREACH(const PAIRTYPE(int, CBlockIndex*)& item, vSortedByHeight)
    {
        CBlockIndex* pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        pindex->nChainDelay = 0 ;
        // We can link the chain of blocks for which we've received transactions at some point.
        // Pruned nodes may have deleted the block.
        if (pindex->nTx > 0) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                    if (pindex->pprev->nChainSproutValue && pindex->nSproutValue) {
                        pindex->nChainSproutValue = *pindex->pprev->nChainSproutValue + *pindex->nSproutValue;
                    } else {
                        pindex->nChainSproutValue = std::nullopt;
                    }
                } else {
                    pindex->nChainTx = 0;
                    pindex->nChainSproutValue = std::nullopt;
                    mapBlocksUnlinked.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
                pindex->nChainSproutValue = pindex->nSproutValue;
            }
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == NULL))
            setBlockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!pindexBestInvalid || pindex->nChainWork > pindexBestInvalid->nChainWork))
            pindexBestInvalid = pindex;
        if (pindex->pprev)
            pindex->BuildSkip();
        if (pindex->IsValid(BLOCK_VALID_TREE) && (pindexBestHeader == NULL || CBlockIndexWorkComparator()(pindexBestHeader, pindex)))
            pindexBestHeader = pindex;

        addToGlobalForkTips(pindex);
    }

    // Load block file info
    pblocktree->ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++) {
        pblocktree->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    }
    LogPrintf("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile].ToString());
    for (int nFile = nLastBlockFile + 1; true; nFile++) {
        CBlockFileInfo info;
        if (pblocktree->ReadBlockFileInfo(nFile, info)) {
            vinfoBlockFile.push_back(info);
        } else {
            break;
        }
    }

    // Check presence of blk files
    LogPrintf("Checking all blk files are present...\n");
    set<int> setBlkDataFiles;
    BOOST_FOREACH(const PAIRTYPE(uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            setBlkDataFiles.insert(pindex->nFile);
        }
    }
    for (std::set<int>::iterator it = setBlkDataFiles.begin(); it != setBlkDataFiles.end(); it++)
    {
        CDiskBlockPos pos(*it, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull()) {
            return false;
        }
    }

    // Check whether we have ever pruned block & undo files
    pblocktree->ReadFlag("prunedblockfiles", fHavePruned);
    if (fHavePruned)
        LogPrintf("LoadBlockIndexDB(): Block files have previously been pruned\n");

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    pblocktree->ReadReindexing(fReindexing);
    fReindex |= fReindexing;

    bool fReindexingFast = false;
    pblocktree->ReadFastReindexing(fReindexingFast);
    fReindexFast |= fReindexingFast;

    // Check whether we have a transaction index
    pblocktree->ReadFlag("txindex", fTxIndex);
    LogPrintf("%s: transaction index %s\n", __func__, fTxIndex ? "enabled" : "disabled");

    // Check whether we have a maturityHeight index
    pblocktree->ReadFlag("maturityheightindex", fMaturityHeightIndex);
    LogPrintf("%s: maturityHeight index %s\n", __func__, fMaturityHeightIndex ? "enabled" : "disabled");

    // Check whether we have an address index
    pblocktree->ReadFlag("addressindex", fAddressIndex);
    LogPrintf("%s: address index %s\n", __func__, fAddressIndex ? "enabled" : "disabled");

    // Check whether we have a timestamp index
    pblocktree->ReadFlag("timestampindex", fTimestampIndex);
    LogPrintf("%s: timestamp index %s\n", __func__, fTimestampIndex ? "enabled" : "disabled");

    // Check whether we have a spent index
    pblocktree->ReadFlag("spentindex", fSpentIndex);
    LogPrintf("%s: spent index %s\n", __func__, fSpentIndex ? "enabled" : "disabled");

    // Fill in-memory data
    BOOST_FOREACH(const PAIRTYPE(uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        // - This relationship will always be true even if pprev has multiple
        //   children, because hashAnchor is technically a property of pprev,
        //   not its children.
        // - This will miss chain tips; we handle the best tip below, and other
        //   tips will be handled by ConnectTip during a re-org.
        if (pindex->pprev) {
            pindex->pprev->hashAnchorEnd = pindex->hashAnchor;
        }
    }

    // Load pointer to end of best chain
    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    if (it == mapBlockIndex.end())
        return true;
    chainActive.SetTip(it->second);
    // Set hashAnchorEnd for the end of best chain
    it->second->hashAnchorEnd = pcoinsTip->GetBestAnchor();

    PruneBlockIndexCandidates();

    LogPrintf("%s: hashBestChain=%s height=%d date=%s progress=%f\n", __func__,
        chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
        Checkpoints::GuessVerificationProgress(chainparams.Checkpoints(), chainActive.Tip()));

    EnforceNodeDeprecation(chainActive.Height(), true);

    return true;
}

CVerifyDB::CVerifyDB()
{
    uiInterface.ShowProgress(_("Verifying blocks..."), 0);
}

CVerifyDB::~CVerifyDB()
{
    uiInterface.ShowProgress("", 100);
}

bool CVerifyDB::VerifyDB(CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    LOCK(cs_main);
    if (chainActive.Tip() == NULL || chainActive.Tip()->pprev == NULL)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > chainActive.Height())
        nCheckDepth = chainActive.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex* pindexState = chainActive.Tip();
    CBlockIndex* pindexFailure = NULL;
    int nGoodTransactions = 0;
    CValidationState state;
    // No need to verify JoinSplits twice
    auto verifier = libzcash::ProofVerifier::Disabled();
    for (CBlockIndex* pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev)
    {
        boost::this_thread::interruption_point();
        uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100)))));
        if (pindex->nHeight < chainActive.Height()-nCheckDepth)
            break;
        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex))
            return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state, verifier))
            return error("VerifyDB(): *** found bad block at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex) {

            IncludeScAttributes includeSc = IncludeScAttributes::ON;

            if (block.nVersion != BLOCK_VERSION_SC_SUPPORT)
                includeSc = IncludeScAttributes::OFF;

            CBlockUndo undo(includeSc);

            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull()) {
                if (!UndoReadFromDisk(undo, pos, pindex->pprev->GetBlockHash()))
                    return error("VerifyDB(): *** found bad undo data at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState && (coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <= nCoinCacheUsage) {
            bool fClean = true;
            if (!DisconnectBlock(block, state, pindex, coins, flagLevelDBIndexesWrite::OFF, &fClean, nullptr))
                return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            pindexState = pindex->pprev;
            if (!fClean) {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else
                nGoodTransactions += block.vtx.size() + block.vcert.size();
        }

        if (ShutdownRequested())
            return true;

    }

    if (pindexFailure)
        return error("VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n", chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4) {
        CBlockIndex *pindex = pindexState;
        CHistoricalChain chainHistorical(chainActive, pindex->nHeight - 1);
        while (pindex != chainActive.Tip()) {
            boost::this_thread::interruption_point();
            uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, 100 - (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;

            if (!ReadBlockFromDisk(block, pindex))
                return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());

            chainHistorical.SetHeight(pindex->nHeight - 1);

            if (!ConnectBlock(block, state, pindex, coins, chainHistorical, flagBlockProcessingType::COMPLETE,
                              flagScRelatedChecks::ON, flagScProofVerification::ON, flagLevelDBIndexesWrite::OFF))
                return error("VerifyDB(): *** found unconnectable block at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        }
    }

    LogPrintf("No coin database inconsistencies in last %i blocks (%i transactions)\n", chainActive.Height() - pindexState->nHeight, nGoodTransactions);

    return true;
}

void UnloadBlockIndex()
{
    LOCK(cs_main);
    setBlockIndexCandidates.clear();
    chainActive.SetTip(NULL);
    pindexBestInvalid = NULL;
    pindexBestHeader = NULL;
    mempool.clear();
    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();
    nSyncStarted = 0;
    mapBlocksUnlinked.clear();
    vinfoBlockFile.clear();
    nLastBlockFile = 0;
    nBlockSequenceId = 1;
    mapBlockSource.clear();
    mapBlocksInFlight.clear();
    nQueuedValidatedHeaders = 0;
    nPreferredDownload = 0;
    setDirtyBlockIndex.clear();
    setDirtyFileInfo.clear();
    mapNodeState.clear();
    recentRejects.reset(NULL);

    BOOST_FOREACH(BlockMap::value_type& entry, mapBlockIndex) {
        delete entry.second;
    }
    mapBlockIndex.clear();
    fHavePruned = false;
}

bool LoadBlockIndex()
{
    // Load block index from databases
    if (fReindex || fReindexFast)
        return true;

    return LoadBlockIndexDB();
}


bool InitBlockIndex() {
    const CChainParams& chainparams = Params();
    LOCK(cs_main);

    // Initialize global variables that cannot be constructed at startup.
    recentRejects.reset(new CRollingBloomFilter(120000, 0.000001));

    // Check whether we're already initialized
    if (chainActive.Genesis() != NULL)
    {
        return true;
    }

    // set the flag upon db initialization
    std::string indexVersionStr = CURRENT_INDEX_VERSION_STR;
    pblocktree->WriteString("indexVersion", indexVersionStr);

    // Use the provided setting for -txindex in the new database
    fTxIndex = GetBoolArg("-txindex", DEFAULT_TXINDEX);
    pblocktree->WriteFlag("txindex", fTxIndex);

    // Use the provided setting for -maturityheightindex in the new database
    fMaturityHeightIndex = GetBoolArg("-maturityheightindex", DEFAULT_MATURITYHEIGHTINDEX);
    pblocktree->WriteFlag("maturityheightindex", fMaturityHeightIndex);

    // Use the provided setting for -addressindex in the new database
    fAddressIndex = GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX);
    pblocktree->WriteFlag("addressindex", fAddressIndex);

    // Use the provided setting for -timestampindex in the new database
    fTimestampIndex = GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX);
    pblocktree->WriteFlag("timestampindex", fTimestampIndex);

    fSpentIndex = GetBoolArg("-spentindex", DEFAULT_SPENTINDEX);
    pblocktree->WriteFlag("spentindex", fSpentIndex);

    LogPrintf("Initializing databases...\n");

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (fReindex || fReindexFast)
        return true;

    try {
        CBlock &block = const_cast<CBlock&>(Params().GenesisBlock());
        // Start new block file
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        CValidationState state;
        if (!FindBlockPos(state, blockPos, nBlockSize+8, 0, block.GetBlockTime()))
            return error("LoadBlockIndex(): FindBlockPos failed");
        if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
            return error("LoadBlockIndex(): writing genesis block to disk failed");
        CBlockIndex *pindex = AddToBlockIndex(block);
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos, NULL))
            return error("LoadBlockIndex(): genesis block not accepted");
        if (!ActivateBestChain(state, &block))
            return error("LoadBlockIndex(): genesis block cannot be activated");
        // Force a chainstate write so that when we VerifyDB in a moment, it doesn't check stale data
        return FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
    } catch (const std::runtime_error& e) {
        return error("LoadBlockIndex(): failed to initialize block database: %s", e.what());
    }

    return true;
}

CBlock LoadBlockFrom(CBufferedFile& blkdat, CDiskBlockPos* pLastLoadedBlkPos)
{
    CBlock res{};

    if (blkdat.eof())
        return res;

    int blkSize = -1;

    //locate Header
    for(uint64_t nRewind = blkdat.GetPos(); !blkdat.eof() && (blkSize == -1);)
    {
        blkdat.SetPos(nRewind); // Note: setPos does NOT simply overwrite the var returned by GetPos()!!
        nRewind++;              // start one byte further next time, in case of failure
        blkdat.SetLimit();      // remove former limit

        try
        {
            unsigned char buf[MESSAGE_START_SIZE];
            blkdat.FindByte(Params().MessageStart()[0]); //throws upon eof
            nRewind = blkdat.GetPos()+1;
            blkdat >> FLATDATA(buf);
            if (memcmp(buf, Params().MessageStart(), MESSAGE_START_SIZE))
                continue; // just first byte of magic number matches. Keep searching

            blkdat >> blkSize; // read size
            if (blkSize < 80 || blkSize > MAX_BLOCK_SIZE) {
                blkSize = -1;
                continue; // while whole magic number matches, it can't be block size. Keep searching
            }
        } catch (const std::exception&) {
            // no valid block header found; don't complain
            break;
        }
    }

    if (blkSize == -1)
        return res;

    //Here block has been found. Load it!
    unsigned int blkStartPos = blkdat.GetPos();
    if (pLastLoadedBlkPos != nullptr) pLastLoadedBlkPos->nPos = blkStartPos;
    blkdat.SetLimit(blkStartPos + blkSize);
    blkdat.SetPos(blkStartPos);
    try {
        blkdat >> res;
    } catch (const std::exception& e) {
        LogPrintf("%s: Deserialize or I/O error - %s\n", __func__, e.what());
    }

    blkdat.SetPos(blkdat.GetPos()); // Note: setPos does NOT simply overwrite the var returned by GetPos()!!
    return res;
}

bool LoadBlocksFromExternalFile(FILE* fileIn, CDiskBlockPos *dbp, bool loadHeadersOnly)
{
    const CChainParams& chainparams = Params();
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    int nLoadedHeaders = 0;
    int nLoadedBlocks = 0;

    try
    {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2*MAX_BLOCK_SIZE, MAX_BLOCK_SIZE+8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof())
        {
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[MESSAGE_START_SIZE];
                blkdat.FindByte(Params().MessageStart()[0]);
                nRewind = blkdat.GetPos()+1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, Params().MessageStart(), MESSAGE_START_SIZE))
                    continue; //only first byte of magic number matches. Keep searching...
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > MAX_BLOCK_SIZE)
                    continue; //magic number matches but size can't be block one. Keep searching...
            } catch (const std::exception&) {
                // no valid block header found; don't complain
                break;
            }
            try
            {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                CBlock loadedBlk;
                blkdat >> loadedBlk;
                nRewind = blkdat.GetPos();
                // detect out of order blocks, and store them for later
                uint256 hash = loadedBlk.GetHash();
                if (hash != chainparams.GetConsensus().hashGenesisBlock && mapBlockIndex.find(loadedBlk.hashPrevBlock) == mapBlockIndex.end()) {
                    LogPrint("reindex", "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(),
                            loadedBlk.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(loadedBlk.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                if (mapBlockIndex.count(hash) == 0 || (mapBlockIndex[hash]->nStatus & BLOCK_HAVE_DATA) == 0)
                {
                    CValidationState state;
                    if (loadHeadersOnly)
                    {
                        if (AcceptBlockHeader(loadedBlk, state, /*ppindex*/nullptr, /*lookForwardTips*/false)) //Todo: verify lookForwardTips
                            ++nLoadedHeaders;

                        if (state.IsError())
                            break;
                    } else
                    {
                        if (ProcessNewBlock(state, NULL, &loadedBlk, true, dbp))
                            nLoadedBlocks++;

                        if (state.IsError())
                            break;
                    }
                } else if (hash != chainparams.GetConsensus().hashGenesisBlock && mapBlockIndex[hash]->nHeight % 1000 == 0) {
                    LogPrintf("Block Import: already had block %s at height %d\n", hash.ToString(), mapBlockIndex[hash]->nHeight);
                }

                // Breath-first process earlier encountered successors of this block
                deque<uint256> queue{hash};
                do
                {
                    uint256 head = queue.front();
                    queue.pop_front();
                    auto range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second)
                    {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        if (ReadBlockFromDisk(loadedBlk, it->second))
                        {
                            CValidationState dummy;
                            if (loadHeadersOnly)
                            {
                                LogPrintf("%s: Processing out of order header, child %s of %s\n", __func__, loadedBlk.GetHash().ToString(),
                                        head.ToString());
                                if (AcceptBlockHeader(loadedBlk, dummy, /*ppindex*/nullptr, /*lookForwardTips*/false))
                                { //Todo: verify lookForwardTips and correctness of not breaking up
                                    nLoadedHeaders++;
                                    queue.push_back(loadedBlk.GetHash());
                                }
                            } else {
                                LogPrintf("%s: Processing out of order block, child %s of %s\n", __func__, loadedBlk.GetHash().ToString(),
                                        head.ToString());

                                //Todo: verify that issue on Process Block does not cause whole stop as before
                                if (ProcessNewBlock(dummy, NULL, &loadedBlk, true, &it->second))
                                {
                                    nLoadedBlocks++;
                                    queue.push_back(loadedBlk.GetHash());
                                }
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                    }
                } while (!queue.empty());
            } catch (const std::exception& e) {
                LogPrintf("%s: Deserialize or I/O error - %s\n", __func__, e.what());
            }
        }
    } catch (const std::runtime_error& e) {
        AbortNode(std::string("System error: ") + e.what());
    }

    if (nLoadedBlocks > 0)
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoadedBlocks, GetTimeMillis() - nStart);
    return (loadHeadersOnly && (nLoadedHeaders > 0)) || (!loadHeadersOnly && (nLoadedBlocks > 0));
}

void static CheckBlockIndex()
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    if (!fCheckBlockIndex) {
        return;
    }

    LOCK(cs_main);

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain.  (A few of the tests when
    // iterating the block tree require that chainActive has been initialized.)
    if (fReindex &&(chainActive.Height() < 0))
    {
        assert(mapBlockIndex.size() <= 1);
        return;
    }

    // fReindexFast loads all headers first, hence no assert on mapBlockIndex size
    if (fReindexFast && (chainActive.Height() < 0))
        return;

    // Build forward-pointing map of the entire block tree.
    std::multimap<CBlockIndex*,CBlockIndex*> forward;
    for(BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); ++it)
        forward.insert(std::make_pair(it->second->pprev, it->second));

    assert(forward.size() == mapBlockIndex.size()); //would fail if same CBlockIndex* is mapped to two different hashes

    std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator, std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangeGenesis = forward.equal_range(NULL);
    CBlockIndex *pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent NULL.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex* pindexFirstInvalid = NULL; // Oldest ancestor of pindex which is invalid.
    CBlockIndex* pindexFirstMissing = NULL; // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex* pindexFirstNeverProcessed = NULL; // Oldest ancestor of pindex for which nTx == 0.
    CBlockIndex* pindexFirstNotTreeValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex* pindexFirstNotTransactionsValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS (regardless of being valid or not).
    CBlockIndex* pindexFirstNotChainValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex* pindexFirstNotScriptsValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).
    while (pindex != NULL) {
        nNodes++;
        if (pindexFirstInvalid == NULL && pindex->nStatus & BLOCK_FAILED_VALID) pindexFirstInvalid = pindex;
        if (pindexFirstMissing == NULL && !(pindex->nStatus & BLOCK_HAVE_DATA)) pindexFirstMissing = pindex;
        if (pindexFirstNeverProcessed == NULL && pindex->nTx == 0) pindexFirstNeverProcessed = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTreeValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTransactionsValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TRANSACTIONS) pindexFirstNotTransactionsValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotChainValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) pindexFirstNotChainValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotScriptsValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (pindex->pprev == NULL) {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == consensusParams.hashGenesisBlock); // Genesis block's hash must match.
            assert(pindex == chainActive.Genesis()); // The current active chain's genesis block must be this block.
        }
        if (pindex->nChainTx == 0) assert(pindex->nSequenceId == 0);  // nSequenceId can't be set for blocks that aren't linked
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or not pruning has occurred).
        // HAVE_DATA is only equivalent to nTx > 0 (or VALID_TRANSACTIONS) if no pruning has occurred.
        if (!fHavePruned) {
            // If we've never pruned, then HAVE_DATA should be equivalent to nTx > 0
            assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
            assert(pindexFirstMissing == pindexFirstNeverProcessed);
        } else {
            // If we have pruned, then we can only say that HAVE_DATA implies nTx > 0
            if (pindex->nStatus & BLOCK_HAVE_DATA) assert(pindex->nTx > 0);
        }
        if (pindex->nStatus & BLOCK_HAVE_UNDO) assert(pindex->nStatus & BLOCK_HAVE_DATA);
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0)); // This is pruning-independent.
        // All parents having had data (at some point) is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to nChainTx being set.
        assert((pindexFirstNeverProcessed != NULL) == (pindex->nChainTx == 0)); // nChainTx != 0 is used to signal that all parent blocks have been processed (but may have been pruned).
        assert((pindexFirstNotTransactionsValid != NULL) == (pindex->nChainTx == 0));
        assert(pindex->nHeight == nHeight); // nHeight must be consistent.
        assert(pindex->pprev == NULL || pindex->nChainWork >= pindex->pprev->nChainWork); // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight))); // The pskip pointer must point back for all but the first 2 blocks.
        assert(pindexFirstNotTreeValid == NULL); // All mapBlockIndex entries must at least be TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) assert(pindexFirstNotTreeValid == NULL); // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) assert(pindexFirstNotChainValid == NULL); // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) assert(pindexFirstNotScriptsValid == NULL); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (pindexFirstInvalid == NULL) {
            // Checks for not-invalid blocks.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0); // The failed mask cannot be set for blocks without invalid parents.
        }
        if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && pindexFirstNeverProcessed == NULL) {
            if (pindexFirstInvalid == NULL) {
                // If this block sorts at least as good as the current tip and
                // is valid and we have all data for its parents, it must be in
                // setBlockIndexCandidates.  chainActive.Tip() must also be there
                // even if some data has been pruned.
                if (pindexFirstMissing == NULL || pindex == chainActive.Tip()) {
                    // LogPrintf("net","ASSERT============>%x  but  %x\n", pindex->phashBlock, chainActive.Tip()->phashBlock);
                    assert(setBlockIndexCandidates.count(pindex));
                }
                // If some parent is missing, then it could be that this block was in
                // setBlockIndexCandidates but had to be removed because of the missing data.
                // In this case it must be in mapBlocksUnlinked -- see test below.
            }
        } else { // If this block sorts worse than the current tip or some ancestor's block has never been seen, it cannot be in setBlockIndexCandidates.
            assert(setBlockIndexCandidates.count(pindex) == 0);
        }
        // Check whether this block is in mapBlocksUnlinked.
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator, std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed != NULL && pindexFirstInvalid == NULL) {
            // If this block has block data available, some parent was never received, and has no invalid parents, it must be in mapBlocksUnlinked.
            assert(foundInUnlinked);
        }
        if (!(pindex->nStatus & BLOCK_HAVE_DATA)) assert(!foundInUnlinked); // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
        if (pindexFirstMissing == NULL) assert(!foundInUnlinked); // We aren't missing data for any parent -- cannot be in mapBlocksUnlinked.
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed == NULL && pindexFirstMissing != NULL) {
            // We HAVE_DATA for this block, have received data for all parents at some point, but we're currently missing data for some parent.
            assert(fHavePruned); // We must have pruned.
            // This block may have entered mapBlocksUnlinked if:
            //  - it has a descendant that at some point had more work than the
            //    tip, and
            //  - we tried switching to that descendant but were missing
            //    data for some intermediate block between chainActive and the
            //    tip.
            // So if this block is itself better than chainActive.Tip() and it wasn't in
            // setBlockIndexCandidates, then it must be in mapBlocksUnlinked.
            if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && setBlockIndexCandidates.count(pindex) == 0) {
                if (pindexFirstInvalid == NULL) {
                    assert(foundInUnlinked);
                }
            }
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // End: actual consistency checks.

        // Try descending into the first subnode.
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator, std::multimap<CBlockIndex*,CBlockIndex*>::iterator> range = forward.equal_range(pindex);
        if (range.first != range.second) {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex) {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid) pindexFirstInvalid = NULL;
            if (pindex == pindexFirstMissing) pindexFirstMissing = NULL;
            if (pindex == pindexFirstNeverProcessed) pindexFirstNeverProcessed = NULL;
            if (pindex == pindexFirstNotTreeValid) pindexFirstNotTreeValid = NULL;
            if (pindex == pindexFirstNotTransactionsValid) pindexFirstNotTransactionsValid = NULL;
            if (pindex == pindexFirstNotChainValid) pindexFirstNotChainValid = NULL;
            if (pindex == pindexFirstNotScriptsValid) pindexFirstNotScriptsValid = NULL;
            // Find our parent.
            CBlockIndex* pindexPar = pindex->pprev;
            // Find which child we just visited.
            std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator, std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex) {
                assert(rangePar.first != rangePar.second); // Our parent must have at least the node we're coming from as child.
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            } else {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}

std::string GetWarnings(const std::string& strFor)
{
    string strStatusBar;
    string strRPC;

    if (!CLIENT_VERSION_IS_RELEASE)
        strStatusBar = _("This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");

    if (GetBoolArg("-testsafemode", false))
        strStatusBar = strRPC = "testsafemode enabled";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "")
    {
        strStatusBar = strMiscWarning;
    }

    if (fLargeWorkForkFound)
    {
        strStatusBar = strRPC = _("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
    }
    else if (fLargeWorkInvalidChainFound)
    {
        strStatusBar = strRPC = _("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
    }

    if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings(): invalid parameter");
    return "error";
}








//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool static AlreadyHave(const CInv& inv) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    switch (inv.type)
    {
    case MSG_TX:
        {
            assert(recentRejects);
            if (chainActive.Tip()->GetBlockHash() != hashRecentRejectsChainTip)
            {
                // If the chain tip has changed previously rejected transactions
                // might be now valid, e.g. due to a nLockTime'd tx becoming valid,
                // or a double-spend. Reset the rejects filter and give those
                // txs a second chance.
                hashRecentRejectsChainTip = chainActive.Tip()->GetBlockHash();
                recentRejects->reset();
            }

            return recentRejects->contains(inv.hash) ||
                   mempool.exists(inv.hash) ||
                   mapOrphanTransactions.count(inv.hash) ||
                   pcoinsTip->HaveCoins(inv.hash);
        }
        case MSG_BLOCK:
        {
            return mapBlockIndex.count(inv.hash);
        }
    }
    // Don't know what it is, just say we already got one
    return true;
}

void static ProcessGetData(CNode* pfrom)
{
    std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();

    vector<CInv> vNotFound;

    LOCK(cs_main);

    while (it != pfrom->vRecvGetData.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        const CInv &inv = *it;
        {
            boost::this_thread::interruption_point();
            it++;

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK)
            {
                bool send = false;
                BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
                if (mi != mapBlockIndex.end())
                {
                    if (chainActive.Contains(mi->second)) {
                        send = true;
                    } else {
                        static const int nOneMonth = 30 * 24 * 60 * 60;
                        // To prevent fingerprinting attacks, only send blocks outside of the active
                        // chain if they are valid, and no more than a month older (both in time, and in
                        // best equivalent proof of work) than the best header chain we know about.

                        // this is set by ConnectBlock method, when a new tip is added to the main chain
                        bool b1 = mi->second->IsValid(BLOCK_VALID_SCRIPTS);
                        bool b2 = (pindexBestHeader != NULL) &&
                                  (pindexBestHeader->GetBlockTime() - mi->second->GetBlockTime() < nOneMonth) &&
                                  (GetBlockProofEquivalentTime(*pindexBestHeader, *mi->second, *pindexBestHeader, Params().GetConsensus()) < nOneMonth);

                        send = b1 && b2;
                        if (!send)
                        {
                            if (b2)
                            {
                                // BLOCK_VALID_SCRIPTS is set when connecting block on main chain, but we must
                                // propagate also when relevant blocks are on a fork. Consider that a further check
                                // on BLOCK_HAVE_DATA is performed below
                                LogPrint("forks", "%s():%d: request from peer=%i: status[0x%x]\n",
                                    __func__, __LINE__, pfrom->GetId(), mi->second->nStatus);
                                send = true;
                            }
                            else
                            {
                                LogPrint("forks", "%s():%d: ignoring request from peer=%i: %s status[0x%x]\n",
                                    __func__, __LINE__, pfrom->GetId(), inv.hash.ToString(), mi->second->nStatus);
                            }
                        }
                    }
                }
                // Pruned nodes may have deleted the block, so check whether
                // it's available before trying to send.
                if (send && (mi->second->nStatus & BLOCK_HAVE_DATA))
                {
                    // Send block from disk
                    CBlock block;
                    if (!ReadBlockFromDisk(block, (*mi).second))
                        assert(!"cannot load block from disk");
                    if (inv.type == MSG_BLOCK)
                    {
                        LogPrint("forks", "%s():%d - Pushing block [%s]\n", __func__, __LINE__, block.GetHash().ToString() );
                        pfrom->PushMessage("block", block);
                    }
                    else // MSG_FILTERED_BLOCK)
                    if (inv.type == MSG_FILTERED_BLOCK)
                    {
                        LOCK(pfrom->cs_filter);
                        if (pfrom->pfilter)
                        {
                            CMerkleBlock merkleBlock(block, *pfrom->pfilter);
                            pfrom->PushMessage("merkleblock", merkleBlock);
                            // CMerkleBlock just contains hashes, so also push any transactions/certs in the block the client did not see
                            // This avoids hurting performance by pointlessly requiring a round-trip
                            // Note that there is currently no way for a node to request any single transactions we didn't send here -
                            // they must either disconnect and retry or request the full block.
                            // Thus, the protocol spec specified allows for us to provide duplicate txn here,
                            // however we MUST always provide at least what the remote peer needs
                            typedef std::pair<unsigned int, uint256> PairType;
                            BOOST_FOREACH(PairType& pair, merkleBlock.vMatchedTxn)
                            {
                                unsigned int pos = pair.first;
                                if (pos < block.vtx.size() )
                                {
                                    if (!pfrom->setInventoryKnown.count(CInv(MSG_TX, pair.second)))
                                        pfrom->PushMessage("tx", block.vtx[pos]);
                                }
                                else
                                if ( pos < (block.vcert.size() + block.vtx.size()) )
                                {
                                    if (!pfrom->setInventoryKnown.count(CInv(MSG_TX, pair.second)))
                                    {
                                        unsigned int offset = pos - block.vtx.size();
                                        pfrom->PushMessage("tx", block.vcert[offset]);
                                    }
                                }
                                else
                                {
                                    LogPrintf("%s():%d -  tx index out of range=%d, can not handle merkle block\n", __func__, __LINE__, pos);
                                }
                            }
                        }
                        // else
                            // no response
                    }
                    else
                    {
                        LogPrint("cert", "%s():%d - inv.type=%d\n", __func__, __LINE__, inv.type);
                    }

                    // Trigger the peer node to send a getblocks request for the next batch of inventory
                    if (inv.hash == pfrom->hashContinue)
                    {
                        // Bypass PushInventory, this must send even if redundant,
                        // and we want it right after the last block so they don't
                        // wait for other stuff first.
                        vector<CInv> vInv;
                        vInv.push_back(CInv(MSG_BLOCK, chainActive.Tip()->GetBlockHash()));
                        LogPrint("forks", "%s():%d - Pushing inv\n", __func__, __LINE__);
                        pfrom->PushMessage("inv", vInv);
                        pfrom->hashContinue.SetNull();
                    }
                }
                else
                {
                    if (send && !(mi->second->nStatus & BLOCK_HAVE_DATA) )
                    {
                        LogPrint("forks", "%s():%d - NOT Pushing incomplete block [%s]\n", __func__, __LINE__, inv.hash.ToString() );
                    }
                }
            }
            else if (inv.IsKnownType())
            {
                // Send stream from relay memory
                bool pushed = false;
                {
                    LOCK(cs_mapRelay);
                    map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
                    if (mi != mapRelay.end()) {
                        pfrom->PushMessage(inv.GetCommand(), (*mi).second);
                        pushed = true;
                    }
                }
                if (!pushed && inv.type == MSG_TX) {
                    CTransaction tx;
                    if (mempool.lookup(inv.hash, tx)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << tx;
                        LogPrint("cert", "%s():%d - pushing tx\n", __func__, __LINE__);
                        pfrom->PushMessage("tx", ss);
                        pushed = true;
                    }
                    else
                    {
                        CScCertificate cert;
                        if (mempool.lookup(inv.hash, cert)) {
                            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                            ss.reserve(1000);
                            ss << cert;
                            LogPrint("cert", "%s():%d - pushing certificate\n", __func__, __LINE__);
                            pfrom->PushMessage("tx", ss);
                            pushed = true;
                        }
                    }
                }
                if (!pushed) {
                    vNotFound.push_back(inv);
                }
            }

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK)
                break;
        }
    }

    pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

    if (!vNotFound.empty()) {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        pfrom->PushMessage("notfound", vNotFound);
    }
}

void ProcessMempoolMsg(const CTxMemPool& pool, CNode* pfrom)
{
    LOCK2(cs_main, pfrom->cs_filter);

    std::vector<uint256> vtxid;
    pool.queryHashes(vtxid);
    vector<CInv> vInv;
    for(uint256& hash: vtxid)
    {
        CInv inv(MSG_TX, hash);
        std::unique_ptr<CTransactionBase> mempoolObjPtr{};
        bool fInMemPool = false;

        if (pool.existsTx(hash))
        {
            CTransaction* txPtr = new CTransaction{};
            fInMemPool = pool.lookup(hash, *txPtr);
            mempoolObjPtr.reset(txPtr);
        } else if (pool.existsCert(hash))
        {
            CScCertificate* certPtr = new CScCertificate{};
            fInMemPool = pool.lookup(hash, *certPtr);
            mempoolObjPtr.reset(certPtr);
        }

        if (!fInMemPool) continue; // another thread removed since queryHashes, maybe...

        if ((pfrom->pfilter && pfrom->pfilter->IsRelevantAndUpdate(*mempoolObjPtr)) || (!pfrom->pfilter))
        {
            vInv.push_back(inv);
        }

        if (vInv.size() == MAX_INV_SZ)
        {
            pfrom->PushInvs("inv", vInv);
            vInv.clear();
        }
    }

    if (vInv.size() > 0)
        pfrom->PushInvs("inv", vInv);

    return;
}

void ProcessTxBaseAcceptToMemoryPool(const CTransactionBase& txBase, CNode* pfrom, BatchVerificationStateFlag proofVerificationState, CValidationState& state)
{
    if (proofVerificationState == BatchVerificationStateFlag::FAILED)
    {
        state.DoS(100, error("%s():%d - cert proof failed to verify", __func__, __LINE__),
                  CValidationState::Code::INVALID_PROOF, "bad-sc-cert-proof");

        RejectMemoryPoolTxBase(state, txBase, pfrom);

        return;
    }

    LOCK(cs_main);

    MempoolProofVerificationFlag verificationFlag = proofVerificationState == BatchVerificationStateFlag::NOT_VERIFIED_YET ?
                                                    MempoolProofVerificationFlag::ASYNC : MempoolProofVerificationFlag::DISABLED;

    MempoolReturnValue res = AcceptTxBaseToMemoryPool(mempool, state, txBase,
                                                      LimitFreeFlag::ON,
                                                      RejectAbsurdFeeFlag::OFF,
                                                      verificationFlag,
                                                      pfrom);

    if (res == MempoolReturnValue::VALID)
    {
        mempool.check(pcoinsTip);
        txBase.Relay();
        std::vector<uint256> vWorkQueue{txBase.GetHash()};
        std::vector<uint256> vEraseQueue;

        LogPrint("mempool", "%s(): peer=%d %s: accepted %s (poolsz %u)\n", __func__,
            pfrom->id, pfrom->cleanSubVer,
            txBase.GetHash().ToString(),
            mempool.size());

        // Recursively process any orphan transactions that depended on this one
        set<NodeId> setMisbehaving;
        for (unsigned int i = 0; i < vWorkQueue.size(); i++)
        {
            map<uint256, set<uint256> >::iterator itByPrev = mapOrphanTransactionsByPrev.find(vWorkQueue[i]);
            if (itByPrev == mapOrphanTransactionsByPrev.end())
                continue;
            for (const uint256& orphanHash: itByPrev->second)
            {
                const CTransactionBase& orphanTx = *mapOrphanTransactions[orphanHash].tx;
                NodeId fromPeer = mapOrphanTransactions[orphanHash].fromPeer;
                bool fMissingInputs2 = false;
                // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                // anyone relaying LegitTxX banned)
                CValidationState stateDummy;

                if (setMisbehaving.count(fromPeer))
                    continue;

                MempoolReturnValue resOrphan = AcceptTxBaseToMemoryPool(mempool, stateDummy, orphanTx,
                            LimitFreeFlag::ON,RejectAbsurdFeeFlag::OFF, MempoolProofVerificationFlag::ASYNC, pfrom);
                if (resOrphan == MempoolReturnValue::VALID)
                {
                    LogPrint("mempool", "   accepted orphan tx %s\n", orphanHash.ToString());
                    orphanTx.Relay();
                    vWorkQueue.push_back(orphanHash);
                    vEraseQueue.push_back(orphanHash);
                }
                else if (resOrphan == MempoolReturnValue::INVALID)
                {
                    if (stateDummy.IsInvalid() && stateDummy.GetDoS() > 0)
                    {
                        // Punish peer that gave us an invalid orphan tx
                        Misbehaving(fromPeer, stateDummy.GetDoS());
                        setMisbehaving.insert(fromPeer);
                        LogPrint("mempool", "   invalid orphan tx %s\n", orphanHash.ToString());
                    }
                    // Has inputs but not accepted to mempool
                    // Probably non-standard or insufficient fee/priority
                    LogPrint("mempool", "   removed orphan tx %s\n", orphanHash.ToString());
                    vEraseQueue.push_back(orphanHash);
                    assert(recentRejects);
                    recentRejects->insert(orphanHash);
                }
                else if (resOrphan == MempoolReturnValue::PARTIALLY_VALIDATED)
                {
                    vEraseQueue.push_back(orphanHash);
                }
                mempool.check(pcoinsTip);
            }
        }

            for(const uint256& hash: vEraseQueue)
            EraseOrphanTx(hash);
    }
    // TODO: currently, prohibit joinsplits from entering mapOrphans
    else if (res == MempoolReturnValue::MISSING_INPUT && txBase.GetVjoinsplit().size() == 0)
    {
        AddOrphanTx(txBase, pfrom->GetId());

        // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
        unsigned int nMaxOrphanTx = (unsigned int)std::max((int64_t)0, GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
        unsigned int nEvicted = LimitOrphanTxSize(nMaxOrphanTx);
        if (nEvicted > 0)
            LogPrint("mempool", "mapOrphan overflow, removed %u tx\n", nEvicted);
    }
}

void ProcessTxBaseMsg(const CTransactionBase& txBase, CNode* pfrom)
{
    CInv inv(MSG_TX, txBase.GetHash());
    pfrom->AddInventoryKnown(inv);

    LOCK(cs_main);

    pfrom->setAskFor.erase(inv.hash);
    mapAlreadyAskedFor.erase(inv);
    mapAlreadyReceived.insert(std::make_pair(inv, GetTimeMicros()));

    MempoolReturnValue res = MempoolReturnValue::INVALID;
    CValidationState state;

    if (!AlreadyHave(inv))
    {
        BatchVerificationStateFlag flag = BatchVerificationStateFlag::NOT_VERIFIED_YET;

        // CODE USED FOR UNIT TEST ONLY [Start]
        if (BOOST_UNLIKELY(Params().NetworkIDString() == "regtest" && GetBoolArg("-skipscproof", false)))
        {
            flag = BatchVerificationStateFlag::VERIFIED;
        }
        // CODE USED FOR UNIT TEST ONLY [End]

        ProcessTxBaseAcceptToMemoryPool(txBase, pfrom, flag, state);
    }
    else
    {
        assert(recentRejects);
        recentRejects->insert(txBase.GetHash());

        if (pfrom->fWhitelisted)
        {
            // Always relay transactions received from whitelisted peers, even
            // if they were already in the mempool or rejected from it due
            // to policy, allowing the node to function as a gateway for
            // nodes hidden behind it.
            //
            // Never relay transactions that we would assign a non-zero DoS
            // score for, as we expect peers to do the same with us in that
            // case.
            if (!state.IsInvalid() || state.GetDoS() == 0)
            {
                LogPrintf("Force relaying tx %s from whitelisted peer=%d\n", txBase.GetHash().ToString(), pfrom->id);
                txBase.Relay();
            }
            else
            {
                LogPrintf("Not relaying invalid transaction %s from whitelisted peer=%d (%s (code %d))\n",
                    txBase.GetHash().ToString(), pfrom->id, state.GetRejectReason(), CValidationState::CodeToChar(state.GetRejectCode()));
            }
        }
    }

    if (state.IsInvalid())
    {
        RejectMemoryPoolTxBase(state, txBase, pfrom);
    }
}

bool static ProcessMessage(CNode* pfrom, string strCommand, CDataStream& vRecv, int64_t nTimeReceived)
{
    const CChainParams& chainparams = Params();
    LogPrint("net", "%s() - received: %s (%u bytes) peer=%d\n", __func__, SanitizeString(strCommand), vRecv.size(), pfrom->id);
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    if (strCommand == "version")
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->PushMessage("reject", strCommand, CValidationState::CodeToChar(CValidationState::Code::DUPLICATED), string("Duplicate version message"));
            Misbehaving(pfrom->GetId(), 1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;
        if (pfrom->nVersion < MIN_PEER_PROTO_VERSION)
        {
            // disconnect from peers older than this proto version
            LogPrintf("peer=%d using obsolete version %i; disconnecting\n", pfrom->id, pfrom->nVersion);
            pfrom->PushMessage("reject", strCommand, CValidationState::CodeToChar(CValidationState::Code::OBSOLETE),
                               strprintf("Version must be %d or greater", MIN_PEER_PROTO_VERSION));
            pfrom->fDisconnect = true;
            return false;
        }

        if (pfrom->nVersion == 10300)
            pfrom->nVersion = 300;
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty()) {
            vRecv >> LIMITED_STRING(pfrom->strSubVer, 256);
            pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);
        }
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;
        if (!vRecv.empty())
            vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
        else
            pfrom->fRelayTxes = true;

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            LogPrintf("connected to self at %s, disconnecting\n", pfrom->addr.ToString());
            pfrom->fDisconnect = true;
            return true;
        }

        pfrom->addrLocal = addrMe;
        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        // Potentially mark this peer as a preferred download peer.
        UpdatePreferredDownload(pfrom, State(pfrom->GetId()));

        // Change version
        pfrom->PushMessage("verack");
        pfrom->ssSend.SetVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (fListen && !IsInitialBlockDownload())
            {
                CAddress addr = GetLocalAddress(&pfrom->addr);
                if (addr.IsRoutable())
                {
                    LogPrintf("ProcessMessages: advertizing address %s\n", addr.ToString());
                    pfrom->PushAddress(addr);
                } else if (IsPeerAddrLocalGood(pfrom)) {
                    addr.SetIP(pfrom->addrLocal);
                    LogPrintf("ProcessMessages: advertizing address %s\n", addr.ToString());
                    pfrom->PushAddress(addr);
                }
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000)
            {
                pfrom->PushMessage("getaddr");
                pfrom->fGetAddr = true;
                // When requesting a getaddr, accept an additional MAX_ADDR_TO_SEND addresses in response
                // (bypassing the MAX_ADDR_PROCESSING_TOKEN_BUCKET limit).
                pfrom->m_addr_token_bucket += MAX_ADDR_TO_SEND;
            }
            addrman.Good(pfrom->addr);
        } else {
            if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
            {
                addrman.Add(addrFrom, addrFrom);
                addrman.Good(addrFrom);
            }
        }

        pfrom->fSuccessfullyConnected = true;

        string remoteAddr;
        if (fLogIPs)
            remoteAddr = ", peeraddr=" + pfrom->addr.ToString();

        LogPrintf("receive version message: %s: version %d, blocks=%d, us=%s, peer=%d%s\n",
                  pfrom->cleanSubVer, pfrom->nVersion,
                  pfrom->nStartingHeight, addrMe.ToString(), pfrom->id,
                  remoteAddr);

        pfrom->nTimeOffset = timeWarning.AddTimeData(pfrom->addr, nTime, GetTime());
    }


    else if (pfrom->nVersion == 0)
    {
        // Must have a version message before anything else
        Misbehaving(pfrom->GetId(), 1);
        return false;
    }


    else if (strCommand == "verack")
    {
        pfrom->SetRecvVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        // Mark this node as currently connected, so we update its timestamp later.
        if (pfrom->fNetworkNode) {
            LOCK(cs_main);
            State(pfrom->GetId())->fCurrentlyConnected = true;
        }
    }


    else if (strCommand == "addr")
    {
        vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000)
        {
            Misbehaving(pfrom->GetId(), 20);
            return error("message addr size() = %u", vAddr.size());
        }

        // Store the new addresses
        vector<CAddress> vAddrOk;
        int64_t nNow = GetTime();
        int64_t nSince = nNow - 10 * 60;

        // Update/increment addr rate limiting bucket.
        const int64_t current_time = GetTimeMicros();
        if (pfrom->m_addr_token_bucket < MAX_ADDR_PROCESSING_TOKEN_BUCKET) {
            // Don't increment bucket if it's already full
            const int64_t time_diff = std::max(current_time - pfrom->m_addr_token_timestamp, (int64_t)0);
            const double increment = time_diff * MAX_ADDR_RATE_PER_SECOND / 1000000.0;
            pfrom->m_addr_token_bucket = std::min<double>(pfrom->m_addr_token_bucket + increment, MAX_ADDR_PROCESSING_TOKEN_BUCKET);
        }
        pfrom->m_addr_token_timestamp = current_time;

        uint64_t num_proc = 0;
        uint64_t num_rate_limit = 0;
        std::shuffle(vAddr.begin(), vAddr.end(), std::mt19937(std::random_device{}()));

        BOOST_FOREACH(CAddress& addr, vAddr)
        {
            boost::this_thread::interruption_point();

            static constexpr bool rate_limited = true;
             // Apply rate limiting.
            if (pfrom->m_addr_token_bucket < 1.0) {
                if (rate_limited) {
                    ++num_rate_limit;
                    continue;
                }
            }
            else {
                pfrom->m_addr_token_bucket -= 1.0;
            }

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            ++num_proc;
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vNodes);
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the addrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(hashSalt) ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60)));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    multimap<uint256, CNode*> mapMix;
                    BOOST_FOREACH(CNode* pnode, vNodes)
                    {
                        if (pnode->nVersion < CADDR_TIME_VERSION)
                            continue;
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = ArithToUint256(UintToArith256(hashRand) ^ nPointer);
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.insert(make_pair(hashKey, pnode));
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (multimap<uint256, CNode*>::iterator mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        ((*mi).second)->PushAddress(addr);
                }
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        CNodeState *nodestate = State(pfrom->GetId());
        pfrom->m_addr_processed += num_proc;
        pfrom->m_addr_rate_limited += num_rate_limit;
        LogPrint("net", "Received addr: %u addresses (%u processed, %u rate-limited) from peer=%d%s\n",
                 vAddr.size(),
                 num_proc,
                 num_rate_limit,
                 pfrom->GetId(),
                 fLogIPs ? ", peeraddr=" + pfrom->addr.ToString() : "");

        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->fDisconnect = true;
    }


    else if (strCommand == "inv")
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            Misbehaving(pfrom->GetId(), 20);
            return error("message inv size() = %u", vInv.size());
        }

        LOCK(cs_main);

        std::vector<CInv> vToFetch;

        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            const CInv &inv = vInv[nInv];

            boost::this_thread::interruption_point();
            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(inv);
            LogPrint("net", "got inv: %s  %s peer=%d,%d/%d\n",
                inv.ToString(), fAlreadyHave ? "have" : "new", pfrom->id, (nInv+1), vInv.size());

            if (!fAlreadyHave && !fImporting && !fReindex && !fReindexFast && inv.type != MSG_BLOCK)
                pfrom->AskFor(inv);

            if (inv.type == MSG_BLOCK) {
                UpdateBlockAvailability(pfrom->GetId(), inv.hash);
                if (!fAlreadyHave && !fImporting && !fReindex && !fReindexFast && !mapBlocksInFlight.count(inv.hash)) {
                    // First request the headers preceding the announced block. In the normal fully-synced
                    // case where a new block is announced that succeeds the current tip (no reorganization),
                    // there are no such headers.
                    // Secondly, and only when we are close to being synced, we request the announced block directly,
                    // to avoid an extra round-trip. Note that we must *first* ask for the headers, so by the
                    // time the block arrives, the header chain leading up to it is already validated. Not
                    // doing this will result in the received block being rejected as an orphan in case it is
                    // not a direct successor.

                    // add fork tips to the locator, they will be used by peer in case we need updating a fork
                    CBlockLocator bl = chainActive.GetLocator(pindexBestHeader);

                    if (mGlobalForkTips.size() > 1)
                    {
                        std::vector<uint256> vOutput;
                        getMostRecentGlobalForkTips(vOutput);

                        BOOST_FOREACH(const uint256& hash, vOutput)
                        {
                            std::vector<uint256>::iterator b = bl.vHave.begin();
                            LogPrint("forks", "%s():%d - adding tip hash [%s]\n", __func__, __LINE__, hash.ToString());
                            bl.vHave.insert(b, hash);
                        }
                    }
                    pfrom->PushMessage("getheaders", bl, inv.hash);
                    CNodeState *nodestate = State(pfrom->GetId());
                    if (chainActive.Tip()->GetBlockTime() > GetTime() - chainparams.GetConsensus().nPowTargetSpacing * 20 &&
                        nodestate->nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                        vToFetch.push_back(inv);
                        // Mark block as in flight already, even though the actual "getdata" message only goes out
                        // later (within the same cs_main lock, though).
                        MarkBlockAsInFlight(pfrom->GetId(), inv.hash, chainparams.GetConsensus());
                    }
                    LogPrint("net", "%s():%d - getheaders (%d) %s to peer=%d\n",
                        __func__, __LINE__, pindexBestHeader->nHeight, inv.hash.ToString(), pfrom->id);
                }
                else
                {
                    if (mapBlocksInFlight.count(inv.hash) )
                    {
                        LogPrint("forks", "%s():%d - inv[%s] is in flight, skipping\n", __func__, __LINE__,
                            inv.hash.ToString());
                    }
                }
            }

            if (pfrom->nSendSize > (SendBufferSize() * 2)) {
                Misbehaving(pfrom->GetId(), 50);
                return error("send buffer size() = %u", pfrom->nSendSize);
            }
        }

        if (!vToFetch.empty())
        {
            LogPrint("forks", "%s():%d - Pushing getdata for %d entries:\n", __func__, __LINE__, vToFetch.size());
            pfrom->PushMessage("getdata", vToFetch);
        }
    }


    else if (strCommand == "getdata")
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            Misbehaving(pfrom->GetId(), 20);
            return error("message getdata size() = %u", vInv.size());
        }

        if (fDebug || (vInv.size() != 1))
            LogPrint("net", "received getdata (%u invsz) peer=%d\n", vInv.size(), pfrom->id);

        if ((fDebug && vInv.size() > 0) || (vInv.size() == 1))
        {
            BOOST_FOREACH(const CInv& ii, vInv) {
                LogPrint("net", "received getdata for: %s peer=%d\n", ii.ToString(), pfrom->id);
            }
        }

        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end());
        ProcessGetData(pfrom);
    }


    else if (strCommand == "getblocks")
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        // Find the last block the caller has in the main chain
        CBlockIndex* pindex = FindForkInGlobalIndex(chainActive, locator);

        // Send the rest of the chain
        if (pindex)
            pindex = chainActive.Next(pindex);
        int nLimit = 500;
        LogPrint("net", "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex))
        {
            if (pindex->GetBlockHash() == hashStop)
            {
                LogPrint("net", "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                break;
            }
            LogPrint("forks", "%s():%d - Node [%s] pushing inv\n", __func__, __LINE__, pfrom->addrName);
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0)
            {
                // When this block is requested, we'll send an inv that'll
                // trigger the peer to getblocks the next batch of inventory.
                LogPrint("net", "  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }


    else if (strCommand == "getheaders")
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        if (IsInitialBlockDownload())
            return true;

        CBlockIndex* pindexReference = NULL;
        bool onMain = getHeadersIsOnMain(locator, hashStop, &pindexReference);

        if (onMain)
        {
            CBlockIndex* pindex = NULL;
            if (locator.IsNull())
            {
                // If locator is null, return the hashStop block
                BlockMap::iterator mi = mapBlockIndex.find(hashStop);
                if (mi == mapBlockIndex.end())
                    return true;
                pindex = (*mi).second;
            }
            else
            {
                // Find the last block the caller has in the main chain
                pindex = FindForkInGlobalIndex(chainActive, locator);
                if (pindex)
                    pindex = chainActive.Next(pindex);
            }

            // we cannot use CBlockHeaders since it won't include the 0x00 nTx count at the end
            // we cannot use CBlock, since we added Certificates and its serialization is not backward compatible
            // We must use CBlockHeaderForNetwork, and ad-hoc class for this task
            vector<CBlockHeaderForNetwork> vHeaders;
            int nLimit = MAX_HEADERS_RESULTS;
            LogPrint("net", "getheaders from h(%d) to %s from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop.ToString(), pfrom->id);
            for (; pindex; pindex = chainActive.Next(pindex))
            {
                vHeaders.push_back(CBlockHeaderForNetwork(pindex->GetBlockHeader()) );
                if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                    break;
            }
            LogPrint("forks", "%s():%d - Pushing %d headers to node[%s]\n", __func__, __LINE__, vHeaders.size(), pfrom->addrName);
            pfrom->PushMessage("headers", vHeaders);
        }
        else
        {
            if(!pindexReference)
            {
                // should never happen
                LogPrint("forks", "%s():%d - reference not found\n", __func__, __LINE__ );
                return true;
            }

            if (hashStop != uint256() )
            {
                BlockMap::iterator mi = mapBlockIndex.find(hashStop);
                if (mi == mapBlockIndex.end() )
                {
                    // should never happen
                    LogPrint("forks", "%s():%d - block [%s] not found\n", __func__, __LINE__, hashStop.ToString() );
                    return true;
                }

                LogPrint("forks", "%s():%d - peer is not using chain active! Starting from %s at h(%d)\n",
                    __func__, __LINE__, pindexReference->GetBlockHash().ToString(), pindexReference->nHeight );

                std::deque<CBlockHeaderForNetwork> dHeadersAlternative;

                bool found = false;

                // the reference is the block which triggered the getheader request (the hashStop)
                while ( pindexReference )
                {
                    dHeadersAlternative.push_front(CBlockHeaderForNetwork(pindexReference->GetBlockHeader()));

                    BOOST_FOREACH(const uint256& hash, locator.vHave)
                    {
                        if (hash == pindexReference->GetBlockHash() )
                        {
                            // we found the tip passed along in locator, we must stop here
                            LogPrint("forks", "%s():%d - matched fork tip in locator [%s]\n",
                                __func__, __LINE__, hash.ToString() );
                            found = true;
                            break;
                        }
                    }

                    if (found || pindexReference->pprev == chainActive.Genesis() )
                    {
                        break;
                    }

                    pindexReference = pindexReference->pprev;
                }

                vector<CBlockHeaderForNetwork> vHeaders;
                int nLimit = MAX_HEADERS_RESULTS;
                // we are on a fork: fill the vector rewinding the deque so that we have the correct ordering
                LogPrint("forks", "%s():%d - Found %d headers to push to node[%s]:\n", __func__, __LINE__, dHeadersAlternative.size(), pfrom->addrName);
                for(const auto& cb : dHeadersAlternative) {
                    LogPrint("forks", "%s():%d -- [%s]\n", __func__, __LINE__, cb.GetHash().ToString() );
                    vHeaders.push_back(cb);
                    if (--nLimit <= 0)
                        break;
                }
                LogPrint("forks", "%s():%d - Pushing %d headers to node[%s]\n", __func__, __LINE__, vHeaders.size(), pfrom->addrName);
                pfrom->PushMessage("headers", vHeaders);
            }
            else
            {
                LogPrint("forks", "%s():%d - hashStop block is null\n", __func__, __LINE__);

                // this is the case when we just sent 160 headers, reference is the header which the last getheader
                // request has reached: more must be sent starting from this one
                std::set<const CBlockIndex*> sProcessed;
                std::vector<CBlockHeaderForNetwork> vHeadersMulti;
                int nLimit = MAX_HEADERS_RESULTS;

                int h = pindexReference->nHeight;

                LogPrint("forks", "%s():%d - Searching up to %s h(%d) from tips backwards\n",
                    __func__, __LINE__, pindexReference->GetBlockHash().ToString(), pindexReference->nHeight);

                // we must follow all forks backwards because we can not tell which is the concerned one
                // peer will discard headers already known if any
                BOOST_FOREACH(auto mapPair, mGlobalForkTips)
                {
                    const CBlockIndex* block = mapPair.first;
                    if (block == chainActive.Tip() || block == pindexBestHeader )
                    {
                        LogPrint("forks", "%s():%d - skipping tips\n", __func__, __LINE__);
                        continue;
                    }

                    std::deque<CBlockHeaderForNetwork> dHeadersAlternativeMulti;

                    LogPrint("forks", "%s():%d - tips %s h(%d)\n",
                        __func__, __LINE__, block->GetBlockHash().ToString(), block->nHeight);

                    while (block &&
                           block != pindexReference &&
                           block->nHeight >= h)
                    {
                        if (!sProcessed.count(block) )
                        {
                            LogPrint("forks", "%s():%d - adding %s h(%d)\n",
                                __func__, __LINE__, block->GetBlockHash().ToString(), block->nHeight);
                            dHeadersAlternativeMulti.push_front(CBlockHeaderForNetwork(block->GetBlockHeader()));
                            sProcessed.insert(block);
                        }
                        block = block->pprev;
                    }

                    if (block == pindexReference)
                    {
                        // we exited from the while loop with the right condition, therefore we must take this branch into account
                        LogPrint("forks", "%s():%d - found reference %s h(%d)\n",
                            __func__, __LINE__, block->GetBlockHash().ToString(), block->nHeight);

                        // we must process each deque in order to have a resulting vector with headers in the correct order
                        // for all possible forks
                        for(const auto& cb : dHeadersAlternativeMulti)
                        {
                            if (--nLimit > 0)
                            {
                                LogPrint("forks", "%s():%d -- [%s]\n", __func__, __LINE__, cb.GetHash().ToString() );
                                vHeadersMulti.push_back(cb);
                            }
                        }
                    }
                    else
                    if (block->nHeight < h)
                    {
                        // we must neglect this branch since not linked to the reference
                        LogPrint("forks", "%s():%d - could not find reference, stopped at %s h(%d)\n",
                            __func__, __LINE__, block->GetBlockHash().ToString(), block->nHeight);
                    }
                    else
                    {
                        // should never happen
                        LogPrint("forks", "%s():%d - block ptr is null\n", __func__, __LINE__);
                    }
                }

                LogPrint("forks", "%s():%d - Pushing %d headers to node[%s]\n",
                    __func__, __LINE__, vHeadersMulti.size(), pfrom->addrName);
                pfrom->PushMessage("headers", vHeadersMulti);

            } // end of hashstop is null

        } // end of is on main

    } // end of command getheaders


    else if (strCommand == "tx")
    {
        int nType = vRecv.nType;
        int nVersion = vRecv.nVersion;

        int txVers = 0;
        ::Unserialize(vRecv, txVers, nType, nVersion);

        // allocated by the callee
        std::unique_ptr<CTransactionBase> pTxBase;
        ::makeSerializedTxObj(vRecv, txVers, pTxBase, nType, nVersion);
        if (pTxBase) {
            ProcessTxBaseMsg(*pTxBase, pfrom);
        } else {
            // This case should never happen. Consider that failing to read stream properly throws an exception
            // which is not handled here
            LogPrintf("%s():%d - pushing reject: invalid obj got from peer=%d %s\n",
                __func__, __LINE__, pfrom->id, pfrom->cleanSubVer);
            pfrom->PushMessage("reject", strCommand, CValidationState::CodeToChar(CValidationState::Code::MALFORMED), string("error parsing message"));
        }
    }

    else if (strCommand == "headers" && !fImporting && !fReindex && !fReindexFast) // Ignore headers received while importing
    {
        std::vector<CBlockHeader> headers;

        // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
        unsigned int nCount = ReadCompactSize(vRecv);
        if (nCount > MAX_HEADERS_RESULTS) {
            Misbehaving(pfrom->GetId(), 20);
            return error("headers message size = %u", nCount);
        }
        headers.resize(nCount);
        for (unsigned int n = 0; n < nCount; n++) {
            vRecv >> headers[n];
            ReadCompactSize(vRecv); // ignore tx count; assume it is 0.
        }

        LOCK(cs_main);

        if (nCount == 0) {
            // Nothing interesting. Stop asking this peers for more headers.
            return true;
        }

        CBlockIndex *pindexLast = NULL;
        int cnt = 0;
        BOOST_FOREACH(const CBlockHeader& header, headers) {
            CValidationState state;
            if (pindexLast != NULL && header.hashPrevBlock != pindexLast->GetBlockHash()) {
                Misbehaving(pfrom->GetId(), 20);
                LogPrint("forks", "%s():%d - non continuous sequence\n", __func__, __LINE__);
                return error("non-continuous headers sequence");
            }

            bool lookForwardTips = (++cnt == MAX_HEADERS_RESULTS);

            if (!AcceptBlockHeader(header, state, &pindexLast, lookForwardTips))
            {
                if (state.IsInvalid())
                {
                    if (state.GetDoS() > 0)
                        Misbehaving(pfrom->GetId(), state.GetDoS());
                    return error("invalid header received");
                }
            }
        }

        if (pindexLast)
            UpdateBlockAvailability(pfrom->GetId(), pindexLast->GetBlockHash());

        if (nCount == MAX_HEADERS_RESULTS && pindexLast) {
            // Headers message had its maximum size; the peer may have more headers.
            // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
            // from there instead.

            CBlockLocator bl = chainActive.GetLocator(pindexLast);
            std::vector<uint256>::iterator b = bl.vHave.begin();
            // get a copy and place on top beside it: peer will detect we are continuing after 160 blocks
            uint256 hash = uint256(*b);
            bl.vHave.insert(b, hash);
            LogPrint("forks", "%s():%d - added duplicate of hash %s to locator\n",
                __func__, __LINE__, hash.ToString() );

            LogPrint("net", "more getheaders (%d) to end to peer=%d (startheight:%d)\n", pindexLast->nHeight, pfrom->id, pfrom->nStartingHeight);
            pfrom->PushMessage("getheaders", bl, uint256());
        }

        CheckBlockIndex();
    }

    else if (strCommand == "block" && !fImporting && !fReindex && !fReindexFast) // Ignore blocks received while importing
    {
        CBlock block;
        vRecv >> block;

        CInv inv(MSG_BLOCK, block.GetHash());
        LogPrint("net", "%s():%d - received block %s peer=%d\n", __func__, __LINE__, inv.hash.ToString(), pfrom->id);

        pfrom->AddInventoryKnown(inv);

        CValidationState state;
        // Process all blocks from whitelisted peers, even if not requested,
        // unless we're still syncing with the network.
        // Such an unrequested block may still be processed, subject to the
        // conditions in AcceptBlock().
        bool forceProcessing = pfrom->fWhitelisted && !IsInitialBlockDownload();
        ProcessNewBlock(state, pfrom, &block, forceProcessing, NULL);
        if (state.IsInvalid())
        {
            LogPrint("forks", "%s():%d - Pushing reject, DoS[%d]\n", __func__, __LINE__, state.GetDoS());
            pfrom->PushMessage("reject", strCommand, CValidationState::CodeToChar(state.GetRejectCode()),
                               state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
            if (state.GetDoS() > 0)
            {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), state.GetDoS());
            }
        }
    }


    // This asymmetric behavior for inbound and outbound connections was introduced
    // to prevent a fingerprinting attack: an attacker can send specific fake addresses
    // to users' AddrMan and later request them by sending getaddr messages.
    // Making nodes which are behind NAT and can only make outgoing connections ignore
    // the getaddr message mitigates the attack.
    else if ((strCommand == "getaddr") && (pfrom->fInbound))
    {
        // Only send one GetAddr response per connection to reduce resource waste
        //  and discourage addr stamping of INV announcements.
        if (pfrom->fSentAddr) {
            LogPrint("net", "Ignoring repeated \"getaddr\". peer=%d\n", pfrom->id);
            return true;
        }
        pfrom->fSentAddr = true;

        pfrom->vAddrToSend.clear();
        vector<CAddress> vAddr = addrman.GetAddr();
        BOOST_FOREACH(const CAddress &addr, vAddr)
            pfrom->PushAddress(addr);
    }


    else if (strCommand == "mempool")
    {
        ProcessMempoolMsg(mempool, pfrom);
    }


    else if (strCommand == "ping")
    {
        if (pfrom->nVersion > BIP0031_VERSION)
        {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            pfrom->PushMessage("pong", nonce);
        }
    }


    else if (strCommand == "pong")
    {
        int64_t pingUsecEnd = nTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        std::string sProblem;

        if (nAvail >= sizeof(nonce)) {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0) {
                if (nonce == pfrom->nPingNonceSent) {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0) {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        pfrom->nMinPingUsecTime = std::min(pfrom->nMinPingUsecTime, pingUsecTime);
                    } else {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                } else {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0) {
                        // This is most likely a bug in another implementation somewhere; cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            } else {
                sProblem = "Unsolicited pong without ping";
            }
        } else {
            // This is most likely a bug in another implementation somewhere; cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty())) {
            LogPrint("net", "pong peer=%d %s: %s, %x expected, %x received, %u bytes\n",
                pfrom->id,
                pfrom->cleanSubVer,
                sProblem,
                pfrom->nPingNonceSent,
                nonce,
                nAvail);
        }
        if (bPingFinished) {
            pfrom->nPingNonceSent = 0;
        }
    }


    else if (strCommand == "filterload")
    {
        CBloomFilter filter;
        vRecv >> filter;

        if (!filter.IsWithinSizeConstraints())
            // There is no excuse for sending a too-large filter
            Misbehaving(pfrom->GetId(), 100);
        else
        {
            LOCK(pfrom->cs_filter);
            delete pfrom->pfilter;
            pfrom->pfilter = new CBloomFilter(filter);
            pfrom->pfilter->UpdateEmptyFull();
        }
        pfrom->fRelayTxes = true;
    }


    else if (strCommand == "filteradd")
    {
        vector<unsigned char> vData;
        vRecv >> vData;

        // Nodes must NEVER send a data item > 520 bytes (the max size for a script data object,
        // and thus, the maximum size any matched object can have) in a filteradd message
        if (vData.size() > MAX_SCRIPT_ELEMENT_SIZE)
        {
            Misbehaving(pfrom->GetId(), 100);
        } else {
            LOCK(pfrom->cs_filter);
            if (pfrom->pfilter)
                pfrom->pfilter->insert(vData);
            else
                Misbehaving(pfrom->GetId(), 100);
        }
    }


    else if (strCommand == "filterclear")
    {
        LOCK(pfrom->cs_filter);
        delete pfrom->pfilter;
        pfrom->pfilter = new CBloomFilter();
        pfrom->fRelayTxes = true;
    }


    else if (strCommand == "reject")
    {
        if (fDebug) {
            try {
                string strMsg; unsigned char ccode; string strReason;
                vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >> ccode >> LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);

                ostringstream ss;
                ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

                if (strMsg == "block" || strMsg == "tx")
                {
                    uint256 hash;
                    vRecv >> hash;
                    ss << ": hash " << hash.ToString();
                }
                LogPrint("net", "Reject %s\n", SanitizeString(ss.str()));
            } catch (const std::ios_base::failure&) {
                // Avoid feedback loops by preventing reject messages from triggering a new reject message.
                LogPrint("net", "Unparseable reject message received\n");
            }
        }
    }

    else if (strCommand == "notfound") {
        // We do not care about the NOTFOUND message, but logging an Unknown Command
        // message would be undesirable as we transmit it ourselves.
    }

    else {
        // Ignore unknown commands for extensibility
        LogPrint("net", "Unknown command \"%s\" from peer=%d\n", SanitizeString(strCommand), pfrom->id);
    }



    return true;
}

// requires LOCK(cs_vRecvMsg)
bool ProcessMessages(CNode* pfrom)
{
    //if (fDebug)
    //    LogPrintf("%s(%u messages)\n", __func__, pfrom->vRecvMsg.size());

    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fOk = true;

    if (!pfrom->vRecvGetData.empty())
        ProcessGetData(pfrom);

    // this maintains the order of responses
    if (!pfrom->vRecvGetData.empty()) return fOk;

    std::deque<CNetMessage>::iterator it = pfrom->vRecvMsg.begin();
    while (!pfrom->fDisconnect && it != pfrom->vRecvMsg.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        // get next message
        CNetMessage& msg = *it;

        //if (fDebug)
        //    LogPrintf("%s(message %u msgsz, %u bytes, complete:%s)\n", __func__,
        //            msg.hdr.nMessageSize, msg.vRecv.size(),
        //            msg.complete() ? "Y" : "N");

        // end, if an incomplete message is found
        if (!msg.complete())
            break;

        // at this point, any failure means we can delete the current message
        it++;

        // Scan for message start
        if (memcmp(msg.hdr.pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE) != 0) {
            LogPrintf("PROCESSMESSAGE: INVALID MESSAGESTART %s peer=%d\n", SanitizeString(msg.hdr.GetCommand()), pfrom->id);
            fOk = false;
            break;
        }

        // Read header
        CMessageHeader& hdr = msg.hdr;
        if (!hdr.IsValid(Params().MessageStart()))
        {
            LogPrintf("PROCESSMESSAGE: ERRORS IN HEADER %s peer=%d\n", SanitizeString(hdr.GetCommand()), pfrom->id);
            continue;
        }
        string strCommand = hdr.GetCommand();

        // Message size
        unsigned int nMessageSize = hdr.nMessageSize;

        // Checksum
        CDataStream& vRecv = msg.vRecv;
        uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
        unsigned int nChecksum = ReadLE32((unsigned char*)&hash);
        if (nChecksum != hdr.nChecksum)
        {
            LogPrintf("%s(%s, %u bytes): CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n", __func__,
               SanitizeString(strCommand), nMessageSize, nChecksum, hdr.nChecksum);
            continue;
        }

        // Process message
        bool fRet = false;
        try
        {
            fRet = ProcessMessage(pfrom, strCommand, vRecv, msg.nTime);
            boost::this_thread::interruption_point();
        }
        catch (const std::ios_base::failure& e)
        {
            pfrom->PushMessage("reject", strCommand, CValidationState::CodeToChar(CValidationState::Code::MALFORMED), string("error parsing message"));
            if (strstr(e.what(), "end of data"))
            {
                // Allow exceptions from under-length message on vRecv
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter than its stated length\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else if (strstr(e.what(), "size too large"))
            {
                // Allow exceptions from over-long size
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else
            {
                PrintExceptionContinue(&e, "ProcessMessages()");
            }
        }
        catch (const boost::thread_interrupted&) {
            throw;
        }
        catch (const std::exception& e) {
            PrintExceptionContinue(&e, "ProcessMessages()");
        } catch (...) {
            PrintExceptionContinue(NULL, "ProcessMessages()");
        }

        if (!fRet)
            LogPrintf("%s(%s, %u bytes) FAILED peer=%d\n", __func__, SanitizeString(strCommand), nMessageSize, pfrom->id);

        break;
    }

    // In case the connection got shut down, its receive buffer was wiped
    if (!pfrom->fDisconnect)
        pfrom->vRecvMsg.erase(pfrom->vRecvMsg.begin(), it);

    return fOk;
}


bool SendMessages(CNode* pto, bool fSendTrickle)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    {
        // Don't send anything until we get its version message
        if (pto->nVersion == 0)
            return true;

        //
        // Message: ping
        //
        bool pingSend = false;
        if (pto->fPingQueued) {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
        }
        if (pingSend) {
            uint64_t nonce = 0;
            while (nonce == 0) {
                GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetTimeMicros();
            if (pto->nVersion > BIP0031_VERSION) {
                pto->nPingNonceSent = nonce;
                pto->PushMessage("ping", nonce);
            } else {
                // Peer is too old to support ping command with nonce, pong will never arrive.
                pto->nPingNonceSent = 0;
                pto->PushMessage("ping");
            }
        }

        TRY_LOCK(cs_main, lockMain); // Acquire cs_main for IsInitialBlockDownload() and CNodeState()
        if (!lockMain)
            return true;

        // Address refresh broadcast
        static int64_t nLastRebroadcast;
        if (!IsInitialBlockDownload() && (GetTime() - nLastRebroadcast > 24 * 60 * 60))
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes)
            {
                // Periodically clear addrKnown to allow refresh broadcasts
                if (nLastRebroadcast)
                    pnode->addrKnown.reset();

                // Rebroadcast our address
                AdvertizeLocal(pnode);
            }
            if (!vNodes.empty())
                nLastRebroadcast = GetTime();
        }

        //
        // Message: addr
        //
        if (fSendTrickle)
        {
            vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            BOOST_FOREACH(const CAddress& addr, pto->vAddrToSend)
            {
                if (!pto->addrKnown.contains(addr.GetKey()))
                {
                    pto->addrKnown.insert(addr.GetKey());
                    vAddr.push_back(addr);
                    // receiver rejects addr messages larger than 1000
                    if (vAddr.size() >= 1000)
                    {
                        pto->PushMessage("addr", vAddr);
                        vAddr.clear();
                    }
                }
            }
            pto->vAddrToSend.clear();
            if (!vAddr.empty())
                pto->PushMessage("addr", vAddr);
        }

        CNodeState &state = *State(pto->GetId());
        if (state.fShouldBan) {
            if (pto->fWhitelisted)
                LogPrintf("Warning: not punishing whitelisted peer %s!\n", pto->addr.ToString());
            else {
                pto->fDisconnect = true;

                bool banLocal = false;

                // Force the ban of local misbehaving nodes when running in "regtest" and the related flag has been set.
                if (BOOST_UNLIKELY(Params().NetworkIDString() == "regtest" && GetBoolArg("-forcelocalban", false)))
                {
                    banLocal = true;
                }

                if (pto->addr.IsLocal() && !banLocal)
                    LogPrintf("Warning: not banning local peer %s!\n", pto->addr.ToString());
                else
                {
                    CNode::Ban(pto->addr);
                }
            }
            state.fShouldBan = false;
        }

        BOOST_FOREACH(const CBlockReject& reject, state.rejects)
            pto->PushMessage("reject", (string)"block", CValidationState::CodeToChar(reject.chRejectCode), reject.strRejectReason, reject.hashBlock);
        state.rejects.clear();

        // Start block sync
        if (pindexBestHeader == NULL)
            pindexBestHeader = chainActive.Tip();
        bool fFetch = state.fPreferredDownload || (nPreferredDownload == 0 && !pto->fClient && !pto->fOneShot); // Download if this is a nice peer, or we have no nice peers and this one might do.
        if (!state.fSyncStarted && !pto->fClient && !fImporting && !fReindex && !fReindexFast) {
            // Only actively request headers from a single peer, unless we're close to today.
            time_t t = time(0);
            int height = chainActive.Tip()->nHeight;
            if (t < ForkManager::getInstance().getMinimumTime(height) && (!ForkManager::getInstance().isAfterChainsplit(height))) {
                fFetch = true;
                if ((nSyncStarted == 0 && fFetch) || pindexBestHeader->GetBlockTime() > GetTime() - 14 * 24 * 60 * 60) {
                    state.fSyncStarted = true;
                    nSyncStarted++;
                    CBlockIndex *pindexStart = pindexBestHeader->pprev ? pindexBestHeader->pprev : pindexBestHeader;
                    LogPrint("net", "%s():%d - initial getheaders (%d) to peer=%d (startheight:%d)\n",
                        __func__, __LINE__, pindexStart->nHeight, pto->id, pto->nStartingHeight);
                    pto->PushMessage("getheaders", chainActive.GetLocator(pindexStart), uint256());
                }
            }
            else {
                if ((nSyncStarted == 0 && fFetch) || pindexBestHeader->GetBlockTime() > GetTime() - 24 * 60 * 60) {
                    state.fSyncStarted = true;
                    nSyncStarted++;
                    CBlockIndex *pindexStart = pindexBestHeader->pprev ? pindexBestHeader->pprev : pindexBestHeader;
                    LogPrint("net", "%s():%d - initial getheaders (%d) to peer=%d (startheight:%d)\n",
                        __func__, __LINE__, pindexStart->nHeight, pto->id, pto->nStartingHeight);
                    pto->PushMessage("getheaders", chainActive.GetLocator(pindexStart), uint256());
                }
            }
        }

        // Resend wallet transactions that haven't gotten in a block yet
        // Except during reindex, importing and IBD, when old wallet
        // transactions become unconfirmed and spams other nodes.
        if (!fReindex && !fReindexFast && !fImporting && !IsInitialBlockDownload())
        {
            GetMainSignals().Broadcast(nTimeBestReceived);
        }

        //
        // Message: inventory
        //
        vector<CInv> vInv;
        vector<CInv> vInvWait;
        {
            LOCK(pto->cs_inventory);
            vInv.reserve(pto->vInventoryToSend.size());
            vInvWait.reserve(pto->vInventoryToSend.size());
            BOOST_FOREACH(const CInv& inv, pto->vInventoryToSend)
            {
                if (pto->setInventoryKnown.count(inv))
                    continue;

                // trickle out tx inv to protect privacy
                if (inv.type == MSG_TX && !fSendTrickle)
                {
                    // 1/4 of tx invs blast to all immediately
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(inv.hash) ^ UintToArith256(hashSalt));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    bool fTrickleWait = ((UintToArith256(hashRand) & 3) != 0);

                    if (fTrickleWait)
                    {
                        vInvWait.push_back(inv);
                        continue;
                    }
                }

                // returns true if wasn't already contained in the set
                if (pto->setInventoryKnown.insert(inv).second)
                {
                    vInv.push_back(inv);
                    if (vInv.size() >= 1000)
                    {
                        LogPrint("forks", "%s():%d - Pushing inv\n", __func__, __LINE__);
                        pto->PushMessage("inv", vInv);
                        vInv.clear();
                    }
                }
            }
            pto->vInventoryToSend = vInvWait;
        }
        if (!vInv.empty())
        {
            LogPrint("forks", "%s():%d - Pushing inv\n", __func__, __LINE__);
            pto->PushMessage("inv", vInv);
        }

        // Detect whether we're stalling
        int64_t nNow = GetTimeMicros();
        if (!pto->fDisconnect && state.nStallingSince && state.nStallingSince < nNow - 1000000 * BLOCK_STALLING_TIMEOUT) {
            // Stalling only triggers when the block download window cannot move. During normal steady state,
            // the download window should be much larger than the to-be-downloaded set of blocks, so disconnection
            // should only happen during initial block download.
            LogPrintf("Peer=%d is stalling block download, disconnecting\n", pto->id);
            pto->fDisconnect = true;
        }
        // In case there is a block that has been in flight from this peer for (2 + 0.5 * N) times the block interval
        // (with N the number of validated blocks that were in flight at the time it was requested), disconnect due to
        // timeout. We compensate for in-flight blocks to prevent killing off peers due to our own downstream link
        // being saturated. We only count validated in-flight blocks so peers can't advertise non-existing block hashes
        // to unreasonably increase our timeout.
        // We also compare the block download timeout originally calculated against the time at which we'd disconnect
        // if we assumed the block were being requested now (ignoring blocks we've requested from this peer, since we're
        // only looking at this peer's oldest request).  This way a large queue in the past doesn't result in a
        // permanently large window for this block to be delivered (ie if the number of blocks in flight is decreasing
        // more quickly than once every 5 minutes, then we'll shorten the download window for this block).
        if (!pto->fDisconnect && state.vBlocksInFlight.size() > 0) {
            QueuedBlock &queuedBlock = state.vBlocksInFlight.front();
            int64_t nTimeoutIfRequestedNow = GetBlockTimeout(nNow, nQueuedValidatedHeaders - state.nBlocksInFlightValidHeaders, consensusParams);
            if (queuedBlock.nTimeDisconnect > nTimeoutIfRequestedNow) {
                LogPrint("net", "Reducing block download timeout for peer=%d block=%s, orig=%d new=%d\n", pto->id, queuedBlock.hash.ToString(), queuedBlock.nTimeDisconnect, nTimeoutIfRequestedNow);
                queuedBlock.nTimeDisconnect = nTimeoutIfRequestedNow;
            }
            if (queuedBlock.nTimeDisconnect < nNow) {
                LogPrintf("Timeout downloading block %s from peer=%d, disconnecting\n", queuedBlock.hash.ToString(), pto->id);
                pto->fDisconnect = true;
            }
        }

        //
        // Message: getdata (blocks)
        //
        vector<CInv> vGetData;
        if (!pto->fDisconnect && !pto->fClient && (fFetch || !IsInitialBlockDownload()) && state.nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
            vector<CBlockIndex*> vToDownload;
            NodeId staller = -1;
            FindNextBlocksToDownload(pto->GetId(), MAX_BLOCKS_IN_TRANSIT_PER_PEER - state.nBlocksInFlight, vToDownload, staller);
            BOOST_FOREACH(CBlockIndex *pindex, vToDownload) {
                vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
                MarkBlockAsInFlight(pto->GetId(), pindex->GetBlockHash(), consensusParams, pindex);
                LogPrint("net", "%s():%d Requesting block %s (%d) peer=%d\n",
                    __func__, __LINE__, pindex->GetBlockHash().ToString(), pindex->nHeight, pto->id);
            }
            if (state.nBlocksInFlight == 0 && staller != -1) {
                if (State(staller)->nStallingSince == 0) {
                    State(staller)->nStallingSince = nNow;
                    LogPrint("net", "Stall started peer=%d\n", staller);
                }
            }
        }

        //
        // Message: getdata (non-blocks)
        //
        while (!pto->fDisconnect && !pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
        {
            const CInv& inv = (*pto->mapAskFor.begin()).second;
            if (!AlreadyHave(inv) && mapAlreadyReceived.find(inv) == mapAlreadyReceived.end())
            {
                if (fDebug)
                    LogPrint("net", "%s():%d - Requesting %s peer=%d\n", __func__, __LINE__, inv.ToString(), pto->id);
                vGetData.push_back(inv);
                if (vGetData.size() >= 1000)
                {
                    pto->PushMessage("getdata", vGetData);
                    vGetData.clear();
                }
            } else {
                //If we're not going to ask, don't expect a response.
                pto->setAskFor.erase(inv.hash);
            }
            pto->mapAskFor.erase(pto->mapAskFor.begin());
        }
        if (!vGetData.empty())
            pto->PushMessage("getdata", vGetData);

    }
    return true;
}

 std::string CBlockFileInfo::ToString() const {
     return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst, nHeightLast, DateTimeStrFormat("%Y-%m-%d", nTimeFirst), DateTimeStrFormat("%Y-%m-%d", nTimeLast));
 }



class CMainCleanup
{
public:
    CMainCleanup() {}
    ~CMainCleanup() {
        // block headers
        BlockMap::iterator it1 = mapBlockIndex.begin();
        for (; it1 != mapBlockIndex.end(); it1++)
            delete (*it1).second;
        mapBlockIndex.clear();

        // orphan transactions
        mapOrphanTransactions.clear();
        mapOrphanTransactionsByPrev.clear();
    }
} instance_of_cmaincleanup;

bool RelayAlternativeChain(CValidationState &state, CBlock *pblock, BlockSet* sForkTips)
{
    if (!pblock)
    {
        LogPrint("forks", "%s():%d - Null pblock!\n", __func__, __LINE__);
        return false;
    }

    const CChainParams& chainParams = Params();
    uint256 hashAlternativeTip = pblock->GetHash();
    //LogPrint("forks", "%s():%d - Entering with hash[%s]\n", __func__, __LINE__, hashAlternativeTip.ToString() );

    // 1. check this is the best chain tip, in this case exit
    if (chainActive.Tip()->GetBlockHash() == hashAlternativeTip)
    {
        //LogPrint("forks", "%s():%d - Exiting: already best tip\n", __func__, __LINE__);
        return true;
    }

    CBlockIndex* pindex = NULL;
    BlockMap::iterator mi = mapBlockIndex.find(hashAlternativeTip);
    if (mi != mapBlockIndex.end())
    {
        pindex = (*mi).second;
    }

    if (!pindex)
    {
        LogPrint("forks", "%s():%d - Null pblock index!\n", __func__, __LINE__);
        return false;
    }

    // 2. check this block is a fork from best chain, otherwise exit
    if (chainActive.Contains(pindex))
    {
        //LogPrint("forks", "%s():%d - Exiting: it belongs to main chain\n", __func__, __LINE__);
        return true;
    }

    // 3. check we have complete list of ancestors
    // --
    // This is due to the fact that blocks can easily be received in sparse order
    // By skipping this block we choose to delay its propagation in the loop
    // below where we look for the best height possible.
    // --
    // Consider that it can be a fork but also be a future best tip as soon as missing blocks are received
    // on the main chain
    if ( pindex->nChainTx <= 0 )
    {
        LogPrint("forks", "%s():%d - Exiting: nChainTx=0\n", __func__, __LINE__);
        return true;
    }

    // 4. Starting from this block, look for the best height that has a complete chain of ancestors
    // --
    // This is done for all of possible forks stem after starting block, potentially more than one height could be found.

    //dump_global_tips();

    LogPrint("forks", "%s():%d - sForkTips(%d) - h[%d] %s\n",
        __func__, __LINE__, sForkTips->size(), pindex->nHeight, pindex->GetBlockHash().ToString() );

    std::vector<CInv> vInv;

    BOOST_FOREACH(const CBlockIndex* block, *sForkTips)
    {
        vInv.push_back(CInv(MSG_BLOCK, block->GetBlockHash()) );
    }

    // 5. push inv list up to the alternative tips
    int nBlockEstimate = 0;
    if (fCheckpointsEnabled)
        nBlockEstimate = Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints());

    int nodeHeight = -1;
    if (nLocalServices & NODE_NETWORK) {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
        {
            if (pnode->nStartingHeight != -1)
            {
                nodeHeight = (pnode->nStartingHeight - 2000);
            }
            else
            {
                nodeHeight = nBlockEstimate;
            }
            if (chainActive.Height() > nodeHeight)
            {
                {
                    BOOST_FOREACH(CInv& inv, vInv)
                    {
                        LogPrint("forks", "%s():%d - Pushing inv to Node [%s] (id=%d) hash[%s]\n",
                            __func__, __LINE__, pnode->addrName, pnode->GetId(), inv.hash.ToString() );
                        pnode->PushInventory(inv);
                    }
                }
            }
        }
    }
    return true;
}


//
// DEBUG Functions
//----------------------------------------------------------------------------
std::string dbg_blk_in_fligth()
{
    std::string ret = "";
    int sz = mapBlocksInFlight.size();
    ret += "Blocks in fligth:" + std::to_string(sz) + "\n";
    ret += "-----------------------\n";
    if (sz <= 0)
    {
        return ret;
    }

    map<uint256, pair<NodeId, list<QueuedBlock>::iterator> >::iterator it;
    for (it = mapBlocksInFlight.begin(); it != mapBlocksInFlight.end(); ++it)
    {
        uint256 hash = it->first;
        ret += hash.GetHex() + "\n";
    }
    return ret;
}

std::string dbg_blk_unlinked()
{
    std::string ret = "";
    int sz = mapBlocksUnlinked.size();
    ret += "Blocks unlinked:" + std::to_string(sz) + "\n";
    ret += "-----------------------\n";
    if (sz <= 0)
    {
        return ret;
    }

    std::multimap<CBlockIndex*, CBlockIndex*>::iterator it;
    for (it = mapBlocksUnlinked.begin(); it != mapBlocksUnlinked.end(); ++it)
    {
        CBlockIndex* index     = it->second;
        CBlockIndex* indexPrev = it->first;
        ret += indexPrev->GetBlockHash().ToString() + "\n";
        ret += "   +--->" + index->GetBlockHash().ToString() + "\n";
    }
    return ret;
}

std::string dbg_blk_candidates()
{
    std::string ret = "";
    int sz = setBlockIndexCandidates.size();
    ret += "Blocks candidate:" + std::to_string(sz) + "\n";
    ret += "-----------------------\n";
    if (sz <= 0)
    {
        return ret;
    }

    std::set<CBlockIndex*, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    for (it = setBlockIndexCandidates.begin(); it != setBlockIndexCandidates.end(); ++it)
    {
        uint256 hash = (*it)->GetBlockHash();
        ret += hash.GetHex() + "\n";
    }
    return ret;
}

std::string dbg_blk_global_tips()
{
    std::string ret = "";
    int sz = mGlobalForkTips.size();
    ret += "Global tips: " + std::to_string(sz) + "\n";
    ret += "-----------------------\n";
    if (sz <= 0)
    {
        return ret;
    }

    for(auto mapPair: mGlobalForkTips)
    {
        const CBlockIndex* pindex = mapPair.first;

        bool onFork = !chainActive.Contains(pindex);
        bool onForkPrev = false;
        if (onFork && pindex->pprev)
        {
            // chanches are that the header is temporarly not a tip but will be promoted soon when the full blocks comes
            onForkPrev = !chainActive.Contains(pindex->pprev);
        }

        uint256 hash = pindex->GetBlockHash();
        int h = pindex->nHeight;
        ret += "h(" + std::to_string(h) + ") " + hash.GetHex() + " onFork";
        if (onFork)
        {
            if (onForkPrev)
            {
                ret += "[X]";
            }
            else
            {
                ret += "[?]";
            }
        }
        else
        {
            ret += "[-]";
        }
        ret += " time[" + std::to_string(mapPair.second) + "]\n";
    }

    std::vector<uint256> vOutput;
    getMostRecentGlobalForkTips(vOutput);

    ret += "Ordered: ---------------\n";
    for(const uint256& hash: vOutput)
    {
        ret += "  [" + hash.GetHex() + "]\n";
    }
    return ret;
}

void dump_index(const CBlockIndex* pindex, int val)
{
    bool onFork = !chainActive.Contains(pindex);
    bool onForkPrev = false;
    if (onFork && pindex->pprev)
    {
        // chanches are that the header is temporarly not a tip but will be promoted soon when the full blocks comes
        onForkPrev = !chainActive.Contains(pindex->pprev);
    }

    std::string offset = "";
    if (onFork)
    {
        // indent any forked block
        offset += "            ";
    }
    LogPrint("forks", "%s-------------------------------------------------\n", offset);
    LogPrint("forks", "%sh(%3d) %s\n", offset, pindex->nHeight, pindex->GetBlockHash().ToString() );
    LogPrint("forks", "%s   onFork[%s]\n", offset, onFork? (onForkPrev?"X":"?"):"-" );
    LogPrint("forks", "%s   nTime[%d]\n", offset, (int)pindex->nTime );
    LogPrint("forks", "%s   nSequenceId[%d]\n", offset, (int)pindex->nSequenceId );
    LogPrint("forks", "%s   delay=%3d,\n", offset, pindex->nChainDelay);
    LogPrint("forks", "%s   prev[%s]\n", offset, pindex->pprev? (pindex->pprev->GetBlockHash().ToString()):"N.A." );
    LogPrint("forks", "%s   chainWork=%.8g\n", offset, log(pindex->nChainWork.getdouble())/log(2.0) );
    LogPrint("forks", "%s   status=%04x VALID_HEADER[%d] HAVE_DATA[%d] HAVE_UNDO[%d]\n", offset,
        pindex->nStatus,
        !!(pindex->nStatus & BLOCK_VALID_HEADER),
        !!(pindex->nStatus & BLOCK_HAVE_DATA),
        !!(pindex->nStatus & BLOCK_HAVE_UNDO) );
    LogPrint("forks", "%s   nChainTx=%d\n", offset, pindex->nChainTx);
    if (val)
    {
        LogPrint("forks", "%s   recv_time=%d\n", offset, val);
    }
}


void dump_db()
{
    if (!LogAcceptCategory("forks") )
    {
        return;
    }

    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
        setTips.insert(item.second);

    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        const CBlockIndex* pprev = item.second->pprev;
        if (pprev)
            setTips.erase(pprev);
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    BOOST_FOREACH(const CBlockIndex* block, setTips)
    {
        LogPrint("forks", "===========================\n" );
        const CBlockIndex* dum = block;

        bool onFork = !chainActive.Contains(dum);

        while (true)
        {
            if (dum)
            {
                dump_index(dum);
                if (dum->pprev)
                {
                    dum = dum->pprev;
                    if (onFork && chainActive.Contains(dum) )
                    {
                        // started on a fork, we reached the main
                        break;
                    }
                }
                else
                {
                    // genesis
                    break;
                }
            }
            else
            {
                assert(false);
            }
        }
    }
}

void dump_candidates()
{
    if (!LogAcceptCategory("forks") )
    {
        return;
    }

    LogPrint("forks", "===== CANDIDATES: %d =================\n", setBlockIndexCandidates.size());
    BOOST_FOREACH(const CBlockIndex* block, setBlockIndexCandidates)
    {
        const CBlockIndex* dum = block;

        dump_index(dum);
    }
}

void dump_global_tips(int limit)
{
    if (!LogAcceptCategory("forks") )
    {
        return;
    }

    int count = limit;

    LogPrint("forks", "===== GLOBAL TIPS: %d =================\n", mGlobalForkTips.size());
    BOOST_FOREACH(auto mapPair, mGlobalForkTips)
    {
        if ( (limit > 0) && (count-- <= 0) )
        {
            LogPrint("forks", "-- stopping after %d elements\n", limit);
            break;
        }
        const CBlockIndex* block = mapPair.first;

        dump_index(block, mapPair.second);
    }

    std::vector<uint256> vOutput;
    getMostRecentGlobalForkTips(vOutput);

    LogPrint("forks", "Ordered by time:\n");
    LogPrint("forks", "----------------------------------------------------------------\n");
    BOOST_FOREACH(const uint256& hash, vOutput)
    {
        LogPrint("forks", "  %s\n", hash.ToString() );
    }
}

void dump_dirty()
{
    if (!LogAcceptCategory("forks") )
    {
        return;
    }

    LogPrint("forks", "===== DIRTIES: %d =================\n", setDirtyBlockIndex.size());
    BOOST_FOREACH(const CBlockIndex* block, setDirtyBlockIndex)
    {
        const CBlockIndex* dum = block;

        dump_index(dum);
    }
}

bool getHeadersIsOnMain(const CBlockLocator& locator, const uint256& hashStop, CBlockIndex** pindexReference)
{
    LogPrint("forks", "%s():%d - Entering hashStop[%s]\n", __func__, __LINE__, hashStop.ToString() );
    if (locator.IsNull() )
    {
        LogPrint("forks", "%s():%d - locator is null, returning TRUE\n", __func__, __LINE__ );
        return true;
    }

    BOOST_FOREACH(const uint256& hash, locator.vHave) {
        LogPrint("forks", "%s():%d - locator has [%s]\n", __func__, __LINE__, hash.ToString() );
    }

    if (hashStop != uint256() )
    {
        BlockMap::iterator mi = mapBlockIndex.find(hashStop);
        if (mi != mapBlockIndex.end() )
        {
            *pindexReference = (*mi).second;
            bool onMain = (chainActive.Contains((*mi).second) );
            LogPrint("forks", "%s():%d - hashStop found, returning %s\n",
                __func__, __LINE__, onMain?"TRUE":"FALSE");
            return onMain;
        }
        else
        {
            // should never happen
            LogPrint("forks", "%s():%d - hashStop not found, returning TRUE\n", __func__, __LINE__);
            return true;
        }
    }
    else
    {
        // hashstop can be null:
        // 1. when a node is syncing after a network join or a node startup
        // 2. when a bunch of 160 headers has been sent and peer requests more

        if (locator.vHave.size() < 2)
        {
            // should never happen
            LogPrint("forks", "%s():%d - short locator, returning TRUE\n", __func__, __LINE__);
            return true;
        }

        const uint256& hash_0 = locator.vHave[0];
        const uint256& hash_1 = locator.vHave[1];

        if (hash_0 == hash_1)
        {
            // we are on case 2. above, check locator for telling if peer is on main or not
            LogPrint("forks", "%s():%d - found duplicate of hash %s in the locator\n",
                __func__, __LINE__, hash_0.ToString() );

            BlockMap::iterator mi = mapBlockIndex.find(hash_0);
            if (mi != mapBlockIndex.end() )
            {
                CBlockIndex* idx = (*mi).second;

                if (!chainActive.Contains(idx))
                {
                    // tip of locator not on main
                    *pindexReference = idx;
                    LogPrint("forks", "%s():%d - hash found, returning FALSE\n",
                        __func__, __LINE__);
                    return false;
                }
            }
            else
            {
                // should never happen
                LogPrint("forks", "%s():%d - hash not found, returning TRUE\n", __func__, __LINE__);
                return true;
            }
        }

        LogPrint("forks", "%s():%d - Exiting returning TRUE\n", __func__, __LINE__);
        return true;
    }

    // should never get here
    LogPrint("forks", "%s():%d - ##### Exiting returning FALSE\n", __func__, __LINE__);
    return false;
}


static int getInitCbhSafeDepth()
{
    if (Params().NetworkIDString() == "regtest")
    {
        int val = (int)(GetArg("-cbhsafedepth", Params().CbhSafeDepth() ));
        LogPrint("cbh", "%s():%d - %s: using val %d \n", __func__, __LINE__, Params().NetworkIDString(), val);
        return val;
    }
    return Params().CbhSafeDepth();
}

int getCheckBlockAtHeightSafeDepth()
{
    // gets constructed just one time
    static int retVal( getInitCbhSafeDepth() );
    return retVal;
}

int getScMinWithdrawalEpochLength()
{
    // gets constructed just one time
    static int retVal(Params().ScMinWithdrawalEpochLength());
    return retVal;
}

int getScMaxWithdrawalEpochLength()
{
    // gets constructed just one time
    static int retVal(Params().ScMaxWithdrawalEpochLength());
    return retVal;
}

static int getInitCbhMinAge()
{
    if (Params().NetworkIDString() == "regtest")
    {
        int val = (int)(GetArg("-cbhminage", Params().CbhMinimumAge() ));
        LogPrint("cbh", "%s():%d - %s: using val %d \n", __func__, __LINE__, Params().NetworkIDString(), val);
        return val;
    }
    return Params().CbhMinimumAge();
}

int getCheckBlockAtHeightMinAge()
{
    // gets constructed just one time
    static int retVal( getInitCbhMinAge() );
    return retVal;
}

static bool getInitRequireStandard()
{
    if ( (Params().NetworkIDString() == "regtest") || (Params().NetworkIDString() == "test") )
    {
        bool val = Params().RequireStandard();

        if ((bool)(GetBoolArg("-allownonstandardtx",  false ) ) )
        {
            // if this flag is set the user wants to allow non-standars tx, therefore we override default param and return false
            val = false;
        }
        LogPrintf("%s():%d - %s: using val %d (%s)\n", __func__, __LINE__, Params().NetworkIDString(), (int)val, (val?"Y":"N"));
        return val;
    }
    return Params().RequireStandard();
}

bool getRequireStandard()
{
    // gets constructed just one time
    static int retVal( getInitRequireStandard() );
    return retVal;
}
