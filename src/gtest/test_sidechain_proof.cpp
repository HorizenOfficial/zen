#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <sc/proofverifier.h>
#include <main.h>
#include <primitives/certificate.h>
#include <sc/TEMP_zendooInterface.h>

TEST(ScProofVerification, DISABLED_Verifier_WCertProof_PositiveVerification) {
    auto verifier = libzendoomc::CScProofVerifier::Strict();
    
    CSidechain realInfo;
    CScCertificate realCert;
    libzendoomc::CWCertProofVerificationContext wCertCtx;

    wCertCtx.updateParameters(realInfo, realCert);
    EXPECT_TRUE(wCertCtx.checkParameters());
    EXPECT_TRUE(wCertCtx.verify(verifier));
}

TEST(ScProofVerification, DISABLED_Verifier_WCertProof_NegativeVerification) {

    auto verifier_strict = libzendoomc::CScProofVerifier::Strict();
    auto verifier_disabled = libzendoomc::CScProofVerifier::Disabled();
    
    CSidechain dummyInfo;
    CScCertificate dummyCert;
    libzendoomc::CWCertProofVerificationContext wCertCtx;

    wCertCtx.updateParameters(dummyInfo, dummyCert);
    EXPECT_FALSE(wCertCtx.checkParameters());
    EXPECT_FALSE(wCertCtx.verify(verifier_strict));
    EXPECT_TRUE(wCertCtx.verify(verifier_disabled));
}

TEST(ScProofVerification, DISABLED_Verifier_WCertProof_MixedVerification) {

    auto verifier_strict = libzendoomc::CScProofVerifier::Strict();
    auto verifier_disabled = libzendoomc::CScProofVerifier::Disabled();
    
    CSidechain dummyInfo, realInfo;
    CScCertificate dummyCert, realCert;
    libzendoomc::CWCertProofVerificationContext wCertCtx;

    wCertCtx.updateParameters(dummyInfo, realCert);
    EXPECT_FALSE(wCertCtx.checkParameters());
    EXPECT_FALSE(wCertCtx.verify(verifier_strict));
    EXPECT_TRUE(wCertCtx.verify(verifier_disabled));

    wCertCtx.updateParameters(realInfo, dummyCert);
    EXPECT_FALSE(wCertCtx.checkParameters());
    EXPECT_FALSE(wCertCtx.verify(verifier_strict));
    EXPECT_TRUE(wCertCtx.verify(verifier_disabled));
}
