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
#include <pubkey.h>
#include <script/standard.h>

#include <streams.h>
#include <clientversion.h>
#include <sc/proofverifier.h> // for MC_CRYPTO_LIB_MOCKED 

static CMutableTransaction CreateDefaultTx()
{
    // Create a tx with a sc creation, a fwt, a bwtr and a csw

    CMutableTransaction mtx;
    mtx.nVersion = SC_TX_VERSION;
    //---
    mtx.vsc_ccout.resize(1);
    mtx.vsc_ccout[0].nValue = CAmount(12000);
    mtx.vsc_ccout[0].withdrawalEpochLength = 150;
    mtx.vsc_ccout[0].wCertVk   = CScVKey{SAMPLE_CERT_DARLIN_VK};
    mtx.vsc_ccout[0].wCeasedVk = CScVKey{SAMPLE_CSW_DARLIN_VK};
    // TODO as of now these are not mandatory but are required by mc crypto lib: test the case of missing values
    mtx.vsc_ccout[0].vFieldElementCertificateFieldConfig.push_back(22);
    mtx.vsc_ccout[0].customData.push_back(0x33);
    //---
    mtx.vft_ccout.resize(1);
    mtx.vft_ccout[0].scId = uint256S("abababcdcdcd");
    mtx.vft_ccout[0].nValue = CAmount(30000);
    //---
    mtx.vmbtr_out.resize(1);
    mtx.vmbtr_out[0].scId = uint256S("abababcdcdcd"); // same as above
    mtx.vmbtr_out[0].vScRequestData.push_back(CFieldElement{SAMPLE_FIELD});
    mtx.vmbtr_out[0].mcDestinationAddress = uint160S("fefefe");
    mtx.vmbtr_out[0].scFee = CAmount(1);
    //---
    mtx.vcsw_ccin.resize(1);

    std::string actCertDataHex         = CFieldElement{SAMPLE_FIELD}.GetHexRepr();
    std::string ceasingCumScTxCommTree = CFieldElement{SAMPLE_FIELD}.GetHexRepr();

    mtx.vcsw_ccin[0] = txCreationUtils::CreateCSWInput(
        /*scid*/uint256S("efefef"), /*nullifierhexstr*/"abab", actCertDataHex, ceasingCumScTxCommTree, /*amount*/ 777); 

    return mtx;
}
static CMutableScCertificate CreateDefaultCert()
{
    CMutableScCertificate mcert;
    mcert.nVersion = SC_CERT_VERSION;
    mcert.scId = uint256S("abababcdcdcd"); // same as above
    mcert.epochNumber = 10;
    mcert.endEpochBlockHash = uint256S("eeeeeeeeeee");;
    mcert.quality = 20;
    mcert.scProof.SetByteArray(SAMPLE_CERT_DARLIN_PROOF);
    mcert.endEpochCumScTxCommTreeRoot.SetByteArray(SAMPLE_FIELD);

    mcert.vin.resize(1);
    mcert.vin[0].prevout.hash = uint256S("1");
    mcert.vin[0].prevout.n = 0;
    
    CScript dummyScriptPubKey =
            GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))), false);
    for(unsigned int idx = 0; idx < 2; ++idx)
        mcert.addOut(CTxOut(1, dummyScriptPubKey));

    for(unsigned int idx = 0; idx < 3; ++idx)
        mcert.addBwt(CTxOut(1000*idx+456, dummyScriptPubKey));

    return mcert;
}


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
    tooShortStream >> tooShortRetrievedField;
    EXPECT_FALSE(tooShortRetrievedField.IsValid());

    ////////////////////
    std::vector<unsigned char> tooBigByteArray(CFieldElement::ByteSize()*2,0x0);
    ASSERT_TRUE(tooBigByteArray.size() > CFieldElement::ByteSize());
    CDataStream tooBigStream(SER_DISK, CLIENT_VERSION);

    tooBigStream << tooBigByteArray;
    CFieldElement tooBigRetrievedField;
    tooBigStream >> tooBigRetrievedField;
    EXPECT_FALSE(tooBigRetrievedField.IsValid());

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
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f
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
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f
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

TEST(SidechainsField, ComputeHash_EmptyField)
{
    std::vector<unsigned char> lhs {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f
    };
    CFieldElement lhsField{lhs};
    ASSERT_TRUE(lhsField.IsValid());

    CFieldElement rhsField{};
    ASSERT_FALSE(rhsField.IsValid());

    //test
    EXPECT_THROW(CFieldElement::ComputeHash(lhsField, rhsField), std::runtime_error);
}

TEST(SidechainsField, ComputeHash_ValidField)
{
    std::vector<unsigned char> lhs {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f
    };
    CFieldElement lhsField{lhs};
    ASSERT_TRUE(lhsField.IsValid());

    std::vector<unsigned char> rhs {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f
    };
    CFieldElement rhsField{rhs};
    ASSERT_TRUE(rhsField.IsValid());

    std::vector<unsigned char> expectedHash {
        0xee, 0x63, 0x04, 0xc4, 0x92, 0xac, 0x10, 0x22,
        0xbe, 0xfd, 0x4d, 0x88, 0x5d, 0x4a, 0x13, 0x8b,
        0x12, 0x99, 0x55, 0xa0, 0xff, 0x20, 0x1a, 0x92,
        0x41, 0xf8, 0xc1, 0x2a, 0x03, 0x21, 0xc7, 0x24
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
    ASSERT_EQ(zendoo_get_field_size_in_bytes(), CFieldElement::ByteSize());

    auto field = zendoo_get_random_field();

    //Serialize and deserialize and check equality
    CctpErrorCode code;
    unsigned char field_bytes[CFieldElement::ByteSize()];
    zendoo_serialize_field(field, field_bytes, &code);
    ASSERT_TRUE(code == CctpErrorCode::OK);

    auto field_deserialized = zendoo_deserialize_field(field_bytes, &code);
    ASSERT_TRUE(field_deserialized != NULL);

    ASSERT_TRUE(zendoo_field_assert_eq(field, field_deserialized));

    zendoo_field_free(field);
    zendoo_field_free(field_deserialized);
}

TEST(SidechainsField, NakedZendooFeatures_PoseidonHashTest)
{
    unsigned char lhs[CFieldElement::ByteSize()] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f
    };

    unsigned char rhs[CFieldElement::ByteSize()] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f
    };

    unsigned char hash[CFieldElement::ByteSize()] = {
        0x42, 0xff, 0xd4, 0x94, 0x7f, 0x76, 0xf7, 0xc1,
        0xba, 0x0a, 0xcf, 0x73, 0xf3, 0x0a, 0xa3, 0x7b,
        0x5a, 0xe8, 0xeb, 0xde, 0x5d, 0x61, 0xc3, 0x19,
        0x70, 0xc2, 0xf6, 0x45, 0x7b, 0x83, 0x2a, 0x39
    };

    CctpErrorCode code;
    auto lhs_field = zendoo_deserialize_field(lhs, &code);
    ASSERT_TRUE(code == CctpErrorCode::OK);
    ASSERT_TRUE(lhs_field != NULL);

    auto rhs_field = zendoo_deserialize_field(rhs, &code);
    ASSERT_TRUE(code == CctpErrorCode::OK);
    ASSERT_TRUE(rhs_field != NULL);

    auto expected_hash = zendoo_deserialize_field(hash, &code);
    ASSERT_TRUE(code == CctpErrorCode::OK);
    ASSERT_TRUE(expected_hash != NULL);

    auto digest = ZendooPoseidonHashConstantLength{2, &code};
    ASSERT_TRUE(code == CctpErrorCode::OK);

    digest.update(lhs_field, &code);
    ASSERT_TRUE(code == CctpErrorCode::OK);

    auto temp_hash = digest.finalize(&code);
    ASSERT_TRUE(code != CctpErrorCode::OK);
    ASSERT_TRUE(temp_hash == nullptr);

    zendoo_field_free(expected_hash);
    zendoo_field_free(lhs_field);
    zendoo_field_free(rhs_field);
}

TEST(SidechainsField, NakedZendooFeatures_PoseidonMerkleTreeTest)
{
    size_t height = 5;

    // Deserialize root
    std::vector<unsigned char> expected_root_bytes {
        113, 174, 41, 1, 227, 14, 47, 27, 44, 172, 21, 18, 63, 182, 174, 162, 239, 251,
        93, 88, 43, 221, 235, 253, 30, 110, 180, 114, 134, 192, 15, 20
    };
    ASSERT_TRUE(expected_root_bytes.size() == CFieldElement::ByteSize());
    CFieldElement expected_root{expected_root_bytes};
    ASSERT_TRUE(expected_root.IsValid());

    //Generate leaves

    //enum removes variable length buffer [-Wstack-protector] warning that simple const int would give
    enum { leaves_len = 32 };
    CFieldElement leaves[leaves_len];
    for (int i = 0; i < leaves_len; i++)
    {
        leaves[i] = CFieldElement{wrappedFieldPtr{zendoo_get_field_from_long(i), CFieldPtrDeleter{}}};
    }

    // Initialize tree
    auto tree = ZendooGingerMerkleTree(height, leaves_len);
    CctpErrorCode code;

    // Add leaves to tree
    for (int i = 0; i < leaves_len; i++){
        tree.append(leaves[i].GetFieldElement().get(), &code);
        ASSERT_TRUE(code == CctpErrorCode::OK);
    }

    // Finalize tree
    tree.finalize_in_place(&code);
    ASSERT_TRUE(code == CctpErrorCode::OK);

    // Compute root and assert equality with expected one
    CFieldElement root = CFieldElement{wrappedFieldPtr{tree.root(&code), CFieldPtrDeleter{}}};
    ASSERT_TRUE(code == CctpErrorCode::OK);
    EXPECT_TRUE(root == expected_root);

    // It is the same by calling finalize()
    auto tree_copy = tree.finalize(&code);
    ASSERT_TRUE(code == CctpErrorCode::OK);
    CFieldElement root_copy = CFieldElement{wrappedFieldPtr{tree_copy.root(&code), CFieldPtrDeleter{}}};
    ASSERT_TRUE(code == CctpErrorCode::OK);
    ASSERT_TRUE(root_copy == expected_root);

    // Test Merkle Paths
    for (int i = 0; i < leaves_len; i++) {
        auto path = tree.get_merkle_path(i, &code);
        ASSERT_TRUE(code == CctpErrorCode::OK);
        ASSERT_TRUE(zendoo_verify_ginger_merkle_path(path, height, leaves[i].GetFieldElement().get(), root.GetFieldElement().get(), &code))
        <<"Merkle Path must be verified";
        ASSERT_TRUE(code == CctpErrorCode::OK);
        zendoo_free_ginger_merkle_path(path);
    }
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
#if 1
    std::cout << "### THIS IS DEACTIVATED --- FIX IT ###" << std::endl;
    ASSERT_TRUE(false);
#else
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
    ASSERT_TRUE(zendoo_verify_certificate_proof(
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
    ASSERT_FALSE(zendoo_verify_certificate_proof(
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
#endif
}

TEST(SidechainsField, NakedZendooFeatures_TreeCommitmentCalculation)
{
    //fPrintToConsole = true;

    //Add txes containing scCreation and fwd transfer + a certificate
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10), /*height*/10);

    CMutableTransaction mutTx = scCreationTx;

    auto ccout = CTxScCreationOut(CAmount(10), uint256S("aaa"), CAmount(0), CAmount(0), Sidechain::ScFixedParameters());
    // set mandatory/legal params
    ccout.withdrawalEpochLength = 11;
    ccout.wCertVk   = CScVKey{SAMPLE_CERT_DARLIN_VK};
    ccout.wCeasedVk = CScVKey{SAMPLE_CSW_DARLIN_VK};
    ccout.vFieldElementCertificateFieldConfig.push_back(44);
    ccout.customData.push_back(0x77);

    mutTx.vsc_ccout.push_back(ccout);
    mutTx.vft_ccout.push_back(CTxForwardTransferOut(uint256S("bbb"), CAmount(1985), uint256S("badcafe")));
    scCreationTx = mutTx;

    uint256 scId = scCreationTx.GetScIdFromScCcOut(0);
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, CAmount(7));

    CScCertificate cert = txCreationUtils::createCertificate(scId,
        /*epochNum*/12, /*endEpochBlockHash*/uint256S("abc"), CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/0,
        /*numChangeOut */0, /*bwtTotalAmount*/1, /*numBwt*/1, /*ftScFee*/0, /*mbtrScFee*/0);

    SidechainTxsCommitmentBuilder builder;

    ASSERT_TRUE(builder.add(scCreationTx));
    ASSERT_TRUE(builder.add(fwdTx));
    ASSERT_TRUE(builder.add(cert));

    uint256 scTxCommitmentHash = builder.getCommitment();

    EXPECT_TRUE(scTxCommitmentHash == uint256S("0ee75779df9e7fab7a6c2f7472acfd24d4ae6b9dc1f077d02ac4ad974cdf0194"))
        <<scTxCommitmentHash.ToString();
}

TEST(SidechainsField, NakedZendooFeatures_EmptyTreeCommitmentCalculation)
{
    //fPrintToConsole = true;
    SidechainTxsCommitmentBuilder builder;

    const CFieldElement& emptyFe = CFieldElement{EMPTY_COMMITMENT_TREE_FIELD};
    uint256 emptySha = emptyFe.GetLegacyHashTO_BE_REMOVED();
    //Nothing to add

    uint256 scTxCommitmentHash = builder.getCommitment();
    EXPECT_TRUE(scTxCommitmentHash == emptySha) <<scTxCommitmentHash.ToString() << "\n" << emptySha.ToString();
}

TEST(CctpLibrary, BitVectorUncompressed)
{
    CctpErrorCode ret_code = CctpErrorCode::OK;

    unsigned char buffer[3] = {0x07, 0x0e, 0x00};
    CompressionAlgorithm e = CompressionAlgorithm::Uncompressed;

    BufferWithSize bws_in(buffer, sizeof(buffer));

    BufferWithSize* bws_ret = nullptr;
    bws_ret = zendoo_compress_bit_vector(&bws_in, e, &ret_code);
    ASSERT_TRUE(bws_ret != nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);

    const unsigned char* ptr = bws_ret->data;

    for (int i = 0; i < sizeof(buffer); i++)
    {
        ASSERT_TRUE(ptr[i+1] == buffer[i]);
    }

    zendoo_free_bit_vector(bws_ret);
}

TEST(CctpLibrary, BitVectorGzip)
{
    CctpErrorCode ret_code = CctpErrorCode::OK;

    unsigned char buffer[5] = {0xad, 0xde, 0xef, 0xbe, 0x00};
    CompressionAlgorithm e = CompressionAlgorithm::Gzip;

    BufferWithSize bws_in(buffer, sizeof(buffer));

    printf("Compressing using gzip...\n");
    BufferWithSize* bws_ret1 = nullptr;
    bws_ret1 = zendoo_compress_bit_vector(&bws_in, e, &ret_code);
    ASSERT_TRUE(bws_ret1 != nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);

    static const int SC_BV_SIZE_IN_BYTES = 12345;
    // make a copy
    unsigned char ptr2[SC_BV_SIZE_IN_BYTES] = {};
    memcpy(ptr2, bws_ret1->data, bws_ret1->len);
    ptr2[0] = (unsigned char)CompressionAlgorithm::Bzip2;
    BufferWithSize bws_in2(ptr2, bws_ret1->len);

    printf("\nDecompressing with an invalid compression algo enum...\n");
    BufferWithSize* bws_null = nullptr;
    bws_null = zendoo_decompress_bit_vector(&bws_in2, bws_in2.len, &ret_code);
    ASSERT_TRUE(bws_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::UncompressError);

    unsigned char ptr3[0] = {};
    BufferWithSize bws_in3(ptr3, 0);
    printf("\nDecompressing an empty buffer...\n");
    bws_null = zendoo_decompress_bit_vector(&bws_in3, bws_in3.len, &ret_code);
    ASSERT_TRUE(bws_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::InvalidBufferLength);

    unsigned char* ptr4 = nullptr;
    BufferWithSize bws_in4(ptr4, 33);
    printf("\nDecompressing a null ptr buffer in a valid struct...\n");
    bws_null = zendoo_decompress_bit_vector(&bws_in4, bws_in4.len, &ret_code);
    ASSERT_TRUE(bws_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::InvalidBufferData);

    BufferWithSize* bws_in5 = nullptr;
    printf("\nDecompressing a null ptr struct ...\n");
    bws_null = zendoo_decompress_bit_vector(bws_in5, bws_in4.len, &ret_code);
    ASSERT_TRUE(bws_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::NullPtr);

    printf("\nDecompressing expecting a wrong size...\n");
    bws_null = zendoo_decompress_bit_vector(bws_ret1, sizeof(buffer)-1, &ret_code);
    ASSERT_TRUE(bws_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::UncompressError);

    printf("\nDecompressing good data...\n");
    BufferWithSize* bws_ret2 = zendoo_decompress_bit_vector(bws_ret1, sizeof(buffer), &ret_code);
    ASSERT_TRUE(bws_ret2 != nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);

    const unsigned char* ptr = bws_ret2->data;

    for (int i = 0; i < sizeof(buffer); i++)
    {
        ASSERT_TRUE(ptr[i] == buffer[i]);
    }

    printf("\nfreeing buffers...\n");
    zendoo_free_bit_vector(bws_ret1);
    zendoo_free_bit_vector(bws_ret2);
}

TEST(CctpLibrary, BitVectorBzip2)
{
    CctpErrorCode ret_code = CctpErrorCode::OK;

    unsigned char buffer[5] = {0xad, 0xde, 0xef, 0xbe, 0x00};
    CompressionAlgorithm e = CompressionAlgorithm::Bzip2;

    BufferWithSize bws_in(buffer, sizeof(buffer));

    printf("Compressing using bzip2...\n");
    BufferWithSize* bws_ret1 = nullptr;
    bws_ret1 = zendoo_compress_bit_vector(&bws_in, e, &ret_code);
    ASSERT_TRUE(bws_ret1 != nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    static const int SC_BV_SIZE_IN_BYTES = 254;

    // make a copy
    unsigned char ptr2[SC_BV_SIZE_IN_BYTES] = {};
    memcpy(ptr2, bws_ret1->data, bws_ret1->len);
    ptr2[0] = (unsigned char)CompressionAlgorithm::Gzip;
    BufferWithSize bws_in2(ptr2, bws_ret1->len);

    printf("\nDecompressing with an invalid compression algo enum...\n");
    BufferWithSize* bws_null = nullptr;
    bws_null = zendoo_decompress_bit_vector(&bws_in2, sizeof(buffer), &ret_code);
    ASSERT_TRUE(bws_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::UncompressError);

    unsigned char ptr3[0] = {};
    BufferWithSize bws_in3(ptr3, 0);
    printf("\nDecompressing an empty buffer...\n");
    bws_null = zendoo_decompress_bit_vector(&bws_in3, bws_in3.len, &ret_code);
    ASSERT_TRUE(bws_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::InvalidBufferLength);

    unsigned char* ptr4 = nullptr;
    BufferWithSize bws_in4(ptr4, 33);
    printf("\nDecompressing a null ptr buffer in a valid struct...\n");
    bws_null = zendoo_decompress_bit_vector(&bws_in4, bws_in4.len, &ret_code);
    ASSERT_TRUE(bws_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::InvalidBufferData);

    BufferWithSize* bws_in5 = nullptr;
    printf("\nDecompressing a null ptr struct ...\n");
    bws_null = zendoo_decompress_bit_vector(bws_in5, bws_in4.len, &ret_code);
    ASSERT_TRUE(bws_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::NullPtr);

    printf("\nDecompressing expecting a wrong size...\n");
    bws_null = zendoo_decompress_bit_vector(bws_ret1, sizeof(buffer)-1, &ret_code);
    ASSERT_TRUE(bws_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::UncompressError);

    printf("\nDecompressing good data...\n");
    BufferWithSize* bws_ret2 = zendoo_decompress_bit_vector(bws_ret1, sizeof(buffer), &ret_code);
    ASSERT_TRUE(bws_ret2 != nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);

    const unsigned char* ptr = bws_ret2->data;

    for (int i = 0; i < sizeof(buffer); i++)
    {
        ASSERT_TRUE(ptr[i] == buffer[i]);
    }

    printf("\nfreeing buffers...\n");
    zendoo_free_bit_vector(bws_ret1);
    zendoo_free_bit_vector(bws_ret2);
}

TEST(CctpLibrary, BitVectorMerkleTree)
{
    CctpErrorCode ret_code = CctpErrorCode::OK;

// cctp does an assert (false) if  the size of buffer is too short
//    unsigned char buffer[5] = {0xad, 0xde, 0xef, 0xbe, 0x00};
    unsigned char buffer[65] = {
        0xad, 0xde, 0xef, 0xbe, 0x00, 0xaa, 0xdd, 0xff,
        0xad, 0xde, 0xef, 0xbe, 0x00, 0xaa, 0xdd, 0xff,
        0xad, 0xde, 0xef, 0xbe, 0x00, 0xaa, 0xdd, 0xff,
        0xad, 0xde, 0xef, 0xbe, 0x00, 0xaa, 0xdd, 0xff,
        0xad, 0xde, 0xef, 0xbe, 0x00, 0xaa, 0xdd, 0xff,
        0xad, 0xde, 0xef, 0xbe, 0x00, 0xaa, 0xdd, 0xff,
        0xad, 0xde, 0xef, 0xbe, 0x00, 0xaa, 0xdd, 0xff,
        0xad, 0xde, 0xef, 0xbe, 0x00, 0xaa, 0xdd, 0xff,
        0x33
    };

    BufferWithSize bws_in_uncomp(buffer, sizeof(buffer));

    printf("\nBuilding using uncompressed data...\n");
    field_t* fe_null = zendoo_merkle_root_from_compressed_bytes(&bws_in_uncomp, sizeof(buffer), &ret_code);
    ASSERT_TRUE(fe_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::MerkleRootBuildError);

    BufferWithSize bws_in(buffer, sizeof(buffer));
    CompressionAlgorithm e = CompressionAlgorithm::Bzip2;
    BufferWithSize* bws_ret1 = nullptr;
    bws_ret1 = zendoo_compress_bit_vector(&bws_in_uncomp, e, &ret_code);
    ASSERT_TRUE(bws_ret1 != nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);

    unsigned char* ptr4 = nullptr;
    BufferWithSize bws_in4(ptr4, 33);
    printf("\nDecompressing a null ptr buffer in a valid struct...\n");
    fe_null = zendoo_merkle_root_from_compressed_bytes(&bws_in4, bws_in4.len, &ret_code);
    ASSERT_TRUE(fe_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::InvalidBufferData);

    BufferWithSize* bws_in5 = nullptr;
    printf("\nBuilding with a null ptr struct ...\n");
    fe_null = zendoo_merkle_root_from_compressed_bytes(bws_in5, 5, &ret_code);
    ASSERT_TRUE(fe_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::NullPtr);

    printf("\nBuilding with a wrong expected size...\n");
    fe_null = zendoo_merkle_root_from_compressed_bytes(bws_ret1, sizeof(buffer)-1, &ret_code);
    ASSERT_TRUE(fe_null == nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::MerkleRootBuildError);

    printf("\nBuilding merkle tree ...\n");
    field_t* fe = zendoo_merkle_root_from_compressed_bytes(bws_ret1, sizeof(buffer), &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    ASSERT_TRUE(fe != nullptr);

    printf("\nfreeing mem...\n");
    zendoo_field_free(fe);
    zendoo_free_bit_vector(bws_ret1);
}

TEST(CctpLibrary, BitVectorMerkleTreeData)
{
    // (see cctp repo at folder: zendoo-cctp-lib/cctp_primitives/test/merkle_tree/ ) 

    // These are 4x254/8 = 127 bytes 
    // --- hexdump -C bvt_4x254_bytes.dat
    unsigned char buffer[] = { 
        0x44, 0xc7, 0xe2, 0x1b, 0xa1, 0xc7, 0xc0, 0xa2, 0x9d, 0xe0, 0x06, 0xcb, 0x80, 0x74, 0xe2, 0xba,
        0x39, 0xf1, 0x5a, 0xbf, 0xef, 0x25, 0x25, 0xa4, 0xcb, 0xb3, 0xf2, 0x35, 0x73, 0x44, 0x10, 0xbd,
        0xa2, 0x1c, 0xda, 0xb6, 0x62, 0x4d, 0xe7, 0x69, 0xce, 0xec, 0x81, 0x8a, 0xc6, 0xc2, 0xd3, 0xa0,
        0x1e, 0x38, 0x2e, 0x35, 0x7d, 0xce, 0x1f, 0x6e, 0x9a, 0x0f, 0xf2, 0x81, 0xf0, 0xfe, 0xda, 0xe0,
        0xef, 0xe2, 0x74, 0x35, 0x1d, 0xb3, 0x75, 0x99, 0xaf, 0x45, 0x79, 0x84, 0xdc, 0xf8, 0xe3, 0xae,
        0x44, 0x79, 0xe0, 0x56, 0x13, 0x41, 0xad, 0xff, 0xf4, 0x74, 0x6f, 0xbe, 0x27, 0x4d, 0x90, 0xf6,
        0xf7, 0x6b, 0x8a, 0x25, 0x52, 0xa6, 0xeb, 0xb9, 0x8a, 0xee, 0x91, 0x8c, 0x7c, 0xea, 0xc0, 0x58,
        0xf4, 0xc1, 0xae, 0x01, 0x31, 0x24, 0x95, 0x46, 0xef, 0x5e, 0x22, 0xf4, 0x18, 0x7a, 0x07
    };

    unsigned char solution[] = {
        0x8a, 0x7d, 0x52, 0x29, 0xf4, 0x40, 0xd4, 0x70, 0x0d, 0x8b, 0x03, 0x43, 0xde, 0x4e, 0x14, 0x40,
        0x0d, 0x1c, 0xb8, 0x74, 0x28, 0xab, 0xf8, 0x3b, 0xd6, 0x71, 0x53, 0xbf, 0x58, 0x87, 0x17, 0x21
    };

    CctpErrorCode ret_code = CctpErrorCode::OK;

    size_t len = sizeof(buffer);
    printf("Size of data buf = %lu ...\n", len);

    BufferWithSize bws_in_uncomp(buffer, len);
    CompressionAlgorithm e = CompressionAlgorithm::Gzip;
    
    printf("\nCompressing data ...\n");
    BufferWithSize* bws_ret = zendoo_compress_bit_vector(&bws_in_uncomp, e, &ret_code);
    ASSERT_TRUE(bws_ret != nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);

    printf("\nCompressed data size = %lu ...\n", bws_ret->len);

    printf("\nBuilding merkle tree ...\n");
    field_t* fe = zendoo_merkle_root_from_compressed_bytes(bws_ret, len, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    ASSERT_TRUE(fe != nullptr);

    printf("\nSerializing result ...\n");
    unsigned char field_bytes[CFieldElement::ByteSize()] = {};

    zendoo_serialize_field(fe, field_bytes, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);

    printf("\nsolution = [");
    for (int i = 0; i < sizeof(solution); i++)
    {
        printf("%02x", ((unsigned char*)field_bytes)[i]);
        ASSERT_TRUE(field_bytes[i] == solution[i]);
    }
    printf("]\n");

    printf("\nfreeing mem...\n");
    zendoo_free_bit_vector(bws_ret);
    zendoo_field_free(fe);
}

TEST(CctpLibrary, BitVectorCertificateFieldNull)
{
    const BitVectorCertificateFieldConfig cfg(1024, 2048);
    BitVectorCertificateField bvField;

    const CFieldElement& fe = bvField.GetFieldElement(cfg);
    EXPECT_FALSE(fe.IsValid());
}

TEST(CctpLibrary, BitVectorCertificateFieldUnsuppComprAlgo)
{
    // unsupported compression algo (header bytes in compressed buffer report used algo)
    const std::vector<unsigned char> bvVec(1024, 0xcc);

    const BitVectorCertificateFieldConfig cfg(1024, 2048);
    BitVectorCertificateField bvField(bvVec);

    const CFieldElement& fe = bvField.GetFieldElement(cfg);
    EXPECT_FALSE(fe.IsValid());
}

TEST(CctpLibrary, BitVectorCertificateFieldBadSize)
{
    CctpErrorCode ret_code = CctpErrorCode::OK;
    // too short an uncompressed data buffer
    unsigned char buffer[5] = {0xad, 0xde, 0xef, 0xbe, 0x00};
    CompressionAlgorithm e = CompressionAlgorithm::Gzip;

    BufferWithSize bws_in(buffer, sizeof(buffer));

    printf("Compressing using gzip...\n");
    BufferWithSize* bws_ret1 = nullptr;
    bws_ret1 = zendoo_compress_bit_vector(&bws_in, e, &ret_code);
    ASSERT_TRUE(bws_ret1 != nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);

    const std::vector<unsigned char> bvVec(bws_ret1->data, bws_ret1->data + bws_ret1->len);

    const BitVectorCertificateFieldConfig cfg(1024, 2048);
    BitVectorCertificateField bvField(bvVec);

    const CFieldElement& fe = bvField.GetFieldElement(cfg);
    EXPECT_FALSE(fe.IsValid());
    zendoo_free_bit_vector(bws_ret1);
}

TEST(CctpLibrary, BitVectorCertificateFieldFull)
{
    CctpErrorCode ret_code = CctpErrorCode::OK;

    // uncompressed buffer size, use the max size
    // TODO currently if a value not consistent with field element splitting is used, cctp does an assert(false)
    static const int SC_BV_SIZE_IN_BYTES = BitVectorCertificateFieldConfig::MAX_COMPRESSED_SIZE_BYTES;

    unsigned char buffer[SC_BV_SIZE_IN_BYTES] = {};
    buffer[0] = 0xff;
    buffer[SC_BV_SIZE_IN_BYTES-1] = 0xff;

    CompressionAlgorithm e = CompressionAlgorithm::Gzip;

    BufferWithSize bws_in(buffer, SC_BV_SIZE_IN_BYTES);

    printf("Compressing using gzip...\n");
    BufferWithSize* bws_ret1 = nullptr;
    bws_ret1 = zendoo_compress_bit_vector(&bws_in, e, &ret_code);
    ASSERT_TRUE(bws_ret1 != nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);

    const std::vector<unsigned char> bvVec(bws_ret1->data, bws_ret1->data + bws_ret1->len);

    int bitVectorSizeBits = SC_BV_SIZE_IN_BYTES*8; // the original size of the buffer
    int maxCompressedSizeBytes = bvVec.size(); // take the compressed data buf as max value 

    const BitVectorCertificateFieldConfig cfg(bitVectorSizeBits, maxCompressedSizeBytes);
    BitVectorCertificateField bvField(bvVec);

    const CFieldElement& fe = bvField.GetFieldElement(cfg);
    EXPECT_TRUE(fe.IsValid());
    zendoo_free_bit_vector(bws_ret1);
}


TEST(CctpLibrary, CommitmentTreeBuilding)
{
    printf("Creating a commitment tree ...\n");
    CctpErrorCode ret_code = CctpErrorCode::OK;
    const CFieldElement& dummyFe = CFieldElement{SAMPLE_FIELD};

    commitment_tree_t* ct = zendoo_commitment_tree_create();
    ASSERT_TRUE(ct != nullptr);

    unsigned char field_bytes[CFieldElement::ByteSize()] = {};

    printf("\nChecking commitment tree with a nullptr obj ...\n");
    field_t* fe_null = zendoo_commitment_tree_get_commitment(nullptr, &ret_code);
    ASSERT_TRUE(ret_code != CctpErrorCode::OK);
    ASSERT_TRUE(fe_null == nullptr);

    printf("\nChecking initial commitment tree ...\n");
    field_t* fe0 = zendoo_commitment_tree_get_commitment(ct, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    ASSERT_TRUE(fe0 != nullptr);

    const CFieldElement& emptyFe = CFieldElement{EMPTY_COMMITMENT_TREE_FIELD};
    wrappedFieldPtr fe_empty_ptr = emptyFe.GetFieldElement();
    ASSERT_TRUE(memcmp(fe_empty_ptr.get(), fe0, Sidechain::SC_FE_SIZE_IN_BYTES) == 0);

    zendoo_serialize_field(fe0, field_bytes, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    printf("ct = [");
    for (int i = 0; i < sizeof(field_bytes); i++)
        printf("%02x", ((unsigned char*)field_bytes)[i]);
    printf("]\n");

    CTransaction tx = CreateDefaultTx();

    const uint256& tx_hash = tx.GetHash();
    BufferWithSize bws_tx_hash(tx_hash.begin(), tx_hash.size());

    printf("tx hash=[%s] ...\n", tx_hash.ToString().c_str());

    uint32_t out_idx = 0;

    for (const CTxScCreationOut& ccout : tx.GetVscCcOut() )
    {
        field_t* scid_fe = (field_t*)ccout.GetScId().begin();
 
        const uint256& pub_key = ccout.address;
        BufferWithSize bws_pk(pub_key.begin(), pub_key.size());
 
        std::unique_ptr<BufferWithSize[]> bws_fe_cfg(nullptr);
        if (!ccout.vFieldElementCertificateFieldConfig.empty())
        {
            bws_fe_cfg.reset(new BufferWithSize(
                (const unsigned char*)&ccout.vFieldElementCertificateFieldConfig[0],
                (size_t)ccout.vFieldElementCertificateFieldConfig.size()
            ));
        }
 
        int bvcfg_size = ccout.vBitVectorCertificateFieldConfig.size(); 
        std::unique_ptr<BitVectorElementsConfig[]> bvcfg(new BitVectorElementsConfig[bvcfg_size]);
        int i = 0;
        for (auto entry: ccout.vBitVectorCertificateFieldConfig)
        {
            bvcfg[i].bit_vector_size_bits     = entry.getBitVectorSizeBits(); 
            bvcfg[i].max_compressed_byte_size = entry.getMaxCompressedSizeBytes(); 
            i++;
        }
 
        std::unique_ptr<BufferWithSize[]> bws_custom_data(nullptr);
        if (!ccout.customData.empty())
        {
            bws_custom_data.reset(new BufferWithSize(
                (unsigned char*)(&ccout.customData[0]),
                ccout.customData.size()
            ));
        }
 
        field_t* constant_fe = nullptr;
        if(ccout.constant.is_initialized())
        {
            constant_fe = ccout.constant->GetFieldElement().get();
        }
            
        BufferWithSize bws_cert_vk(ccout.wCertVk.GetDataBuffer(), ccout.wCertVk.GetDataSize());
 
        BufferWithSize bws_csw_vk(nullptr, 0);
        if(ccout.wCeasedVk.is_initialized())
        {
            bws_csw_vk.data = ccout.wCeasedVk->GetDataBuffer();
            bws_csw_vk.len = ccout.wCeasedVk->GetDataSize();
        }

        printf("Adding a sc creation to the commitment tree ...\n");
        bool ret = zendoo_commitment_tree_add_scc(ct,
             scid_fe, 
             ccout.nValue,
             &bws_pk,
             &bws_tx_hash,
             out_idx,
             ccout.withdrawalEpochLength,
             ccout.mainchainBackwardTransferRequestDataLength,
             bws_fe_cfg.get(),
             bvcfg.get(),
             bvcfg_size,
             ccout.mainchainBackwardTransferRequestScFee, 
             ccout.forwardTransferScFee, 
             bws_custom_data.get(),
             constant_fe, 
             &bws_cert_vk,
             &bws_csw_vk,
             &ret_code
        );
        ASSERT_TRUE(ret == true);
        ASSERT_TRUE(ret_code == CctpErrorCode::OK);
 
        out_idx++;
    }

    printf("\nChecking commitment tree after sc add ...\n");
    field_t* fe1 = zendoo_commitment_tree_get_commitment(ct, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    ASSERT_TRUE(fe1 != nullptr);
    ASSERT_TRUE(memcmp(fe0, fe1, Sidechain::SC_FE_SIZE_IN_BYTES) != 0);

    zendoo_serialize_field(fe1, field_bytes, &ret_code);
        ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    printf("ct = [");
    for (int i = 0; i < sizeof(field_bytes); i++)
        printf("%02x", ((unsigned char*)field_bytes)[i]);
    printf("]\n");

    for (const CTxForwardTransferOut& ccout : tx.GetVftCcOut() )
    {
        field_t* scid_fe = (field_t*)ccout.GetScId().begin();

        const uint256& fwt_pub_key = ccout.address;
        BufferWithSize bws_fwt_pk((unsigned char*)fwt_pub_key.begin(), fwt_pub_key.size());

        printf("Adding a fwt to the commitment tree ...\n");
        bool ret = zendoo_commitment_tree_add_fwt(ct,
             scid_fe,
             ccout.nValue,
             &bws_fwt_pk,
             &bws_tx_hash,
             out_idx,
             &ret_code
        );
        ASSERT_TRUE(ret == true);
        ASSERT_TRUE(ret_code == CctpErrorCode::OK);
 
        out_idx++;
    }

    printf("\nChecking commitment tree after fwt add ...\n");
    field_t* fe2 = zendoo_commitment_tree_get_commitment(ct, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    ASSERT_TRUE(fe2 != nullptr);
    ASSERT_TRUE(memcmp(fe1, fe2, Sidechain::SC_FE_SIZE_IN_BYTES) != 0);

    zendoo_serialize_field(fe2, field_bytes, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    printf("ct = [");
    for (int i = 0; i < sizeof(field_bytes); i++)
        printf("%02x", ((unsigned char*)field_bytes)[i]);
    printf("]\n");

    for (const CBwtRequestOut& ccout : tx.GetVBwtRequestOut() )
    {
        field_t* scid_fe = (field_t*)ccout.GetScId().begin();
 
        int sc_req_data_len = ccout.vScRequestData.size(); 
        std::unique_ptr<const field_t*[]> sc_req_data(new const field_t*[sc_req_data_len]);
        int i = 0;
        for (auto entry: ccout.vScRequestData)
        {
            sc_req_data[i] = entry.GetFieldElement().get();
            i++;
        }

        const uint160& bwtr_pk_hash = ccout.mcDestinationAddress;
        BufferWithSize bws_bwtr_pk_hash(bwtr_pk_hash.begin(), bwtr_pk_hash.size());

        printf("Negative: adding a bwtr with swapped args to the commitment tree: expecting failure ...\n");
        bool ret = zendoo_commitment_tree_add_bwtr(ct,
            scid_fe,
            ccout.scFee,
            sc_req_data.get(),
            sc_req_data_len,
            &bws_tx_hash, // swapped
            &bws_bwtr_pk_hash, // swapped
            out_idx,
            &ret_code
        );
        ASSERT_FALSE(ret == true);
        ASSERT_TRUE(ret_code != CctpErrorCode::OK);
 
        printf("Adding a bwtr to the commitment tree ...\n");
        ret = zendoo_commitment_tree_add_bwtr(ct,
            scid_fe,
            ccout.scFee,
            sc_req_data.get(),
            sc_req_data_len,
            &bws_bwtr_pk_hash,
            &bws_tx_hash,
            out_idx,
            &ret_code
        );
        ASSERT_TRUE(ret == true);
        ASSERT_TRUE(ret_code == CctpErrorCode::OK);
 
        out_idx++;
    }

    printf("\nChecking commitment tree after bwtr add ...\n");
    field_t* fe3 = zendoo_commitment_tree_get_commitment(ct, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    ASSERT_TRUE(fe3 != nullptr);
    ASSERT_TRUE(memcmp(fe2, fe3, Sidechain::SC_FE_SIZE_IN_BYTES) != 0);

    zendoo_serialize_field(fe3, field_bytes, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    printf("ct = [");
    for (int i = 0; i < sizeof(field_bytes); i++)
        printf("%02x", ((unsigned char*)field_bytes)[i]);
    printf("]\n");

    for (const CTxCeasedSidechainWithdrawalInput& ccin : tx.GetVcswCcIn() )
    {
        field_t* scid_fe = (field_t*)ccin.scId.begin();

        const uint160& csw_pk_hash = ccin.pubKeyHash;
        BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());

        printf("Adding a csw to the commitment tree ...\n");
        bool ret = zendoo_commitment_tree_add_csw(ct,
             scid_fe,
             ccin.nValue,
             ccin.nullifier.GetFieldElement().get(),
             &bws_csw_pk_hash,
             &ret_code
        );
        ASSERT_TRUE(ret == true);
        ASSERT_TRUE(ret_code == CctpErrorCode::OK);
 
        out_idx++;
    }

    printf("\nChecking commitment tree after csw add ...\n");
    field_t* fe4 = zendoo_commitment_tree_get_commitment(ct, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    ASSERT_TRUE(fe4 != nullptr);
    ASSERT_TRUE(memcmp(fe3, fe4, Sidechain::SC_FE_SIZE_IN_BYTES) != 0);

    zendoo_serialize_field(fe4, field_bytes, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    printf("ct = [");
    for (int i = 0; i < sizeof(field_bytes); i++)
        printf("%02x", ((unsigned char*)field_bytes)[i]);
    printf("]\n");

    CScCertificate cert = CreateDefaultCert();

    printf("Adding a cert to the commitment tree ...\n");
    field_t* scid_fe = (field_t*)cert.GetScId().begin();
    int epoch_number = cert.epochNumber;
    int quality      = cert.quality;

    const backward_transfer_t* bt_list =  nullptr;
    std::vector<backward_transfer_t> vbt_list;
    for(int pos = cert.nFirstBwtPos; pos < cert.GetVout().size(); ++pos)
    {
        const CTxOut& out = cert.GetVout()[pos];
        const auto& bto = CBackwardTransferOut(out);
        backward_transfer_t x;
        x.amount = bto.nValue;
        memcpy(x.pk_dest, bto.pubKeyHash.begin(), sizeof(x.pk_dest));
        vbt_list.push_back(x);
    }

    if (!vbt_list.empty())
        bt_list = (const backward_transfer_t*)&vbt_list[0];

    size_t bt_list_len = vbt_list.size();

    int custom_fields_len = cert.vFieldElementCertificateField.size(); 
    std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
    int i = 0;
    for (auto entry: cert.vFieldElementCertificateField)
    {
        CFieldElement fe{entry.getVRawData()};
        custom_fields[i] = fe.GetFieldElement().get();
        i++;
    }

    bool ret = zendoo_commitment_tree_add_cert(ct,
         scid_fe,
         epoch_number,
         quality,
         bt_list,
         bt_list_len,
         custom_fields.get(),
         custom_fields_len,
         cert.endEpochCumScTxCommTreeRoot.GetFieldElement().get(),
         cert.forwardTransferScFee,
         cert.mainchainBackwardTransferRequestScFee,
         &ret_code
    );
    ASSERT_TRUE(ret == true);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);

    printf("\nChecking commitment tree after cert add ...\n");
    field_t* fe5 = zendoo_commitment_tree_get_commitment(ct, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    ASSERT_TRUE(fe5 != nullptr);
    ASSERT_TRUE(memcmp(fe4, fe5, Sidechain::SC_FE_SIZE_IN_BYTES) != 0);

    zendoo_serialize_field(fe5, field_bytes, &ret_code);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    printf("ct = [");
    for (int i = 0; i < sizeof(field_bytes); i++)
        printf("%02x", ((unsigned char*)field_bytes)[i]);
    printf("]\n");

    printf("Deleting a nullptr commitment tree ...\n");
    zendoo_commitment_tree_delete(nullptr);

    printf("Deleting the commitment tree ...\n");
    zendoo_commitment_tree_delete(ct);

    zendoo_field_free(fe0);
    zendoo_field_free(fe1);
    zendoo_field_free(fe2);
    zendoo_field_free(fe3);
    zendoo_field_free(fe4);
    zendoo_field_free(fe5);
}

TEST(CctpLibrary, CommitmentTreeBuilding_Negative)
{
    printf("Creating a commitment tree ...\n");
    CctpErrorCode ret_code = CctpErrorCode::OK;

    commitment_tree_t* ct = zendoo_commitment_tree_create();
    ASSERT_TRUE(ct != nullptr);

    unsigned char field_bytes[CFieldElement::ByteSize()] = {};

    CTransaction tx = CreateDefaultTx();

    const uint256& tx_hash = tx.GetHash();
    BufferWithSize bws_tx_hash(tx_hash.begin(), tx_hash.size());

    uint32_t out_idx = 0;

    for (const CTxScCreationOut& ccout : tx.GetVscCcOut() )
    {
        const uint256& scId = ccout.GetScId();
        field_t* scid_fe = (field_t*)scId.begin();

        CAmount crAmount = ccout.nValue;

        const uint256& pub_key = ccout.address;
        BufferWithSize bws_pk(pub_key.begin(), pub_key.size());

        uint32_t epoch_len = ccout.withdrawalEpochLength;
        uint8_t  mbtr_len  = ccout.mainchainBackwardTransferRequestDataLength;

        BufferWithSize bws_fe_cfg(
            (const unsigned char*)&ccout.vFieldElementCertificateFieldConfig[0],
            (size_t)ccout.vFieldElementCertificateFieldConfig.size()
        );

        int bvcfg_size = ccout.vBitVectorCertificateFieldConfig.size(); 
        std::unique_ptr<BitVectorElementsConfig[]> bvcfg(new BitVectorElementsConfig[bvcfg_size]);
        int i = 0;
        for (auto entry: ccout.vBitVectorCertificateFieldConfig)
        {
            bvcfg[i].bit_vector_size_bits     = entry.getBitVectorSizeBits(); 
            bvcfg[i].max_compressed_byte_size = entry.getMaxCompressedSizeBytes(); 
            i++;
        }

        BufferWithSize bws_custom_data(nullptr, 0);
        if (!ccout.customData.empty())
        {
            bws_custom_data.data = (unsigned char*)(&ccout.customData[0]);
            bws_custom_data.len = ccout.customData.size();
        }
 
        field_t* constant_fe = nullptr;
        if(ccout.constant.is_initialized())
        {
            constant_fe = ccout.constant->GetFieldElement().get();
        }
        
        BufferWithSize bws_cert_vk(ccout.wCertVk.GetDataBuffer(), ccout.wCertVk.GetDataSize());
 
        BufferWithSize bws_csw_vk(nullptr, 0);
        if(ccout.wCeasedVk.is_initialized())
        {
            bws_csw_vk.data = ccout.wCeasedVk->GetDataBuffer();
            bws_csw_vk.len = ccout.wCeasedVk->GetDataSize();
        }

        bool ret = false;
 
        printf("Adding a sc creation to the commitment tree - using null ptr obj ...\n");
        ret = zendoo_commitment_tree_add_scc(nullptr, // null ptr obj
             scid_fe,
             crAmount,
             &bws_pk,
             &bws_tx_hash,
             out_idx,
             epoch_len,
             mbtr_len,
             &bws_fe_cfg,
             bvcfg.get(),
             bvcfg_size,
             ccout.mainchainBackwardTransferRequestScFee, 
             ccout.forwardTransferScFee, 
             &bws_custom_data,
             constant_fe, 
             &bws_cert_vk,
             &bws_csw_vk,
             &ret_code
        );
        ASSERT_TRUE(ret == false);
        ASSERT_TRUE(ret_code == CctpErrorCode::NullPtr);
 
        printf("Adding a sc creation to the commitment tree - using null ptr params ...\n");
        BufferWithSize bws_bad(nullptr, sizeof(uint256));
        ret = zendoo_commitment_tree_add_scc(ct,
             scid_fe,
             crAmount,
             &bws_pk,
             &bws_tx_hash,
             out_idx,
             epoch_len,
             mbtr_len,
             &bws_bad, // bad params
             bvcfg.get(),
             bvcfg_size,
             ccout.mainchainBackwardTransferRequestScFee, 
             ccout.forwardTransferScFee, 
             &bws_custom_data,
             constant_fe, 
             &bws_cert_vk,
             &bws_csw_vk,
             &ret_code
        );
        ASSERT_TRUE(ret == false);
        ASSERT_TRUE(ret_code == CctpErrorCode::InvalidBufferData);
 
        printf("Adding a sc creation to the commitment tree - using null ptr buff as a param ...\n");
        ret = zendoo_commitment_tree_add_scc(ct,
             scid_fe,
             crAmount,
             nullptr, // null ptr
             &bws_tx_hash,
             out_idx,
             epoch_len,
             mbtr_len,
             &bws_fe_cfg,
             bvcfg.get(),
             bvcfg_size,
             ccout.mainchainBackwardTransferRequestScFee, 
             ccout.forwardTransferScFee, 
             &bws_custom_data,
             constant_fe, 
             &bws_cert_vk,
             &bws_csw_vk,
             &ret_code
        );
        ASSERT_TRUE(ret == false);
        ASSERT_TRUE(ret_code == CctpErrorCode::NullPtr);
 
        out_idx++;
    }

    printf("Deleting the commitment tree ...\n");
    zendoo_commitment_tree_delete(ct);
}

TEST(CctpLibrary, CommitmentTreeBuilding_Object)
{
    SidechainTxsCommitmentBuilder cmtObj;

    uint256 cmt = cmtObj.getCommitment();

    printf("cmt = [%s]\n", cmt.ToString().c_str());

    CTransaction tx = CreateDefaultTx();

    ASSERT_TRUE(cmtObj.add(tx));

    cmt = cmtObj.getCommitment();
    printf("cmt = [%s]\n", cmt.ToString().c_str());

    CScCertificate cert = CreateDefaultCert();
    ASSERT_TRUE(cmtObj.add(cert));

    cmt = cmtObj.getCommitment();
    printf("cmt = [%s]\n", cmt.ToString().c_str());
}

static unsigned char genericArr[37] = {
    0x3e, 0x61, 0xea, 0xe3, 0x11, 0xa5, 0xe1, 0x1a,
    0x52, 0xdf, 0xb5, 0xe1, 0xc0, 0x06, 0xe1, 0x77,
    0x8a, 0xb8, 0x8d, 0xd3, 0x32, 0x8f, 0xff, 0xe8,
    0x9d, 0xdf, 0xa6, 0xc2, 0x1a, 0xff, 0xe4, 0x33,
    0x6a, 0xf1, 0x36, 0xb2, 0x1b
};

#if 0
static const int NUM_OF_ITER = 1000000;
//static const int NUM_OF_ITER = 10;

TEST(CctpLibrary, GetPoseidonHashFromByteArray)
{
    CctpErrorCode code;
    unsigned char field_bytes[CFieldElement::ByteSize()] = {};

    for (int i = 0; i < NUM_OF_ITER; i++)
    {
        const BufferWithSize bws(genericArr, sizeof(genericArr));
        field_t* fe = zendoo_poseidon_hash(&bws);
        //ASSERT_TRUE(fe != nullptr);
 
        zendoo_serialize_field(fe, field_bytes, &code);

        zendoo_field_free(fe);
    }

    printf("ct = [");
    for (int i = 0; i < sizeof(field_bytes); i++)
        printf("%02x", ((unsigned char*)field_bytes)[i]);
    printf("]\n");
}

TEST(CctpLibrary, GetShaHashFromByteArray)
{
    std::vector<unsigned char> vbuf(genericArr, genericArr + sizeof(genericArr));
    uint256 theHash;

    for (int i = 0; i < NUM_OF_ITER; i++)
    {
        CHashWriter ss(SER_GETHASH, 0);
        ss << vbuf;
        theHash = ss.GetHash();
    }
    printf("hash = [%s]\n", theHash.ToString().c_str());
}
#endif

TEST(CctpLibrary, CheckTypeSize)
{
    // check rust getters are aligned with mc crypto lib header file
    ASSERT_TRUE(Sidechain::SC_FE_SIZE_IN_BYTES == zendoo_get_field_size_in_bytes());
    ASSERT_TRUE(Sidechain::MAX_SC_CUSTOM_DATA_LEN == zendoo_get_sc_custom_data_size_in_bytes());
}

