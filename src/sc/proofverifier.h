#ifndef _SC_PROOF_VERIFIER_H
#define _SC_PROOF_VERIFIER_H

#include <sc/sidechaintypes.h> //CHECK IF IT CAN BE REPLACED WITH FORWARD DECLARATION
#include <amount.h>
#include <boost/variant.hpp>

class CSidechain;
class CScCertificate;
class CTxCeasedSidechainWithdrawalInput;
class uint256;

/* Class for instantiating a verifier able to verify different kind of ScProof for different kind of ScProof(s) */
class CScProofVerifier
{
public:
    enum class Verification {
        Strict,
        Loose
    };
    const  Verification verificationMode;

    CScProofVerifier(Verification mode): verificationMode(mode) {}
    ~CScProofVerifier() = default;

    // CScProofVerifier should never be copied
    CScProofVerifier(const CScProofVerifier&) = delete;
    CScProofVerifier& operator=(const CScProofVerifier&) = delete;

    bool verifyCScCertificate(
        const ScConstant& constant,
        const libzendoomc::ScVk& wCertVk,
        const uint256& prev_end_epoch_block_hash,
        const CScCertificate& scCert
    ) const;

    bool verifyCTxCeasedSidechainWithdrawalInput(
        const CFieldElement& certDataHash,
        const libzendoomc::ScVk& wCeasedVk,
        const CTxCeasedSidechainWithdrawalInput& csw
    ) const;

    bool verifyCBwtRequest(
        const uint256& scId,
        const CFieldElement& scRequestData,
        const uint160& mcDestinationAddress,
        CAmount scFees,
        const libzendoomc::ScProof& scProof,
        const boost::optional<libzendoomc::ScVk>& wMbtrVk,
        const CFieldElement& certDataHash
    ) const;
};

#endif // _SC_PROOF_VERIFIER_H
