#include <gtest/gtest.h>

#include "chainparams.h"

class SidechainTypesTestSuite : public ::testing::Test {
  public:
    void SetUp() override { SelectParams(CBaseChainParams::REGTEST); };

    void TearDown() override{};
};

///////////////////////////////////////////////////////////////////////////////
/////////////////////// CZendooBatchProofVerifierResult ///////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTypesTestSuite, CZendooBatchProofVerifierResultDestructor) {
    size_t arraySize = 5;

    // Initialize a raw pointer
    ZendooBatchProofVerifierResult* rawPointer = new ZendooBatchProofVerifierResult(new uint32_t[arraySize], arraySize);
    rawPointer->result = false;

    ASSERT_EQ(rawPointer->result, false);
    ASSERT_NE(rawPointer->failing_proofs, nullptr);
    ASSERT_EQ(rawPointer->failing_proofs_len, arraySize);

    {
        // Create a "smart object" from the raw pointer
        CZendooBatchProofVerifierResult obj(rawPointer);

        ASSERT_EQ(obj.Result(), false);
        ASSERT_EQ(obj.FailedProofs().size(), arraySize);
    }

    // Check with a memory profiler (e.g. Valgrind) that there are no memory leaks.
}