#include <txBaseMsgProcessor.h>
#include <sc/proofverifier.h>
#include <main.h>
#include <util.h>
#include <consensus/validation.h>

#include <set>

TxBaseMsgProcessor::TxBaseMsg_DataToProcess::TxBaseMsg_DataToProcess():
    txBaseHash(), sourceNodeId(-1),
    pTxBase(nullptr), pSourceNode(nullptr),
    txBaseProcessingState(MempoolReturnValue::NOT_PROCESSED_YET), txBaseValidationState() {};

TxBaseMsgProcessor::TxBaseMsgProcessor() { this->reset(); }
TxBaseMsgProcessor::~TxBaseMsgProcessor() { this->reset(); }

void TxBaseMsgProcessor::reset()
{
    {
        LOCK(cs_rejectedTxes);
        recentRejects.reset(nullptr);
        recentRejects.reset(new CRollingBloomFilter(120000, 0.000001));
    }

    {
        LOCK(cs_orphanTxes);
        mapOrphanTransactions.clear();
        mapOrphanTransactionsByPrev.clear();
    }
    return;
}

void TxBaseMsgProcessor::addTxBaseMsgToProcess(const CTransactionBase& txBase, CNodeInterface* pfrom)
{
    CInv inv(MSG_TX, txBase.GetHash());
    pfrom->AddInventoryKnown(inv);

    pfrom->StopAskingFor(inv);
    mapAlreadyAskedFor.erase(inv);

    bool bAlreadyHave = false;
    {
        LOCK(cs_main);
        bAlreadyHave = AlreadyHave(inv);
    }
    if(bAlreadyHave)
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
    dataToAdd.txBaseHash             = txBase.GetHash();
    dataToAdd.sourceNodeId           = pfrom->GetId();
    dataToAdd.pTxBase                = txBase.MakeShared();
    dataToAdd.pSourceNode            = pfrom;
    dataToAdd.txBaseProcessingState  = MempoolReturnValue::NOT_PROCESSED_YET;

    boost::unique_lock<boost::mutex> lock(mutex);
    txBaseMsgQueue.push_back(dataToAdd);
    lock.unlock();
    condWorker.notify_one();

    return;
}

void TxBaseMsgProcessor::startLoop(const processMempoolTx& mempoolProcess)
{
    while(true)
    {
        boost::unique_lock<boost::mutex> lock(mutex);
        condWorker.wait(lock); // wait
        ProcessTxBaseMsg(mempoolProcess); //this is automatically protected by the lock
    }
    return;
}

void TxBaseMsgProcessor::ProcessTxBaseMsg(const processMempoolTx& mempoolProcess)
{
    std::vector<uint256> vEraseQueue;
    std::set<NodeId> setMisbehaving;

    while (!txBaseMsgQueue.empty())
    {
         //Copy to allow immediate erasure from queue
        TxBaseMsg_DataToProcess dataToProcess = txBaseMsgQueue.at(0);
        txBaseMsgQueue.erase(txBaseMsgQueue.begin());

        const uint256& hashToProcess        = dataToProcess.txBaseHash;
        NodeId sourceNodeId                 = dataToProcess.sourceNodeId;
        const CTransactionBase& txToProcess = *dataToProcess.pTxBase;
        CNodeInterface* pSourceNode         = dataToProcess.pSourceNode;
        CValidationState& dataState         = dataToProcess.txBaseValidationState;
        if (setMisbehaving.count(sourceNodeId))
        {
            vEraseQueue.push_back(hashToProcess);
            continue;
        }

        if (dataToProcess.txBaseProcessingState == MempoolReturnValue::NOT_PROCESSED_YET)
        {
            LOCK(cs_main);
            //Note: sc proof validation is skipped here, to be able to batch it with potentially others
            dataToProcess.txBaseProcessingState = mempoolProcess(mempool, dataState,
                txToProcess, LimitFreeFlag::ON,RejectAbsurdFeeFlag::OFF, ValidateSidechainProof::OFF);

            txBaseMsgQueue.push_back(dataToProcess); //Reinsert dataToProcess
        } else if (dataToProcess.txBaseProcessingState == MempoolReturnValue::VALID)
        {
            LogPrint("mempool", "%s(): peer=%d %s: accepted (poolsz %u)\n", __func__, sourceNodeId, hashToProcess.ToString(), mempool.size());

            txToProcess.Relay();

            vEraseQueue.push_back(hashToProcess);

            std::map<uint256, std::set<uint256> >::iterator unlockedOrphansIt = mapOrphanTransactionsByPrev.find(hashToProcess);
            if (unlockedOrphansIt == mapOrphanTransactionsByPrev.end())
            {
                continue; //hashToProcess does not unlock any orphan
            }

            for(const uint256& orphanHash: unlockedOrphansIt->second)
            {
                TxBaseMsg_DataToProcess dataToAdd;
                dataToAdd.txBaseHash   = orphanHash;
                dataToAdd.sourceNodeId = mapOrphanTransactions.at(orphanHash).fromPeer;
                dataToAdd.pTxBase      = mapOrphanTransactions.at(orphanHash).tx;
                dataToAdd.pSourceNode  = nullptr;

                txBaseMsgQueue.push_back(dataToAdd);
            }
        } else if (dataToProcess.txBaseProcessingState == MempoolReturnValue::MISSING_INPUT)
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
        } else if (dataToProcess.txBaseProcessingState == MempoolReturnValue::INVALID)
        {
            // Has inputs but not accepted to mempool
            // Probably non-standard or insufficient fee/priority
            LogPrint("mempool", "%s from peer=%d was not accepted into the memory pool: %s\n",
                    hashToProcess.ToString(), sourceNodeId, dataState.GetRejectReason());

            MarkAsRejected(hashToProcess);
            int nDoS = dataState.GetDoS();
            if (nDoS > 0)
            {
                {
                    LOCK(cs_main);
                    Misbehaving(sourceNodeId, nDoS);
                }
                setMisbehaving.insert(sourceNodeId);
            }

            if (pSourceNode != nullptr)
            {
                pSourceNode->PushMessage("reject", std::string("tx"), CValidationState::CodeToChar(dataState.GetRejectCode()),
                        dataState.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH),
                        hashToProcess);

                if ((nDoS == 0) && (pSourceNode->IsWhiteListed()))
                {
                    LogPrintf("Force relaying tx %s from whitelisted peer=%d\n", hashToProcess.ToString(), sourceNodeId);
                    txToProcess.Relay();
                }
                if ((nDoS > 0) && (pSourceNode->IsWhiteListed()))
                {
                    LogPrintf( "Not relaying invalid transaction %s from whitelisted peer=%d (%s (code %d))\n",
                            hashToProcess.ToString(), sourceNodeId, dataState.GetRejectReason(), CValidationState::CodeToChar(dataState.GetRejectCode()));
                }
            }

            vEraseQueue.push_back(hashToProcess);
        } else if (dataToProcess.txBaseProcessingState == MempoolReturnValue::PARTIALLY_VALIDATED)
        {
            LOCK2(cs_main, mempool.cs);
            CCoinsViewMemPool backingView(pcoinsTip, mempool);
            CCoinsViewCache supportView(&backingView);
            CScProofVerifier scVerifier{CScProofVerifier::Verification::Strict};
            std::map<uint256,unsigned int> hashToIdx;

            // update in-place all PARTIALLY_VALIDATED txes/certs
            txBaseMsgQueue.push_back(dataToProcess); //Reinsert dataToProcess to process all PARTIALLY_VALIDATED txes
            for(unsigned int idx = 0; idx < txBaseMsgQueue.size(); ++idx)
            {
                TxBaseMsg_DataToProcess& enqueuedItem = txBaseMsgQueue.at(idx);
                if (enqueuedItem.txBaseProcessingState != MempoolReturnValue::PARTIALLY_VALIDATED)
                    continue;

                // Mempool may have changed since partial validation has been made. Recheck for double spends
                if (!supportView.HaveInputs(*enqueuedItem.pTxBase))
                {
                    enqueuedItem.txBaseProcessingState = MempoolReturnValue::INVALID;
                    enqueuedItem.txBaseValidationState.Invalid(error("%s(): inputs already spent", __func__),
                                         CValidationState::Code::DUPLICATE, "bad-txns-inputs-spent");
                    continue;
                }

                if(enqueuedItem.pTxBase->IsCertificate())
                {
                    const CScCertificate& cert = dynamic_cast<const CScCertificate&>(*enqueuedItem.pTxBase);

                    // Mempool may have changed since partial validation has been made. Recheck for conflicts
                    if (!mempool.checkIncomingCertConflicts(cert))
                    {
                        enqueuedItem.txBaseProcessingState = MempoolReturnValue::INVALID;
                        continue;
                    }

                    std::pair<uint256, CAmount> conflictingCertData = mempool.FindCertWithQuality(cert.GetScId(), cert.quality);
                    CAmount nFees = cert.GetFeeAmount(supportView.GetValueIn(cert));
                    if (!conflictingCertData.first.IsNull() && conflictingCertData.second >= nFees)
                    {
                        LogPrintf("%s():%d - Dropping cert %s : low fee and same quality as other cert in mempool\n",
                                __func__, __LINE__, cert.GetHash().ToString());
                        enqueuedItem.txBaseProcessingState = MempoolReturnValue::INVALID;
                        continue;
                    }

                    scVerifier.LoadDataForCertVerification(supportView, cert);
                } else
                {
                    const CTransaction& tx = dynamic_cast<const CTransaction&>(*enqueuedItem.pTxBase);

                    // Mempool may have changed since partial validation has been made. Recheck for conflicts
                    if (!mempool.checkIncomingTxConflicts(tx))
                    {
                        enqueuedItem.txBaseProcessingState = MempoolReturnValue::INVALID;
                        continue;
                    }

                    scVerifier.LoadDataForCswVerification(supportView, tx);
                }
                hashToIdx[enqueuedItem.pTxBase->GetHash()] = idx;
            }

            std::map</*certHash*/uint256, bool> resCert = scVerifier.batchVerifyCerts();
            for(const auto& certItem: resCert)
            {
                TxBaseMsg_DataToProcess& processedCert = txBaseMsgQueue.at(hashToIdx.at(certItem.first));
                if (certItem.second)
                {
                    const CScCertificate& cert = dynamic_cast<const CScCertificate&>(*processedCert.pTxBase);
                    if(!mempool.existsCert(cert.GetHash()))
                        StoreCertToMempool(cert, mempool, supportView);
                    processedCert.txBaseProcessingState = MempoolReturnValue::VALID;
                } else
                {
                    processedCert.txBaseValidationState.Invalid(error("%s():%d - ERROR: sc-related tx [%s] proofs do not verify\n",
                                  __func__, __LINE__, certItem.first.ToString()), CValidationState::Code::INVALID, "bad-sc-tx-proof");
                    processedCert.txBaseProcessingState = MempoolReturnValue::INVALID;
                }
            }

            std::map</*scTxHash*/uint256, bool> resCsw = scVerifier.batchVerifyCsws();

            for(const auto& scTxItem: resCsw)
            {
                TxBaseMsg_DataToProcess& processedScTx = txBaseMsgQueue.at(hashToIdx.at(scTxItem.first));

                if(resCsw.count(scTxItem.first) && !resCsw.at(scTxItem.first))
                {
                    processedScTx.txBaseValidationState.Invalid(error("%s():%d - ERROR: sc-related tx [%s] proofs do not verify\n",
                                  __func__, __LINE__, scTxItem.first.ToString()), CValidationState::Code::INVALID, "bad-sc-tx-proof");
                    processedScTx.txBaseProcessingState = MempoolReturnValue::INVALID;
                } else
                {
                    const CTransaction& scTx = dynamic_cast<const CTransaction&>(*processedScTx.pTxBase);
                    if(!mempool.existsTx(scTx.GetHash()))
                        StoreTxToMempool(scTx, mempool, supportView);
                    processedScTx.txBaseProcessingState = MempoolReturnValue::VALID;
                }
            }

            mempool.check(pcoinsTip);
        } else
        {
            assert(false && "Unhandled MempoolReturnValue");
        }
    }

    for(const uint256& hash: vEraseQueue)
        EraseOrphanTx(hash);

    return;
}


// Orphan Txes/Certs Tracker section
bool TxBaseMsgProcessor::AddOrphanTx(const CTransactionBase& txObj, NodeId peer)
{
    LOCK(cs_orphanTxes);
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
    LOCK(cs_orphanTxes);
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
    LOCK(cs_orphanTxes);
    return mapOrphanTransactions.count(txBaseHash) != 0;
}

const CTransactionBase* TxBaseMsgProcessor::PickRandomOrphan()
{
    LOCK(cs_orphanTxes);
    uint256 randomhash = GetRandHash();
    std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
    if (it == mapOrphanTransactions.end())
        it = mapOrphanTransactions.begin();

    return (it->second.tx).get();
}

unsigned int TxBaseMsgProcessor::LimitOrphanTxSize(unsigned int nMaxOrphans)
{
    LOCK(cs_orphanTxes);
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
    LOCK(cs_orphanTxes);
    return mapOrphanTransactions.size();
}

void TxBaseMsgProcessor::EraseOrphansFor(NodeId peer)
{
    LOCK(cs_orphanTxes);
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
// End of Orphan Txes/Certs Tracker section

// Rejected Txes/Certs Tracker section
void TxBaseMsgProcessor::SetupRejectionFilter(unsigned int nElements, double nFPRate)
{
    LOCK(cs_rejectedTxes);
    recentRejects.reset(new CRollingBloomFilter(nElements, nFPRate));
}

bool TxBaseMsgProcessor::HasBeenRejected(const uint256& txBaseHash) const
{
    LOCK(cs_rejectedTxes);
    assert(recentRejects);
    return recentRejects->contains(txBaseHash);
}

void TxBaseMsgProcessor::MarkAsRejected(const uint256& txBaseHash)
{
    LOCK(cs_rejectedTxes);
    assert(recentRejects);
    recentRejects->insert(txBaseHash);
}

void TxBaseMsgProcessor::RefreshRejected(const uint256& currentTipHash)
{
    LOCK(cs_rejectedTxes);
    assert(recentRejects);
    if (currentTipHash != hashRecentRejectsChainTip)
    {
        // If the chain tip has changed previously rejected transactions
        // might be now valid, e.g. due to a nLockTime'd tx becoming valid,
        // or a double-spend. Reset the rejects filter and give those
        // txs a second chance.
        hashRecentRejectsChainTip = currentTipHash;
        recentRejects.reset(nullptr);
        recentRejects.reset(new CRollingBloomFilter(120000, 0.000001));
    }
}
// End of Rejected Txes/Certs Tracker section
