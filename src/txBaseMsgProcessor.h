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

enum class LimitFreeFlag       { ON, OFF };
enum class RejectAbsurdFeeFlag { ON, OFF };
enum class MempoolReturnValue { INVALID, MISSING_INPUT, VALID };

typedef std::function<MempoolReturnValue(CTxMemPool& pool, CValidationState &state, const CTransactionBase &txBase,
        LimitFreeFlag fLimitFree, RejectAbsurdFeeFlag fRejectAbsurdFee)> processMempoolTx;

class TxBaseMsgProcessor
{
public:
	TxBaseMsgProcessor(const TxBaseMsgProcessor&) = delete;
	TxBaseMsgProcessor& operator=(const TxBaseMsgProcessor&) = delete;

	static TxBaseMsgProcessor& getProcessor() {static TxBaseMsgProcessor theProcessor{}; return theProcessor; }
    ~TxBaseMsgProcessor() = default;

    void addTxBaseMsgToProcess(const CTransactionBase& txBase, CNodeInterface* pfrom);
    void ProcessTxBaseMsg(const processMempoolTx& mempoolProcess); // PUBLIC TILL SINGLE THREAD

    struct COrphanTx {
        std::shared_ptr<const CTransactionBase> tx;
        NodeId fromPeer;
    };
    std::map<uint256, COrphanTx>          mapOrphanTransactions       GUARDED_BY(cs_main);
    std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev GUARDED_BY(cs_main);
    bool AddOrphanTx(const CTransactionBase& txObj, NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void EraseOrphanTx(uint256 hash)                             EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans)     EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void EraseOrphansFor(NodeId peer)                            EXCLUSIVE_LOCKS_REQUIRED(cs_main);
	void clearOrphans() { mapOrphanTransactions.clear(); mapOrphanTransactionsByPrev.clear(); return; }
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

private:
    TxBaseMsgProcessor() = default;
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
