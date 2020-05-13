#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <sc/proofverifier.h>
#include <main.h>
#include <primitives/certificate.h>
#include <sc/TEMP_zendooInterface.h>

////////////// MOCKS
class CFaultyVerifyClass : public libzendoomc::CVerifyFunction {
public:
    bool run(const libzendoomc::CProofVerificationContext& params){
        if (!checkInputsParameters(params))
            return false;

        return NO_zendoo_verify_sc_proof(params.end_epoch_mc_b_hash, params.prev_end_epoch_mc_b_hash,
                                         params.bt_list, params.bt_list_len, params.quality,
                                         params.constant, params.proofdata, params.sc_proof, params.sc_vk);
    }
private:
    bool NO_zendoo_verify_sc_proof(
        const unsigned char* end_epoch_mc_b_hash,
        const unsigned char* prev_end_epoch_mc_b_hash,
        const backward_transfer_t* bt_list,
        size_t bt_list_len,
        uint64_t quality,
        const field_t* constant,
        const field_t* proofdata,
        const sc_proof_t* sc_proof,
        const sc_vk_t* sc_vk
    ) {return false;}
} globalObj_FaultyProof;

class CGoodVerifyClass : public libzendoomc::CVerifyFunction {
public:
    bool run(const libzendoomc::CProofVerificationContext& params) {
        if (!checkInputsParameters(params))
            return false;

        return YES_zendoo_verify_sc_proof(params.end_epoch_mc_b_hash, params.prev_end_epoch_mc_b_hash,
                                         params.bt_list, params.bt_list_len, params.quality,
                                         params.constant, params.proofdata, params.sc_proof, params.sc_vk);
    }
private:
    bool YES_zendoo_verify_sc_proof(
        const unsigned char* end_epoch_mc_b_hash,
        const unsigned char* prev_end_epoch_mc_b_hash,
        const backward_transfer_t* bt_list,
        size_t bt_list_len,
        uint64_t quality,
        const field_t* constant,
        const field_t* proofdata,
        const sc_proof_t* sc_proof,
        const sc_vk_t* sc_vk
    ) {return true;}
} globalObj_GoodProof;
////////////// END MOCKS

///////////////// TESTS
TEST(ScVerification, Verifier_ChangeVerifyFunction) {
    libzendoomc::CProofVerifier verifier;
    verifier.setVerifyFunction(&globalObj_GoodProof);

    CSidechain dummyInfo;
    CScCertificate emptyCert;
    verifier.loadData(dummyInfo, emptyCert);
    EXPECT_TRUE(verifier.execute());

    verifier.setVerifyFunction(&globalObj_FaultyProof);
    EXPECT_FALSE(verifier.execute());
}

TEST(ScVerification, Verifier_HardcodedContext) {

    unsigned char end_epoch_mc_b_hash[32] = {
        48, 202, 96, 61, 206, 20, 30, 152, 124, 86, 199, 13, 154, 135, 39, 58, 53, 150, 69, 169, 123, 71, 0, 29, 62, 97,
        198, 19, 5, 184, 196, 31
    };

    unsigned char prev_end_epoch_mc_b_hash[32] = {
        241, 72, 150, 254, 135, 196, 102, 189, 247, 180, 78, 56, 187, 156, 23, 190, 23, 27, 165, 52, 6, 74, 221, 100,
        220, 174, 251, 72, 134, 19, 158, 238
    };

    unsigned char constant_bytes[96] = {
        218, 197, 230, 227, 177, 215, 180, 32, 249, 205, 103, 89, 92, 233, 4, 105, 201, 216, 112, 32, 168, 129, 18, 94,
        199, 130, 168, 130, 150, 128, 178, 170, 98, 98, 118, 187, 73, 126, 4, 218, 2, 240, 197, 4, 236, 226, 238, 149,
        151, 108, 163, 148, 180, 175, 38, 59, 87, 38, 42, 213, 100, 214, 12, 117, 186, 161, 114, 100, 120, 85, 6, 211,
        34, 173, 106, 43, 111, 104, 185, 243, 108, 0, 126, 16, 190, 8, 113, 39, 195, 175, 189, 138, 132, 104, 0, 0
    };

    uint64_t quality = 2;

    //Create dummy bt
    constexpr int bt_list_len = 10;
    const backward_transfer_t bt_list[bt_list_len] = { {0}, 0 };

    const libzendoomc::CProofVerificationContext& ctx = libzendoomc::CProofVerificationContext(
        end_epoch_mc_b_hash, prev_end_epoch_mc_b_hash, bt_list, bt_list_len,
        quality, zendoo_deserialize_field(constant_bytes), nullptr, nullptr, nullptr);

    libzendoomc::CProofVerifier verifier;
    verifier.setVerifyFunction(&globalObj_GoodProof);
    verifier.setVerificationContext(ctx);
}
