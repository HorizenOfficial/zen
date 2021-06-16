#ifndef _SC_ASYNC_PROOF_VERIFIER_H
#define _SC_ASYNC_PROOF_VERIFIER_H

#include <map>

#include <boost/variant.hpp>

#include "amount.h"
#include "chainparams.h"
#include "primitives/certificate.h"
#include "primitives/transaction.h"
#include "sc/proofverifier.h"
#include "sc/sidechaintypes.h"

class CSidechain;
class CScCertificate;
class uint256;
class CCoinsViewCache;

/**
 * @brief A structure storing statistics about the async batch verifier process.
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

    virtual void LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom = nullptr) override;
    virtual void LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom = nullptr) override;
    void RunPeriodicVerification();

    static const uint32_t BATCH_VERIFICATION_MAX_DELAY;   /**< The maximum delay in milliseconds between batch verification requests */
    static const uint32_t BATCH_VERIFICATION_MAX_SIZE;      /**< The threshold size of the proof queue that triggers a call to the batch verification. */

    static uint32_t GetCustomMaxBatchVerifyDelay();
    static uint32_t GetCustomMaxBatchVerifyMaxSize();

private:

    friend class TEST_FRIEND_CScAsyncProofVerifier;

    static const uint32_t THREAD_WAKE_UP_PERIOD = 100;           /**< The period of time in milliseconds after which the thread wakes up. */

    CCriticalSection cs_asyncQueue;         /**< The lock to be used for entering the critical section in async mode only. */

    // Members used for REGTEST mode only. [Start]
    AsyncProofVerifierStatistics stats;     /**< Async proof verifier statistics. */
    bool skipAcceptToMemoryPool;            /**< True to skip the call to AcceptToMemoryPool at the end of the proof verification, false otherwise (default false), */
    // Members used for REGTEST mode only. [End]

    CScAsyncProofVerifier() :
        CScProofVerifier(Verification::Strict),
        skipAcceptToMemoryPool(false)
    {
    }

    void ProcessVerificationOutputs(const std::map<uint256, ProofVerifierOutput> outputs,
                                    std::map</* Tx hash */ uint256, CProofVerifierItem>& proofs);
    void UpdateStatistics(const ProofVerifierOutput& output);
};

/**
 * @brief Friend class of the CScAsyncProofVerifier to be used in unit tests
 * to access private fields.
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
        int counter = 0;

        for (auto item : CScAsyncProofVerifier::GetInstance().proofQueue)
        {
            if (item.second.certInput)
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
        int counter = 0;

        for (auto item : CScAsyncProofVerifier::GetInstance().proofQueue)
        {
            if (item.second.cswInputs)
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
     * @brief Resets the async proof verifier statistics and queues.
     */
    void Reset()
    {
        CScAsyncProofVerifier& verifier = CScAsyncProofVerifier::GetInstance();

        verifier.proofQueue.clear();
        verifier.stats = AsyncProofVerifierStatistics();
    }

private:
    TEST_FRIEND_CScAsyncProofVerifier()
    {
        // Disables the call to AcceptToMemory pool from the async proof verifier when performing unit tests (not python ones).
        CScAsyncProofVerifier::GetInstance().skipAcceptToMemoryPool = true;
    }
};

#endif // _SC_ASYNC_PROOF_VERIFIER_H
