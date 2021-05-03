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

    // these would become obsolete once batch verification will be implemented
    //---
    // Returns false if proof verification has failed or deserialization of certificate's elements
    // into libzendoomc's elements has failed.
    bool verifyCScCertificate() const;
    // Returns false if proof verification has failed or deserialization of CSW's elements
    // into libzendoomc's elements has failed.
    bool verifyCTxCeasedSidechainWithdrawalInput() const; 

private:
    
    // these would be useful once batch verification will be implemented
    std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>> cswEnqueuedData;
    std::map</*certHash*/uint256, CCertProofVerifierInput> certEnqueuedData;

    bool _verifyCertInternal(const CCertProofVerifierInput& input) const; 

    // theses would become obsolete once batch verification will be implemented
    CCertProofVerifierInput certData;
    CCswProofVerifierInput  cswData;
};

#endif // _SC_PROOF_VERIFIER_H
