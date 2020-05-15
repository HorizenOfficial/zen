#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <sc/proofverifier.h>
#include <main.h>
#include <primitives/certificate.h>
#include <sc/TEMP_zendooInterface.h>

///////////////////////////////////////////////////MOCKS

////////ZENDOO-MC-CRYPTOLIB MOCKS

field_t* dummy_field_ptr = new field_t();
sc_proof_t* dummy_sc_proof_ptr = new sc_proof_t();
sc_vk_t* dummy_sc_vk_ptr = new sc_vk_t();

const size_t DUMMY_FIELD_SIZE = 1;
const size_t DUMMY_SC_PROOF_SIZE = 1;

bool zendoo_verify_sc_proof_mock(
    const unsigned char* end_epoch_mc_b_hash,
    const unsigned char* prev_end_epoch_mc_b_hash,
    const backward_transfer_t* bt_list,
    size_t bt_list_len,
    uint64_t quality,
    const field_t* constant,
    const field_t* proofdata,
    const sc_proof_t* sc_proof,
    const sc_vk_t* sc_vk
) { return true; }

size_t zendoo_get_field_size_in_bytes_mock(){
    return DUMMY_FIELD_SIZE;
}

size_t zendoo_get_sc_proof_size_mock(){
    return DUMMY_SC_PROOF_SIZE;
}

field_t* zendoo_deserialize_field_mock(const unsigned char* field_bytes){
    if (field_bytes == nullptr)
        return nullptr;
    else
        return dummy_field_ptr;
}

sc_proof_t* zendoo_deserialize_sc_proof_mock(const unsigned char* sc_proof_bytes){
    if (sc_proof_bytes == nullptr)
        return nullptr;
    else
        return dummy_sc_proof_ptr;
}

sc_vk_t* zendoo_deserialize_sc_vk_mock(const unsigned char* sc_vk_bytes){
    if (sc_vk_bytes == nullptr)
        return nullptr;
    else
        return dummy_sc_vk_ptr;
}

////////PROOF VERIFIER MOCKS

class CScWCertProofVerificationParametersMock: public libzendoomc::CScWCertProofVerificationParameters {
    public:
        CScWCertProofVerificationParametersMock(const CSidechain& scInfo, const CScCertificate& scCert):
            libzendoomc::CScWCertProofVerificationParameters(scInfo, scCert) { }
        
        bool createParameters() override {
            //Assumption: both ScInfo and ScCertificate are semantically valid
            
            //Deserialize constant
            auto constant_bytes = scInfo.creationData.customData;
            if (!constant_bytes.size() == 0){ //Constant can be optional

                constant = zendoo_deserialize_field_mock(constant_bytes.data()); 
                if (constant_bytes.size() != zendoo_get_field_size_in_bytes_mock() || //For now constant must be just a single field element
                    constant == nullptr) {
                    return false;
                }
            } else {
                constant = nullptr;
            }

            //Initialize quality and proofdata
            quality = scCert.quality;
            proofdata = nullptr; //Note: For now proofdata is not present in WCert

            //Deserialize proof
            auto sc_proof_bytes = scCert.scProof;
            sc_proof = zendoo_deserialize_sc_proof_mock(sc_proof_bytes.data());
            if (sc_proof_bytes.size() != zendoo_get_sc_proof_size_mock() ||
                sc_proof == nullptr)
                return false;

            //Deserialize sc_vk
            //TODO: Insert correct data after having built logic to handle vks
            sc_vk = zendoo_deserialize_sc_vk_mock(nullptr);

            if (sc_vk == nullptr)
                return false;

            //Retrieve MC block hashes
            //LOCK(cs_main); TODO: Is LOCK needed here ?
            end_epoch_mc_b_hash = scCert.endEpochBlockHash.begin();
            int targetHeight = scInfo.StartHeightForEpoch(scCert.epochNumber) - 1; //Is this ok ? Can epochNumber be 0 or 1 ?
            auto mc_block = chainActive[targetHeight];
            if(mc_block == nullptr) {
                prev_end_epoch_mc_b_hash = nullptr;
            }
            //prev_end_epoch_mc_b_hash = (chainActive[targetHeight] -> GetBlockHash()).begin();

            //Retrieve BT list
            std::vector<backward_transfer_t> btList;
            int btListLen = 0; //What if BTList is 0 in the certificate ?
            for (auto out : scCert.GetVout()){
                if (out.isFromBackwardTransfer){
                    CBackwardTransferOut btout(out);
                    backward_transfer bt;

                    //TODO: Find a better way
                    for(int i = 0; i < 20; i++)
                        bt.pk_dest[i] = btout.pubKeyHash.begin()[i];

                    bt.amount = btout.nValue;

                    btList.push_back(bt);
                    btListLen += 1;
                }
            }
            bt_list = btList.data();
            bt_list_len = btListLen;
            return true;
        }

        bool verifierCall() const override {
            return zendoo_verify_sc_proof_mock(
                end_epoch_mc_b_hash, prev_end_epoch_mc_b_hash, bt_list,
                bt_list_len, quality, constant, proofdata, sc_proof, sc_vk
            );
        }
};

class CScProofVerifierMock: public libzendoomc::CScProofVerifier {
    bool operator()(const CScCertificate& scCert) const {
        auto params = CScWCertProofVerificationParametersMock(*scInfo, scCert);
        return performVerification(params);
    }
};

////////////////////////////////////////////////////END MOCKS

bool verify(const libzendoomc::CScProofVerifier& verifier, const libzendoomc::CScProofVerificationContext& ctx){
    return boost::apply_visitor(verifier, ctx);
}

TEST(ScProofVerification, DISABLED_StrictVerifier_WCertProof_WrongInputs) {
    CSidechain dummyInfo;
    CScCertificate realCertGood;
    auto verifier1 = CScProofVerifierMock::Strict(&dummyInfo);

    //Should fail due to error in "constant" deserialization
    EXPECT_FALSE(verify(verifier1, realCertGood));

    CSidechain realInfoGood;
    CScCertificate dummyCert;
    auto verifier2 = CScProofVerifierMock::Strict(&realInfoGood);

    //Should fail due to error in "sc_proof" deserialization
    EXPECT_FALSE(verify(verifier2, dummyCert));

    //TODO: Once defined vk logic add another test case for error in "sc_vk" deserialization
}

TEST(ScProofVerification, DISABLED_StrictVerifier_WCertProof_PositiveVerification) {
    CSidechain realInfoGood;
    CScCertificate realCertGood;

    auto verifier = CScProofVerifierMock::Strict(&realInfoGood);

    EXPECT_TRUE(verify(verifier, realCertGood));
}

TEST(ScProofVerification, DISABLED_StrictVerifier_WCertProof_NegativeVerification) {
    CSidechain realInfoFaulty;
    CScCertificate realCertFaulty;

    auto verifier_strict = CScProofVerifierMock::Strict(&realInfoFaulty);

    EXPECT_FALSE(verify(verifier_strict, realCertFaulty));
}

TEST(ScProofVerification, DISABLED_StrictVerifier_WCertProof_MixedVerification) {
    CSidechain realInfoGood, realInfoFaulty;
    CScCertificate realCertGood, realCertFaulty;

    auto verifier_strict = CScProofVerifierMock::Strict(&realInfoFaulty);
    
    EXPECT_FALSE(verify(verifier_strict, realCertGood));

    auto verifier_strict_new = CScProofVerifierMock::Strict(&realInfoGood);

    EXPECT_FALSE(verify(verifier_strict_new, realCertFaulty));
}

TEST(ScProofVerification, DISABLED_DisabledVerifier_WCertProof_AlwaysPositiveVerification) {
    CScCertificate dummyCert, realCertGood, realCertFaulty;

    auto verifier_disabled = CScProofVerifierMock::Disabled();
    
    EXPECT_TRUE(verify(verifier_disabled, dummyCert));
    EXPECT_TRUE(verify(verifier_disabled, realCertGood));
    EXPECT_TRUE(verify(verifier_disabled, realCertFaulty));

}