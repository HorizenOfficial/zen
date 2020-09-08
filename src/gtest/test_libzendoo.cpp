#include <gtest/gtest.h>

#include <primitives/transaction.h>
#include <primitives/certificate.h>
#include <sc/sidechainTxsCommitmentBuilder.h>
#include "tx_creation_utils.h"

#include <gtest/libzendoo_test_files.h>
#include <util.h>
#include <cstring>
#include <utilstrencodings.h>

TEST(ZendooLib, FieldTest)
{
    //Size is the expected one
    int field_len = zendoo_get_field_size_in_bytes();
    ASSERT_EQ(field_len, SC_FIELD_SIZE);

    auto field = zendoo_get_random_field();

    //Serialize and deserialize and check equality
    unsigned char field_bytes[SC_FIELD_SIZE];
    zendoo_serialize_field(field, field_bytes);

    auto field_deserialized = zendoo_deserialize_field(field_bytes);
    ASSERT_TRUE(field_deserialized != NULL);

    ASSERT_TRUE(zendoo_field_assert_eq(field, field_deserialized));

    zendoo_field_free(field);
    zendoo_field_free(field_deserialized);
}


TEST(ZendooLib, PoseidonHashTest) 
{
    unsigned char lhs[SC_FIELD_SIZE] = {
        138, 206, 199, 243, 195, 254, 25, 94, 236, 155, 232, 182, 89, 123, 162, 207, 102, 52, 178, 128, 55, 248, 234,
        95, 33, 196, 170, 12, 118, 16, 124, 96, 47, 203, 160, 167, 144, 153, 161, 86, 213, 126, 95, 76, 27, 98, 34, 111,
        144, 36, 205, 124, 200, 168, 29, 196, 67, 210, 100, 154, 38, 79, 178, 191, 246, 115, 84, 232, 87, 12, 34, 72,
        88, 23, 236, 142, 237, 45, 11, 148, 91, 112, 156, 47, 68, 229, 216, 56, 238, 98, 41, 243, 225, 192, 0, 0
    };

    unsigned char rhs[SC_FIELD_SIZE] = {
        199, 130, 235, 52, 44, 219, 5, 195, 71, 154, 54, 121, 3, 11, 111, 160, 86, 212, 189, 66, 235, 236, 240, 242,
        126, 248, 116, 0, 48, 95, 133, 85, 73, 150, 110, 169, 16, 88, 136, 34, 106, 7, 38, 176, 46, 89, 163, 49, 162,
        222, 182, 42, 200, 240, 149, 226, 173, 203, 148, 194, 207, 59, 44, 185, 67, 134, 107, 221, 188, 208, 122, 212,
        200, 42, 227, 3, 23, 59, 31, 37, 91, 64, 69, 196, 74, 195, 24, 5, 165, 25, 101, 215, 45, 92, 1, 0
    };

    unsigned char hash[SC_FIELD_SIZE] = {
        53, 2, 235, 12, 255, 18, 125, 167, 223, 32, 245, 103, 38, 74, 43, 73, 254, 189, 174, 137, 20, 90, 195, 107, 202,
        24, 151, 136, 85, 23, 9, 93, 207, 33, 229, 200, 178, 225, 221, 127, 18, 250, 108, 56, 86, 94, 171, 1, 76, 21,
        237, 254, 26, 235, 196, 14, 18, 129, 101, 158, 136, 103, 147, 147, 239, 140, 163, 94, 245, 147, 110, 28, 93,
        231, 66, 7, 111, 11, 202, 99, 146, 211, 117, 143, 224, 99, 183, 108, 157, 200, 119, 169, 180, 148, 0, 0,
    };

    auto lhs_field = zendoo_deserialize_field(lhs);
    ASSERT_TRUE(lhs_field != NULL);

    auto rhs_field = zendoo_deserialize_field(rhs);
    ASSERT_TRUE(rhs_field != NULL);


    auto expected_hash = zendoo_deserialize_field(hash);
    ASSERT_TRUE(expected_hash != NULL);

    auto digest = ZendooPoseidonHash(NULL, 0);

    digest.update(lhs_field);

    auto temp_hash = digest.finalize();
    digest.update(rhs_field); // Call to finalize keeps the state

    auto actual_hash = digest.finalize();
    ASSERT_TRUE(("Expected hashes to be equal", zendoo_field_assert_eq(actual_hash, expected_hash)));
    zendoo_field_free(actual_hash);

    auto actual_hash_2 = digest.finalize(); // finalize() is idempotent
    ASSERT_TRUE(("Expected hashes to be equal", zendoo_field_assert_eq(actual_hash_2, expected_hash)));
    zendoo_field_free(actual_hash_2);

    zendoo_field_free(expected_hash);
    zendoo_field_free(temp_hash);
    zendoo_field_free(lhs_field);
    zendoo_field_free(rhs_field);
}

TEST(ZendooLib, PoseidonMerkleTreeTest)  {

    size_t height = 10;

    // Deserialize root
    unsigned char expected_root_bytes[SC_FIELD_SIZE] = {
        231, 64, 42, 251, 206, 22, 102, 105, 222, 145, 252, 133, 62, 169, 60, 150, 50, 133, 187, 38, 47, 246, 192, 170,
        161, 204, 152, 177, 20, 209, 217, 101, 34, 159, 246, 176, 23, 223, 62, 191, 103, 165, 210, 114, 179, 110, 140,
        252, 250, 167, 106, 31, 7, 178, 109, 108, 20, 239, 162, 121, 99, 207, 137, 224, 124, 212, 65, 229, 5, 112, 116,
        75, 145, 11, 77, 252, 134, 37, 127, 54, 244, 236, 68, 129, 16, 191, 196, 6, 17, 185, 138, 98, 183, 153, 1, 0
    };
    auto expected_root = zendoo_deserialize_field(expected_root_bytes);
    ASSERT_TRUE(expected_root != NULL);

    //Generate leaves

    //enum removes variable length buffer [-Wstack-protector] warning that simple const int would give
    enum { leaves_len = 512 };

    const field_t* leaves[leaves_len];
    for (int i = 0; i < leaves_len; i++){
        leaves[i] = zendoo_get_field_from_long(i);
    }

    // Initialize tree
    auto tree = ZendooGingerRandomAccessMerkleTree(height);

    // Add leaves to tree
    for (int i = 0; i < leaves_len; i++){
        tree.append(leaves[i]);
    }

    // Finalize tree
    tree.finalize_in_place();

    // Compute root and assert equality with expected one
    auto root = tree.root();
    ASSERT_TRUE(("Expected roots to be equal", zendoo_field_assert_eq(root, expected_root)));

    // It is the same by calling finalize()
    auto tree_copy = tree.finalize();
    auto root_copy = tree_copy.root();
    ASSERT_TRUE(("Expected roots to be equal", zendoo_field_assert_eq(root_copy, expected_root)));

    // Free memory
    zendoo_field_free(expected_root);
    for (int i = 0; i < leaves_len; i++){
        zendoo_field_free((field_t*)leaves[i]);
    }
    zendoo_field_free(root);
    zendoo_field_free(root_copy);
}

// Execute the test from zen directory
TEST(ZendooLib, TestProof)
{
    //Deserialize zero knowledge proof
    auto proof_serialized = ParseHex(SAMPLE_PROOF);
    ASSERT_EQ(proof_serialized.size(), zendoo_get_sc_proof_size_in_bytes());
    auto proof = zendoo_deserialize_sc_proof(proof_serialized.data());
    ASSERT_TRUE(proof != NULL);

    //Inputs
    unsigned char end_epoch_mc_b_hash[32] = {
        157, 219, 85, 159, 75, 56, 146, 21, 107, 239, 76, 31, 208, 213, 230, 24, 44, 74, 250, 66, 71, 23, 106, 4, 138,
        157, 28, 43, 158, 39, 152, 91
    };

    unsigned char prev_end_epoch_mc_b_hash[32] = {
        74, 229, 219, 59, 25, 231, 227, 68, 3, 118, 194, 58, 99, 219, 112, 39, 73, 202, 238, 140, 114, 144, 253, 32,
        237, 117, 117, 60, 200, 70, 187, 171
    };

    unsigned char constant_bytes[SC_FIELD_SIZE] = {
        234, 144, 148, 15, 127, 44, 243, 131, 152, 238, 209, 246, 126, 175, 154, 42, 208, 215, 180, 233, 20, 153, 7, 10,
        180, 78, 89, 9, 9, 160, 1, 42, 91, 202, 221, 104, 241, 231, 8, 59, 174, 159, 27, 108, 74, 80, 118, 192, 127, 238,
        216, 167, 72, 15, 61, 97, 121, 13, 48, 143, 255, 165, 228, 6, 121, 210, 112, 228, 161, 214, 233, 137, 108, 184,
        80, 27, 213, 72, 110, 7, 200, 194, 23, 95, 102, 236, 181, 230, 139, 215, 104, 22, 214, 70, 0, 0
    };

    auto constant = zendoo_deserialize_field(constant_bytes);
    ASSERT_TRUE(constant != NULL);

    uint64_t quality = 2;

    //Create dummy bt
    const backward_transfer_t bt_list[10] = { {0}, 0 };

    //Deserialize vk
    auto vk_serialized = ParseHex(SAMPLE_VK);
    ASSERT_EQ(vk_serialized.size(), zendoo_get_sc_vk_size_in_bytes());
    auto vk = zendoo_deserialize_sc_vk(vk_serialized.data());
    ASSERT_TRUE(vk != NULL);

    //Verify zkproof
    ASSERT_TRUE(zendoo_verify_sc_proof(
            end_epoch_mc_b_hash,
            prev_end_epoch_mc_b_hash,
            bt_list,
            10,
            quality,
            constant,
            NULL,
            proof,
            vk
        ));

    //Negative test: change quality (for instance) and ASSERT_TRUE proof failure
    ASSERT_FALSE(zendoo_verify_sc_proof(
            end_epoch_mc_b_hash,
            prev_end_epoch_mc_b_hash,
            bt_list,
            10,
            quality - 1,
            constant,
            NULL,
            proof,
            vk
        ));

    //Free proof
    zendoo_sc_proof_free(proof);
    zendoo_sc_vk_free(vk);
    zendoo_field_free(constant);

}

// Execute the test from zen directory
TEST(ZendooLib, TestProofNoBwt)
{
    //Deserialize zero knowledge proof
    auto proof_serialized = ParseHex(SAMPLE_PROOF_NO_BWT);
    ASSERT_EQ(proof_serialized.size(), zendoo_get_sc_proof_size_in_bytes());
    auto proof = zendoo_deserialize_sc_proof(proof_serialized.data());
    ASSERT_TRUE(proof != NULL);

    //Inputs
    unsigned char end_epoch_mc_b_hash[32] = {
        8, 57, 79, 205, 58, 30, 190, 170, 144, 137, 231, 236, 172, 54, 173, 50, 69, 208, 163, 134, 201, 131, 129, 223,
        143, 76, 119, 48, 95, 6, 141, 17
    };

    unsigned char prev_end_epoch_mc_b_hash[32] = {
        172, 64, 135, 162, 30, 208, 207, 7, 107, 205, 4, 141, 230, 6, 119, 131, 112, 98, 170, 234, 70, 66, 95, 11, 159,
        178, 50, 37, 95, 187, 147, 1
    };

    unsigned char constant_bytes[SC_FIELD_SIZE] = {
        53, 15, 18, 36, 121, 179, 90, 14, 215, 218, 231, 181, 9, 186, 122, 78, 227, 142, 190, 43, 134, 218, 178, 160,
        251, 246, 207, 130, 247, 53, 246, 68, 251, 126, 22, 250, 0, 135, 243, 13, 97, 76, 166, 142, 143, 19, 69, 66,
        225, 142, 210, 176, 253, 197, 145, 68, 142, 4, 96, 91, 23, 39, 56, 43, 96, 115, 57, 59, 34, 62, 156, 221, 27,
        174, 134, 170, 26, 86, 112, 176, 126, 207, 29, 213, 99, 3, 183, 43, 191, 43, 211, 110, 177, 152, 0, 0
    };

    auto constant = zendoo_deserialize_field(constant_bytes);
    ASSERT_TRUE(constant != NULL);

    uint64_t quality = 2;

    //Create empty bt_list
    std::vector<backward_transfer_t> bt_list;

    //Deserialize vk
    auto vk_serialized = ParseHex(SAMPLE_VK_NO_BWT);
    ASSERT_EQ(vk_serialized.size(), zendoo_get_sc_vk_size_in_bytes());
    auto vk = zendoo_deserialize_sc_vk(vk_serialized.data());
    ASSERT_TRUE(vk != NULL);

    //Verify zkproof
    ASSERT_TRUE(zendoo_verify_sc_proof(
            end_epoch_mc_b_hash,
            prev_end_epoch_mc_b_hash,
            bt_list.data(),
            0,
            quality,
            constant,
            NULL,
            proof,
            vk
        ));

    //Negative test: change quality (for instance) and ASSERT_TRUE proof failure
    ASSERT_FALSE(zendoo_verify_sc_proof(
            end_epoch_mc_b_hash,
            prev_end_epoch_mc_b_hash,
            bt_list.data(),
            0,
            quality - 1,
            constant,
            NULL,
            proof,
            vk
        ));

    //Free proof
    zendoo_sc_proof_free(proof);
    zendoo_sc_vk_free(vk);
    zendoo_field_free(constant);
}

TEST(ScTxCommitmentTree, SimpleTest)
{
    SidechainTxsCommitmentBuilder builder;

    //Add txes containing scCreation and fwd transfer + a certificate
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10), /*height*/10);
    uint256 scId = scCreationTx.GetScIdFromScCcOut(0);
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, CAmount(7));

    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNum*/12, /*endEpochBlockHash*/uint256S("abc"));

    builder.add(scCreationTx);
    builder.add(fwdTx);
    builder.add(cert);

    uint256 scTxCommitmentHash = builder.getCommitment();
    EXPECT_TRUE(scTxCommitmentHash == uint256S("42a46e330aedeea97ef3b3be71ad065b77580fa566d0ce9ae756bf4d64a1c838"))
        <<scTxCommitmentHash.ToString();
}
