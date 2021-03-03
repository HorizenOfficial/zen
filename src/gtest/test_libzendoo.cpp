#include <gtest/gtest.h>
#include <gtest/libzendoo_test_files.h>
#include <util.h>
#include "zendoo/zendoo_mc.h"
#include "zendoo/error.h"
#include <stdio.h>
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
    unsigned char lhs[96] = {
        138, 206, 199, 243, 195, 254, 25, 94, 236, 155, 232, 182, 89, 123, 162, 207, 102, 52, 178, 128, 55, 248, 234,
        95, 33, 196, 170, 12, 118, 16, 124, 96, 47, 203, 160, 167, 144, 153, 161, 86, 213, 126, 95, 76, 27, 98, 34, 111,
        144, 36, 205, 124, 200, 168, 29, 196, 67, 210, 100, 154, 38, 79, 178, 191, 246, 115, 84, 232, 87, 12, 34, 72,
        88, 23, 236, 142, 237, 45, 11, 148, 91, 112, 156, 47, 68, 229, 216, 56, 238, 98, 41, 243, 225, 192, 0, 0
    };

    unsigned char rhs[96] = {
        199, 130, 235, 52, 44, 219, 5, 195, 71, 154, 54, 121, 3, 11, 111, 160, 86, 212, 189, 66, 235, 236, 240, 242,
        126, 248, 116, 0, 48, 95, 133, 85, 73, 150, 110, 169, 16, 88, 136, 34, 106, 7, 38, 176, 46, 89, 163, 49, 162,
        222, 182, 42, 200, 240, 149, 226, 173, 203, 148, 194, 207, 59, 44, 185, 67, 134, 107, 221, 188, 208, 122, 212,
        200, 42, 227, 3, 23, 59, 31, 37, 91, 64, 69, 196, 74, 195, 24, 5, 165, 25, 101, 215, 45, 92, 1, 0
    };

    unsigned char hash[96] = {
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

    const field_t* hash_input[] = {lhs_field, rhs_field};

    auto actual_hash = zendoo_compute_poseidon_hash(hash_input, 2);
    ASSERT_TRUE(actual_hash != NULL);

    ASSERT_TRUE(zendoo_field_assert_eq(expected_hash, actual_hash));

    zendoo_field_free(lhs_field);
    zendoo_field_free(rhs_field);
    zendoo_field_free(expected_hash);
    zendoo_field_free(actual_hash);
}

TEST(ZendooLib, PoseidonMerkleTreeTest)  {

    //Generate random leaves
    enum {leaves_len = 16};
    const field_t* leaves[leaves_len];
    for (int i = 0; i < leaves_len; i++){
        leaves[i] = zendoo_get_random_field();
    }

    //Create Merkle Tree and get the root
    auto tree = ginger_mt_new(leaves, leaves_len);
    ASSERT_TRUE(tree != NULL);

    auto root = ginger_mt_get_root(tree);

    //Verify Merkle Path is ok for each leaf
    for (int i = 0; i < leaves_len; i++) {

        //Create Merkle Path for the i-th leaf
        auto path = ginger_mt_get_merkle_path(leaves[i], i, tree);
        ASSERT_TRUE(path != NULL);

        //Verify Merkle Path for the i-th leaf
        ASSERT_TRUE(ginger_mt_verify_merkle_path(leaves[i], root, path));

        //Free Merkle Path
        ginger_mt_path_free(path);
    }

    //Free the tree
    ginger_mt_free(tree);

    //Free the root
    zendoo_field_free(root);

    //Free all the leaves
    for (int i = 0; i < leaves_len; i++){
        zendoo_field_free((field_t*)leaves[i]);
    }
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
        78, 85, 161, 67, 167, 192, 185, 56, 133, 49, 134, 253, 133, 165, 182, 80, 152, 93, 203, 77, 165, 13, 67, 0, 64,
        200, 185, 46, 93, 135, 238, 70
    };

    unsigned char prev_end_epoch_mc_b_hash[32] = {
        68, 214, 34, 70, 20, 109, 48, 39, 210, 156, 109, 60, 139, 15, 102, 79, 79, 2, 87, 190, 118, 38, 54, 18, 170, 67,
        212, 205, 183, 115, 182, 198
    };

    unsigned char constant_bytes[96] = {
        170, 190, 140, 27, 234, 135, 240, 226, 158, 16, 29, 161, 178, 36, 69, 34, 29, 75, 195, 247, 29, 93, 92, 48, 214,
        102, 70, 134, 68, 165, 170, 201, 119, 162, 19, 254, 229, 115, 80, 248, 106, 182, 164, 40, 21, 154, 15, 177, 158,
        16, 172, 169, 189, 253, 206, 182, 72, 183, 128, 160, 182, 39, 98, 76, 95, 198, 62, 39, 87, 213, 251, 12, 154,
        180, 125, 231, 222, 73, 129, 120, 144, 197, 116, 248, 95, 206, 147, 108, 252, 125, 79, 118, 57, 26, 0, 0
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
TEST(ZendooLib, TestProofNoBwt){
    //Deserialize zero knowledge proof
    auto proof_serialized = ParseHex(SAMPLE_PROOF_NO_BWT);
    ASSERT_EQ(proof_serialized.size(), zendoo_get_sc_proof_size_in_bytes());
    auto proof = zendoo_deserialize_sc_proof(proof_serialized.data());
    ASSERT_TRUE(proof != NULL);

    //Inputs
    unsigned char end_epoch_mc_b_hash[32] = {
        28, 207, 62, 204, 135, 33, 168, 143, 231, 177, 64, 181, 184, 237, 93, 185, 196, 115, 241, 65, 176, 205, 254, 83,
        216, 229, 119, 73, 184, 217, 26, 109
    };

    unsigned char prev_end_epoch_mc_b_hash[32] = {
        64, 236, 160, 62, 217, 6, 240, 243, 184, 32, 158, 223, 218, 177, 165, 121, 12, 124, 153, 137, 218, 208, 152, 125,
        187, 145, 172, 244, 223, 220, 234, 195
    };

    unsigned char constant_bytes[96] = {
        249, 199, 228, 179, 227, 163, 140, 243, 174, 240, 187, 245, 152, 245, 74, 136, 36, 142, 231, 196, 162, 148, 139,
        157, 198, 117, 186, 83, 72, 103, 121, 253, 5, 64, 230, 173, 84, 236, 12, 3, 199, 26, 171, 58, 141, 171, 85, 151,
        209, 228, 76, 0, 21, 241, 65, 100, 50, 194, 8, 163, 121, 129, 242, 124, 166, 105, 158, 76, 146, 169, 188, 243,
        188, 82, 176, 244, 255, 122, 125, 90, 154, 45, 12, 223, 62, 156, 140, 20, 35, 83, 55, 111, 47, 10, 1, 0
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
