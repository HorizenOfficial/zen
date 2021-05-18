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

// as of now proofing system apis are mocked in the ginger lib version that mc crypto lib is built upon
#define MC_CRYPTO_LIB_MOCKED 1

/* Class for instantiating a verifier able to verify different kind of ScProof for different kind of ScProof(s) */
class CScProofVerifier
{
public:

    enum class Verification
    {
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

    void LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx);
    bool BatchVerify() const;

private:
    
    // these would be useful once batch verification will be implemented
    std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>> cswEnqueuedData;
    std::map</*certHash*/uint256, CCertProofVerifierInput> certEnqueuedData;

    //bool _verifyCertInternal(const CCertProofVerifierInput& input) const;
    bool _batchVerifyInternal(const std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>>& cswEnqueuedData,
                                            const std::map</*certHash*/uint256, CCertProofVerifierInput>& certEnqueuedData) const;

};

#endif // _SC_PROOF_VERIFIER_H
