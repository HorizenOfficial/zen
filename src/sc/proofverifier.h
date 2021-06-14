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

/**
 * @brief The base struct for items added to the queue of the proof verifier.
 */
struct CBaseProofVerifierItem
{
    uint32_t proofId;                               /**< A unique number identifying the proof. */
    std::shared_ptr<CTransactionBase> parentPtr;    /**< The parent (Transaction or Certificate) that owns the item (CSW input or certificate itself). */
    CNode* node;                                    /**< The node that sent the parent (Transaction or Certiticate). */
    CScProof proof;                                 /**< The proof to be verified. */
    CScVKey verificationKey;                        /**< The key to be used for the verification. */
};

/**
 * @brief A structure that includes all the arguments needed for verifying the proof of a CSW input.
 */
struct CCswProofVerifierItem : CBaseProofVerifierItem
{
    CFieldElement ceasingCumScTxCommTree;
    CFieldElement certDataHash;
    CAmount nValue;
    CFieldElement nullifier;
    uint160 pubKeyHash;
    uint256 scId;
};

/**
 * @brief A structure that includes all the arguments needed for verifying the proof of a certificate.
 */
struct CCertProofVerifierItem : CBaseProofVerifierItem
{
    uint256 certHash;
    CFieldElement constant;
    uint32_t epochNumber;
    uint64_t quality;
    std::vector<backward_transfer_t> bt_list;
    std::vector<CFieldElement> vCustomFields;
    CFieldElement endEpochCumScTxCommTreeRoot;
    uint64_t mainchainBackwardTransferRequestScFee;
    uint64_t forwardTransferScFee;
};

/**
 * The enumeration of possible results of the proof verifier for any proof processed.
 */
enum ProofVerificationResult
{
    Unknown,    /**< The result of the batch verification is unknown. */
    Failed,     /**< The proof verification failed. */
    Passed      /**< The proof verification passed. */
};

/**
 * @brief A structure containing the output data of a proof verification.
 */
struct ProofVerifierOutput
{
    std::shared_ptr<CTransactionBase> tx;    /**< The transaction which the proof verification refers to. */
    CNode* node;                             /**< The node that sent the transaction. */
    ProofVerificationResult proofResult;     /**< The result of the proof verification. */
};

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

    static CCertProofVerifierItem CertificateToVerifierItem(const CScCertificate& certificate, const Sidechain::ScFixedParameters& scFixedParams, CNode* pfrom);
    static CCswProofVerifierItem CswInputToVerifierItem(const CTxCeasedSidechainWithdrawalInput& cswInput, const CTransaction* cswTransaction, const Sidechain::ScFixedParameters& scFixedParams, CNode* pfrom);

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
    std::map</*scTxHash*/uint256, std::vector<CCswProofVerifierItem>> cswEnqueuedData;      /**< The queue of CSW proofs to be verified. */
    std::map</*certHash*/uint256, std::vector<CCertProofVerifierItem>> certEnqueuedData;    /**< The queue of certificate proofs to be verified. */
    //std::map</* Cert or Tx hash */ uint256, std::vector<CBaseProofVerifierItem*>> proofQueue;   /** The queue of proofs to be verified. */

    std::pair<bool, std::map<uint256, ProofVerifierOutput>> BatchVerifyInternal(const std::map</* Tx hash */ uint256, std::vector<CCswProofVerifierItem>>& cswProofs,
                                                                                const std::map</* Cert hash */ uint256, std::vector<CCertProofVerifierItem>>& certProofs) const;

private:

    static std::atomic<uint32_t> proofIdCounter;   /**< The counter used to get a unique ID for proofs. */

    const Verification verificationMode;    /**< The type of verification to be performed by this instance of proof verifier. */

    std::map<uint256, ProofVerifierOutput> GenerateVerifierResults(const std::map</* Tx hash */ uint256, std::vector<CCswProofVerifierItem>>& cswProofs,
                                                                       const std::map</* Cert hash */ uint256, std::vector<CCertProofVerifierItem>>& certProofs,
                                                                       ProofVerificationResult defaultResult) const;
};

#endif // _SC_PROOF_VERIFIER_H
