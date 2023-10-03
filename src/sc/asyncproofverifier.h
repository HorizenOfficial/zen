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
    uint32_t okCertCounter = 0;     /**< The number of certificate proofs that have been correctly verified. */
    uint32_t okCswCounter = 0;      /**< The number of CSW input proofs that have been correctly verified. */
    uint32_t failedCertCounter = 0; /**< The number of certificate proofs whose verification failed. */
    uint32_t failedCswCounter = 0;  /**< The number of CSW input proofs whose verification failed. */
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

    friend class TEST_FRIEND_CScAsyncProofVerifier;         /**< A friend class used as a proxy for private members in unit tests (Regtest mode only). */

    static const uint32_t THREAD_WAKE_UP_PERIOD = 100;           /**< The period of time in milliseconds after which the thread wakes up. */

    std::map</* Cert or Tx hash */ uint256, uint64_t> proofsInsertionMillisecondsQueue;   /**< The queue of insertion time for proofs to be verified. */ 

    std::map</* Cert or Tx hash */ uint256, CProofVerifierItem> proofsInVerificationQueue;   /**< The queue of proofs being verified. */

    bool discardAllProofsVerifications = false;   /**< Flag indicating if all proofs verifications has to be discard (from current execution). */

    std::vector</* Cert or Tx hash */ uint256> proofsVerificationsToDiscard;   /**< The vector of proofs verifications to discard (from current execution). */

    CCriticalSection cs_asyncQueue;         /**< The lock to be used for entering the critical section in async mode only. */

    CCriticalSection cs_asyncQueueInVerification;         /**< The lock to be used for entering the critical section in async mode only (for proofsInVerificationQueue). */

    // Members used for REGTEST mode only. [Start]
    AsyncProofVerifierStatistics stats;     /**< Async proof verifier statistics. */
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
        LOCK(CScAsyncProofVerifier::GetInstance().cs_asyncQueue);

        int counter = 0;

        for (auto item : CScAsyncProofVerifier::GetInstance().proofsQueue)
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
        LOCK(CScAsyncProofVerifier::GetInstance().cs_asyncQueueInVerification);

        int counter = 0;

        for (auto item : CScAsyncProofVerifier::GetInstance().proofsInVerificationQueue)
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
        LOCK(CScAsyncProofVerifier::GetInstance().cs_asyncQueue);

        int counter = 0;

        for (auto item : CScAsyncProofVerifier::GetInstance().proofsQueue)
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
        LOCK(CScAsyncProofVerifier::GetInstance().cs_asyncQueueInVerification);

        int counter = 0;

        for (auto item : CScAsyncProofVerifier::GetInstance().proofsInVerificationQueue)
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
        CScAsyncProofVerifier& verifier = CScAsyncProofVerifier::GetInstance();

        {
            LOCK(verifier.cs_asyncQueueInVerification);
            verifier.stats = AsyncProofVerifierStatistics();
        }
    }

    /**
     * @brief Clears proofs from the async queue.
     */
    void ClearFromQueue(const std::vector<uint256>& proofsToClear = std::vector<uint256>())
    {
        CScAsyncProofVerifier& verifier = CScAsyncProofVerifier::GetInstance();

        {
            LOCK(verifier.cs_asyncQueue);
            if (verifier.proofsQueue.size() > 0)
            {
                if (proofsToClear.size() == 0)
                {
                    LogPrint("cert", "%s():%d - %d proofs cleared from async verification queue\n",
                             __func__, __LINE__, verifier.proofsQueue.size());
                    verifier.proofsQueue.clear();
                    verifier.proofsInsertionMillisecondsQueue.clear();
                    assert(verifier.proofsQueue.size() == 0);
                    assert(verifier.proofsInsertionMillisecondsQueue.size() == 0);
                }
                else
                {
                    for (auto proofToClear : proofsToClear)
                    {
                        LogPrint("cert", "%s():%d - %s proofs cleared from async verification queue\n",
                                 __func__, __LINE__, proofToClear.ToString());
                        verifier.proofsQueue.erase(proofToClear);
                        verifier.proofsInsertionMillisecondsQueue.erase(proofToClear);
                    }
                    assert(verifier.proofsQueue.size() == verifier.proofsInsertionMillisecondsQueue.size());
                }
            }
        }
    }

    /**
     * @brief Discards proof verification results from the current async verification
     */
    void DiscardFromCurrentVerification(const std::vector<uint256>& proofsVerificationsToDiscard = std::vector<uint256>())
    {
        CScAsyncProofVerifier& verifier = CScAsyncProofVerifier::GetInstance();

        {
            LOCK(verifier.cs_asyncQueueInVerification);
            if (verifier.proofsInVerificationQueue.size() > 0)
            {
                if (proofsVerificationsToDiscard.size() == 0)
                {
                    verifier.discardAllProofsVerifications = true;
                }
                else
                {
                    for (auto proofToDiscard : proofsVerificationsToDiscard)
                    {
                        verifier.proofsVerificationsToDiscard.push_back(proofToDiscard);
                    }
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
