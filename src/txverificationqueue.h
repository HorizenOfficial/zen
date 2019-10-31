class CTxVerificationQueueEntry
{
private:
    CTransaction tx;
    NodeId nodeId;
public:
    void setTX(const CTransaction& newTx);
    const CTransaction& getTX();
    void setNodeId(NodeId newNodeId);
    NodeId getNodeId();
};

CTxVerificationQueueEntry* createCTxVerificationQueueEntry(const CTransaction& tx, NodeId nodeId);

class CTxVerificationQueue
{
public:
    std::deque<CTxVerificationQueueEntry*> dequeTX;
    void append(CTxVerificationQueueEntry* ctxvqe);
    void verifyOne();
    bool isEmpty();
};