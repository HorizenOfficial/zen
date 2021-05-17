#ifndef _SC_ASYNC_PROOF_VERIFIER_H
#define _SC_ASYNC_PROOF_VERIFIER_H

#include <map>

#include <boost/variant.hpp>

#include "amount.h"
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

    static const uint32_t THREAD_WAKE_UP_PERIOD = 100;           /**< The period of time in milliseconds after which the thread wakes up. */
    static const uint32_t BATCH_VERIFICATION_MAX_DELAY = 5000;   /**< The maximum delay in milliseconds between batch verification requests */
    static const uint32_t BATCH_VERIFICATION_MAX_SIZE = 10;      /**< The threshold size of the proof queue that triggers a call to the batch verification. */

    CCriticalSection cs_asyncQueue;
    std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>> cswEnqueuedData; /**< The queue of CSW proves to be verified. */
    std::map</*certHash*/uint256, CCertProofVerifierInput> certEnqueuedData;    /**< The queue of certificate proves to be verified. */

    CScAsyncProofVerifier() {}

    std::pair<bool, std::vector<AsyncProofVerifierOutput>> BatchVerify(const std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int,
                                                                        CCswProofVerifierInput>>& cswInputs,
                                                                        const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const;
    std::vector<AsyncProofVerifierOutput> NormalVerify(const std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int,
                                                       CCswProofVerifierInput>>& cswInputs,
                                                       const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const;
    bool NormalVerifyCertificate(CCertProofVerifierInput input) const;
    bool NormalVerifyCsw(uint256 txHash, std::map</*outputPos*/unsigned int, CCswProofVerifierInput> inputMap) const;

    std::pair<bool, std::vector<AsyncProofVerifierOutput>> _batchVerifyInternal(const std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>>& cswInputs,
                                                                                 const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const;

};

#endif // _SC_ASYNC_PROOF_VERIFIER_H
