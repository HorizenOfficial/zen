#ifndef _SC_PROOF_VERIFIER_H
#define _SC_PROOF_VERIFIER_H

#include <map>

#include <sc/sidechaintypes.h>
#include <amount.h>
#include <boost/variant.hpp>
#include <primitives/transaction.h>

class CSidechain;
class CScCertificate;
class uint256;
class CCoinsViewCache;

/* Class for instantiating a verifier able to verify different kind of ScProof for different kind of ScProof(s) */
class CScProofVerifier
{
public:
    enum class Verification {
        Strict,
        Loose
    };
    const Verification verificationMode;

    CScProofVerifier(Verification mode): verificationMode(mode) {}
    ~CScProofVerifier() = default;

    // CScProofVerifier should never be copied
    CScProofVerifier(const CScProofVerifier&) = delete;
    CScProofVerifier& operator=(const CScProofVerifier&) = delete;

    void LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert);
    std::map</*certHash*/uint256,bool> batchVerifyCerts() const;

    void LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx);
    std::map</*scTxHash*/uint256,bool> batchVerifyCsws() const;

    // Returns false if proof verification has failed or deserialization of certificate's elements
    // into libzendoomc's elements has failed.
    bool verifyCScCertificate() const;

    // Returns false if proof verification has failed or deserialization of CSW's elements
    // into libzendoomc's elements has failed.
    bool verifyCTxCeasedSidechainWithdrawalInput(
        const CFieldElement& certDataHash,
        const CScVKey& wCeasedVk,
        const CTxCeasedSidechainWithdrawalInput& csw
    ) const;


private:
    struct mbtrVerifierInputsList
    {
        uint256 scId;
        CFieldElement scRequestData;
        uint160 mcDestinationAddress;
        CAmount scFee;
        CScProof scProof;
        CScVKey mbtrVk;
        CFieldElement certDataHash;
    };
    std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, mbtrVerifierInputsList>> mbtrEnqueuedData;

    struct cswVerifierInputsList
    {
        CTxCeasedSidechainWithdrawalInput cswOut;
        CScVKey ceasedVk;
        CFieldElement certDataHash;
    };
    std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, cswVerifierInputsList>> cswEnqueuedData;

    struct certVerifierInput
    {
        uint256 certHash;
        uint256 endEpochBlockHash;
        uint256 prevEndEpochBlockHash;
        std::vector<backward_transfer_t> bt_list;
        int64_t quality;
        ScConstant constant;
        CFieldElement proofdata;
        CScProof certProof;
        CScVKey CertVk;
    };

    certVerifierInput certData;

};

#endif // _SC_PROOF_VERIFIER_H
