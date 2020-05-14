#ifndef _SC_PROOF_VERIFIER_H
#define _SC_PROOF_VERIFIER_H

#include <vector>
#include <boost/foreach.hpp>
#include <boost/variant.hpp>

class CSidechain;
class CScCertificate;

namespace libzendoomc{

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
            // into libzendoomc's elements has failed.
            bool operator()(const CScCertificate& scCert) const;
    };

}

#endif // _SC_PROOF_VERIFIER_H