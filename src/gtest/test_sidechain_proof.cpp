#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <sc/proofverifier.h>
#include <main.h>
#include <primitives/certificate.h>
#include <sc/TEMP_zendooInterface.h>

///////////////////////////////////////////////////MOCKS

////////PROOF VERIFIER MOCKS

class CScWCertProofVerificationParametersMock: public libzendoomc::CScWCertProofVerificationParameters {
    public:
        static bool okDeserializeField;
        static bool okDeserializeScVk;
        static bool okDeserializeScProof;
        static bool okVerifyScProof;

        CScWCertProofVerificationParametersMock(const CSidechain& scInfo, const CScCertificate& scCert):
            libzendoomc::CScWCertProofVerificationParameters(scInfo, scCert) {}

        field_t* deserialize_field(const unsigned char* field_bytes) const override {
            if(okDeserializeField)
                return zendoo_deserialize_field(field_bytes);
            else
                return nullptr;
        }

        sc_proof_t* deserialize_sc_proof(const unsigned char* sc_proof_bytes) const override {
            if(okDeserializeScProof)
                return zendoo_deserialize_sc_proof(sc_proof_bytes);
            else
                return nullptr;
        }

        sc_vk_t* deserialize_sc_vk_from_file(const path_char_t* vk_path, size_t vk_path_len) const override {
            if(okDeserializeScVk)
                return zendoo_deserialize_sc_vk_from_file(vk_path, vk_path_len);
            else
                return nullptr;
        }

        bool verify_sc_proof(
            const unsigned char* end_epoch_mc_b_hash,
            const unsigned char* prev_end_epoch_mc_b_hash,
            const backward_transfer_t* bt_list,
            size_t bt_list_len,
            uint64_t quality,
            const field_t* constant,
            const field_t* proofdata,
            const sc_proof_t* sc_proof,
            const sc_vk_t* sc_vk
        ) const override
        {
            if(okVerifyScProof)
                return true;
            else
                return false;
        }
};

//Static variables declaration
bool CScWCertProofVerificationParametersMock::okDeserializeField;
bool CScWCertProofVerificationParametersMock::okDeserializeScVk;
bool CScWCertProofVerificationParametersMock::okDeserializeScProof;
bool CScWCertProofVerificationParametersMock::okVerifyScProof;

// Cannot inherit from CScProofVerifier and directly override the operator().
// The easiest solution is to create a new one which inherits boost::static_visitor too.
class TestCScProofVerifier: public boost::static_visitor<bool> {
    protected:
        const CSidechain* scInfo;

        TestCScProofVerifier(bool perform_verification): perform_verification(perform_verification), scInfo(nullptr) {}
        TestCScProofVerifier(bool perform_verification, const CSidechain* scInfo) :
            perform_verification(perform_verification), scInfo(scInfo) {}

    public:
        bool perform_verification;
        static TestCScProofVerifier Strict(const CSidechain* scInfo){ return TestCScProofVerifier(true, scInfo); }
        static TestCScProofVerifier Disabled() { return TestCScProofVerifier(false); }

        bool operator()(const CScCertificate& scCert) const {
            return CScWCertProofVerificationParametersMock(*scInfo, scCert).run(perform_verification);
        }
};

////////////////////////////////////////////////////END MOCKS

class CScProofTestSuite: public ::testing::Test {
    public:
        CScProofTestSuite(): scInfo(nullptr), scCert(nullptr), verifier(TestCScProofVerifier::Disabled()) {}
        ~CScProofTestSuite() = default;

        void SetUp() override {
            scInfo = new CSidechain();

            //Needed to avoid passing checks that shouldn't pass, because customData is optional
            scInfo->creationData.customData.push_back('0');
            
            scCert = new CScCertificate();

            verifier = TestCScProofVerifier::Strict(scInfo);
        }

        void TearDown() override {
            delete scInfo;
            scInfo = nullptr;

            delete scCert;
            scCert = nullptr;
        }
    
        CSidechain* scInfo;
        CScCertificate* scCert;
        TestCScProofVerifier verifier;

        bool verifyCert(
            bool okDeserializeField,
            bool okDeserializeScVk,
            bool okDeserializeScProof,
            bool okVerifyScProof
        )
        {
            CScWCertProofVerificationParametersMock::okDeserializeField = okDeserializeField;
            CScWCertProofVerificationParametersMock::okDeserializeScVk = okDeserializeScVk;
            CScWCertProofVerificationParametersMock::okDeserializeScProof = okDeserializeScProof;
            CScWCertProofVerificationParametersMock::okVerifyScProof = okVerifyScProof;
            return verify(*scCert);
        }

        void setScInfo(CSidechain* newScInfo) { delete scInfo; scInfo = newScInfo; }
        void setScCert(CScCertificate* newScCert) { delete scCert; scCert = newScCert; }
        void setVerifier(bool strict){
            if(strict)
                verifier = TestCScProofVerifier::Strict(scInfo);
            else
                verifier = TestCScProofVerifier::Disabled();
        }
        
    protected:
        bool verify(const libzendoomc::CScProofVerificationContext& ctx){
            return boost::apply_visitor(verifier, ctx);
        }
};

TEST_F(CScProofTestSuite, StrictVerifier_WCertProof_WrongInputs) {
    EXPECT_FALSE(verifyCert(false, true, true, true)); //Failed to deserialize constant, assuming it's present
    EXPECT_FALSE(verifyCert(true, false, true, true)); //Failed to deserialize sc_vk
    EXPECT_FALSE(verifyCert(true, true, false, true)); //Failed to deserialize proof
}

TEST_F(CScProofTestSuite, StrictVerifier_WCertProof_PositiveVerification) {
    EXPECT_TRUE(verifyCert(true, true, true, true));
}

TEST_F(CScProofTestSuite, StrictVerifier_WCertProof_NegativeVerification) {
    EXPECT_FALSE(verifyCert(true, true, true, false));
}

//More of a test with semantic meaning, probably it's not the time for this
TEST_F(CScProofTestSuite, DISABLED_StrictVerifier_WCertProof_MixedVerification) {
    CSidechain infoGood, infoFaulty;
    CScCertificate certGood, certFaulty;

    //ScInfo is wrong for the actual ScCertificate
    setScInfo(&infoFaulty);
    setScCert(&certGood);
    setVerifier(true);
    EXPECT_FALSE(verifyCert(true, true, true, false));

    //ScCertificate is wrong for the actual ScInfo
    setScInfo(&infoGood);
    setScCert(&certFaulty);
    setVerifier(true);
    EXPECT_FALSE(verifyCert(true, true, true, false));
}

TEST_F(CScProofTestSuite, DisabledVerifier_WCertProof_AlwaysPositiveVerification) {
    std::vector<bool> b = {true, false};
    setVerifier(false);

    //Any combination is allowed
    for(bool var1: b)
        for(bool var2: b)
            for(bool var3: b)
                for(bool var4: b)
                    EXPECT_TRUE(verifyCert(var1, var2, var3, var4));
}