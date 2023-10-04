#ifndef _SC_ASYNC_PROOF_VERIFIER_H
#define _SC_ASYNC_PROOF_VERIFIER_H

#include <map>

#include <boost/variant.hpp>

#include "amount.h"
#include "chainparams.h"
#include "main.h"
#include "primitives/certificate.h"
#include "primitives/transaction.h"
#include "sc/proofverifier.h"
#include "sc/sidechaintypes.h"

class CSidechain;
class CScCertificate;
class uint256;
class CCoinsViewCache;

/**
 * @brief A structure that stores statistics about the async batch verifier process.
 * 
 */
struct AsyncProofVerifierStatistics
{
    uint32_t okCertCounter = 0;           /**< The number of certificate proofs that have been correctly verified. */
    uint32_t okCswCounter = 0;            /**< The number of CSW input proofs that have been correctly verified. */
    uint32_t failedCertCounter = 0;       /**< The number of certificate proofs whose verification failed. */
    uint32_t failedCswCounter = 0;        /**< The number of CSW input proofs whose verification failed. */
    uint32_t removedFromQueueCounter = 0; /**< The number of certificate/csw proofs that have been removed from queue. */
    uint32_t discardedResultCounter = 0;  /**< The number of certificate/csw proofs whose verification result has been discarded. */
};

/**
 * @brief An asynchronous version of the sidechain Proof Verifier.
 * 
 */
class CScAsyncProofVerifier : public CScProofVerifier
{
public:

    static CScAsyncProofVerifier& GetInstance()
    {
        static CScAsyncProofVerifier instance;
        
        return instance;
    }

    CScAsyncProofVerifier(const CScAsyncProofVerifier&) = delete;
    CScAsyncProofVerifier& operator=(const CScAsyncProofVerifier&) = delete;

    void LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom = nullptr) override;
    void LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom = nullptr) override;
    void RunPeriodicVerification();

    static const uint32_t BATCH_VERIFICATION_MAX_DELAY;   /**< The maximum delay in milliseconds between batch verification requests */
    static const uint32_t BATCH_VERIFICATION_MAX_SIZE;    /**< The threshold size of the proof queue that triggers a call to the batch verification. */

    static uint32_t GetCustomMaxBatchVerifyDelay();
    static uint32_t GetCustomMaxBatchVerifyMaxSize();

private:

    friend class TEST_FRIEND_CScAsyncProofVerifier;                                         /**< A friend class used as a proxy for private members in unit tests (Regtest mode only). */

    static const uint32_t THREAD_WAKE_UP_PERIOD = 100;                                      /**< The period of time in milliseconds after which the thread wakes up. */

    std::map</* Cert or Tx hash */ uint256, uint64_t> proofsInsertionMillisecondsQueue;     /**< The queue of insertion time for proofs to be verified. */ 

    std::map</* Cert or Tx hash */ uint256, CProofVerifierItem> proofsInVerificationQueue;  /**< The queue of proofs being verified. */

    bool discardAllProofsVerifications = false;                                             /**< Flag indicating if all proofs verifications has to be discard (from current execution). */

    std::vector</* Cert or Tx hash */ uint256> proofsVerificationsToDiscard;                /**< The vector of proofs verifications to discard (from current execution). */

    CCriticalSection cs_asyncQueue;                                                         /**< The lock to be used for entering the critical section in async mode only. */

    CCriticalSection cs_asyncQueueInVerification;                                           /**< The lock to be used for entering the critical section in async mode only (for proofsInVerificationQueue). */

    CCriticalSection cs_asyncQueueToDiscard;                                                /**< The lock to be used for entering the critical section in async mode only (for discardAllProofsVerifications and proofsVerificationsToDiscard). */

    // Members used for REGTEST mode only. [Start]
    AsyncProofVerifierStatistics stats;                                                     /**< Async proof verifier statistics. */
    // Members used for REGTEST mode only. [End]

    /**
     * @brief The function to be called to make the mempool process a certificate/transaction after the verification of the proof.
     */
    std::function<void(const CTransactionBase&, CNode*, BatchVerificationStateFlag, CValidationState&)> mempoolCallback;

    CScAsyncProofVerifier() :
        CScProofVerifier(Verification::Strict, Priority::Low), // CScAsyncProofVerifier always executes verification with low priority
        mempoolCallback(ProcessTxBaseAcceptToMemoryPool)
    {
    }

    void ProcessVerificationOutputs();
    void UpdateStatistics(const CProofVerifierItem& item);
};

/**
 * @brief Friend class of the CScAsyncProofVerifier to be used in unit tests
 * to access private fields.
 * 
 * It is a singleton and can be instanciated in REGTEST mode only.
 */
class TEST_FRIEND_CScAsyncProofVerifier
{
public:

    static TEST_FRIEND_CScAsyncProofVerifier& GetInstance()
    {
        assert(Params().NetworkIDString() == "regtest");

        static TEST_FRIEND_CScAsyncProofVerifier instance;
        return instance;
    }

    TEST_FRIEND_CScAsyncProofVerifier(const TEST_FRIEND_CScAsyncProofVerifier&) = delete;
    TEST_FRIEND_CScAsyncProofVerifier& operator=(const TEST_FRIEND_CScAsyncProofVerifier&) = delete;

    ~TEST_FRIEND_CScAsyncProofVerifier()
    {
        if(lowPrioThreadGuard != NULL)
            delete lowPrioThreadGuard;
    }

    /**
     * @brief Gets the async proof verifier statistics.
     * 
     * @return The proof verifier statistics. 
     */
    AsyncProofVerifierStatistics GetStatistics()
    {
        auto& verifier = CScAsyncProofVerifier::GetInstance();
        LOCK2(verifier.cs_asyncQueue, verifier.cs_asyncQueueInVerification);
        return CScAsyncProofVerifier::GetInstance().stats;
    }

    /**
     * @brief Gets the current number of certificate proofs waiting to be verified
     * by the async proof verifier.
     * 
     * @return size_t The number of pending certificate proofs.
     */
    size_t PendingAsyncCertProofs()
    {
        auto& verifier = CScAsyncProofVerifier::GetInstance();
        LOCK(verifier.cs_asyncQueue);

        int counter = 0;

        for (auto item : verifier.proofsQueue)
        {
            if (item.second.proofInput.type() == typeid(CCertProofVerifierInput))
            {
                counter++;
            }
        }

        return counter;
    }

     /**
     * @brief Gets the current number of certificate proofs in verification
     * by the async proof verifier.
     * 
     * @return size_t The number of certificate proofs in verification.
     */
    size_t PendingAsyncCertProofsInVerification()
    {
        auto& verifier = CScAsyncProofVerifier::GetInstance();
        LOCK(verifier.cs_asyncQueueInVerification);

        int counter = 0;

        for (auto item : verifier.proofsInVerificationQueue)
        {
            if (item.second.proofInput.type() == typeid(CCertProofVerifierInput))
            {
                counter++;
            }
        }

        return counter;
    }

    /**
     * @brief Gets the current number of CSW proofs waiting to be verified
     * by the async proof verifier.
     * 
     * @return size_t The number of pending CSW proofs.
     */
    size_t PendingAsyncCswProofs()
    {
        auto& verifier = CScAsyncProofVerifier::GetInstance();
        LOCK(verifier.cs_asyncQueue);

        int counter = 0;

        for (auto item : verifier.proofsQueue)
        {
            if (item.second.proofInput.type() == typeid(std::vector<CCswProofVerifierInput>))
            {
                counter++;
            }
        }

        return counter;
    }

     /**
     * @brief Gets the current number of CSW proofs in verification
     * by the async proof verifier.
     * 
     * @return size_t The number of CSW proofs in verification.
     */
    size_t PendingAsyncCswProofsInVerification()
    {
        auto& verifier = CScAsyncProofVerifier::GetInstance();
        LOCK(verifier.cs_asyncQueueInVerification);

        int counter = 0;

        for (auto item : verifier.proofsInVerificationQueue)
        {
            if (item.second.proofInput.type() == typeid(std::vector<CCswProofVerifierInput>))
            {
                counter++;
            }
        }

        return counter;
    }

    /**
     * @brief Get the max delay between async batch verifications.
     * 
     * @return uint32_t The max delay between async batch verifications.
     */
    uint32_t GetMaxBatchVerifyDelay()
    {
        return CScAsyncProofVerifier::GetCustomMaxBatchVerifyDelay();
    }

    /**
     * @brief Resets the async proof verifier statistics.
     */
    void ResetStatistics()
    {
        CScAsyncProofVerifier::GetInstance().stats = AsyncProofVerifierStatistics();
    }

    /**
     * @brief Clears proofs from the async queue (before they are started to be verified)
     */
    void ClearFromQueue(const std::vector<uint256>& proofsToClear = std::vector<uint256>())
    {
        auto& verifier = CScAsyncProofVerifier::GetInstance();
        LOCK(verifier.cs_asyncQueue);

        size_t clearedProofs = 0;

        if (verifier.proofsQueue.size() > 0)
        {
            if (proofsToClear.size() == 0)
            {
                // clear all proofs
                clearedProofs = verifier.proofsQueue.size();
                LogPrint("sc", "%s():%d - %d proofs cleared from async verification queue\n",
                         __func__, __LINE__, clearedProofs);
                verifier.proofsQueue.clear();
                verifier.proofsInsertionMillisecondsQueue.clear();
                assert(verifier.proofsQueue.size() == 0);
                assert(verifier.proofsInsertionMillisecondsQueue.size() == 0);
            }
            else
            {
                // clear only provided proofs
                for (auto proofToClear : proofsToClear)
                {
                    if (verifier.proofsQueue.erase(proofToClear) > 0)
                    {
                        ++clearedProofs;
                        LogPrint("sc", "%s():%d - %s proof cleared from async verification queue\n",
                                 __func__, __LINE__, proofToClear.ToString());
                        verifier.proofsQueue.erase(proofToClear);
                        verifier.proofsInsertionMillisecondsQueue.erase(proofToClear);
                    }
                }
                assert(verifier.proofsQueue.size() == verifier.proofsInsertionMillisecondsQueue.size());
            }

            if (BOOST_UNLIKELY(Params().NetworkIDString() == "regtest"))
            {
                verifier.stats.removedFromQueueCounter += clearedProofs;
            }
        }       
    }

    /**
     * @brief Set discarding of proof verification results from the current async verification
     *        (will actually take place at the end of current async verification, if any)
     */
    void SetDiscardingFromCurrentVerification(const std::vector<uint256>& proofsVerificationsToDiscard = std::vector<uint256>())
    {
        auto& verifier = CScAsyncProofVerifier::GetInstance();
        LOCK(verifier.cs_asyncQueueInVerification);


        if (verifier.proofsInVerificationQueue.size() > 0)
        {
            if (proofsVerificationsToDiscard.size() == 0)
            {
                LogPrint("sc", "%s():%d - %d proofs verifications results will be discarded from current verification\n",
                         __func__, __LINE__, verifier.proofsInVerificationQueue.size());
                verifier.discardAllProofsVerifications = true;
            }
            else
            {
                for (auto proofToDiscard : proofsVerificationsToDiscard)
                {
                    LogPrint("sc", "%s():%d - %s proof verification result will be discarded from current verification\n",
                             __func__, __LINE__, proofToDiscard.ToString());
                    verifier.proofsVerificationsToDiscard.push_back(proofToDiscard);
                }
            }
        }
    }

    /**
     * @brief Gets the async proof verifier statistics.
     *
     * @return The proof verifier statistics.
     */
    void setProofVerifierLowPriorityGuard(bool isEnabled)
    {
        if(lowPrioThreadGuard != NULL)
            delete lowPrioThreadGuard;
        lowPrioThreadGuard = new CZendooLowPrioThreadGuard(isEnabled);

    }

    void DisableMempoolCallback()
    {
        // Disables the call to AcceptToMemory pool from the async proof verifier when performing unit tests (not python ones).
        CScAsyncProofVerifier::GetInstance().mempoolCallback = [](const CTransactionBase&, CNode*, BatchVerificationStateFlag, CValidationState&){};
    }

private:
    TEST_FRIEND_CScAsyncProofVerifier()
    {
    }
    
    CZendooLowPrioThreadGuard* lowPrioThreadGuard = NULL;
};

#endif // _SC_ASYNC_PROOF_VERIFIER_H
