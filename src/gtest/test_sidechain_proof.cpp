#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <sc/proofverifier.h>
#include <main.h>
#include <primitives/certificate.h>
#include <sc/TEMP_zendooInterface.h>

bool verify(const libzendoomc::CScProofVerifier& verifier, const libzendoomc::CScProofVerificationContext& ctx){
    return boost::apply_visitor(verifier, ctx);
}

TEST(ScProofVerification, DISABLED_Verifier_WCertProof_PositiveVerification) {
    CSidechain realInfo;
    CScCertificate realCert;
    auto verifier = libzendoomc::CScProofVerifier::Strict(&realInfo);
    EXPECT_TRUE(verify(verifier, realCert));
}

TEST(ScProofVerification, DISABLED_Verifier_WCertProof_NegativeVerification) {
    CSidechain dummyInfo;
    CScCertificate dummyCert;

    auto verifier_strict = libzendoomc::CScProofVerifier::Strict(&dummyInfo);
    auto verifier_disabled = libzendoomc::CScProofVerifier::Disabled();
    
    EXPECT_FALSE(verify(verifier_strict, dummyCert));
    EXPECT_TRUE(verify(verifier_disabled, dummyCert));
}

TEST(ScProofVerification, DISABLED_Verifier_WCertProof_MixedVerification) {
    CSidechain dummyInfo, realInfo;
    CScCertificate dummyCert, realCert;

    auto verifier_strict = libzendoomc::CScProofVerifier::Strict(&dummyInfo);
    auto verifier_disabled = libzendoomc::CScProofVerifier::Disabled();
    
    EXPECT_FALSE(verify(verifier_strict, realCert));
    EXPECT_TRUE(verify(verifier_disabled, realCert));

    auto verifier_strict_new = libzendoomc::CScProofVerifier::Strict(&realInfo);

    EXPECT_FALSE(verify(verifier_strict_new, dummyCert));
    EXPECT_TRUE(verify(verifier_disabled, dummyCert));
}
