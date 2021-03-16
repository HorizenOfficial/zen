#include <gtest/gtest.h>
#include <gtest/libzendoo_test_files.h>
#include <util.h>
#include <stdio.h>
#include <cstring>
#include <utilstrencodings.h>

#include <sc/proofverifier.h>
#include <streams.h>
#include <clientversion.h>
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
    const unsigned char buffer[3] = {0x07, 0x0e, 0x00};
    CompressionAlgorithm e = CompressionAlgorithm::Uncompressed;

    bv_buffer* ct = zendoo_compress_bit_vector(buffer, sizeof(buffer), e);

    unsigned char* ptr = (unsigned char*)ct;

    for (int i = 0; i < sizeof(buffer); i++)
    {
        ASSERT_TRUE(ptr[i+1] == buffer[i]);
    }

    zendoo_bit_vector_buffer_free(ct);
}

TEST(SidechainsField, BitVectorGzip)
{
    const unsigned char buffer[5] = {0xad, 0xde, 0xef, 0xbe, 0x00};
    CompressionAlgorithm e = CompressionAlgorithm::Gzip;

    printf("Compressing using gzip...\n");
    bv_buffer* buf_1 = zendoo_compress_bit_vector(buffer, sizeof(buffer), e);

    // make a copy
    unsigned char ptr2[BV_SIZE_IN_BYTES] = {};
    memcpy(ptr2, buf_1, BV_SIZE_IN_BYTES);
    ptr2[0] = (unsigned char)CompressionAlgorithm::Bzip2;

    printf("\nDecompressing with an invalid compression algo enum...\n");
    bv_buffer* ct = zendoo_decompress_bit_vector(ptr2, sizeof(buffer));
    ASSERT_TRUE(ct == nullptr);

    unsigned char ptr3[0] = {};
    printf("\nDecompressing an empty buffer...\n");
    ct = zendoo_decompress_bit_vector(ptr3, 0);
    ASSERT_TRUE(ct == nullptr);

    unsigned char* ptr4 = nullptr;
    printf("\nDecompressing a null ptr buffer...\n");
    ct = zendoo_decompress_bit_vector(ptr4, 0);
    ASSERT_TRUE(ct == nullptr);

    printf("\nDecompressing expecting a wrong size...\n");
    ct = zendoo_decompress_bit_vector((unsigned char*)buf_1, sizeof(buffer)-1);
    ASSERT_TRUE(ct == nullptr);

    printf("\nDecompressing good data...\n");
    bv_buffer* buf_2 = zendoo_decompress_bit_vector((unsigned char*)buf_1, sizeof(buffer));
    ASSERT_TRUE(buf_2 != nullptr);

    unsigned char* ptr = (unsigned char*)buf_2;

    for (int i = 0; i < sizeof(buffer); i++)
    {
        ASSERT_TRUE(ptr[i] == buffer[i]);
    }

    printf("\nfreeing buffers...\n");
    zendoo_bit_vector_buffer_free(buf_1);
    zendoo_bit_vector_buffer_free(buf_2);
}

TEST(SidechainsField, BitVectorBzip2)
{
    const unsigned char buffer[5] = {0xad, 0xde, 0xef, 0xbe, 0x00};
    CompressionAlgorithm e = CompressionAlgorithm::Bzip2;

    printf("Compressing using bzip2...\n");
    bv_buffer* buf_1 = zendoo_compress_bit_vector(buffer, sizeof(buffer), e);

    // make a copy
    unsigned char ptr2[BV_SIZE_IN_BYTES] = {};
    memcpy(ptr2, buf_1, BV_SIZE_IN_BYTES);
    ptr2[0] = (unsigned char)CompressionAlgorithm::Gzip;

    printf("\nDecompressing with an invalid compression algo enum...\n");
    bv_buffer* ct = zendoo_decompress_bit_vector(ptr2, sizeof(buffer));
    ASSERT_TRUE(ct == nullptr);

    printf("\nDecompressing expecting a wrong size...\n");
    ct = zendoo_decompress_bit_vector((unsigned char*)buf_1, sizeof(buffer)-1);
    ASSERT_TRUE(ct == nullptr);

    printf("\nDecompressing good data...\n");
    bv_buffer* buf_2 = zendoo_decompress_bit_vector((unsigned char*)buf_1, sizeof(buffer));
    ASSERT_TRUE(buf_2 != nullptr);

    unsigned char* ptr = (unsigned char*)buf_2;

    for (int i = 0; i < sizeof(buffer); i++)
    {
        ASSERT_TRUE(ptr[i] == buffer[i]);
    }

    printf("\nfreeing buffers...\n");
    zendoo_bit_vector_buffer_free(buf_1);
    zendoo_bit_vector_buffer_free(buf_2);
}

TEST(SidechainsField, BitVectorMerkleTree)
{
    const unsigned char buffer[5] = {0xad, 0xde, 0xef, 0xbe, 0x00};
    CompressionAlgorithm e = CompressionAlgorithm::Bzip2;

    printf("Compressing using bzip2...\n");
    bv_buffer* buf_1 = zendoo_compress_bit_vector(buffer, sizeof(buffer), e);

    printf("building merkle tree ...\n");
    field_t* fe = zendoo_merkle_root_from_bytes((const unsigned char*)buf_1, sizeof(buffer));

    ASSERT_TRUE(fe != nullptr);
    zendoo_field_free(fe);
}


