#include "primitives/transaction.h"
//#include "txverificationqueue.h"
#include "main.h"
//#include "coin.h"

void CTxVerificationQueueEntry::setTX(const CTransaction& newTx)
{
    tx = newTx;
};

const CTransaction& CTxVerificationQueueEntry::getTX() const
{
    return tx;
};

void CTxVerificationQueueEntry::setNodeId(NodeId newNodeId){
    nodeId = newNodeId;
}

NodeId CTxVerificationQueueEntry::getNodeId() const
{
    return nodeId;
}

void CTxVerificationQueue::createAndAppendCTxVerificationQueueEntry(const CTransaction& tx, NodeId nodeId)
{
    CTxVerificationQueueEntry ctxvqe = CTxVerificationQueueEntry();

    ctxvqe.setTX(tx);
    ctxvqe.setNodeId(nodeId);

    dequeTX.push_back(ctxvqe);
}

void CTxVerificationQueue::verifyOne()
{
    const CTxVerificationQueueEntry& ctxvqe = dequeTX.front();
    CNode* node = FindNode(ctxvqe.getNodeId());
    if (node != NULL){
        checkOneTx(node, ctxvqe.getTX());
    }
    dequeTX.pop_front();
}

bool CTxVerificationQueue::isEmpty(){
    return dequeTX.empty();
}
