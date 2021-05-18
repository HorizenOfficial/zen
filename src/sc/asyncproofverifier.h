#ifndef _SC_ASYNC_PROOF_VERIFIER_H
#define _SC_ASYNC_PROOF_VERIFIER_H

#include <map>

#include <boost/variant.hpp>

#include "amount.h"
#include "chainparams.h"
#include "primitives/certificate.h"
#include "primitives/transaction.h"
#include "sc/sidechaintypes.h"

class CSidechain;
class CScCertificate;
class uint256;
class CCoinsViewCache;

/**
 * @brief A structure containing the output data of a proof verification.
 */
struct AsyncProofVerifierOutput
{
    std::shared_ptr<CTransactionBase> tx;    /**< The transaction which the proof verification refers to. */
    CNode* node;                             /**< The node that sent the transaction. */
    bool proofVerified;                      /**< True if the proof has been correctly verified, false otherwise. */
};

/**
 * @brief A structure storing statistics about the async batch verifier process.
 * 
 */
struct AsyncProofVerifierStatistics
{
    uint32_t okCertCounter = 0;     /**< The number of certificate proves that have been correctly verified. */
    uint32_t okCswCounter = 0;      /**< The number of CSW input proves that have been correctly verified. */
    uint32_t failedCertCounter = 0; /**< The number of certificate proves whose verification failed. */
    uint32_t failedCswCounter = 0;  /**< The number of CSW input proves whose verification failed. */
};

/**
 * @brief An asynchronous version of the sidechain Proof Verifier.
 * 
 */
class CScAsyncProofVerifier
{
public:

    static CScAsyncProofVerifier& GetInstance()
    {
        static CScAsyncProofVerifier instance;
        
        return instance;
    }

    CScAsyncProofVerifier(const CScAsyncProofVerifier&) = delete;
    CScAsyncProofVerifier& operator=(const CScAsyncProofVerifier&) = delete;

    void LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom);
    void LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom);
    void RunPeriodicVerification();

private:

    friend class TEST_FRIEND_CScAsyncProofVerifier;

    static const uint32_t THREAD_WAKE_UP_PERIOD = 100;           /**< The period of time in milliseconds after which the thread wakes up. */
    static const uint32_t BATCH_VERIFICATION_MAX_DELAY = 5000;   /**< The maximum delay in milliseconds between batch verification requests */
    static const uint32_t BATCH_VERIFICATION_MAX_SIZE = 10;      /**< The threshold size of the proof queue that triggers a call to the batch verification. */

    CCriticalSection cs_asyncQueue;
    std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>> cswEnqueuedData; /**< The queue of CSW proves to be verified. */
    std::map</*certHash*/uint256, CCertProofVerifierInput> certEnqueuedData;    /**< The queue of certificate proves to be verified. */

    // Members used for REGTEST mode only. [Start]
    AsyncProofVerifierStatistics stats;     /**< Async proof verifier statistics. */
    bool skipAcceptToMemoryPool;            /**< True to skip the call to AcceptToMemoryPool at the end of the proof verification, false otherwise (default false), */
    // Members used for REGTEST mode only. [End]

    CScAsyncProofVerifier(): skipAcceptToMemoryPool(false) {}

    std::pair<bool, std::vector<AsyncProofVerifierOutput>> BatchVerify(const std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int,
                                                                        CCswProofVerifierInput>>& cswInputs,
                                                                        const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const;
    std::vector<AsyncProofVerifierOutput> NormalVerify(const std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int,
                                                       CCswProofVerifierInput>>& cswInputs,
                                                       const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const;
    bool NormalVerifyCertificate(CCertProofVerifierInput input) const;
    bool NormalVerifyCsw(uint256 txHash, std::map</*outputPos*/unsigned int, CCswProofVerifierInput> inputMap) const;

    void UpdateStatistics(const AsyncProofVerifierOutput& output);

    std::pair<bool, std::vector<AsyncProofVerifierOutput>> _batchVerifyInternal(const std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>>& cswInputs,
                                                                                 const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const;

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
     * @brief Gets the current number of certificate proves waiting to be verified
     * by the async proof verifier.
     * 
     * @return size_t The number of pending certificate proves.
     */
    size_t PendingAsyncCertProves()
    {
        return CScAsyncProofVerifier::GetInstance().certEnqueuedData.size();
    }

    /**
     * @brief Gets the current number of CSW proves waiting to be verified
     * by the async proof verifier.
     * 
     * @return size_t The number of pending CSW proves.
     */
    size_t PendingAsyncCswProves()
    {
        return CScAsyncProofVerifier::GetInstance().cswEnqueuedData.size();
    }

    /**
     * @brief Get the max delay between async batch verifications.
     * 
     * @return uint32_t The max delay between async batch verifications.
     */
    uint32_t GetMaxBatchVerifyDelay()
    {
        return CScAsyncProofVerifier::GetInstance().BATCH_VERIFICATION_MAX_DELAY;
    }

    /**
     * @brief Resets the async proof verifier statistics and queues.
     */
    void Reset()
    {
        CScAsyncProofVerifier& verifier = CScAsyncProofVerifier::GetInstance();

        verifier.certEnqueuedData.clear();
        verifier.cswEnqueuedData.clear();
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
