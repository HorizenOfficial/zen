#include "asyncproofverifier.h"

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
}

void CScAsyncProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom)
{
    LOCK(cs_asyncQueue);
    CScProofVerifier::LoadDataForCswVerification(view, scTx, pfrom);
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

void CScAsyncProofVerifier::RunPeriodicVerification()
{
    /**
     * The age of the queue in milliseconds.
     * This value represents the time spent in the queue by the oldest proof in the queue.
     */
    uint32_t queueAge = 0;

    uint32_t batchVerificationMaxDelay = GetCustomMaxBatchVerifyDelay();
    uint32_t batchVerificationMaxSize  = GetCustomMaxBatchVerifyMaxSize();


    while (!ShutdownRequested())
    {
        size_t currentQueueSize = certEnqueuedData.size() + cswEnqueuedData.size();

        if (currentQueueSize > 0)
        {
            queueAge += THREAD_WAKE_UP_PERIOD;

            /**
             * The batch verification can be triggered by two events:
             * 
             * 1. The queue has grown up beyond the threshold size;
             * 2. The oldest proof in the queue has waited for too long.
             */
            if (queueAge > batchVerificationMaxDelay || currentQueueSize > batchVerificationMaxSize)
            {
                queueAge = 0;
                std::map</*scTxHash*/uint256, std::vector<CCswProofVerifierItem>> tempCswData;
                std::map</*certHash*/uint256, std::vector<CCertProofVerifierItem>> tempCertData;

                {
                    LOCK(cs_asyncQueue);

                    size_t cswQueueSize = cswEnqueuedData.size();
                    size_t certQueueSize = certEnqueuedData.size();

                    LogPrint("cert", "%s():%d - Async verification triggered, %d certificates and %d CSW inputs to verify \n",
                             __func__, __LINE__, certQueueSize, cswQueueSize);

                    // Move the queued proofs into local maps, so that we can release the lock
                    tempCswData = std::move(cswEnqueuedData);
                    tempCertData = std::move(certEnqueuedData);

                    assert(cswEnqueuedData.size() == 0);
                    assert(certEnqueuedData.size() == 0);
                    assert(tempCswData.size() == cswQueueSize);
                    assert(tempCertData.size() == certQueueSize);
                }

                std::pair<bool, std::map<uint256, ProofVerifierOutput>> batchResult = BatchVerifyInternal(tempCswData, tempCertData);
                ProcessVerificationOutputs(batchResult.second, tempCswData, tempCertData);

                if (!batchResult.first)
                {
                    LogPrint("cert", "%s():%d - Batch verification failed, removed proofs that caused the failure and trying again... \n", __func__, __LINE__);

                    batchResult = BatchVerifyInternal(tempCswData, tempCertData);
                    ProcessVerificationOutputs(batchResult.second, tempCswData, tempCertData);

                    if (!batchResult.first)
                    {
                        LogPrint("cert", "%s():%d - Batch verification failed again, verifying proofs one by one... \n", __func__, __LINE__);

                        // As last attempt, verify the proofs one by one.
                        ProcessVerificationOutputs(NormalVerify(tempCswData, tempCertData), tempCswData, tempCertData);
                    }
                }
            }
        }

        MilliSleep(THREAD_WAKE_UP_PERIOD);
    }
}

/**
 * @brief Process the outputs of the batch verification.
 * This function is meant to process all the outputs having a state PASSED or FAILED;
 * the related proofs are then removed from the cswProofs and certProofs maps.
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
 * 
 * @param outputs The set of outputs returned by the batch verification process
 * @param cswProofs The set of CSW proofs submitted as input to the proof verifier
 * @param certProofs The set of certificate proofs submitted as inputs to the proof verifier
 */
void CScAsyncProofVerifier::ProcessVerificationOutputs(const std::map<uint256, ProofVerifierOutput> outputs,
                                                       std::map</* Tx hash */ uint256, std::vector<CCswProofVerifierItem>>& cswProofs,
                                                       std::map</* Cert hash */ uint256, std::vector<CCertProofVerifierItem>>& certProofs)
{
    // Post processing of proofs
    for (auto entry : outputs)
    {
        ProofVerifierOutput output = entry.second;

        if (output.proofResult == ProofVerificationResult::Failed || output.proofResult == ProofVerificationResult::Passed)
        {
            if (output.tx->IsCertificate())
            {
                assert(certProofs.erase(output.tx->GetHash()) == 1);
            }
            else
            {
                assert(cswProofs.erase(output.tx->GetHash()) == 1);
            }
        }

        LogPrint("cert", "%s():%d - Post processing certificate or transaction [%s] from node [%d], result [%d] \n",
                    __func__, __LINE__, output.tx->GetHash().ToString(), output.node->GetId(), output.proofResult);

        // CODE USED FOR UNIT TEST ONLY [Start]
        if (BOOST_UNLIKELY(Params().NetworkIDString() == "regtest"))
        {
            UpdateStatistics(output); // Update the statistics

            // Check if the AcceptToMemoryPool has to be skipped.
            if (skipAcceptToMemoryPool)
            {
                continue;
            }
        }
        // CODE USED FOR UNIT TEST ONLY [End]

        CValidationState dummyState;
        ProcessTxBaseAcceptToMemoryPool(*output.tx.get(), output.node,
                                        output.proofResult == ProofVerificationResult::Passed ? BatchVerificationStateFlag::VERIFIED : BatchVerificationStateFlag::FAILED,
                                        dummyState);
    }
}

/**
 * @brief Update the statistics of the proof verifier.
 * It must be used in regression test mode only.
 * @param output The result of the proof verification that has been performed.
 */
void CScAsyncProofVerifier::UpdateStatistics(const ProofVerifierOutput& output)
{
    assert(Params().NetworkIDString() == "regtest");

    if (output.tx->IsCertificate())
    {
        if (output.proofResult == ProofVerificationResult::Passed)
        {
            stats.okCertCounter++;
        }
        else if (output.proofResult == ProofVerificationResult::Failed)
        {
            stats.failedCertCounter++;
        }
    }
    else
    {
        if (output.proofResult == ProofVerificationResult::Passed)
        {
            stats.okCswCounter++;
        }
        else if (output.proofResult == ProofVerificationResult::Failed)
        {
            stats.failedCswCounter++;
        }
    }
}
