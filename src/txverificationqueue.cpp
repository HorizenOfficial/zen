#include "primitives/transaction.h"
//#include "txverificationqueue.h"
#include "main.h"
//#include "coin.h"

void CTxVerificationQueueEntry::setTX(const CTransaction& newTx)
{
    tx = newTx;
};

const CTransaction& CTxVerificationQueueEntry::getTX()
{
    return tx;
};

void CTxVerificationQueueEntry::setNodeId(NodeId newNodeId){
    nodeId = newNodeId;
}

NodeId CTxVerificationQueueEntry::getNodeId()
{
    return nodeId;
}

CTxVerificationQueueEntry* createCTxVerificationQueueEntry(const CTransaction& tx, NodeId nodeId)
{
    CTxVerificationQueueEntry* ctxvqe = new CTxVerificationQueueEntry();

    ctxvqe->setTX(tx);
    ctxvqe->setNodeId(nodeId);

    return ctxvqe;
};

void CTxVerificationQueue::append(CTxVerificationQueueEntry* ctxvqe)
{
    dequeTX.push_back(ctxvqe);
}

void CTxVerificationQueue::verifyOne()
{
    CTxVerificationQueueEntry* ctxvqe = dequeTX.front();
    CNode* node = FindNode(ctxvqe->getNodeId());
    if (node != NULL){
        checkOneTx(node, ctxvqe->getTX());
    }
    dequeTX.pop_front();
    delete ctxvqe;
}

bool CTxVerificationQueue::isEmpty(){
    return dequeTX.empty();
}
