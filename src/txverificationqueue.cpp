#include "primitives/transaction.h"
//#include "txverificationqueue.h"
#include "main.h"
//#include "coin.h"

void CTxVerificationQueueEntry::setTX(CTransaction newTx)
{
    tx = newTx;
};

CTransaction CTxVerificationQueueEntry::getTX()
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

std::shared_ptr<CTxVerificationQueueEntry> createCTxVerificationQueueEntry(CTransaction tx, CNode* pfrom)
{
    std::shared_ptr<CTxVerificationQueueEntry> ctxvqe (new CTxVerificationQueueEntry());

    ctxvqe->setTX(tx);

    NodeId nodeId = pfrom->GetId();
    ctxvqe->setNodeId(nodeId);

    return ctxvqe;
};

void CTxVerificationQueue::append(std::shared_ptr<CTxVerificationQueueEntry> ctxvqe)
{
    vectorTX.push_back(ctxvqe);
}

void CTxVerificationQueue::verifyOne()
{
    std::shared_ptr<CTxVerificationQueueEntry> ctxvqe = vectorTX.back();
    vectorTX.pop_back();
    CNode* node = FindNode(ctxvqe->getNodeId());
    if (node != NULL){
        checkOneTx(node, ctxvqe->getTX());
    }
}

bool CTxVerificationQueue::isEmpty(){
    return vectorTX.empty();
}
