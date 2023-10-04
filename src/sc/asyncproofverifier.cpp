#include "asyncproofverifier.h"
#include "proofverificationmanager.hpp"

#include "coins.h"
#include "init.h"
#include "main.h"
#include "util.h"
#include "primitives/certificate.h"

const uint32_t CScAsyncProofVerifier::BATCH_VERIFICATION_MAX_DELAY = 5000;   /**< The maximum delay in milliseconds between batch verification requests */
const uint32_t CScAsyncProofVerifier::BATCH_VERIFICATION_MAX_SIZE = 10;      /**< The threshold size of the proof queue that triggers a call to the batch verification. */


#ifndef BITCOIN_TX
void CScAsyncProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom)
{
    LOCK(cs_asyncQueue);
    CScProofVerifier::LoadDataForCertVerification(view, scCert, pfrom);
    proofsInsertionMillisecondsQueue.insert({scCert.GetHash(), GetTimeMillis()});
    assert(proofsQueue.size() == proofsInsertionMillisecondsQueue.size());
}

void CScAsyncProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom)
{
    LOCK(cs_asyncQueue);
    CScProofVerifier::LoadDataForCswVerification(view, scTx, pfrom);
    proofsInsertionMillisecondsQueue.insert({scTx.GetHash(), GetTimeMillis()});
    assert(proofsQueue.size() == proofsInsertionMillisecondsQueue.size());
}
#endif

uint32_t CScAsyncProofVerifier::GetCustomMaxBatchVerifyDelay()
{
    int32_t delay = GetArg("-scproofverificationdelay", BATCH_VERIFICATION_MAX_DELAY);
    if (delay < 0)
    {
        LogPrintf("%s():%d - ERROR: scproofverificationdelay=%d, must be non negative, setting to default value = %d\n",
            __func__, __LINE__, delay, BATCH_VERIFICATION_MAX_DELAY);
        delay = BATCH_VERIFICATION_MAX_DELAY;
    }
    return static_cast<uint32_t>(delay);
}

uint32_t CScAsyncProofVerifier::GetCustomMaxBatchVerifyMaxSize()
{
    int32_t size = GetArg("-scproofqueuesize", BATCH_VERIFICATION_MAX_SIZE);
    if (size < 0)
    {
        LogPrintf("%s():%d - ERROR: scproofqueuesize=%d, must be non negative, setting to default value = %d\n",
            __func__, __LINE__, size, BATCH_VERIFICATION_MAX_SIZE);
        size = BATCH_VERIFICATION_MAX_SIZE;
    }
    return static_cast<uint32_t>(size);
}

/**
 * @brief A function that periodically performs batch verification over the queued proofs.
 * It should run on a dedicated thread.
 */
void CScAsyncProofVerifier::RunPeriodicVerification()
{
    /**
     * The age of the queue in milliseconds.
     * This value represents the time spent in the queue by the oldest proof in the queue.
     */
    uint32_t queueAge = 0;
    std::map<uint256, uint64_t>::iterator oldestProof;

    uint32_t batchVerificationMaxDelay = GetCustomMaxBatchVerifyDelay();
    uint32_t batchVerificationMaxSize  = GetCustomMaxBatchVerifyMaxSize();

    auto proofVerificationManager = &CScProofVerificationManager::GetInstance();

    while (!ShutdownRequested())
    {
        {
            LOCK(cs_asyncQueue);

            if (proofsQueue.size() > 0)
            {
                {
                    LOCK(proofVerificationManager->cs_proofsVerificationsResults);
                    size_t clearedProofs = 0;

                    if (proofVerificationManager->mostRecentProofsVerificationsResults.size() > 0)
                    {
                        for (auto it = proofsQueue.cbegin(); it != proofsQueue.cend(); )
                        {
                            if (proofVerificationManager->mostRecentProofsVerificationsResults.find(it->first) !=
                                proofVerificationManager->mostRecentProofsVerificationsResults.end())
                            {
                                ++clearedProofs;
                                LogPrint("cert", "%s():%d - %s proof cleared from async verification queue\n",
                                         __func__, __LINE__, it->first.ToString());
                                proofsInsertionMillisecondsQueue.erase(it->first);
                                proofsQueue.erase(it++);
                            }
                            else
                            {
                                ++it;
                            }
                        }
                        assert(proofsQueue.size() == proofsInsertionMillisecondsQueue.size());
                        if (BOOST_UNLIKELY(Params().NetworkIDString() == "regtest"))
                        {
                            stats.removedFromQueueCounter += clearedProofs;
                        }
                    }
                }

                if (proofsQueue.size() > 0)
                {
                    oldestProof = std::min_element(proofsInsertionMillisecondsQueue.begin(),
                                                   proofsInsertionMillisecondsQueue.end(),
                                                   [](const auto& l, const auto& r) { return l.second < r.second; });
                    queueAge = GetTimeMillis() - oldestProof->second;

                    /**
                     * The batch verification can be triggered by two events:
                     * 
                     * 1. The queue has grown up beyond the threshold size;
                     * 2. The oldest proof in the queue has waited for too long.
                     */
                    if (queueAge > batchVerificationMaxDelay || proofsQueue.size() > batchVerificationMaxSize)
                    {
                        {
                            LOCK(cs_asyncQueueInVerification);

                            LogPrint("cert", "%s():%d - Async verification triggered, %d proofs to be verified \n",
                                     __func__, __LINE__, proofsQueue.size());

                            // Move the queued proofs into map dedicated to proofs in verification, so that we can release the lock
                            proofsInVerificationQueue = std::move(proofsQueue);
                            proofsQueue.clear();
                            proofsInsertionMillisecondsQueue.clear();

                            assert(proofsQueue.size() == 0);
                            assert(proofsInsertionMillisecondsQueue.size() == 0);
                        }
                    }
                }
            }
        }

        {
            if (proofsInVerificationQueue.size() > 0)
            {
                BatchVerifyInternal(proofsInVerificationQueue);
                ProcessVerificationOutputs();

                if (proofsInVerificationQueue.size() > 0)
                {
                    LogPrint("cert", "%s():%d - Batch verification failed, removed proofs that caused the failure and trying again... \n", __func__, __LINE__);

                    BatchVerifyInternal(proofsInVerificationQueue);
                    ProcessVerificationOutputs();

                    if (proofsInVerificationQueue.size() > 0)
                    {
                        LogPrint("cert", "%s():%d - Batch verification failed again, verifying proofs one by one... \n", __func__, __LINE__);

                        // As last attempt, verify the proofs one by one.
                        NormalVerify(proofsInVerificationQueue);
                        ProcessVerificationOutputs();
                    }

                    assert(proofsInVerificationQueue.size() == 0);
                }
            }
        }

        MilliSleep(THREAD_WAKE_UP_PERIOD);
    }
}

/**
 * @brief Process the outputs of the batch verification.
 * This function is meant to process all the outputs having a state PASSED or FAILED;
 * the related proofs are then removed from the proofs map.
 * 
 * So, when this function returns:
 * 
 * 1. All the transactions/certificates that passed the verification are resubmitted to
 * the memory pool for continuing the add operation and the related proofs are removed
 * from the cswProofs and certProofs maps.
 * 
 * 2. All the transactions/certificates that did not pass the verification are rejected
 * and the sender nodes are notified and the related proofs are removed
 * from the cswProofs and certProofs maps.
 * 
 * 3. All the transactions/certificates that are in an UNKNOWN state are not processed
 * and are kept into the related maps.
 */
void CScAsyncProofVerifier::ProcessVerificationOutputs()
{
    LOCK(cs_asyncQueueInVerification);
    size_t discardedProofsVerifications = 0;

    if (discardAllProofsVerifications)
    {
        discardedProofsVerifications = proofsInVerificationQueue.size();
        proofsInVerificationQueue.clear();
        discardAllProofsVerifications = false;
        LogPrint("cert", "%s():%d - Discarding %d proofs verifications from current processing\n",
                 __func__, __LINE__, discardedProofsVerifications);
    }
    else if (proofsVerificationsToDiscard.size() > 0)
    {
        for (auto& proofVerificationToDiscard : proofsVerificationsToDiscard)
        {
            if (proofsInVerificationQueue.erase(proofVerificationToDiscard) > 0)
            {
                ++discardedProofsVerifications;
                LogPrint("cert", "%s():%d - Discarding proof verification for proof %s from current processing\n",
                         __func__, __LINE__, proofVerificationToDiscard.ToString());
            }
        }
        proofsVerificationsToDiscard.clear();
    }
    if (BOOST_UNLIKELY(Params().NetworkIDString() == "regtest"))
    {
        stats.discardedResultCounter += discardedProofsVerifications;
    }

    // Post processing of proofs
    for (auto i = proofsInVerificationQueue.begin(); i != proofsInVerificationQueue.end();)
    {
        CProofVerifierItem item = i->second;

        if (item.result == ProofVerificationResult::Unknown)
        {
            i++;
        }
        else
        {
            LogPrint("cert", "%s():%d - Post processing certificate or transaction [%s] from node [%d], result [%s] \n",
                    __func__, __LINE__, item.parentPtr->GetHash().ToString(), item.node->GetId(), ProofVerificationResultToString(item.result));

            // CODE USED FOR UNIT TEST ONLY [Start]
            if (BOOST_UNLIKELY(Params().NetworkIDString() == "regtest"))
            {
                UpdateStatistics(item); // Update the statistics
            }
            // CODE USED FOR UNIT TEST ONLY [End]

            CValidationState dummyState;
            mempoolCallback(*item.parentPtr.get(), item.node,
                            item.result == ProofVerificationResult::Passed ? BatchVerificationStateFlag::VERIFIED : BatchVerificationStateFlag::FAILED,
                            dummyState);

            i = proofsInVerificationQueue.erase(i);
        }
    }
}

/**
 * @brief Updates the statistics of the proof verifier.
 * It is available in regression test mode only.
 * 
 * @param item The item that has been processed by the proof verifier
 */
void CScAsyncProofVerifier::UpdateStatistics(const CProofVerifierItem& item)
{
    assert(Params().NetworkIDString() == "regtest");

    if (!item.resultReused)
    {
        if (item.parentPtr->IsCertificate())
        {
            if (item.result == ProofVerificationResult::Passed)
            {
                stats.okCertCounter++;
            }
            else if (item.result == ProofVerificationResult::Failed)
            {
                stats.failedCertCounter++;
            }
        }
        else
        {
            if (item.result == ProofVerificationResult::Passed)
            {
                stats.okCswCounter++;
            }
            else if (item.result == ProofVerificationResult::Failed)
            {
                stats.failedCswCounter++;
            }
        }
    }
}
