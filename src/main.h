// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAIN_H
#define BITCOIN_MAIN_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "spentindex.h"
#include "timestampindex.h"

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "net.h"
#include "script/script.h"
#include "sync.h"
#include "tinyformat.h"
#include "txmempool.h"
#include "uint256.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <boost/unordered_map.hpp>

class CTransaction;
class CCoins;
class CCoinsViewCache;
class CCoinsView;
class CBlock;
class CBlockLocator;
class CBlockTreeDB;
class CScriptCheck;
class CValidationState;
class CTxUndo;
struct CNodeStateStats;
class CTxInUndo;

// Enforce 64-bit architecture requirement
#if defined(__clang__) || defined(__GNUC__)
    #if defined(__x86_64)
        #define BITNESS_64
    #else
        #define BITNESS_32
    #endif
#else
    #error "Zend only supports GCC and Clang compilers"
#endif
#if defined(BITNESS_32)
    #error "Zend is supported only on x86-64 architecture"
#endif
#undef BITNESS_32
#undef BITNESS_64

#if defined(_WIN32)
    // On Windows we assume little-endian
#elif defined(__BYTE_ORDER__)
    #if (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
        #error "Zend is not supported on big-endian architectures"
    #endif
#else
    #error "Undetectable endianness"
#endif

/** Default for -blockmaxsize and -blockminsize, which control the range of sizes the mining code will create **/
static const unsigned int DEFAULT_BLOCK_MAX_SIZE = MAX_BLOCK_SIZE;
static const unsigned int DEFAULT_BLOCK_MAX_SIZE_BEFORE_SC = MAX_BLOCK_SIZE_BEFORE_SC;
static const unsigned int DEFAULT_BLOCK_MIN_SIZE = 0;

/** Default for -blocktxpartitionmaxsize which control the partition in block reserved for tx*/
static const unsigned int DEFAULT_BLOCK_TX_PART_MAX_SIZE = BLOCK_TX_PARTITION_SIZE;

/** Default for -blockprioritysize, maximum space for zero/low-fee transactions **/
static const unsigned int DEFAULT_BLOCK_PRIORITY_SIZE = BLOCK_TX_PARTITION_SIZE / 2;
static const unsigned int DEFAULT_BLOCK_PRIORITY_SIZE_BEFORE_SC = MAX_BLOCK_SIZE_BEFORE_SC / 2;

/** Default for -blockmaxcomplexity, which control the maximum comlexity of the block during template creation **/
static const unsigned int DEFAULT_BLOCK_MAX_COMPLEXITY_SIZE = 0;
/** Default for accepting alerts from the P2P network. */
static const bool DEFAULT_ALERTS = true;
/** Minimum alert priority for enabling safe mode. */
static const int ALERT_PRIORITY_SAFE_MODE = 4000;
/** Maximum number of signature check operations in an IsStandard() P2SH script */
static const unsigned int MAX_P2SH_SIGOPS = 15;
/** The maximum number of sigops we're willing to relay/mine in a single tx */
static const unsigned int MAX_STANDARD_TX_SIGOPS = MAX_BLOCK_SIGOPS/5;
/** Default for -minrelaytxfee, minimum relay fee for transactions */
static const unsigned int DEFAULT_MIN_RELAY_TX_FEE = 100;
/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** The maximum size of a blk?????.dat file (since 0.8) */
static const unsigned int MAX_BLOCKFILE_SIZE = 0x8000000; // 128 MiB
/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
static const unsigned int BLOCKFILE_CHUNK_SIZE = 0x1000000; // 16 MiB
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
static const unsigned int UNDOFILE_CHUNK_SIZE = 0x100000; // 1 MiB
/** Maximum number of script-checking threads allowed */
static const int MAX_SCRIPTCHECK_THREADS = 16;
/** -par default (number of script-checking threads, 0 = auto) */
static const int DEFAULT_SCRIPTCHECK_THREADS = 0;
/** Number of blocks that can be requested at any given time from a single peer. */
static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16;
/** Timeout in seconds during which a peer must stall block download progress before being disconnected. */
static const unsigned int BLOCK_STALLING_TIMEOUT = 2;
/** Number of headers sent in one getheaders result. We rely on the assumption that if a peer sends
 *  less than this number, we reached its tip. Changing this value is a protocol upgrade. */
static const unsigned int MAX_HEADERS_RESULTS = 160;
/** Size of the "block download window": how far ahead of our current height do we fetch?
 *  Larger windows tolerate larger download speed differences between peer, but increase the potential
 *  degree of disordering of blocks on disk (which make reindexing and in the future perhaps pruning
 *  harder). We'll probably want to make this a per-peer adaptive value at some point. */
static const unsigned int BLOCK_DOWNLOAD_WINDOW = 1024;
/** Time to wait (in seconds) between writing blocks/block index to disk. */
static const unsigned int DATABASE_WRITE_INTERVAL = 60 * 60;
/** Time to wait (in seconds) between flushing chainstate to disk. */
static const unsigned int DATABASE_FLUSH_INTERVAL = 24 * 60 * 60;
/** Maximum length of reject messages. */
static const unsigned int MAX_REJECT_MESSAGE_LENGTH = 111;
/* Maximum number of heigths meaningful when looking for block finality */
static const int MAX_BLOCK_AGE_FOR_FINALITY = 2000;

static const bool DEFAULT_TXINDEX = false;
static const bool DEFAULT_MATURITYHEIGHTINDEX = false;
static const bool DEFAULT_ADDRESSINDEX = false;
static const bool DEFAULT_TIMESTAMPINDEX = false;
static const bool DEFAULT_SPENTINDEX = false;

// Sanity check the magic numbers when we change them
BOOST_STATIC_ASSERT(DEFAULT_BLOCK_MAX_SIZE <= MAX_BLOCK_SIZE);
BOOST_STATIC_ASSERT(MAX_BLOCK_SIZE > MAX_CERT_SIZE);
BOOST_STATIC_ASSERT(MAX_BLOCK_SIZE > BLOCK_TX_PARTITION_SIZE);
BOOST_STATIC_ASSERT(BLOCK_TX_PARTITION_SIZE > MAX_TX_SIZE);
BOOST_STATIC_ASSERT(DEFAULT_BLOCK_PRIORITY_SIZE <= DEFAULT_BLOCK_MAX_SIZE);
BOOST_STATIC_ASSERT(DEFAULT_BLOCK_PRIORITY_SIZE_BEFORE_SC <= DEFAULT_BLOCK_MAX_SIZE_BEFORE_SC);

#define equihash_parameters_acceptable(N, K) \
    ((CBlockHeader::HEADER_SIZE + equihash_solution_size(N, K))*MAX_HEADERS_RESULTS < \
     MAX_PROTOCOL_MESSAGE_LENGTH-1000)

extern CScript COINBASE_FLAGS;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;
typedef boost::unordered_map<uint256, CBlockIndex*, ObjectHasher> BlockMap;
extern BlockMap mapBlockIndex;
typedef boost::unordered_map<uint256, int, ObjectHasher> ScCumTreeRootMap;
extern ScCumTreeRootMap mapCumtreeHeight;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockCert;
extern uint64_t nLastBlockSize;
extern uint64_t nLastBlockTxPartitionSize;
extern const std::string strMessageMagic;
extern CWaitableCriticalSection csBestBlock;
extern CConditionVariable cvBlockChange;
extern bool fExperimentalMode;
extern bool fImporting;
extern bool fReindex;
extern bool fReindexFast;
extern int nScriptCheckThreads;

extern bool fAddressIndex;
extern bool fTimestampIndex;
extern bool fSpentIndex;
extern bool fTxIndex;
extern bool fMaturityHeightIndex;
extern bool fIsBareMultisigStd;
extern bool fCheckBlockIndex;
extern bool fCheckpointsEnabled;
extern bool fRegtestAllowDustOutput;
extern size_t nCoinCacheUsage;
extern CFeeRate minRelayTxFee;
extern bool fAlerts;

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight
{
    bool operator()(const CBlockIndex* a, const CBlockIndex* b) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction. */

        if (a->nHeight != b->nHeight)
          return (a->nHeight > b->nHeight);

        return a < b;
    }
};

typedef std::map<const CBlockIndex*, int, CompareBlocksByHeight> BlockTimeMap;
extern BlockTimeMap mGlobalForkTips;

typedef std::set<const CBlockIndex*, CompareBlocksByHeight> BlockSet;
extern BlockSet sGlobalForkTips;
static const int MAX_NUM_GLOBAL_FORKS = 3;

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex *pindexBestHeader;

/** Minimum disk space required - used in CheckDiskSpace() */
static const uint64_t nMinDiskSpace = 52428800;

/** Pruning-related variables and constants */
/** True if any block files have ever been pruned. */
extern bool fHavePruned;
/** True if we're running in -prune mode. */
extern bool fPruneMode;
/** Number of MiB of block files that we're trying to stay below. */
extern uint64_t nPruneTarget;
/** Block files containing a block-height within MIN_BLOCKS_TO_KEEP of chainActive.Tip() will not be pruned. */
static const unsigned int MIN_BLOCKS_TO_KEEP = 288;

// Require that user allocate at least 550MB for block & undo files (blk???.dat and rev???.dat)
// At 1MB per block, 288 blocks = 288MB.
// Add 15% for Undo data = 331MB
// Add 20% for Orphan block rate = 397MB
// We want the low water mark after pruning to be at least 397 MB and since we prune in
// full block file chunks, we need the high water mark which triggers the prune to be
// one 128MB block file + added 15% undo data = 147MB greater for a total of 545MB
// Setting the target to > than 550MB will make it likely we can respect the target.
static const uint64_t MIN_DISK_SPACE_FOR_BLOCK_FILES = 550 * 1024 * 1024;

/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals& nodeSignals);
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals& nodeSignals);

/**
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 *
 * @param[out]  state   This may be set to an Error state if any error occurred processing it, including during validation/connection/etc of otherwise unrelated blocks during reorganisation; or it may be set to an Invalid state if pblock is itself invalid (but this is not guaranteed even when the block is checked). If you want to *possibly* get feedback on whether pblock is valid, you must also install a CValidationInterface (see validationinterface.h) - this will have its BlockChecked method called whenever *any* block completes validation.
 * @param[in]   pfrom   The node which we are receiving the block from; it is added to mapBlockSource and may be penalised if the block is invalid.
 * @param[in]   pblock  The block we want to process.
 * @param[in]   fForceProcessing Process this block even if unrequested; used for non-network block sources and whitelisted peers.
 * @param[out]  dbp     If pblock is stored to disk (or already there), this will be set to its location.
 * @return True if state.IsValid()
 */
bool ProcessNewBlock(CValidationState &state, CNode* pfrom, CBlock* pblock, bool fForceProcessing, CDiskBlockPos *dbp);
/** Check whether enough disk space is available for an incoming block */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);
/** Open a block file (blk?????.dat) */
FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly = false);
/** Open an undo file (rev?????.dat) */
FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly = false);
/** Translation to a filesystem path */
boost::filesystem::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix);
/** Import blocks from an external file, possibly headers only */
bool LoadBlocksFromExternalFile(FILE* fileIn, CDiskBlockPos *dbp, bool loadHeadersOnly);
/** Initialize a new block tree database + block data on disk */
bool InitBlockIndex();
/** Load the block tree and coins database from disk */
bool LoadBlockIndex();
/** Unload database information */
void UnloadBlockIndex();
// Utilities refactored out of ProcessMessages
void ProcessMempoolMsg(const CTxMemPool& pool, CNode* pfrom);

/**
 * @brief The enumeration of states of the sidechain batch proof verification.
 *
 * It is useful to correctly handle the behavior of the ProcessTxBaseMsg() function,
 * in particular it should request an asynchronous batch verification of the proof
 * when called for the first time (with parameter NOT_VERIFIED_YET) and then should
 * be called a second time by the batch proof verification thread with the parameter
 * VERIFIED.
 */
enum class BatchVerificationStateFlag
{
    NOT_VERIFIED_YET,   /**< The sidechain proof verification has not been verified yet. */
    VERIFIED,           /**< The sidechain proof has been correctly verified. */
    FAILED              /**< The sidechain proof has been rejected. */
};

/** Process a transaction or certificate that has to be added to memory pool */
void ProcessTxBaseAcceptToMemoryPool(const CTransactionBase& txBase, CNode* pfrom,
                                     BatchVerificationStateFlag proofVerificationState,
                                     CValidationState& state);
/** Process protocol message of type "tx" */
void ProcessTxBaseMsg(const CTransactionBase& txBase, CNode* pfrom);
/** Process protocol messages received from a given node */
bool ProcessMessages(CNode* pfrom);
/**
 * Send queued protocol messages to be sent to a give node.
 *
 * @param[in]   pto             The node which we are sending messages to.
 * @param[in]   fSendTrickle    When true send the trickled data, otherwise trickle the data until true.
 */
bool SendMessages(CNode* pto, bool fSendTrickle);
/** Run an instance of the script checking thread */
void ThreadScriptCheck();
/** Try to detect Partition (network isolation) attacks against us */
void PartitionCheck(bool (*initialDownloadCheck)(), CCriticalSection& cs, const CBlockIndex *const &bestHeader, int64_t nPowTargetSpacing);
/** Check whether we are doing an initial block download (synchronizing from disk or network) */
bool IsInitialBlockDownload();
/** Format a string that describes several potential problems detected by the core */
std::string GetWarnings(const std::string& strFor);
/** Retrieve a transaction (from memory pool, or from disk, if possible) */
bool GetTransaction(const uint256 &hash, CTransaction &tx, uint256 &hashBlock, bool fAllowSlow = false);
/** Retrieve a certificate (from memory pool, or from disk, if possible) */
bool GetCertificate(const uint256 &hash, CScCertificate &cert, uint256 &hashBlock, bool fAllowSlow = false);
/** Retrieve a base obj (from memory pool, or from disk, if possible) */
bool GetTxBaseObj(const uint256 &hash, std::unique_ptr<CTransactionBase>& pTxBase, uint256 &hashBlock, bool fAllowSlow = false);

static bool DUMMY_FALSE_VALUE = false;
/** Find the best known block, and make it the tip of the block chain */
bool ActivateBestChain(CValidationState &state, CBlock *pblock = NULL, bool &postponeRelay = DUMMY_FALSE_VALUE);
/** Find an alternative chain tip and propagate to the network */
bool RelayAlternativeChain(CValidationState &state, CBlock *pblock, BlockSet* sForkTips);

CBlockIndex* AddToBlockIndex(const CBlockHeader& block);
bool addToGlobalForkTips(const CBlockIndex* pindex);
int getMostRecentGlobalForkTips(std::vector<uint256>& output);
bool updateGlobalForkTips(const CBlockIndex* pindex, bool lookForwardTips);
bool getHeadersIsOnMain(const CBlockLocator& locator, const uint256& hashStop, CBlockIndex** pindexReference);

int getCheckBlockAtHeightSafeDepth();
int getScMinWithdrawalEpochLength();
int getScMaxWithdrawalEpochLength();
int getCheckBlockAtHeightMinAge();
bool getRequireStandard();

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams);

/**
 * Prune block and undo files (blk???.dat and undo???.dat) so that the disk space used is less than a user-defined target.
 * The user sets the target (in MB) on the command line or in config file.  This will be run on startup and whenever new
 * space is allocated in a block or undo file, staying below the target. Changing back to unpruned requires a reindex
 * (which in this case means the blockchain must be re-downloaded.)
 *
 * Pruning functions are called from FlushStateToDisk when the global fCheckForPruning flag has been set.
 * Block and undo files are deleted in lock-step (when blk00003.dat is deleted, so is rev00003.dat.)
 * Pruning cannot take place until the longest chain is at least a certain length (100000 on mainnet, 1000 on testnet, 10 on regtest).
 * Pruning will never delete a block within a defined distance (currently 288) from the active chain's tip.
 * The block index is updated by unsetting HAVE_DATA and HAVE_UNDO for any blocks that were stored in the deleted files.
 * A db flag records the fact that at least some block files have been pruned.
 *
 * @param[out]   setFilesToPrune   The set of file indices that can be unlinked will be returned
 */
void FindFilesToPrune(std::set<int>& setFilesToPrune);

/**
 *  Actually unlink the specified files
 */
void UnlinkPrunedFiles(std::set<int>& setFilesToPrune);

/** Create a new block index entry for a given block hash */
CBlockIndex * InsertBlockIndex(uint256 hash);
/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);
/** Increase a node's misbehavior score. */
void Misbehaving(NodeId nodeid, int howmuch);
/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk();
/** Prune block files and flush state to disk. */
void PruneAndFlush();

// Accept Tx/Cert ToMempool parameters types and signature
enum class LimitFreeFlag       { ON, OFF };
enum class RejectAbsurdFeeFlag { ON, OFF };
enum class MempoolReturnValue  { INVALID, MISSING_INPUT, VALID, PARTIALLY_VALIDATED };

/**
 * @brief The enumeration of possible states of the sidechain proof verification
 * inside the Accept(Tx/Cert)ToMempool().
 */
enum class MempoolProofVerificationFlag
{
    DISABLED,   /**< The proof verification is not required. */
    SYNC,       /**< The proof verification is enabled and will be performed synchronously on the calling thread. */
    ASYNC       /**< The proof verification is enabled and will be performed asynchronously on a separate thread. */
};

/**
 * @brief Rejects a certificate or transaction submitted to memory pool.
 *
 * It sends an error message to the node that has sent the invalid entry
 * and eventually bans it.
 *
 * @param state The state of the validation process (containing the error information)
 * @param txBase The transaction or certificate that failed the verification
 * @param pfrom The node that sent the offending transaction or certificate
 */
void RejectMemoryPoolTxBase(const CValidationState& state, const CTransactionBase& txBase, CNode* pfrom);

/** (try to) add transaction to memory pool **/
MempoolReturnValue AcceptTxBaseToMemoryPool(CTxMemPool& pool, CValidationState &state, const CTransactionBase &txBase,
    LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee, MempoolProofVerificationFlag fProofVerification, CNode* pfrom = nullptr);

MempoolReturnValue AcceptTxToMemoryPool(CTxMemPool& pool, CValidationState &state, const CTransaction &tx,
    LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee, MempoolProofVerificationFlag fProofVerification, CNode* pfrom = nullptr);

MempoolReturnValue AcceptCertificateToMemoryPool(CTxMemPool& pool, CValidationState &state, const CScCertificate &cert,
    LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee, MempoolProofVerificationFlag fProofVerification, CNode* pfrom = nullptr);

struct CNodeStateStats {
    int nMisbehavior;
    int nSyncHeight;
    int nCommonHeight;
    std::vector<int> vHeightInFlight;
};

struct COrphanTx {
    std::shared_ptr<const CTransactionBase> tx;
    NodeId fromPeer;
};

CAmount GetMinRelayFee(const CTransactionBase& tx, unsigned int nBytes, bool fAllowFree, unsigned int block_priority_size);

/**
 * Check transaction inputs, and make sure any
 * pay-to-script-hash transactions are evaluating IsStandard scripts
 *
 * Why bother? To avoid denial-of-service attacks; an attacker
 * can submit a standard HASH... OP_EQUAL transaction,
 * which will get accepted into blocks. The redemption
 * script can be anything; an attacker could use a very
 * expensive-to-check-upon-redemption script like:
 *   DUP CHECKSIG DROP ... repeated 100 times... OP_1
 */

/**
 * Check for standard transaction types
 * @param[in] mapInputs    Map of previous transactions that have outputs we're spending
 * @return True if all inputs (scriptSigs) use only standard transaction forms
 */
bool AreInputsStandard(const CTransactionBase& txBase, const CCoinsViewCache& mapInputs);

/**
 * Count ECDSA signature operations the old-fashioned (pre-0.6) way
 * @return number of sigops this transaction's outputs will produce when spent
 * @see CTransaction::FetchInputs
 */
unsigned int GetLegacySigOpCount(const CTransactionBase& tx);

/**
 * Count ECDSA signature operations in pay-to-script-hash inputs.
 *
 * @param[in] mapInputs Map of previous transactions that have outputs we're spending
 * @return maximum number of sigops required to validate this transaction's inputs
 * @see CTransaction::FetchInputs
 */
unsigned int GetP2SHSigOpCount(const CTransactionBase& tx, const CCoinsViewCache& mapInputs);


/**
 * Check whether the specified input (either regular or CSW) of this transaction has valid scripts & sigs.
 * This does not modify the UTXO set. If pvChecks is not NULL, script checks are pushed onto it
 * instead of being performed inline.
 */
bool InputScriptCheck(const CScript& scriptPubKey, const CTransactionBase& tx, unsigned int nIn,
                      const CChain& chain, unsigned int flags, bool cache,  CValidationState &state, std::vector<CScriptCheck> *pvChecks);
/**
 * Check whether all inputs (either regular and CSW) of this transaction are valid (no double spends, scripts & sigs, amounts)
 * This does not modify the UTXO set. If pvChecks is not NULL, script checks are pushed onto it
 * instead of being performed inline.
 */
bool ContextualCheckTxInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &view, bool fScriptChecks,
                           const CChain& chain, unsigned int flags, bool cacheStore, const Consensus::Params& consensusParams,
                           std::vector<CScriptCheck> *pvChecks = NULL);
/**
 * Check whether all inputs of this certificates are valid (no double spends, scripts & sigs, amounts)
 * This does not modify the UTXO set. If pvChecks is not NULL, script checks are pushed onto it
 * instead of being performed inline.
 */
bool ContextualCheckCertInputs(const CScCertificate& cert, CValidationState &state, const CCoinsViewCache &view, bool fScriptChecks,
                           const CChain& chain, unsigned int flags, bool cacheStore, const Consensus::Params& consensusParams,
                           std::vector<CScriptCheck> *pvChecks = NULL);

/** Apply the effects of this transaction on the UTXO set represented by view */
bool ApplyTxInUndo(const CTxInUndo& undo, CCoinsViewCache& view, const COutPoint& out);
void UpdateCoins(const CTransaction& tx, CCoinsViewCache &inputs, CTxUndo& txundo, int nHeight);
void UpdateCoins(const CScCertificate& cert, CCoinsViewCache &inputs, CTxUndo& txundo, int nHeight, bool isBlockTopQualityCert);

std::map<uint256,uint256> HighQualityCertData(const CBlock& blockToConnect, const CCoinsViewCache& view);
std::map<uint256,uint256> HighQualityCertData(const CBlock& blockToDisconnect, const CBlockUndo& blockUndo);

/** Context-independent validity checks */
bool CheckTransaction(const CTransaction& tx, CValidationState& state, libzcash::ProofVerifier& verifier);
bool CheckCertificate(const CScCertificate& cert, CValidationState& state);
bool CheckTransactionWithoutProofVerification(const CTransaction& tx, CValidationState &state);
bool CheckCertificatesOrdering(const std::vector<CScCertificate>& certList, CValidationState& state);

/** Check for standard transaction types
 * @return True if all outputs (scriptPubKeys) use only standard transaction forms
 */
bool IsStandardTx(const CTransactionBase& txBase, std::string& reason, int nHeight);

/**
 * Check if transaction is final and can be included in a block with the
 * specified height and time. Consensus critical.
 */
bool IsFinalTx(const CTransactionBase &tx, int nBlockHeight, int64_t nBlockTime);

/**
 * Check if transaction will be final in the next block to be created.
 *
 * Calls IsFinalTx() with current block height and appropriate block time.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckFinalTx(const CTransactionBase &tx, int flags = -1);

/**
 * Closure representing one script verification
 * Note that this stores references to the spending transaction
 */
class CScriptCheck
{
private:
    CScript scriptPubKey;
    const CTransactionBase *ptxTo;
    unsigned int nIn;
    const CChain *chain;
    unsigned int nFlags;
    bool cacheStore;
    ScriptError error;

public:
    CScriptCheck();
    CScriptCheck(const CCoins& txFromIn, const CTransactionBase& txToIn, unsigned int nInIn, const CChain* chainIn, unsigned int nFlagsIn, bool cacheIn);
    CScriptCheck(const CScript& scriptPubKeyIn, const CTransactionBase& txToIn, unsigned int nInIn, const CChain* chainIn, unsigned int nFlagsIn, bool cacheIn);
    bool operator()();
    void swap(CScriptCheck &check);
    ScriptError GetScriptError() const;
};

bool GetTimestampIndex(const unsigned int &high, const unsigned int &low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &hashes);
bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value);
bool GetAddressIndex(uint160 addressHash, int type,
                     std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> > &addressIndex,
                     int start = 0, int end = 0);
bool GetAddressUnspent(uint160 addressHash, int type,
                       std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs);


/** Functions for disk access for blocks */
bool WriteBlockToDisk(CBlock& block, CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& messageStart);
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex);
CBlock LoadBlockFrom(CBufferedFile& blkdat, CDiskBlockPos* pLastLoadedBlkPos);

/** Functions for validating blocks and updating the block tree */

/**
 * @brief The enumeration to enable/disable Level DB indexes write.
 * It is used in the ConnectBlock() and DisconnectBlock() to prevent updating the DB
 * when called from VerifyDB() and TestBlockValidity().
 * Such flag is needed because the indexes don't have a cache as CoinDB.
 */
enum class flagLevelDBIndexesWrite { ON, OFF };

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  In case pfClean is provided, operation will try to be tolerant about errors, and *pfClean
 *  will be true if no problems were found. Otherwise, the return value will be false in case
 *  of problems. Note that in any case, coins may be modified. */
bool DisconnectBlock(CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& coins, flagLevelDBIndexesWrite explorerIndexesWrite,
                     bool* pfClean = NULL, std::vector<CScCertificateStatusUpdateInfo>* pCertsStateInfo = nullptr);

/** Apply the effects of this block (with given index) on the UTXO set represented by coins */
enum class flagCheckPow             { ON, OFF };
enum class flagCheckMerkleRoot      { ON, OFF };
enum class flagScRelatedChecks      { ON, OFF };
enum class flagScProofVerification  { ON, OFF };

/**
 * @brief The enumeration of allowed types of block processing.
 * It is used in the ConnectBlock() function to choose between the full/normal processing
 * or a dry-run intended to check only the validity (without applying any changes).
 */
enum class flagBlockProcessingType
{
    COMPLETE,       /**< Perform the normal/complete procedure applying changes. */
    CHECK_ONLY      /**< Perofrm only the validity check and do not apply any changes. */
};

bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex,
    CCoinsViewCache& coins, const CChain& chain, flagBlockProcessingType processingType,
    flagScRelatedChecks fScRelatedChecks, flagScProofVerification fScProofVerification,
    flagLevelDBIndexesWrite explorerIndexesWrite,
    std::vector<CScCertificateStatusUpdateInfo>* pCertsStateInfo = nullptr);

/** Find the position in block files (blk??????.dat) in which a block must be written. */
bool FindBlockPos(CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false);

/** Context-independent validity checks */
bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, flagCheckPow fCheckPOW = flagCheckPow::ON);
bool CheckBlock(const CBlock& block, CValidationState& state,
                libzcash::ProofVerifier& verifier,
                flagCheckPow fCheckPOW = flagCheckPow::ON,
                flagCheckMerkleRoot fCheckMerkleRoot = flagCheckMerkleRoot::ON);

/** Context-dependent validity checks */
bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex *pindexPrev);
bool ContextualCheckBlock(const CBlock& block, CValidationState& state, CBlockIndex *pindexPrev);

/** Check a block is completely valid from start to finish (only works on top of our current best block, with cs_main held) */
bool TestBlockValidity(CValidationState &state, const CBlock& block, CBlockIndex *pindexPrev,
        flagCheckPow fCheckPOW, flagCheckMerkleRoot fCheckMerkleRoot, flagScRelatedChecks fScRelatedChecks);

/**
 * Store block on disk.
 * JoinSplit proofs are never verified, because:
 * - AcceptBlock doesn't perform script checks either.
 * - The only caller of AcceptBlock verifies JoinSplit proofs elsewhere.
 * If dbp is non-NULL, the file is known to already reside on disk
 */
bool AcceptBlock(CBlock& block, CValidationState& state, CBlockIndex **pindex, bool fRequested, CDiskBlockPos* dbp, BlockSet* sForkTips = NULL);
bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex **ppindex= NULL, bool lookForwardTips = false);


class CBlockFileInfo
{
public:
    unsigned int nBlocks;      //! number of blocks stored in file
    unsigned int nSize;        //! number of used bytes of block file
    unsigned int nUndoSize;    //! number of used bytes in the undo file
    unsigned int nHeightFirst; //! lowest height of block in file
    unsigned int nHeightLast;  //! highest height of block in file
    uint64_t nTimeFirst;         //! earliest time of block in file
    uint64_t nTimeLast;          //! latest time of block in file

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(VARINT(nBlocks));
        READWRITE(VARINT(nSize));
        READWRITE(VARINT(nUndoSize));
        READWRITE(VARINT(nHeightFirst));
        READWRITE(VARINT(nHeightLast));
        READWRITE(VARINT(nTimeFirst));
        READWRITE(VARINT(nTimeLast));
    }

     void SetNull() {
         nBlocks = 0;
         nSize = 0;
         nUndoSize = 0;
         nHeightFirst = 0;
         nHeightLast = 0;
         nTimeFirst = 0;
         nTimeLast = 0;
     }

     CBlockFileInfo() {
         SetNull();
     }

     std::string ToString() const;

     /** update statistics (does not update nSize) */
     void AddBlock(unsigned int nHeightIn, uint64_t nTimeIn) {
         if (nBlocks==0 || nHeightFirst > nHeightIn)
             nHeightFirst = nHeightIn;
         if (nBlocks==0 || nTimeFirst > nTimeIn)
             nTimeFirst = nTimeIn;
         nBlocks++;
         if (nHeightIn > nHeightLast)
             nHeightLast = nHeightIn;
         if (nTimeIn > nTimeLast)
             nTimeLast = nTimeIn;
     }
};

/** RAII wrapper for VerifyDB: Verify consistency of the block and coin databases */
class CVerifyDB {
public:
    CVerifyDB();
    ~CVerifyDB();
    bool VerifyDB(CCoinsView *coinsview, int nCheckLevel, int nCheckDepth);
};

/** Find the last common block between the parameter chain and a locator. */
CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator);

/** Mark a block as invalid. */
bool InvalidateBlock(CValidationState& state, CBlockIndex *pindex);

/** Remove invalidity status from a block and its descendants. */
bool ReconsiderBlock(CValidationState& state, CBlockIndex *pindex);

/** The currently-connected chain of blocks. */
extern CChain chainActive;

/** Global variable that points to the active CCoinsView (protected by cs_main) */
extern CCoinsViewCache *pcoinsTip;

/** Global variable that points to the active block tree (protected by cs_main) */
extern CBlockTreeDB *pblocktree;

/**
 * Check if the output nIn is CF Reward
 */
bool IsCommunityFund(const CCoins *coins, int nIn);

namespace Consensus {
bool CheckTxInputs(const CTransactionBase& txBase, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, const Consensus::Params& consensusParams);
}

struct CTransactionNetworkObj
{
    CTransaction tx;
    CScCertificate cert;

    int32_t nVersion;

    bool IsCertificate() const { return (nVersion == SC_CERT_VERSION); }
    bool IsTx() const { return !IsCertificate(); }

    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        SerializationOp(s, CSerActionUnserialize(), nType, nVersion);
    }

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {

        ::Unserialize(s, this->nVersion, nType, nVersion);
        nVersion = this->nVersion;
        s.Rewind(sizeof(nVersion));

        if (this->IsCertificate())
        {
            ::Unserialize(s, *const_cast<CScCertificate*>(&cert), nType, nVersion);
        }
        else
        {
            ::Unserialize(s, *const_cast<CTransaction*>(&tx), nType, nVersion);
        }
    }
};

#endif // BITCOIN_MAIN_H
