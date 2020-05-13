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

    /* 
     * Abstract class holding eveything needed to verify a specific kind of ScProof, including the proof
     * and the verification key themselves. The class doesn't know how to verify the corresponding proof:
     * the verification is delegated to a visitor class CScProofVerifier, to which this class must provide
     * all the needed data to perform the verification itself.
     */
    class CScProofVerificationContext {
        public:

            /* Verifies the data provided by this class using the specified verifier */
            virtual bool verify(const class CScProofVerifier& verifier) const = 0;
    };

    /* Class holding everything needed to verify a WCertScProof. The built-in struct is resettable
     * in order to allow reusing the same instance to verify multiple proofs (TODO: Is this really needed ?)
     */
    class CWCertProofVerificationContext: public CScProofVerificationContext {
        public:
            struct CWCertProofParameters {
                const unsigned char* end_epoch_mc_b_hash;
                const unsigned char* prev_end_epoch_mc_b_hash;
                const backward_transfer_t* bt_list;
                size_t bt_list_len;
                uint64_t quality;
                const field_t* constant; 
                const field_t* proofdata; 
                const sc_proof_t* sc_proof; 
                const sc_vk_t* sc_vk; 
            } params;

            ~CWCertProofVerificationContext();

            /* Updates CWCertProofParameters according to scInfo and scCert */
            bool updateParameters(CSidechain& scInfo, CScCertificate& scCert);

            /* Validates correctness of CWCertProofParameters */
            bool checkParameters() const;

            /* Free the memory from CWCertProofParameters*/
            void reset();

            /* Verify CWCertProofParameters using verifier */
            bool verify(const CScProofVerifier& verifier) const override;

        private:
            void setNull();
    };

    class CScProofVerifier {
        private:
            bool perform_verification;
            CScProofVerifier(bool perform_verification) : perform_verification(perform_verification) {}

        public:
            // CScProofVerifier should never be copied
            CScProofVerifier(const CScProofVerifier&) = delete;
            CScProofVerifier& operator=(const CScProofVerifier&) = delete;
            CScProofVerifier(CScProofVerifier&&);
            CScProofVerifier& operator=(CScProofVerifier&&);

            // Creates a verification context that strictly verifies
            // all proofs using zendoo-mc-cryptolib's API.
            static CScProofVerifier Strict(){ return CScProofVerifier(true); }

            // Creates a verifier that performs no
            // verification, used when avoiding duplicate effort
            // such as during reindexing.
            static CScProofVerifier Disabled() { return CScProofVerifier(false); }

            // Visitor functions
            bool verify(const CWCertProofVerificationContext& wCertCtx) const;
    };

}

#endif // _SC_PROOF_VERIFIER_H