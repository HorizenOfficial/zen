class CTxVerificationQueueEntry
{
private:
    CTransaction tx;
    CAmount nFee;
    size_t nTxSize;
    NodeId nodeId;
public:
    void setTX(CTransaction newTx);
    void setNFee(CAmount newNFee);
    void setNTxSize(size_t newNTxSize);
    CTransaction getTX();
    void setNodeId(NodeId newNodeId);
    NodeId getNodeId();
};

std::shared_ptr<CTxVerificationQueueEntry> createCTxVerificationQueueEntry(CTransaction tx, CNode* pfrom);

class CTxVerificationQueue
{
public:
    std::vector<std::shared_ptr<CTxVerificationQueueEntry>> vectorTX;;
    void append(std::shared_ptr<CTxVerificationQueueEntry> ctxvqe);
    void verifyOne();
    bool isEmpty();
};