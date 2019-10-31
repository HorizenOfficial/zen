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

class CTxVerificationQueue
{
public:
    std::deque<CTxVerificationQueueEntry> dequeTX;
    void createAndAppendCTxVerificationQueueEntry(const CTransaction& tx, NodeId nodeId);
    void verifyOne();
    bool isEmpty();
};