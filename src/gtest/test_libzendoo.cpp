#include <gtest/gtest.h>
#include <gtest/libzendoo_test_files.h>
#include <util.h>
#include <stdio.h>
#include <cstring>
#include <utilstrencodings.h>

#include <primitives/transaction.h>
#include <sc/proofverifier.h>
#include <streams.h>
#include <clientversion.h>
#include <gtest/tx_creation_utils.h>

TEST(SidechainsField, FieldSizeIsAlwaysTheExpectedOne)
{
    CSidechainField emptyField;
    EXPECT_TRUE(emptyField.GetByteArray().size() == CSidechainField::ByteSize());
}

TEST(SidechainsField, Serialization)
{
    std::vector<unsigned char> zeroLengthByteArray {};
    CDataStream zeroLengthStream(SER_DISK, CLIENT_VERSION);

    zeroLengthStream << zeroLengthByteArray;
    CSidechainField zeroLengthRetrievedField;
    EXPECT_THROW(zeroLengthStream >> zeroLengthRetrievedField,std::ios_base::failure)
    <<"THIS MUST BE GREEN AS SOON AS 253 BITS FIELD IS INTRODUCED";

    ///////////////////
    std::vector<unsigned char> tooShortByteArray(19,'a');
    ASSERT_TRUE(tooShortByteArray.size() < CSidechainField::ByteSize());
    CDataStream tooShortStream(SER_DISK, CLIENT_VERSION);

    tooShortStream << tooShortByteArray;
    CSidechainField tooShortRetrievedField;
    EXPECT_THROW(tooShortStream >> tooShortRetrievedField,std::ios_base::failure)
    <<"THIS MUST BE GREEN AS SOON AS 253 BITS FIELD IS INTRODUCED";

    ////////////////////
    std::vector<unsigned char> tooBigByteArray(CSidechainField::ByteSize()*2,0x0);
    ASSERT_TRUE(tooBigByteArray.size() > CSidechainField::ByteSize());
    CDataStream tooBigStream(SER_DISK, CLIENT_VERSION);

    tooBigStream << tooBigByteArray;
    CSidechainField tooBigRetrievedField;
    EXPECT_THROW(tooBigStream >> tooBigRetrievedField,std::ios_base::failure);

    ////////////////////
    std::vector<unsigned char> nonZeroTerminatedByteArray(CSidechainField::ByteSize(),0xff);
    ASSERT_TRUE(nonZeroTerminatedByteArray.size() == CSidechainField::ByteSize());
    CDataStream nonZeroStream(SER_DISK, CLIENT_VERSION);

    nonZeroStream << nonZeroTerminatedByteArray;
    CSidechainField nonZeroRetrievedField;
    EXPECT_NO_THROW(nonZeroStream >> nonZeroRetrievedField);
    EXPECT_FALSE(nonZeroRetrievedField.IsValid());

    ////////////////////
    std::vector<unsigned char> overModuleByteArray = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f
    };
    ASSERT_TRUE(overModuleByteArray.size() == CSidechainField::ByteSize());
    CDataStream overModuleStream(SER_DISK, CLIENT_VERSION);

    overModuleStream << overModuleByteArray;
    CSidechainField overModuleRetrievedField;
    EXPECT_NO_THROW(overModuleStream >> overModuleRetrievedField);
    EXPECT_FALSE(overModuleRetrievedField.IsValid());

    ////////////////////
    std::vector<unsigned char> validByteArray = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f
    };
    ASSERT_TRUE(validByteArray.size() == CSidechainField::ByteSize());
    CDataStream validStream(SER_DISK, CLIENT_VERSION);

    validStream << validByteArray;
    CSidechainField validRetrievedField;
    EXPECT_NO_THROW(validStream >> validRetrievedField);
    EXPECT_TRUE(validRetrievedField.IsValid());
}

TEST(SidechainsField, IsValid)
{
    std::vector<unsigned char> zeroLengthByteArray{};
    EXPECT_THROW(CSidechainField{zeroLengthByteArray}, std::invalid_argument)
    <<"THIS MUST BE GREEN AS SOON AS 253 BITS FIELD IS INTRODUCED";

    std::vector<unsigned char> shortByteArray(19,'a');
    EXPECT_THROW(CSidechainField{shortByteArray}, std::invalid_argument)
    <<"THIS MUST BE GREEN AS SOON AS 253 BITS FIELD IS INTRODUCED";

    std::vector<unsigned char> tooBigByteArray(CSidechainField::ByteSize()*2,0x0);
    EXPECT_THROW(CSidechainField{tooBigByteArray}, std::invalid_argument)
    <<"THIS MUST BE GREEN AS SOON AS 253 BITS FIELD IS INTRODUCED";

    std::vector<unsigned char> nonZeroTerminatedByteArray(CSidechainField::ByteSize(),0xff);
    CSidechainField nonZeroTerminatedField {nonZeroTerminatedByteArray};
    EXPECT_FALSE(nonZeroTerminatedField.IsValid());

    // not depending on the curve choosen in the crypto lib, this is over module for a field element of 32 bytes 
    // (this is 255 bit set to 1 and the last bit set to 0)
    std::vector<unsigned char> overModuleByteArray = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f
    };
    ASSERT_TRUE(overModuleByteArray.size() == CSidechainField::ByteSize());
    CSidechainField overModuleField{overModuleByteArray};
    EXPECT_FALSE(overModuleField.IsValid());

    // similarly, not depending on the curve choosen in the crypto lib, this is always valid for a field element of 32 bytes 
    // (this is 254 bit set to 1 and the last two bits set to 0)
    std::vector<unsigned char> validByteArray = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f
    };
    ASSERT_TRUE(validByteArray.size() == CSidechainField::ByteSize());
    CSidechainField validField {validByteArray};
    EXPECT_TRUE(validField.IsValid());
}

TEST(SidechainsField, CopyAndAssignement)
{
#if 0
    std::vector<unsigned char> validByteArray = {
        138, 206, 199, 243, 195, 254, 25, 94, 236, 155, 232, 182, 89, 123, 162, 207, 102, 52, 178, 128, 55, 248, 234,
        95, 33, 196, 170, 12, 118, 16, 124, 96, 47, 203, 160, 167, 144, 153, 161, 86, 213, 126, 95, 76, 27, 98, 34, 111,
        144, 36, 205, 124, 200, 168, 29, 196, 67, 210, 100, 154, 38, 79, 178, 191, 246, 115, 84, 232, 87, 12, 34, 72,
        88, 23, 236, 142, 237, 45, 11, 148, 91, 112, 156, 47, 68, 229, 216, 56, 238, 98, 41, 243, 225, 192, 1, 0
    };
#else
    std::vector<unsigned char> validByteArray = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f
    };
#endif
    ASSERT_TRUE(validByteArray.size() == CSidechainField::ByteSize());
    {
        CSidechainField AValidField {validByteArray};
        const field_t* const AValidPtr = AValidField.GetFieldElement();
        ASSERT_TRUE(AValidPtr != nullptr);

        ASSERT_TRUE(AValidField.IsValid());

        { //Scoped to invoke copied obj dtor
        	CSidechainField copiedField{AValidField};
            const field_t* const copiedPtr = copiedField.GetFieldElement();

        	EXPECT_TRUE(copiedField.IsValid());
        	EXPECT_TRUE(copiedField == AValidField);
            ASSERT_TRUE(copiedPtr != nullptr);
            ASSERT_TRUE(copiedPtr != AValidPtr);
        }
        EXPECT_TRUE(AValidField.IsValid()); //NO side effect from copy

        { //Scoped to invoke assigned obj dtor
        	CSidechainField assignedField{};
            const field_t* const assignedPtr = assignedField.GetFieldElement();

        	assignedField = AValidField;
        	EXPECT_TRUE(assignedField.IsValid());
        	EXPECT_TRUE(assignedField == AValidField);
            ASSERT_TRUE(assignedPtr != nullptr);
            ASSERT_TRUE(assignedPtr != AValidPtr);
        }
        EXPECT_TRUE(AValidField.IsValid()); //NO side effect from copy
        const field_t* const ptr = AValidField.GetFieldElement();
        ASSERT_TRUE(ptr == AValidPtr);
    }
}

TEST(SidechainsField, PoseidonHashTest)
{
    std::vector<unsigned char> lhs {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f
    };
    CSidechainField lhsField{lhs};
    ASSERT_TRUE(lhsField.IsValid());

    std::vector<unsigned char> rhs {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f
    };
    CSidechainField rhsField{rhs};
    ASSERT_TRUE(rhsField.IsValid());

    // TODO check this
    std::vector<unsigned char> expectedHash {
        0x42, 0xff, 0xd4, 0x94, 0x7f, 0x76, 0xf7, 0xc1,
        0xba, 0x0a, 0xcf, 0x73, 0xf3, 0x0a, 0xa3, 0x7b,
        0x5a, 0xe8, 0xeb, 0xde, 0x5d, 0x61, 0xc3, 0x19,
        0x70, 0xc2, 0xf6, 0x45, 0x7b, 0x83, 0x2a, 0x39
    };
    CSidechainField expectedField{expectedHash};
    ASSERT_TRUE(expectedField.IsValid());

    EXPECT_TRUE(CSidechainField::ComputeHash(lhsField, rhsField) == expectedField)
    <<"expectedField "<<expectedField.GetHexRepr()<<"\n"
	<<"actualField   "<<CSidechainField::ComputeHash(lhsField, rhsField).GetHexRepr();

    // TODO try using an invalid rhs for instance and see that an exc is thrown
}

TEST(ZendooLib, FieldTest)
{
    //Size is the expected one
    int field_len = zendoo_get_field_size_in_bytes();
    ASSERT_EQ(field_len, CSidechainField::ByteSize());

    auto field = zendoo_get_random_field();

    //Serialize and deserialize and check equality
    unsigned char field_bytes[CSidechainField::ByteSize()];
    zendoo_serialize_field(field, field_bytes);

    auto field_deserialized = zendoo_deserialize_field(field_bytes);
    ASSERT_TRUE(field_deserialized != NULL);

    ASSERT_TRUE(zendoo_field_assert_eq(field, field_deserialized));

    zendoo_field_free(field);
    zendoo_field_free(field_deserialized);
}

#if 0
TEST(ZendooLib, PoseidonMerkleTreeTest)  {

    //Generate random leaves
    static const int leaves_len = 16;
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
#endif

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

TEST(SidechainsField, BitVectorUncompressed)
{
    BitVectorErrorCode ret_code = BitVectorErrorCode::OK;

    unsigned char buffer[3] = {0x07, 0x0e, 0x00};
    CompressionAlgorithm e = CompressionAlgorithm::Uncompressed;

    BitVectorBuffer bvb_in(buffer, sizeof(buffer));

    BitVectorBuffer* bvb_ret = nullptr;
    bvb_ret = zendoo_compress_bit_vector(&bvb_in, e, &ret_code);
    ASSERT_TRUE(bvb_ret != nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::OK);

    unsigned char* ptr = bvb_ret->data;

    for (int i = 0; i < sizeof(buffer); i++)
    {
        ASSERT_TRUE(ptr[i+1] == buffer[i]);
    }

    zendoo_free_bit_vector(bvb_ret);
}

TEST(SidechainsField, BitVectorGzip)
{
    BitVectorErrorCode ret_code = BitVectorErrorCode::OK;

    unsigned char buffer[5] = {0xad, 0xde, 0xef, 0xbe, 0x00};
    CompressionAlgorithm e = CompressionAlgorithm::Gzip;

    BitVectorBuffer bvb_in(buffer, sizeof(buffer));

    printf("Compressing using gzip...\n");
    BitVectorBuffer* bvb_ret1 = nullptr;
    bvb_ret1 = zendoo_compress_bit_vector(&bvb_in, e, &ret_code);
    ASSERT_TRUE(bvb_ret1 != nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::OK);

    // make a copy
    unsigned char ptr2[BV_SIZE_IN_BYTES] = {};
    memcpy(ptr2, bvb_ret1->data, bvb_ret1->len);
    ptr2[0] = (unsigned char)CompressionAlgorithm::Bzip2;
    BitVectorBuffer bvb_in2(ptr2, bvb_ret1->len);

    printf("\nDecompressing with an invalid compression algo enum...\n");
    BitVectorBuffer* bvb_null = nullptr;
    bvb_null = zendoo_decompress_bit_vector(&bvb_in2, bvb_in2.len, &ret_code);
    ASSERT_TRUE(bvb_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::UncompressError);

    unsigned char ptr3[0] = {};
    BitVectorBuffer bvb_in3(ptr3, 0);
    printf("\nDecompressing an empty buffer...\n");
    bvb_null = zendoo_decompress_bit_vector(&bvb_in3, bvb_in3.len, &ret_code);
    ASSERT_TRUE(bvb_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::InvalidBufferLength);

    unsigned char* ptr4 = nullptr;
    BitVectorBuffer bvb_in4(ptr4, 33);
    printf("\nDecompressing a null ptr buffer in a valid struct...\n");
    bvb_null = zendoo_decompress_bit_vector(&bvb_in4, bvb_in4.len, &ret_code);
    ASSERT_TRUE(bvb_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::InvalidBufferData);

    BitVectorBuffer* bvb_in5 = nullptr;
    printf("\nDecompressing a null ptr struct ...\n");
    bvb_null = zendoo_decompress_bit_vector(bvb_in5, bvb_in4.len, &ret_code);
    ASSERT_TRUE(bvb_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::NullPtr);

    printf("\nDecompressing expecting a wrong size...\n");
    bvb_null = zendoo_decompress_bit_vector(bvb_ret1, sizeof(buffer)-1, &ret_code);
    ASSERT_TRUE(bvb_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::UncompressError);

    printf("\nDecompressing good data...\n");
    BitVectorBuffer* bvb_ret2 = zendoo_decompress_bit_vector(bvb_ret1, sizeof(buffer), &ret_code);
    ASSERT_TRUE(bvb_ret2 != nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::OK);

    unsigned char* ptr = bvb_ret2->data;

    for (int i = 0; i < sizeof(buffer); i++)
    {
        ASSERT_TRUE(ptr[i] == buffer[i]);
    }

    printf("\nfreeing buffers...\n");
    zendoo_free_bit_vector(bvb_ret1);
    zendoo_free_bit_vector(bvb_ret2);
}

TEST(SidechainsField, BitVectorBzip2)
{
    BitVectorErrorCode ret_code = BitVectorErrorCode::OK;

    unsigned char buffer[5] = {0xad, 0xde, 0xef, 0xbe, 0x00};
    CompressionAlgorithm e = CompressionAlgorithm::Bzip2;

    BitVectorBuffer bvb_in(buffer, sizeof(buffer));

    printf("Compressing using bzip2...\n");
    BitVectorBuffer* bvb_ret1 = nullptr;
    bvb_ret1 = zendoo_compress_bit_vector(&bvb_in, e, &ret_code);
    ASSERT_TRUE(bvb_ret1 != nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::OK);

    // make a copy
    unsigned char ptr2[BV_SIZE_IN_BYTES] = {};
    memcpy(ptr2, bvb_ret1->data, bvb_ret1->len);
    ptr2[0] = (unsigned char)CompressionAlgorithm::Gzip;
    BitVectorBuffer bvb_in2(ptr2, bvb_ret1->len);

    printf("\nDecompressing with an invalid compression algo enum...\n");
    BitVectorBuffer* bvb_null = nullptr;
    bvb_null = zendoo_decompress_bit_vector(&bvb_in2, sizeof(buffer), &ret_code);
    ASSERT_TRUE(bvb_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::UncompressError);

    unsigned char ptr3[0] = {};
    BitVectorBuffer bvb_in3(ptr3, 0);
    printf("\nDecompressing an empty buffer...\n");
    bvb_null = zendoo_decompress_bit_vector(&bvb_in3, bvb_in3.len, &ret_code);
    ASSERT_TRUE(bvb_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::InvalidBufferLength);

    unsigned char* ptr4 = nullptr;
    BitVectorBuffer bvb_in4(ptr4, 33);
    printf("\nDecompressing a null ptr buffer in a valid struct...\n");
    bvb_null = zendoo_decompress_bit_vector(&bvb_in4, bvb_in4.len, &ret_code);
    ASSERT_TRUE(bvb_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::InvalidBufferData);

    BitVectorBuffer* bvb_in5 = nullptr;
    printf("\nDecompressing a null ptr struct ...\n");
    bvb_null = zendoo_decompress_bit_vector(bvb_in5, bvb_in4.len, &ret_code);
    ASSERT_TRUE(bvb_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::NullPtr);

    printf("\nDecompressing expecting a wrong size...\n");
    bvb_null = zendoo_decompress_bit_vector(bvb_ret1, sizeof(buffer)-1, &ret_code);
    ASSERT_TRUE(bvb_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::UncompressError);

    printf("\nDecompressing good data...\n");
    BitVectorBuffer* bvb_ret2 = zendoo_decompress_bit_vector(bvb_ret1, sizeof(buffer), &ret_code);
    ASSERT_TRUE(bvb_ret2 != nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::OK);

    unsigned char* ptr = bvb_ret2->data;

    for (int i = 0; i < sizeof(buffer); i++)
    {
        ASSERT_TRUE(ptr[i] == buffer[i]);
    }

    printf("\nfreeing buffers...\n");
    zendoo_free_bit_vector(bvb_ret1);
    zendoo_free_bit_vector(bvb_ret2);
}

TEST(SidechainsField, BitVectorMerkleTree)
{
    BitVectorErrorCode ret_code = BitVectorErrorCode::OK;

    unsigned char buffer[5] = {0xad, 0xde, 0xef, 0xbe, 0x00};

    BitVectorBuffer bvb_in_uncomp(buffer, sizeof(buffer));

    printf("\nBuilding using uncompressed data...\n");
    field_t* fe_null = zendoo_merkle_root_from_compressed_bytes(&bvb_in_uncomp, sizeof(buffer), &ret_code);
    ASSERT_TRUE(fe_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::MerkleRootBuildError);

    BitVectorBuffer bvb_in(buffer, sizeof(buffer));
    CompressionAlgorithm e = CompressionAlgorithm::Bzip2;
    BitVectorBuffer* bvb_ret1 = nullptr;
    bvb_ret1 = zendoo_compress_bit_vector(&bvb_in_uncomp, e, &ret_code);
    ASSERT_TRUE(bvb_ret1 != nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::OK);

    unsigned char* ptr4 = nullptr;
    BitVectorBuffer bvb_in4(ptr4, 33);
    printf("\nDecompressing a null ptr buffer in a valid struct...\n");
    fe_null = zendoo_merkle_root_from_compressed_bytes(&bvb_in4, bvb_in4.len, &ret_code);
    ASSERT_TRUE(fe_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::InvalidBufferData);

    BitVectorBuffer* bvb_in5 = nullptr;
    printf("\nBuilding with a null ptr struct ...\n");
    fe_null = zendoo_merkle_root_from_compressed_bytes(bvb_in5, 5, &ret_code);
    ASSERT_TRUE(fe_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::NullPtr);

    printf("\nBuilding with a wrong expected size...\n");
    fe_null = zendoo_merkle_root_from_compressed_bytes(bvb_ret1, sizeof(buffer)-1, &ret_code);
    ASSERT_TRUE(fe_null == nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::MerkleRootBuildError);

    printf("\nBuilding merkle tree ...\n");
    field_t* fe = zendoo_merkle_root_from_compressed_bytes(bvb_ret1, sizeof(buffer), &ret_code);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::OK);
    ASSERT_TRUE(fe != nullptr);

    printf("\nfreeing mem...\n");
    zendoo_field_free(fe);
    zendoo_free_bit_vector(bvb_ret1);
}

TEST(SidechainsField, BitVectorMerkleTreeData)
{
    unsigned char buffer[] = { 
        0xbd, 0x98, 0xa9, 0x6f, 0x5b, 0x1d, 0xcd, 0xf4, 0x86, 0xdf, 0x04, 0x95, 0x13, 0xd1, 0xb6, 0xda,
        0x2f, 0x4f, 0x1f, 0xfd, 0xc6, 0x7f, 0xaa, 0xfb, 0x51, 0x28, 0x6b, 0xce, 0x11, 0x37, 0xc8, 0x63,
        0x78, 0xdf, 0x4f, 0x5c, 0x97, 0xcd, 0x12, 0x86, 0x96, 0xba, 0x1f, 0x2d, 0xa6, 0xf8, 0x42, 0x8d,
        0x2f, 0x85, 0x11, 0xf2, 0x98, 0x96, 0xb1, 0x6f, 0xc1, 0x0d, 0xb4, 0xa8, 0x1f, 0xe1, 0xa7, 0xe2,
        0xc4, 0xc9, 0xba, 0x12, 0x93, 0xdd, 0x9a, 0xa9, 0xcc, 0x80, 0x98, 0xf9, 0x4c, 0xdf, 0xbd, 0x2a,
        0xcf, 0x68, 0x2a, 0x4f, 0x41, 0x5f, 0x1b, 0x7d, 0x42, 0xe7, 0x07, 0xdb, 0xb6, 0x42, 0x79, 0xeb,
        0x30, 0xbf, 0x82, 0x58, 0xa6, 0x31, 0xcd, 0x79, 0xfb, 0xa4, 0xb9, 0x60, 0x08, 0xae, 0x52, 0xae,
        0x75, 0xfb, 0x27, 0x62, 0x26, 0x07, 0x15, 0x4b, 0x48, 0xe5, 0xe2, 0x98, 0x8e, 0xbb, 0x7e, 0x50,
        0x42, 0x9d, 0x8b, 0xfc, 0x4a, 0x97, 0xaa, 0xe8, 0x6a, 0x28, 0x02, 0x0d, 0x95, 0xa5, 0x0f, 0xc9,
        0x9d, 0x20, 0xc1, 0x2d, 0x92, 0x97, 0xe9, 0xf8, 0x02, 0x24, 0xee, 0x5d, 0x5c, 0xfb, 0x39, 0xfc,
        0x80, 0x44, 0x30, 0xf7, 0xdc, 0x98, 0x19, 0x37, 0xdf, 0xcc, 0x51, 0xbd, 0xfd, 0x80, 0xf8, 0xa2,
        0xb3, 0x69, 0xc9, 0x36, 0xd9, 0x94, 0xdd, 0xf4, 0x20, 0x6d, 0x8e, 0x16, 0x89, 0xe0, 0xaa, 0x8c,
        0xcb, 0x3b, 0xc3, 0x29, 0x75, 0xb0, 0xfd, 0xd9, 0xea, 0x97, 0x6f, 0x30, 0x1e, 0x0a, 0x2b, 0x18,
        0xb1, 0xe9, 0xe6, 0x21, 0x2b, 0x79, 0xdf, 0x38, 0x54, 0x12, 0x94, 0x65, 0x61, 0xb4, 0xdc, 0x18,
        0xb7, 0x6b, 0x50, 0x09, 0x38, 0xec, 0xc7, 0xf9, 0x22, 0x00, 0xa8, 0x23, 0x3f, 0x9b, 0xbc, 0x56,
        0x30, 0x26, 0xd3, 0xb7, 0x40, 0xa6, 0xc2, 0x3c, 0x2d, 0x62, 0xd0, 0x5b, 0x6d, 0x0f, 0x91, 0xe1,
        0x1a, 0x24, 0xc6, 0xeb, 0x7d, 0xaa, 0x31, 0xc8, 0xab, 0xcc, 0x1f, 0x96, 0xba, 0xae, 0xb0, 0x6a,
        0xc7, 0x9f, 0x3e, 0x61, 0xea, 0xe3, 0x11, 0xa5, 0xe1, 0x1a, 0x64, 0xb8, 0x8e, 0xa9, 0x22, 0x8b,
        0x42, 0xc3, 0x52, 0xdf, 0xb5, 0xe1, 0xc0, 0x06, 0xe1, 0x77, 0xac, 0xfa, 0x47, 0x53, 0x78, 0x3d,
        0xb6, 0xa3, 0x8a, 0xb8, 0x8d, 0xd3, 0x32, 0x8f, 0xff, 0xe8, 0xcc, 0x67, 0xd2, 0xe4, 0x39, 0x2f,
        0x93, 0x9a, 0x9d, 0xdf, 0xa6, 0xc2, 0x1a, 0xff, 0xe4, 0x33, 0x45, 0x8b, 0xb9, 0x51, 0x6b, 0xb8,
        0x63, 0x16, 0x6a, 0xf1, 0x36, 0xb2, 0x1b, 0x58, 0x14, 0xdc, 0x8f, 0xb2, 0x1d, 0xc3, 0x89, 0x48,
        0x78, 0xdd, 0x67, 0x80, 0x49, 0x32, 0x81, 0x3e, 0x30, 0x26, 0xe0, 0xec, 0x44, 0x0f, 0xaa, 0x6a,
        0x70, 0xa7, 0x93, 0x4f, 0xde, 0x74, 0xed, 0xcf, 0x2d, 0x6d, 0x10, 0xe5, 0x37, 0x20, 0x46, 0x10,
        0x45, 0x34, 0x66, 0x5a, 0x34, 0xf0, 0xd3, 0x96, 0xd1, 0xa5, 0x78, 0x17, 0xf1, 0x24, 0x5f, 0x2a,
        0xe9, 0x2f, 0x0a, 0xda, 0xf7, 0xe5, 0xa8, 0x67, 0x3e, 0x3b, 0xb9, 0xa9, 0x78, 0xc3, 0x0d, 0x68,
        0x0a, 0xa0, 0x00, 0x6e, 0xc7, 0xb1, 0xba, 0x01, 0x4f, 0xb6, 0x41, 0x41, 0x52, 0xf6, 0xa8, 0x5b,
        0xd4, 0xae, 0x79, 0xb9, 0xc3, 0x8b, 0xa5, 0x93, 0x66, 0xc8, 0x6b, 0xb2, 0x8d, 0x4d, 0xdf, 0x64,
        0xc2, 0x0c, 0x48, 0x9b, 0x0e, 0x66, 0xa7, 0xdf, 0xc1, 0x98, 0x3c, 0xec, 0x2c, 0x56, 0x84, 0x9e,
        0xf8, 0xa5, 0xc7, 0x61, 0x9e, 0xcb, 0x64, 0xee, 0xdc, 0xda, 0x6f, 0x54, 0x2e, 0x32, 0x46, 0xce,
        0xa7, 0x48, 0xe7, 0xb7, 0x38, 0x65, 0xab, 0x49, 0x16, 0x42, 0x09, 0x2b, 0x02, 0x8a, 0x71, 0x5b,
        0xac, 0xf9, 0xb6, 0x64, 0xaa, 0x6a, 0x33, 0x34, 0x7f, 0xcd, 0x1f, 0xbf, 0x4c, 0x03, 0x30, 0x53,
        0x23, 0x2f, 0xcb, 0xba, 0xf7, 0xf3, 0x8c, 0x64, 0x39, 0xb7, 0xfe, 0x55, 0x7e, 0x5a, 0x46, 0xa1,
        0x92, 0x22, 0x35, 0x3b, 0xa5, 0x41, 0x3a, 0x76, 0xd5, 0x5f, 0xb3, 0x09, 0x24, 0x8d, 0xeb, 0x4a,
        0x7c, 0xdd, 0x46, 0x46, 0x3a, 0xf4, 0xe6, 0x55, 0x7f, 0x46, 0x99, 0xb6, 0x09, 0x5f, 0xa8, 0x75,
        0x3f, 0xa3, 0x41, 0xe0, 0xcf, 0xff, 0xd4, 0x8d, 0x88, 0x9e, 0x25, 0xad, 0x6b, 0x0e, 0x91, 0x73,
        0x64, 0x38, 0xe7, 0xef, 0x2a, 0x3b, 0xc2, 0xf2, 0x1d, 0x0c, 0x5c, 0xf5, 0x3f, 0x76, 0x2f, 0x6e,
        0xb4, 0xb2, 0x0a, 0x27, 0xe6, 0x82, 0x1c, 0x80, 0x50, 0xd9, 0x5d, 0x0a, 0x27, 0xda, 0xa9, 0x4d,
        0xa2, 0xcf, 0x1f, 0x99, 0x4c, 0xbf, 0x8f, 0x7d, 0xd7, 0x5d, 0x9e, 0x66, 0x13, 0x87, 0x74, 0x22,
        0x86, 0x80, 0x02, 0xf5, 0xb9, 0xfe, 0xd0, 0xba, 0xd3, 0x2d, 0x38, 0xb7, 0xb6, 0x8e, 0x79, 0xe1,
        0xc0, 0x7c, 0xe6, 0x93, 0xe9, 0xac, 0x21, 0xb3, 0xe9, 0xfa, 0x4c, 0x5d, 0x23, 0x38, 0x7c, 0xb4,
        0xfe, 0xdc, 0xbb, 0xb9, 0x64, 0x49, 0x82, 0xad, 0xdb, 0x86, 0xe2, 0xd1, 0x96, 0x47, 0x56, 0xc9,
        0xc4, 0x65, 0xae, 0xdf, 0x03, 0x73, 0x60, 0xa1, 0x1f, 0xcf, 0x78, 0x89, 0x74, 0xa5, 0x83, 0x45,
        0x13, 0xb0, 0x5f, 0x7a, 0x7b, 0xa1, 0xe4, 0xc6, 0xd1, 0x72, 0x72, 0xcc, 0xa1, 0x7c, 0xc2, 0xbe,
        0xb1, 0x6c, 0xf1, 0xea, 0x88, 0xed, 0x75, 0x6e, 0x80, 0x94, 0xf1, 0x84, 0x03, 0x9a, 0x7b, 0x4f,
        0x04, 0x31, 0x80, 0xe5, 0xc3, 0xa1, 0x1a, 0x30, 0x0e, 0x02, 0x85, 0x02, 0xb8, 0x1d, 0x81, 0x04,
        0x87, 0x19, 0x78, 0x00, 0x50, 0xaa, 0x76, 0x2d, 0x92, 0xd7, 0x1f, 0xb5, 0x8a, 0x14, 0xed, 0xed,
        0x1e, 0x0c, 0x3a, 0x8b, 0x46, 0xa9, 0x7c, 0xbe, 0xb4, 0x34, 0x9a, 0xb7, 0x9c, 0x0e, 0x88, 0x56,
        0xb8, 0x78, 0x2c, 0x7b, 0xfd, 0xbb, 0x40, 0x31, 0x53, 0xb8, 0x37, 0x14, 0xb9, 0x18, 0xff, 0x45,
        0x4e, 0x06, 0xed, 0x57, 0xc9, 0x49, 0x0c, 0xa3, 0xc1, 0x57, 0x58, 0xc5, 0x0e, 0x9d, 0x90, 0xa7,
        0x1c, 0xb9, 0xf2, 0xef, 0x3f, 0xd1, 0xb2, 0xa5, 0xa9, 0x48, 0xa4, 0x6e, 0x32, 0x45, 0x5b, 0x49,
        0xd5, 0xfb, 0xe1, 0x19, 0xa0, 0x2e, 0x8f, 0x57, 0x7c, 0xe7, 0x34, 0x56, 0x88, 0xb5, 0x98, 0x93,
        0x6c, 0x86, 0x82, 0xb9, 0xd1, 0x7f, 0xc3, 0xe6, 0xdc, 0xc1, 0x4d, 0xfe, 0xb4, 0xbd, 0x81, 0x61,
        0x4b, 0x97, 0x45, 0x59, 0x2a, 0x5d, 0xa4, 0x3c, 0x8c, 0x90, 0x0d, 0x24, 0xf8, 0x18, 0xff, 0x15,
        0xd2, 0x46, 0x54, 0xd8, 0x16, 0xd4, 0x50, 0x53, 0x57, 0x1a, 0x24, 0xb6, 0x70, 0xe3, 0xf9, 0x04,
        0xde, 0x01, 0xcc, 0x10, 0x6f, 0x8c, 0xf6, 0x42, 0x5f, 0x7c, 0x3c, 0x82, 0x67, 0x8f, 0xed, 0x11,
        0x29, 0xcd, 0x43, 0x72, 0xdc, 0x3e, 0x15, 0xf2, 0xde, 0xbe, 0x8f, 0xd1, 0x22, 0xbb, 0x5c, 0x0e,
        0x96, 0xc9, 0xbc, 0x2b, 0x96, 0x65, 0x2a, 0xd1, 0x96, 0x98, 0x51, 0xb6, 0x81, 0x75, 0x61, 0xe9,
        0x69, 0x1c, 0xfd, 0xe9, 0x5b, 0x8c, 0xf1, 0x3b, 0xa9, 0xd6, 0x03, 0xb1, 0x44, 0x48, 0x32, 0x58,
        0x74, 0xa0, 0x6a, 0x82, 0x7c, 0x60, 0x4f, 0x0a, 0xec, 0x89, 0xa1, 0x6d, 0xe6, 0x70, 0x61, 0x15,
        0xa6, 0x71, 0xd9, 0xdd, 0x19, 0x6a, 0x69, 0x4b, 0xff, 0xce, 0x33, 0xfb, 0x3f, 0x70, 0x07, 0xe2,
        0x3c, 0xa7, 0x47, 0x60, 0xbb, 0x60, 0x0c, 0x0b, 0xe4, 0xec, 0x07, 0x86, 0x09, 0x5d, 0xc9, 0x09,
        0x4a, 0xa7, 0xbf, 0x30, 0xa7, 0x36, 0x76, 0x15, 0x49, 0x4d, 0xb7, 0x65, 0x21, 0xbf, 0x51, 0xc8,
        0xd0, 0x6f, 0x6d, 0x88, 0xb2, 0x95, 0x98, 0xeb
    };

    unsigned char solution[] = {
        0xd2, 0x56, 0x67, 0x0e, 0xe5, 0x14, 0x95, 0xd8, 0xc0, 0x0e, 0x96, 0x4e, 0x3d, 0xd8, 0x28, 0x76,
        0x5a, 0xab, 0xdb, 0x76, 0x7b, 0x55, 0xe6, 0x80, 0xb2, 0xda, 0x36, 0xb8, 0x9d, 0x9e, 0xe1, 0x3c
    };

    BitVectorErrorCode ret_code = BitVectorErrorCode::OK;

    size_t len = sizeof(buffer);
    printf("Size of data buf = %lu ...\n", len);

    BitVectorBuffer bvb_in_uncomp(buffer, len);
    CompressionAlgorithm e = CompressionAlgorithm::Gzip;
    
    printf("\nCompressing data ...\n");
    BitVectorBuffer* bvb_ret = zendoo_compress_bit_vector(&bvb_in_uncomp, e, &ret_code);
    ASSERT_TRUE(bvb_ret != nullptr);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::OK);

    printf("\nCompressed data size = %lu ...\n", bvb_ret->len);

    printf("\nBuilding merkle tree ...\n");
    field_t* fe = zendoo_merkle_root_from_compressed_bytes(bvb_ret, len, &ret_code);
    ASSERT_TRUE(ret_code == BitVectorErrorCode::OK);
    ASSERT_TRUE(fe != nullptr);

    printf("\nSerializing result ...\n");
    unsigned char field_bytes[CSidechainField::ByteSize()];
    zendoo_serialize_field(fe, field_bytes);

    printf("\nsolution = [");
    for (int i = 0; i < sizeof(solution); i++)
    {
        printf("%02x", ((unsigned char*)field_bytes)[i]);
        ASSERT_TRUE(field_bytes[i] == solution[i]);
    }
    printf("]\n");

    printf("\nfreeing mem...\n");
    zendoo_free_bit_vector(bvb_ret);
    zendoo_field_free(fe);
}

TEST(SidechainsField, CommitmentTreeCreation)
{
    printf("Creating a commitment tree ...\n");
    commitment_tree_t* ct = zendoo_commitment_tree_create();
    ASSERT_TRUE(ct != nullptr);

    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(1000);
    const uint256& scId = aTransaction.GetScIdFromScCcOut(0);

    const uint256& tx_hash = aTransaction.GetHash();

    uint32_t out_idx = 0;
    for (const CTxScCreationOut& ccout : aTransaction.GetVscCcOut() )
    {
        CAmount crAmount = ccout.nValue;
        const uint256& pub_key = ccout.address;
        uint32_t epoch_len = ccout.withdrawalEpochLength;

        // TODO what to do for empty or variable size vectors?
        //      currently this is considered a 32 bytes fields but this is not correct, should be 1024 at most
        //      proposal: lets make it a struct{data, len}
        std::vector<unsigned char> customData;
        if (!ccout.customData.empty())
            customData = ccout.customData;
        customData.resize(32, 0xff);
 
        // TODO what to do for optional fixed size params?
        //      proposal: lets make it a struct{data, len} as well
        std::vector<unsigned char> constant;
        if(ccout.constant.is_initialized())
            constant = ccout.constant->GetByteArray();
        constant.resize(32, 0xff);
            
        std::vector<unsigned char> cert_vk(ccout.wCertVk.begin(), ccout.wCertVk.end());
 
        std::vector<unsigned char> btr_vk;
        if(ccout.wMbtrVk.is_initialized())
            btr_vk = std::vector<unsigned char>(ccout.wMbtrVk->begin(), ccout.wMbtrVk->end());
        constant.resize(1544, 0xff);
            
        std::vector<unsigned char> csw_vk;
        if(ccout.wCeasedVk.is_initialized())
            csw_vk = std::vector<unsigned char>(ccout.wCeasedVk->begin(), ccout.wCeasedVk->end());
        constant.resize(1544, 0xff);

        printf("Adding a sc creation to the commitment tree ...\n");
        bool ret = zendoo_commitment_tree_add_scc(ct,
             (unsigned char*)scId.begin(),
             crAmount,
             (unsigned char*)pub_key.begin(),
             epoch_len,
             (unsigned char*)&customData[0],
             (unsigned char*)&constant[0],
             (unsigned char*)&cert_vk[0],
             (unsigned char*)&btr_vk[0],
             (unsigned char*)&csw_vk[0],
             (unsigned char*)tx_hash.begin(),
             out_idx
        );
 
        out_idx++;
    }


    printf("Deleting a nullptr commitment tree ...\n");
    zendoo_commitment_tree_delete(nullptr);

    printf("Deleting the commitment tree ...\n");
    zendoo_commitment_tree_delete(ct);
}

