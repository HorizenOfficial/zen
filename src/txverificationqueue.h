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

std::shared_ptr<CTxVerificationQueueEntry> createCTxVerificationQueueEntry(const CTransaction& tx, NodeId nodeId);

class CTxVerificationQueue
{
public:
    std::vector<std::shared_ptr<CTxVerificationQueueEntry>> vectorTX;;
    void append(std::shared_ptr<CTxVerificationQueueEntry> ctxvqe);
    void verifyOne();
    bool isEmpty();
};