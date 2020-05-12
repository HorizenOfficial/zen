#ifndef _SC_PROOF_VERIFIER_H
#define _SC_PROOF_VERIFIER_H

#include "verifierutils.h"
#include "sidechain.h"

namespace libzendoomc{

    static constexpr size_t GROTH_PROOF_SIZE = (
        193 +  // π_A
        193 + // π_B
        385);  // π_C

    typedef std::vector<unsigned char> ScProof;
    typedef std::vector<unsigned char> ScVk;

    /* 
     * Declares and holds proof, vk and the inputs. For the moment these are only the ones related
     * to the Withdrawal Certificate proof.
     */
    struct CProofVerificationContext {
        const unsigned char* end_epoch_mc_b_hash;
        const unsigned char* prev_end_epoch_mc_b_hash;
        const backward_transfer_t* bt_list;
        size_t bt_list_len;
        uint64_t quality;
        const field_t* constant; 
        const field_t* proofdata; 
        const sc_proof_t* sc_proof; 
        const sc_vk_t* sc_vk; 
        CProofVerificationContext(): end_epoch_mc_b_hash(nullptr), prev_end_epoch_mc_b_hash(nullptr),
                                     bt_list(nullptr), bt_list_len(-1), quality(0), constant(nullptr),
                                     proofdata(nullptr), sc_proof(nullptr), sc_vk(nullptr) {}

        CProofVerificationContext(const unsigned char* end_epoch_mc_b_hash, const unsigned char* prev_end_epoch_mc_b_hash, 
                                  const backward_transfer_t* bt_list, size_t bt_list_len, uint64_t quality, const field_t* constant,
                                  const field_t* proofdata, const sc_proof_t* sc_proof, const sc_vk_t* sc_vk):
                                  end_epoch_mc_b_hash(end_epoch_mc_b_hash), prev_end_epoch_mc_b_hash(prev_end_epoch_mc_b_hash),
                                  bt_list(bt_list), bt_list_len(bt_list_len), quality(quality), constant(constant),
                                  proofdata(proofdata), sc_proof(sc_proof), sc_vk(sc_vk) {}

        void setNull();
        bool isNull() const;
    };

    /* Abstract functor for verifying a sc proof given a proof verification context */
    class CVerifyFunction {
        public:
            CVerifyFunction() = default;

            /* Run the verification function wrapped by this class using the provided params */
            virtual bool run(const CProofVerificationContext& params) = 0;

            virtual ~CVerifyFunction() = default;
        protected:
            /* 
             * Check the pre-requisites to be able to call the proof verification function using
             * the CProofVerification Context provided by params.
             */
            virtual bool checkInputsParameters(const CProofVerificationContext& params) const { return true; }
    };

    /* Functor for verifying a Withdrawal Certificate SNARK proof */
    class CWCertVerifyFunction : public CVerifyFunction {
        public:

            bool run(const CProofVerificationContext& params){
                if (!checkInputsParameters(params))
                    return false;

                return zendoo_verify_sc_proof(params.end_epoch_mc_b_hash, params.prev_end_epoch_mc_b_hash,
                                              params.bt_list, params.bt_list_len, params.quality,
                                              params.constant, params.proofdata, params.sc_proof, params.sc_vk);
            }

            bool checkInputsParameters(const CProofVerificationContext& params) const override;
    };

    /* Given a CVerifyFunction and a CProofVerificationContext, this class is able to verify the corresponding CCTP SNARK proof. */
    class CProofVerifier {
        public:
            CProofVerifier(): verificationFunc(nullptr) {};

            /* Set the CVerifyFunction to be used by this CProofVerifier */
            void setVerifyFunction(CVerifyFunction* func) {verificationFunc = func;};

            /* Set the CProofVerificationContext to be used by this CProofVerifier. For testing purposes */
            void setVerificationContext(const CProofVerificationContext& verificationContext) { ctx = verificationContext; }

            /* Update the CProofVerificationContext according to data provided by scInfo and cert */
            bool loadData(const CSidechain& scInfo, const CScCertificate& cert);

            /* Run verificationFunc using ctx*/
            bool execute() const {
                if(!verificationFunc)
                    return false;

                return verificationFunc->run(this->ctx);
            }
        private:
            CVerifyFunction* verificationFunc;
            CProofVerificationContext ctx;
    };
};

#endif // _SC_PROOF_VERIFIER_H