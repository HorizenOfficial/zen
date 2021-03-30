#ifndef BITCOIN_TX_BASE_MSG_PROCESSOR_H
#define BITCOIN_TX_BASE_MSG_PROCESSOR_H

#include <functional>
#include <map>

#include <uint256.h>
#include <net.h>

class CTransactionBase;
class CNodeInterface;
class CTxMemPool;
class CValidationState;

// Accept Tx/Cert ToMempool parameters types and signature
enum class LimitFreeFlag       { ON, OFF };
enum class RejectAbsurdFeeFlag { ON, OFF };
enum class MempoolReturnValue { INVALID, MISSING_INPUT, VALID };

typedef std::function<MempoolReturnValue(CTxMemPool& pool, CValidationState &state, const CTransactionBase &txBase,
        LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee)> processMempoolTx;

class TxBaseMsgProcessor
{
public:
    static TxBaseMsgProcessor& get() {static TxBaseMsgProcessor theProcessor{}; return theProcessor; }

    // Tx/Certs processing Section
    void addTxBaseMsgToProcess(const CTransactionBase& txBase, CNodeInterface* pfrom);
    void ProcessTxBaseMsg(const processMempoolTx& mempoolProcess); // PUBLIC TILL SINGLE THREAD
    // End of Tx/Certs processing Section

    // Orphan Txes/Certs Tracker section
    bool AddOrphanTx(const CTransactionBase& txObj, NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void EraseOrphanTx(const uint256& hash)                      EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool IsOrphan(const uint256& txBaseHash) const               EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    const CTransactionBase* PickRandomOrphan()                   EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans)     EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    unsigned int countOrphans() const                            EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    void EraseOrphansFor(NodeId peer)                            EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void clearOrphans()                                          EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    // End of Orphan Txes/Certs Tracker section

    // Rejected Txes/Certs Tracker section
    void SetupRejectionFilter(unsigned int nElements, double nFPRate);
    void ResetRejectionFilter();
    bool HasBeenRejected(const uint256& txBaseHash) const;
    void RefreshRejected(const uint256& currentTipHash);
    // End of Rejected Txes/Certs Tracker section

private:
    TxBaseMsgProcessor() = default;
    ~TxBaseMsgProcessor() = default;
    TxBaseMsgProcessor(const TxBaseMsgProcessor&) = delete;
    TxBaseMsgProcessor& operator=(const TxBaseMsgProcessor&) = delete;

    struct COrphanTx {
        std::shared_ptr<const CTransactionBase> tx;
        NodeId fromPeer;
    };
    std::map<uint256, COrphanTx>          mapOrphanTransactions       GUARDED_BY(cs_main);
    std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev GUARDED_BY(cs_main);

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
    inline void MarkAsRejected(const uint256& txBaseHash);

    struct TxBaseMsg_DataToProcess
    {
        // required data
        uint256 txBaseHash;
        NodeId sourceNodeId;

        // optional data, only for txes which has not been processed yet, just to inform sender
        std::shared_ptr<const CTransactionBase> pTxBase; //owning pointer
        CNodeInterface* pSourceNode; //non-owning pointer. Null if source node already got its answer and we do not need to send any message to it

        TxBaseMsg_DataToProcess(): txBaseHash(), sourceNodeId(-1), pTxBase(nullptr), pSourceNode(nullptr) {};
    };
    std::vector<TxBaseMsg_DataToProcess> processTxBaseMsg_WorkQueue;
};

#endif
