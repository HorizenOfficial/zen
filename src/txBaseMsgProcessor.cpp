#include <txBaseMsgProcessor.h>
#include <main.h>
#include <util.h>
#include <consensus/validation.h>

#include <set>

void TxBaseMsgProcessor::addTxBaseMsgToProcess(const CTransactionBase& txBase, CNodeInterface* pfrom)
{
    CInv inv(MSG_TX, txBase.GetHash());
    pfrom->AddInventoryKnown(inv);

    LOCK(cs_main);

    pfrom->StopAskingFor(inv);
    mapAlreadyAskedFor.erase(inv);

    if(AlreadyHave(inv))
    {
        if (pfrom->IsWhiteListed())
        {
            // Always relay transactions received from whitelisted peers, even
            // if they were already in the mempool or rejected from it due
            // to policy, allowing the node to function as a gateway for
            // nodes hidden behind it.
            //
            // Non-zero DoS txes should never be relayed, but here we are going to relay
            // right away, without re-checking. Why? Because on first reception, node would be banned already
            LogPrintf("Force relaying tx %s from whitelisted peer=%d\n", txBase.GetHash().ToString(), pfrom->GetId());
            txBase.Relay();
        }
        return;
    }

    TxBaseMsg_DataToProcess dataToAdd;
    dataToAdd.txBaseHash   = txBase.GetHash();
    dataToAdd.sourceNodeId = pfrom->GetId();
    dataToAdd.pTxBase      = txBase.MakeShared();
    dataToAdd.pSourceNode  = pfrom;
    processTxBaseMsg_WorkQueue.push_back(dataToAdd);
    return;
}

void TxBaseMsgProcessor::ProcessTxBaseMsg(const processMempoolTx& mempoolProcess)
{
    LOCK(cs_main);
    std::vector<uint256> vEraseQueue;
    std::set<NodeId> setMisbehaving;

    while (!processTxBaseMsg_WorkQueue.empty())
    {
        const uint256& hashToProcess        = processTxBaseMsg_WorkQueue.at(0).txBaseHash;
        NodeId sourceNodeId                 = processTxBaseMsg_WorkQueue.at(0).sourceNodeId;
        const CTransactionBase& txToProcess = *processTxBaseMsg_WorkQueue.at(0).pTxBase;
        CNodeInterface* pSourceNode         =  processTxBaseMsg_WorkQueue.at(0).pSourceNode;

        if (setMisbehaving.count(sourceNodeId))
        {
            vEraseQueue.push_back(hashToProcess);
            processTxBaseMsg_WorkQueue.erase(processTxBaseMsg_WorkQueue.begin());
            continue;
        }

        CValidationState state;
        MempoolReturnValue res = mempoolProcess(mempool, state, txToProcess, LimitFreeFlag::ON,RejectAbsurdFeeFlag::OFF);
        mempool.check(pcoinsTip);

        if (res == MempoolReturnValue::VALID)
        {
            LogPrint("mempool", "%s(): peer=%d %s: accepted (poolsz %u)\n", __func__, sourceNodeId, hashToProcess.ToString(), mempool.size());

            txToProcess.Relay();

            vEraseQueue.push_back(hashToProcess);

            std::map<uint256, std::set<uint256> >::iterator unlockedOrphansIt = mapOrphanTransactionsByPrev.find(hashToProcess);
            if (unlockedOrphansIt == mapOrphanTransactionsByPrev.end())
            {
                processTxBaseMsg_WorkQueue.erase(processTxBaseMsg_WorkQueue.begin());
                continue; //hashToProcess does not unlock any orphan
            }

            for(const uint256& orphanHash: unlockedOrphansIt->second)
            {
                TxBaseMsg_DataToProcess dataToAdd;
                dataToAdd.txBaseHash   = orphanHash;
                dataToAdd.sourceNodeId = mapOrphanTransactions.at(orphanHash).fromPeer;
                dataToAdd.pTxBase      = mapOrphanTransactions.at(orphanHash).tx;
                dataToAdd.pSourceNode  = nullptr;

                processTxBaseMsg_WorkQueue.push_back(dataToAdd);
            }

            processTxBaseMsg_WorkQueue.erase(processTxBaseMsg_WorkQueue.begin());
        }

        if (res == MempoolReturnValue::MISSING_INPUT)
        {
            if (txToProcess.GetVjoinsplit().size() != 0)
            {
                // prohibit joinsplits from entering mapOrphans but relay right away if it's from whitelisted node
                MarkAsRejected(hashToProcess);
                if (pSourceNode != nullptr && pSourceNode->IsWhiteListed())
                {
                    LogPrintf("Force relaying tx %s from whitelisted peer=%d\n", hashToProcess.ToString(), sourceNodeId);
                    txToProcess.Relay();
                }
            } else
            {
                bool newTx = AddOrphanTx(txToProcess, sourceNodeId);
                if (newTx)
                {
                    // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
                    unsigned int nMaxOrphanTx = (unsigned int) (std::max((int64_t) (0), GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS)));
                    unsigned int nEvicted = LimitOrphanTxSize(nMaxOrphanTx);
                    if (nEvicted > 0)
                        LogPrint("mempool", "mapOrphan overflow, removed %u tx\n", nEvicted);
                }
            }

            processTxBaseMsg_WorkQueue.erase(processTxBaseMsg_WorkQueue.begin());
        }

        if (res == MempoolReturnValue::INVALID)
        {
            // Has inputs but not accepted to mempool
            // Probably non-standard or insufficient fee/priority
            LogPrint("mempool", "%s from peer=%d was not accepted into the memory pool: %s\n",
                    hashToProcess.ToString(), sourceNodeId, state.GetRejectReason());

            MarkAsRejected(hashToProcess);
            int nDoS = 0;
            state.IsInvalid(nDoS); //retrieve nDoS
            if (nDoS > 0)
            {
                Misbehaving(sourceNodeId, nDoS);
                setMisbehaving.insert(sourceNodeId);
            }

            if (pSourceNode != nullptr)
            {
                pSourceNode->PushMessage("reject", std::string("tx"), state.GetRejectCode(),
                        state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH),
                        hashToProcess);

                if ((nDoS == 0) && (pSourceNode->IsWhiteListed()))
                {
                    LogPrintf("Force relaying tx %s from whitelisted peer=%d\n", hashToProcess.ToString(), sourceNodeId);
                    txToProcess.Relay();
                }
                if ((nDoS > 0) && (pSourceNode->IsWhiteListed()))
                {
                    LogPrintf( "Not relaying invalid transaction %s from whitelisted peer=%d (%s (code %d))\n",
                            hashToProcess.ToString(), sourceNodeId, state.GetRejectReason(), state.GetRejectCode());
                }
            }

            vEraseQueue.push_back(hashToProcess);
            processTxBaseMsg_WorkQueue.erase(processTxBaseMsg_WorkQueue.begin());
        }
    }

    for(const uint256& hash: vEraseQueue)
        EraseOrphanTx(hash);
}


// Orphan Txes/Certs Tracker section
bool TxBaseMsgProcessor::AddOrphanTx(const CTransactionBase& txObj, NodeId peer)
{
    const uint256& hash = txObj.GetHash();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 10,000 orphans, each of which is at most 5,000 bytes big is
    // at most 500 megabytes of orphans:
    unsigned int sz = txObj.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
    if (sz > 5000)
    {
        LogPrint("mempool", "ignoring large orphan tx (size: %u, hash: %s)\n", sz, hash.ToString());
        return false;
    }

    mapOrphanTransactions[hash].tx = txObj.MakeShared();
    mapOrphanTransactions[hash].fromPeer = peer;
    for(const CTxIn& txin: txObj.GetVin())
        mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);

    LogPrint("mempool", "stored orphan tx %s (mapsz %u prevsz %u)\n", hash.ToString(),
             mapOrphanTransactions.size(), mapOrphanTransactionsByPrev.size());
    return true;
}

void TxBaseMsgProcessor::EraseOrphanTx(const uint256& hash)
{
    std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end())
        return;

    for(const CTxIn& txin: it->second.tx->GetVin())
    {
        std::map<uint256, std::set<uint256> >::iterator itPrev = mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == mapOrphanTransactionsByPrev.end())
            continue;

        itPrev->second.erase(hash);
        if (itPrev->second.empty())
            mapOrphanTransactionsByPrev.erase(itPrev);
    }

    mapOrphanTransactions.erase(it);
}

bool TxBaseMsgProcessor::IsOrphan(const uint256& txBaseHash) const
{
    return mapOrphanTransactions.count(txBaseHash) != 0;
}

const CTransactionBase* TxBaseMsgProcessor::PickRandomOrphan()
{
    uint256 randomhash = GetRandHash();
    std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
    if (it == mapOrphanTransactions.end())
        it = mapOrphanTransactions.begin();

    return (it->second.tx).get();
}

unsigned int TxBaseMsgProcessor::LimitOrphanTxSize(unsigned int nMaxOrphans)
{
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans)
    {
    	EraseOrphanTx(PickRandomOrphan()->GetHash());
        ++nEvicted;
    }
    return nEvicted;
}

unsigned int TxBaseMsgProcessor::countOrphans() const
{
    return mapOrphanTransactions.size();
}

void TxBaseMsgProcessor::EraseOrphansFor(NodeId peer)
{
    int nErased = 0;
    std::map<uint256, COrphanTx>::iterator iter = mapOrphanTransactions.begin();
    while (iter != mapOrphanTransactions.end())
    {
        std::map<uint256, COrphanTx>::iterator maybeErase = iter++; // increment to avoid iterator becoming invalid
        if (maybeErase->second.fromPeer == peer)
        {
            EraseOrphanTx(maybeErase->second.tx->GetHash());
            ++nErased;
        }
    }
    if (nErased > 0) LogPrint("mempool", "Erased %d orphan tx from peer %d\n", nErased, peer);
}

void TxBaseMsgProcessor::clearOrphans()
{
	mapOrphanTransactions.clear();
	mapOrphanTransactionsByPrev.clear();
	return;
}
// End of Orphan Txes/Certs Tracker section

// Rejected Txes/Certs Tracker section
void TxBaseMsgProcessor::SetupRejectionFilter(unsigned int nElements, double nFPRate)
{
    recentRejects.reset(new CRollingBloomFilter(nElements, nFPRate));
}

void TxBaseMsgProcessor::ResetRejectionFilter()
{
    recentRejects.reset(nullptr);
}

bool TxBaseMsgProcessor::HasBeenRejected(const uint256& txBaseHash) const
{
    assert(recentRejects);
    return recentRejects->contains(txBaseHash);
}

void TxBaseMsgProcessor::MarkAsRejected(const uint256& txBaseHash)
{
    assert(recentRejects);
    recentRejects->insert(txBaseHash);
}

void TxBaseMsgProcessor::RefreshRejected(const uint256& currentTipHash)
{
    assert(recentRejects);
    if (currentTipHash != hashRecentRejectsChainTip)
    {
        // If the chain tip has changed previously rejected transactions
        // might be now valid, e.g. due to a nLockTime'd tx becoming valid,
        // or a double-spend. Reset the rejects filter and give those
        // txs a second chance.
        hashRecentRejectsChainTip = currentTipHash;
        ResetRejectionFilter();
    }
}
// End of Rejected Txes/Certs Tracker section
