#ifndef _SC_PROOF_VERIFIER_H
#define _SC_PROOF_VERIFIER_H

#include <map>

#include <sc/sidechaintypes.h>
#include <amount.h>
#include <boost/variant.hpp>
#include <primitives/certificate.h>
#include <primitives/transaction.h>

class CSidechain;
class CScCertificate;
class uint256;
class CCoinsViewCache;

/* Class for instantiating a verifier that is able to verify different kind of ScProof for different kind of ScProof(s) */
class CScProofVerifier
{
public:

    /**
     * @brief The types of verification that can be requested the the proof verifier.
     */
    enum class Verification
    {
        Strict,     /**< Perform normal verification. */
        Loose       /**< Skip verification. */
    };

    static CCertProofVerifierInput CertificateToVerifierInput(const CScCertificate& certificate, const Sidechain::ScFixedParameters& scFixedParams, CNode* pfrom);
    static CCswProofVerifierInput CswInputToVerifierInput(const CTxCeasedSidechainWithdrawalInput& cswInput, const CTransaction* cswTransaction, const Sidechain::ScFixedParameters& scFixedParams, CNode* pfrom);

    CScProofVerifier(Verification mode) :
    verificationMode(mode)
    {
    }
    ~CScProofVerifier() = default;

    // CScProofVerifier should never be copied
    CScProofVerifier(const CScProofVerifier&) = delete;
    CScProofVerifier& operator=(const CScProofVerifier&) = delete;

    virtual void LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom = nullptr);

    virtual void LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom = nullptr);
    bool BatchVerify() const;

protected:
    std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>> cswEnqueuedData; /**< The queue of CSW proofs to be verified. */
    std::map</*certHash*/uint256, CCertProofVerifierInput> certEnqueuedData;    /**< The queue of certificate proofs to be verified. */

private:

    static std::atomic<uint32_t> proofIdCounter;   /**< The counter used to get a unique ID for proofs. */

    const Verification verificationMode;    /**< The type of verification to be performed by this instance of proof verifier. */

    std::pair<bool, std::vector<uint32_t>> BatchVerifyInternal(
                                            const std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>>& cswInputs,
                                            const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const;
};

#endif // _SC_PROOF_VERIFIER_H
