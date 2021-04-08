#include <gtest/gtest.h>

#include <sc/sidechaintypes.h>
#include <primitives/transaction.h>
#include <primitives/certificate.h>
#include <sc/sidechainTxsCommitmentBuilder.h>
#include "tx_creation_utils.h"

#include <gtest/libzendoo_test_files.h>
#include <util.h>
#include <cstring>
#include <utilstrencodings.h>

#include <streams.h>
#include <clientversion.h>

TEST(SidechainsField, GetByteArray)
{
    CFieldElement emptyField{};
    EXPECT_TRUE(emptyField.IsNull());
    EXPECT_TRUE(emptyField.GetByteArray().size() == 0);

    ///////////////////
    CFieldElement validField {SAMPLE_FIELD};
    ASSERT_TRUE(validField.GetByteArray().size() == CFieldElement::ByteSize());
}

TEST(SidechainsField, Serialization)
{
    ////////////////////
    CFieldElement emptyFieldElement{};
    CDataStream emptyFieldStream(SER_DISK, CLIENT_VERSION);

    emptyFieldStream << emptyFieldElement;
    CFieldElement emptyRetrievedField;
    EXPECT_NO_THROW(emptyFieldStream >> emptyRetrievedField);
    EXPECT_TRUE(emptyRetrievedField == emptyFieldElement);

    ////////////////////
    CDataStream zeroLengthStream(SER_DISK, CLIENT_VERSION);
    CFieldElement zeroLengthRetrievedField{};
    EXPECT_THROW(zeroLengthStream >> zeroLengthRetrievedField,std::ios_base::failure);

    ///////////////////
    std::vector<unsigned char> tooShortByteArray(19,'a');
    ASSERT_TRUE(tooShortByteArray.size() < CFieldElement::ByteSize());
    CDataStream tooShortStream(SER_DISK, CLIENT_VERSION);

    tooShortStream << tooShortByteArray;
    CFieldElement tooShortRetrievedField;
    EXPECT_THROW(tooShortStream >> tooShortRetrievedField,std::ios_base::failure);

    ////////////////////
    std::vector<unsigned char> tooBigByteArray(CFieldElement::ByteSize()*2,0x0);
    ASSERT_TRUE(tooBigByteArray.size() > CFieldElement::ByteSize());
    CDataStream tooBigStream(SER_DISK, CLIENT_VERSION);

    tooBigStream << tooBigByteArray;
    CFieldElement tooBigRetrievedField;
    EXPECT_THROW(tooBigStream >> tooBigRetrievedField,std::ios_base::failure);

    ////////////////////
    std::vector<unsigned char> nonZeroTerminatedByteArray(CFieldElement::ByteSize(),0xff);
    ASSERT_TRUE(nonZeroTerminatedByteArray.size() == CFieldElement::ByteSize());
    CDataStream nonZeroStream(SER_DISK, CLIENT_VERSION);

    nonZeroStream << nonZeroTerminatedByteArray;
    CFieldElement nonZeroRetrievedField;
    EXPECT_NO_THROW(nonZeroStream >> nonZeroRetrievedField);
    EXPECT_FALSE(nonZeroRetrievedField.IsValid());

    ////////////////////
    std::vector<unsigned char> overModuleByteArray = {
        138, 206, 199, 243, 195, 254, 25, 94, 236, 155, 232, 182, 89, 123, 162, 207, 102, 52, 178, 128, 55, 248, 234,
        95, 33, 196, 170, 12, 118, 16, 124, 96, 47, 203, 160, 167, 144, 153, 161, 86, 213, 126, 95, 76, 27, 98, 34, 111,
        144, 36, 205, 124, 200, 168, 29, 196, 67, 210, 100, 154, 38, 79, 178, 191, 246, 115, 84, 232, 87, 12, 34, 72,
        88, 23, 236, 142, 237, 45, 11, 148, 91, 112, 156, 47, 68, 229, 216, 56, 238, 98, 41, 243, 225, 192, 2, 0
    };
    ASSERT_TRUE(overModuleByteArray.size() == CFieldElement::ByteSize());

    CDataStream overModuleStream(SER_DISK, CLIENT_VERSION);
    overModuleStream << overModuleByteArray;

    CFieldElement overModuleRetrievedField;
    EXPECT_NO_THROW(overModuleStream >> overModuleRetrievedField);
    EXPECT_FALSE(overModuleRetrievedField.IsValid());

    ////////////////////
    CFieldElement fieldElement{SAMPLE_FIELD};
    CDataStream validStream(SER_DISK, CLIENT_VERSION);

    validStream << fieldElement;
    CFieldElement validRetrievedField;
    EXPECT_NO_THROW(validStream >> validRetrievedField);
    EXPECT_TRUE(validRetrievedField.IsValid());
    EXPECT_TRUE(validRetrievedField == fieldElement);
}

TEST(SidechainsField, IsValid)
{
    CFieldElement emptyFieldElement{};
    EXPECT_FALSE(emptyFieldElement.IsValid());

    std::vector<unsigned char> zeroLengthByteArray{};
    EXPECT_DEATH(CFieldElement{zeroLengthByteArray}, "");

    std::vector<unsigned char> shortByteArray(19,'a');
    EXPECT_DEATH(CFieldElement{shortByteArray}, "");

    std::vector<unsigned char> tooBigByteArray(CFieldElement::ByteSize()*2,0x0);
    EXPECT_DEATH(CFieldElement{tooBigByteArray}, "");

    std::vector<unsigned char> nonZeroTerminatedByteArray(CFieldElement::ByteSize(),0xff);
    CFieldElement nonZeroTerminatedField {nonZeroTerminatedByteArray};
    EXPECT_FALSE(nonZeroTerminatedField.IsValid());

    std::vector<unsigned char> overModuleByteArray = {
        138, 206, 199, 243, 195, 254, 25, 94, 236, 155, 232, 182, 89, 123, 162, 207, 102, 52, 178, 128, 55, 248, 234,
        95, 33, 196, 170, 12, 118, 16, 124, 96, 47, 203, 160, 167, 144, 153, 161, 86, 213, 126, 95, 76, 27, 98, 34, 111,
        144, 36, 205, 124, 200, 168, 29, 196, 67, 210, 100, 154, 38, 79, 178, 191, 246, 115, 84, 232, 87, 12, 34, 72,
        88, 23, 236, 142, 237, 45, 11, 148, 91, 112, 156, 47, 68, 229, 216, 56, 238, 98, 41, 243, 225, 192, 2, 0
    };
    ASSERT_TRUE(overModuleByteArray.size() == CFieldElement::ByteSize());
    CFieldElement overModuleField{overModuleByteArray};
    EXPECT_FALSE(overModuleField.IsValid());

    CFieldElement validField {SAMPLE_FIELD};
    EXPECT_TRUE(validField.IsValid());
}

TEST(SidechainsField, CopyAndAssignement)
{
    {
        CFieldElement AValidField {SAMPLE_FIELD};
        ASSERT_TRUE(AValidField.IsValid());
        wrappedFieldPtr AValidPtr = AValidField.GetFieldElement();
        ASSERT_TRUE(AValidPtr.get() != nullptr);

        { //Scoped to invoke copied obj dtor
            CFieldElement copiedField(AValidField);
            EXPECT_TRUE(copiedField.IsValid());
            EXPECT_TRUE(copiedField == AValidField);

            wrappedFieldPtr copiedPtr = copiedField.GetFieldElement();
            ASSERT_TRUE(copiedPtr.get() != nullptr);
            ASSERT_TRUE(copiedPtr != AValidPtr);
        }
        EXPECT_TRUE(AValidField.IsValid()); //NO side effect from copy
        wrappedFieldPtr ptr = AValidField.GetFieldElement();
        ASSERT_TRUE(ptr.get() != nullptr);
        ASSERT_TRUE(ptr != AValidPtr);

        { //Scoped to invoke assigned obj dtor
            CFieldElement assignedField{};
            assignedField = AValidField;
            EXPECT_TRUE(assignedField.IsValid());
            EXPECT_TRUE(assignedField == AValidField);

            wrappedFieldPtr assignedPtr = assignedField.GetFieldElement();
            ASSERT_TRUE(assignedPtr.get() != nullptr);
            ASSERT_TRUE(assignedPtr != AValidPtr);
        }
        EXPECT_TRUE(AValidField.IsValid()); //NO side effect from copy
        ptr = AValidField.GetFieldElement();
        ASSERT_TRUE(ptr.get() != nullptr);
        ASSERT_TRUE(ptr != AValidPtr);
    }
}

TEST(SidechainsField, PoseidonHashTest)
{
    std::vector<unsigned char> lhs {
        138, 206, 199, 243, 195, 254, 25, 94, 236, 155, 232, 182, 89, 123, 162, 207, 102, 52, 178, 128, 55, 248, 234,
        95, 33, 196, 170, 12, 118, 16, 124, 96, 47, 203, 160, 167, 144, 153, 161, 86, 213, 126, 95, 76, 27, 98, 34, 111,
        144, 36, 205, 124, 200, 168, 29, 196, 67, 210, 100, 154, 38, 79, 178, 191, 246, 115, 84, 232, 87, 12, 34, 72,
        88, 23, 236, 142, 237, 45, 11, 148, 91, 112, 156, 47, 68, 229, 216, 56, 238, 98, 41, 243, 225, 192, 0, 0
    };
    CFieldElement lhsField{lhs};
    ASSERT_TRUE(lhsField.IsValid());

    std::vector<unsigned char> rhs {
        199, 130, 235, 52, 44, 219, 5, 195, 71, 154, 54, 121, 3, 11, 111, 160, 86, 212, 189, 66, 235, 236, 240, 242,
        126, 248, 116, 0, 48, 95, 133, 85, 73, 150, 110, 169, 16, 88, 136, 34, 106, 7, 38, 176, 46, 89, 163, 49, 162,
        222, 182, 42, 200, 240, 149, 226, 173, 203, 148, 194, 207, 59, 44, 185, 67, 134, 107, 221, 188, 208, 122, 212,
        200, 42, 227, 3, 23, 59, 31, 37, 91, 64, 69, 196, 74, 195, 24, 5, 165, 25, 101, 215, 45, 92, 1, 0
    };
    CFieldElement rhsField{rhs};
    ASSERT_TRUE(rhsField.IsValid());

    std::vector<unsigned char> expectedHash {
        53, 2, 235, 12, 255, 18, 125, 167, 223, 32, 245, 103, 38, 74, 43, 73, 254, 189, 174, 137, 20, 90, 195, 107, 202,
        24, 151, 136, 85, 23, 9, 93, 207, 33, 229, 200, 178, 225, 221, 127, 18, 250, 108, 56, 86, 94, 171, 1, 76, 21,
        237, 254, 26, 235, 196, 14, 18, 129, 101, 158, 136, 103, 147, 147, 239, 140, 163, 94, 245, 147, 110, 28, 93,
        231, 66, 7, 111, 11, 202, 99, 146, 211, 117, 143, 224, 99, 183, 108, 157, 200, 119, 169, 180, 148, 0, 0,
    };
    CFieldElement expectedField{expectedHash};
    ASSERT_TRUE(expectedField.IsValid());

    EXPECT_TRUE(CFieldElement::ComputeHash(lhsField, rhsField) == expectedField)
    <<"expectedField "<<expectedField.GetHexRepr()<<"\n"
    <<"actualField   "<<CFieldElement::ComputeHash(lhsField, rhsField).GetHexRepr();
}

TEST(SidechainsField, NakedZendooFeatures_FieldTest)
{
    //Size is the expected one
    int field_len = zendoo_get_field_size_in_bytes();
    ASSERT_EQ(field_len, CFieldElement::ByteSize());

    auto field = zendoo_get_random_field();

    //Serialize and deserialize and check equality
    unsigned char field_bytes[CFieldElement::ByteSize()];
    zendoo_serialize_field(field, field_bytes);

    auto field_deserialized = zendoo_deserialize_field(field_bytes);
    ASSERT_TRUE(field_deserialized != NULL);

    ASSERT_TRUE(zendoo_field_assert_eq(field, field_deserialized));

    zendoo_field_free(field);
    zendoo_field_free(field_deserialized);
}

TEST(SidechainsField, NakedZendooFeatures_PoseidonHashTest)
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

    auto digest = ZendooPoseidonHash();

    digest.update(lhs_field);

    auto temp_hash = digest.finalize();
    digest.update(rhs_field); // Call to finalize keeps the state

    auto actual_hash = digest.finalize();
    ASSERT_TRUE((zendoo_field_assert_eq(actual_hash, expected_hash)))
    <<"Expected hashes to be equal";
    zendoo_field_free(actual_hash);

    auto actual_hash_2 = digest.finalize(); // finalize() is idempotent
    ASSERT_TRUE((zendoo_field_assert_eq(actual_hash_2, expected_hash)))
    <<"Expected hashes to be equal";
    zendoo_field_free(actual_hash_2);

    zendoo_field_free(expected_hash);
    zendoo_field_free(temp_hash);
    zendoo_field_free(lhs_field);
    zendoo_field_free(rhs_field);
}

TEST(SidechainsField, NakedZendooFeatures_PoseidonMerkleTreeTest)
{
    size_t height = 5;

    // Deserialize root
    unsigned char expected_root_bytes[SC_FIELD_SIZE] = {
        192, 138, 102, 85, 151, 8, 139, 184, 209, 249, 171, 182, 227, 80, 52, 215, 32, 37, 145, 166,
        74, 136, 40, 200, 213, 72, 124, 101, 91, 235, 114, 0, 147, 61, 180, 29, 183, 111, 247, 2,
        169, 12, 179, 173, 87, 88, 187, 229, 26, 139, 80, 228, 125, 246, 145, 141, 43, 19, 148, 94,
        190, 140, 20, 123, 208, 132, 48, 243, 14, 2, 48, 106, 100, 13, 41, 254, 129, 225, 168, 23,
        72, 215, 207, 255, 98, 156, 102, 215, 201, 158, 10, 123, 107, 238, 0, 0
    };
    auto expected_root = zendoo_deserialize_field(expected_root_bytes);
    ASSERT_TRUE(expected_root != NULL);

    //Generate leaves

    //enum removes variable length buffer [-Wstack-protector] warning that simple const int would give
    enum { leaves_len = 32 };
    const field_t* leaves[leaves_len];
    for (int i = 0; i < leaves_len; i++){
        leaves[i] = zendoo_get_field_from_long(i);
    }

    // Initialize tree
    auto tree = ZendooGingerMerkleTree(height, leaves_len);

    // Add leaves to tree
    for (int i = 0; i < leaves_len; i++){
        tree.append(leaves[i]);
    }

    // Finalize tree
    tree.finalize_in_place();

    // Compute root and assert equality with expected one
    auto root = tree.root();
    ASSERT_TRUE(zendoo_field_assert_eq(root, expected_root))
    <<"Expected roots to be equal";

    // It is the same by calling finalize()
    auto tree_copy = tree.finalize();
    auto root_copy = tree_copy.root();
    ASSERT_TRUE(zendoo_field_assert_eq(root_copy, expected_root))
    <<"Expected roots to be equal";

    // Test Merkle Paths
    for (int i = 0; i < leaves_len; i++) {
        auto path = tree.get_merkle_path(i);
        ASSERT_TRUE(zendoo_verify_ginger_merkle_path(path, height, (field_t*)leaves[i], root))
        <<"Merkle Path must be verified";
        zendoo_free_ginger_merkle_path(path);
    }

    // Free memory
    zendoo_field_free(expected_root);
    for (int i = 0; i < leaves_len; i++){
        zendoo_field_free((field_t*)leaves[i]);
    }
    zendoo_field_free(root);
    zendoo_field_free(root_copy);
}

// SILENCED SINCE BROKEN. TODO: Come up with correct byte arrays
//// Execute the test from zen directory
//TEST(SidechainsField, NakedZendooFeatures_TestProof)
//{
//    //Deserialize zero knowledge proof
//    auto proof_serialized = ParseHex(SAMPLE_PROOF);
//    ASSERT_EQ(proof_serialized.size(), zendoo_get_sc_proof_size_in_bytes());
//    auto proof = zendoo_deserialize_sc_proof(proof_serialized.data());
//    ASSERT_TRUE(proof != NULL);
//
//    //Inputs
//    unsigned char end_epoch_mc_b_hash[32] = {
//        78, 85, 161, 67, 167, 192, 185, 56, 133, 49, 134, 253, 133, 165, 182, 80, 152, 93, 203, 77, 165, 13, 67, 0, 64,
//        200, 185, 46, 93, 135, 238, 70
//    };
//
//    unsigned char prev_end_epoch_mc_b_hash[32] = {
//        68, 214, 34, 70, 20, 109, 48, 39, 210, 156, 109, 60, 139, 15, 102, 79, 79, 2, 87, 190, 118, 38, 54, 18, 170, 67,
//        212, 205, 183, 115, 182, 198
//    };
//
//    unsigned char constant_bytes[96] = {
//        170, 190, 140, 27, 234, 135, 240, 226, 158, 16, 29, 161, 178, 36, 69, 34, 29, 75, 195, 247, 29, 93, 92, 48, 214,
//        102, 70, 134, 68, 165, 170, 201, 119, 162, 19, 254, 229, 115, 80, 248, 106, 182, 164, 40, 21, 154, 15, 177, 158,
//        16, 172, 169, 189, 253, 206, 182, 72, 183, 128, 160, 182, 39, 98, 76, 95, 198, 62, 39, 87, 213, 251, 12, 154,
//        180, 125, 231, 222, 73, 129, 120, 144, 197, 116, 248, 95, 206, 147, 108, 252, 125, 79, 118, 57, 26, 0, 0
//    };
//
//    auto constant = zendoo_deserialize_field(constant_bytes);
//    ASSERT_TRUE(constant != NULL);
//
//    uint64_t quality = 2;
//
//    //Create dummy bt
//    const backward_transfer_t bt_list[10] = { {0}, 0 };
//
//    //Deserialize vk
//    auto vk_serialized = ParseHex(SAMPLE_VK);
//    ASSERT_EQ(vk_serialized.size(), zendoo_get_sc_vk_size_in_bytes());
//    auto vk = zendoo_deserialize_sc_vk(vk_serialized.data());
//    ASSERT_TRUE(vk != NULL);
//
//    //Verify zkproof
//    ASSERT_TRUE(zendoo_verify_sc_proof(
//            end_epoch_mc_b_hash,
//            prev_end_epoch_mc_b_hash,
//            bt_list,
//            10,
//            quality,
//            constant,
//            NULL,
//            proof,
//            vk
//        ));
//
//    //Negative test: change quality (for instance) and ASSERT_TRUE proof failure
//    ASSERT_FALSE(zendoo_verify_sc_proof(
//            end_epoch_mc_b_hash,
//            prev_end_epoch_mc_b_hash,
//            bt_list,
//            10,
//            quality - 1,
//            constant,
//            NULL,
//            proof,
//            vk
//        ));
//
//    //Free proof
//    zendoo_sc_proof_free(proof);
//    zendoo_sc_vk_free(vk);
//    zendoo_field_free(constant);
//
//}

// Execute the test from zen directory
TEST(SidechainsField, NakedZendooFeatures_TestProofNoBwt)
{
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

TEST(SidechainsField, NakedZendooFeatures_TreeCommitmentCalculation)
{
    //fPrintToConsole = true;

    SidechainTxsCommitmentBuilder builder;

    //Add txes containing scCreation and fwd transfer + a certificate
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10), /*height*/10);
    CMutableTransaction mutTx = scCreationTx;
    mutTx.vsc_ccout.push_back(CTxScCreationOut(CAmount(10), uint256S("aaa"), Sidechain::ScCreationParameters()));
    mutTx.vft_ccout.push_back(CTxForwardTransferOut(uint256S("bbb"), CAmount(1985), uint256S("badcafe")));
    scCreationTx = mutTx;

    uint256 scId = scCreationTx.GetScIdFromScCcOut(0);
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, CAmount(7));

    CScCertificate cert = txCreationUtils::createCertificate(scId,
        /*epochNum*/12, /*endEpochBlockHash*/uint256S("abc"), CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/0,
        /*numChangeOut */0, /*bwtTotalAmount*/1, /*numBwt*/1);

    builder.add(scCreationTx);
    builder.add(fwdTx);
    builder.add(cert);

    uint256 scTxCommitmentHash = builder.getCommitment();

    EXPECT_TRUE(scTxCommitmentHash == uint256S("34700bf5038263d9034574ba4a507b480a88b74600b194dcf86cfb3ec6393e8b"))
        <<scTxCommitmentHash.ToString();
}

TEST(SidechainsField, NakedZendooFeatures_EmptyTreeCommitmentCalculation)
{
    //fPrintToConsole = true;
    SidechainTxsCommitmentBuilder builder;

    //Nothing to add

    uint256 scTxCommitmentHash = builder.getCommitment();
    EXPECT_TRUE(scTxCommitmentHash == uint256S("3a464e1e43410c7add1dd81c3f10486f41eb473bb43e8d64feca3c7f0c8028d3"))
        <<scTxCommitmentHash.ToString();
}
