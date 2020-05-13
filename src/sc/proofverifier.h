#ifndef _SC_PROOF_VERIFIER_H
#define _SC_PROOF_VERIFIER_H

#include <vector>
#include <sc/TEMP_zendooInterface.h>
#include <boost/foreach.hpp>
#include <boost/variant.hpp>

class CSidechain;
class CScCertificate;

namespace libzendoomc{

    static constexpr size_t GROTH_PROOF_SIZE = (
        193 +  // π_A
        385 +  // π_B
        193);  // π_C

    typedef std::vector<unsigned char> ScProof;
    typedef std::vector<unsigned char> ScVk;
    typedef boost::variant<CScCertificate> CScProofVerificationContext;

    class CScProofVerifier : public boost::static_visitor<bool> {
        private:
            bool perform_verification;
            CSidechain* scInfo;
            CScProofVerifier(bool perform_verification): perform_verification(perform_verification), scInfo(nullptr) {}
            CScProofVerifier(bool perform_verification, CSidechain* scInfo) :
                perform_verification(perform_verification), scInfo(scInfo) {}

        public:
            // CScProofVerifier should never be copied
            CScProofVerifier(const CScProofVerifier&) = delete;
            CScProofVerifier& operator=(const CScProofVerifier&) = delete;
            CScProofVerifier(CScProofVerifier&&);
            CScProofVerifier& operator=(CScProofVerifier&&);

            // Creates a verification context that strictly verifies all proofs using zendoo-mc-cryptolib's API.
            static CScProofVerifier Strict(CSidechain* scInfo){ return CScProofVerifier(true, scInfo); }

            // Creates a verifier that performs no verification, used when avoiding duplicate effort
            // such as during reindexing.
            static CScProofVerifier Disabled() { return CScProofVerifier(false); }

            // Visitor functions

            // Returns false if proof verification has failed or deserialization of certificate's elements
            // into libzendoomc's elements has failed. The error variable set by libzendoomc can be checked
            // outside. The function assumes scInfo and scCert already checked to be non null and 
            // "semantically valid". (The alternative is to pass a CValidationState to the constructor of
            // this class and log both libzendoomc errors or errors related to scInfo and scCert be malformed;
            // probably the easiest solution).
            bool operator()(const CScCertificate& scCert) const;
    };

}

#endif // _SC_PROOF_VERIFIER_H