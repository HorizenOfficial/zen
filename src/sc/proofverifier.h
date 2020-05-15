#ifndef _SC_PROOF_VERIFIER_H
#define _SC_PROOF_VERIFIER_H

#include <sc/TEMP_zendooInterface.h>
#include <vector>
#include <boost/foreach.hpp>
#include <boost/variant.hpp>

class CSidechain;
class CScCertificate;

namespace libzendoomc{

    typedef std::vector<unsigned char> ScProof;
    typedef std::vector<unsigned char> ScVk;
    
    /* Visitable classes, one for each element of the CCTP that requires a SNARK proof verification. */
    typedef boost::variant<CScCertificate> CScProofVerificationContext; 

    /* 
     * Abstract class for handling all the needed data to verify a specific kind of ScProof,
     * including allocation/deallocation of proof inputs, the proof and the vk themselves,
     * as well as the specific zendoo-mc-cryptolib function to be called.
     */
    class CScProofVerificationParameters {
        public:
            virtual bool createParameters() = 0;
            virtual bool verifierCall() const = 0;
            virtual void freeParameters() = 0;
    };

    /* Implementation of CScProofVerificationParameters for WCert SNARK proof verification. */
    class CScWCertProofVerificationParameters: public CScProofVerificationParameters {
        public:
            CScWCertProofVerificationParameters(const CSidechain& scInfo, const CScCertificate& scCert): 
                scInfo(scInfo), scCert(scCert) { }
            
            bool createParameters() override;
            bool verifierCall() const override;
            void freeParameters() override;

        protected:
            const unsigned char* end_epoch_mc_b_hash;
            const unsigned char* prev_end_epoch_mc_b_hash;
            const backward_transfer_t* bt_list;
            size_t bt_list_len;
            uint64_t quality;
            const field_t* constant;
            const field_t* proofdata;
            const sc_proof_t* sc_proof;
            const sc_vk_t* sc_vk;
            const CSidechain& scInfo;
            const CScCertificate& scCert;
    };

    /* Visitor class able to verify different kind of ScProof for different CScProofVerificationContext(s) */
    class CScProofVerifier : public boost::static_visitor<bool> {
        protected:
            bool perform_verification;
            const CSidechain* scInfo;

            CScProofVerifier(bool perform_verification): perform_verification(perform_verification), scInfo(nullptr) {}
            CScProofVerifier(bool perform_verification, const CSidechain* scInfo) :
                perform_verification(perform_verification), scInfo(scInfo) {}

            bool performVerification(CScProofVerificationParameters& params) const {
                if(!perform_verification){
                    return true;
                } else {                    
                    if(!params.createParameters()){
                        params.freeParameters(); //Free memory from parameters allocated up to the error point
                        return false;
                    }

                    bool result = params.verifierCall();
                    params.freeParameters();
                    return result;
                }
            }

        public:
            // CScProofVerifier should never be copied
            CScProofVerifier(const CScProofVerifier&) = delete;
            CScProofVerifier& operator=(const CScProofVerifier&) = delete;
            CScProofVerifier(CScProofVerifier&&);
            CScProofVerifier& operator=(CScProofVerifier&&);

            // Creates a verification context that strictly verifies all proofs using zendoo-mc-cryptolib's API.
            static CScProofVerifier Strict(const CSidechain* scInfo){ return CScProofVerifier(true, scInfo); }

            // Creates a verifier that performs no verification, used when avoiding duplicate effort
            // such as during reindexing.
            static CScProofVerifier Disabled() { return CScProofVerifier(false); }

            // Visitor functions

            // Returns false if proof verification has failed or deserialization of certificate's elements
            // into libzendoomc's elements has failed.
            bool operator()(const CScCertificate& scCert) const {
                auto params = CScWCertProofVerificationParameters(*scInfo, scCert);
                return performVerification(params);
            }
    };
}

#endif // _SC_PROOF_VERIFIER_H