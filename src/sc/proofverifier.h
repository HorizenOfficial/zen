#ifndef _SC_PROOF_VERIFIER_H
#define _SC_PROOF_VERIFIER_H

#include <vector>
#include <sc/verifierutils.h>
class CSidechain;
class CScCertificate;

namespace libzendoomc {

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
        CProofVerificationContext();
        CProofVerificationContext(const unsigned char* end_epoch_mc_b_hash, const unsigned char* prev_end_epoch_mc_b_hash,
                                  const backward_transfer_t* bt_list, size_t bt_list_len, uint64_t quality, const field_t* constant,
                                  const field_t* proofdata, const sc_proof_t* sc_proof, const sc_vk_t* sc_vk);

        void setNull();
        bool isNull() const;
    };

    /* Abstract functor for verifying a sc proof given a proof verification context */
    class CVerifyFunction
    {
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
            virtual bool checkInputsParameters(const CProofVerificationContext& params) const { return true; };
    };

    /* Functor for verifying a Withdrawal Certificate SNARK proof */
    class CWCertVerifyFunction : public CVerifyFunction {
        public:
            bool checkInputsParameters(const CProofVerificationContext& params) const override;
            bool run(const CProofVerificationContext& params);
    };

    /* Given a CVerifyFunction and a CProofVerificationContext, this class is able to verify the corresponding CCTP SNARK proof. */
    class CProofVerifier
    {
        public:
            CProofVerifier();

            /* Set the CVerifyFunction to be used by this CProofVerifier */
            void setVerifyFunction(CVerifyFunction* func);

            /* Set the CProofVerificationContext to be used by this CProofVerifier. For testing purposes */
            void setVerificationContext(const CProofVerificationContext& verificationContext);

            /* Update the CProofVerificationContext according to data provided by scInfo and cert */
            bool loadData(const CSidechain& scInfo, const CScCertificate& cert);

            /* Run verificationFunc using ctx*/
            bool execute() const;
        private:
            CVerifyFunction* verificationFunc;
            CProofVerificationContext ctx;
    };
};

#endif // _SC_PROOF_VERIFIER_H
