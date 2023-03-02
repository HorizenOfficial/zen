#include <gtest/gtest.h>

#include <sc/sidechaintypes.h>
#include <primitives/transaction.h>
#include <primitives/certificate.h>
#include <sc/sidechainTxsCommitmentBuilder.h>
#include <sc/sidechainTxsCommitmentGuard.h>
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

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/dynamic_bitset.hpp>
#include <fstream>

extern unsigned char ReverseBitsInByte(unsigned char input);

using namespace blockchain_test_utils;

// Keep them aligned with those defined in CCTPlib!
constexpr int CCTP_COMMITMENT_BUILDER_SC_LIMIT   = 4096;
constexpr int CCTP_COMMITMENT_BUILDER_FT_LIMIT   = 4095;
constexpr int CCTP_COMMITMENT_BUILDER_BWTR_LIMIT = 4095;
constexpr int CCTP_COMMITMENT_BUILDER_CERT_LIMIT = 4095;
constexpr int CCTP_COMMITMENT_BUILDER_CSW_LIMIT  = 4095;

/**
 * @brief Custom field randomly generated from seed 1641809674 using the rand() function.
 * Also the seed has been chosen randomly so that the first byte of the custom field
 * has the most significant bit set (to avoid that unit tests pass due to wrong endianness
 * even when they should not).
 */
const uint8_t TEST_CUSTOM_FIELD[] = {0xbe, 0x61, 0x16, 0xab, 0x27, 0xee, 0xab, 0xbc,
                                     0x09, 0x35, 0xb3, 0xe2, 0x1b, 0xc3, 0xcf, 0xcd,
                                     0x3f, 0x06, 0xac, 0xb3, 0x8a, 0x5c, 0xeb, 0xd4,
                                     0x42, 0xf4, 0x96, 0xd8, 0xbf, 0xd3, 0x8e, 0x7d};

/**
 * @brief Value of the modulus for Tweedle Fr (little endian).
 * It is the field used for the representation of Field Elements, useful for unit tests
 * involving the custom fields validation.
 */
const uint8_t TWEEDLE_FR_MODULUS[] = {0x01, 0x00, 0x00, 0x00, 0xe2, 0x64, 0x40, 0xa1,
                                      0xb9, 0x59, 0x3f, 0x6c, 0x27, 0xa1, 0x8a, 0x03,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40};

static CMutableTransaction CreateDefaultTx(uint8_t sidechainVersion)
{
    // Create a tx with a sc creation, a fwt, a bwtr and a csw

    CMutableTransaction mtx;
    mtx.nVersion = SC_TX_VERSION;
    //---
    mtx.vsc_ccout.resize(1);
    mtx.vsc_ccout[0].version = sidechainVersion;
    mtx.vsc_ccout[0].nValue = CAmount(12000);
    mtx.vsc_ccout[0].withdrawalEpochLength = 150;
    mtx.vsc_ccout[0].wCertVk   = CScVKey{SAMPLE_CERT_DARLIN_VK};
    mtx.vsc_ccout[0].wCeasedVk = CScVKey{SAMPLE_CSW_DARLIN_VK};
    //---
    mtx.vft_ccout.resize(1);
    mtx.vft_ccout[0].scId = uint256S("abababcdcdcd");
    mtx.vft_ccout[0].nValue = CAmount(30000);
    mtx.vft_ccout[0].address = uint256S("abcdef");
    mtx.vft_ccout[0].mcReturnAddress = uint160S("abcdef");
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

static CCertProofVerifierInput CreateDefaultCertInput(bool keyrot, TestCircuitType circuitType = TestCircuitType::Certificate)
{
    CCertProofVerifierInput certInput;

    switch(circuitType)
    {
        // set constant only if required 
        case TestCircuitType::Certificate:
            certInput.constant = CFieldElement(SAMPLE_FIELD);
            break;

        case TestCircuitType::CertificateNoConstant:
        default:
            break;
    }

    certInput.epochNumber = 7;
    certInput.quality = 10;
    certInput.endEpochCumScTxCommTreeRoot = CFieldElement(SAMPLE_FIELD);
    certInput.mainchainBackwardTransferRequestScFee = 1;
    certInput.forwardTransferScFee = 1;
    certInput.lastCertHash = keyrot ? CFieldElement(SAMPLE_FIELD) : CFieldElement();
    return certInput;
}

static CCswProofVerifierInput CreateDefaultCswInput(TestCircuitType circuitType = TestCircuitType::CSW)
{
    CCswProofVerifierInput cswInput;

    switch(circuitType)
    {
        // set constant only if required 
        case TestCircuitType::CSW:
            cswInput.constant = CFieldElement(SAMPLE_FIELD);
            break;

        case TestCircuitType::CSWNoConstant:
        default:
            break;
    }

    cswInput.ceasingCumScTxCommTree = CFieldElement(SAMPLE_FIELD);
    cswInput.certDataHash = CFieldElement(SAMPLE_FIELD);
    cswInput.nValue = CAmount(15);
    cswInput.nullifier = CFieldElement(SAMPLE_FIELD);
    cswInput.scId = uint256S("aaaa");

    return cswInput;
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
    CFieldElement AValidField {SAMPLE_FIELD};

    // internal wrapped ptr is not built yet
    EXPECT_EQ(AValidField.getUseCount(), 0);

    ASSERT_TRUE(AValidField.IsValid());

    // now it is
    EXPECT_EQ(AValidField.getUseCount(), 1);

    {
        wrappedFieldPtr AValidPtr = AValidField.GetFieldElement();
        ASSERT_TRUE(AValidPtr.get() != nullptr);
        EXPECT_EQ(AValidPtr.use_count(), 2);

        { //Scoped to invoke copied obj dtor
            CFieldElement copiedField(AValidField);
            EXPECT_TRUE(copiedField.IsValid());
            EXPECT_TRUE(copiedField == AValidField);
            EXPECT_TRUE(AValidPtr.use_count() == 3);

            wrappedFieldPtr copiedPtr = copiedField.GetFieldElement();
            ASSERT_TRUE(copiedPtr.get() != nullptr);
            ASSERT_TRUE(copiedPtr == AValidPtr);
            ASSERT_TRUE(copiedPtr.get() == AValidPtr.get());
            EXPECT_TRUE(AValidPtr.use_count() == 4);
        }

        EXPECT_TRUE(AValidPtr.use_count() == 2);
        EXPECT_TRUE(AValidField.IsValid()); //NO side effect from copy

        wrappedFieldPtr ptr = AValidField.GetFieldElement();
        ASSERT_TRUE(ptr.get() != nullptr);
        ASSERT_TRUE(ptr == AValidPtr);
        EXPECT_TRUE(AValidPtr.use_count() == 3);

        {   //Scoped to invoke assigned obj dtor
            CFieldElement assignedField{};
            assignedField = AValidField;
            EXPECT_TRUE(assignedField.IsValid());
            EXPECT_TRUE(assignedField == AValidField);
            EXPECT_TRUE(AValidPtr.use_count() == 4);

            wrappedFieldPtr assignedPtr = assignedField.GetFieldElement();
            ASSERT_TRUE(assignedPtr.get() != nullptr);
            ASSERT_TRUE(assignedPtr == AValidPtr);
            EXPECT_TRUE(AValidPtr.use_count() == 5);
        }
        // two objects in the previous scope
        EXPECT_TRUE(AValidPtr.use_count() == 3);
        EXPECT_TRUE(AValidField.IsValid()); //NO side effect from copy

        // reassignment, no side effect also in reference counting
        ptr = AValidField.GetFieldElement();
        ASSERT_TRUE(ptr.get() != nullptr);
        ASSERT_TRUE(ptr == AValidPtr);
        EXPECT_EQ(AValidPtr.use_count(), 3);
    }

    // just the initial instance is left
    EXPECT_EQ(AValidField.getUseCount(), 1);
}

TEST(SidechainsField, CtorWithWrappedPtr)
{
    CFieldElement fe_A {SAMPLE_FIELD};
    EXPECT_EQ(fe_A.getUseCount(), 0);

    {
        wrappedFieldPtr wp_A = fe_A.GetFieldElement();
        EXPECT_EQ(fe_A.getUseCount(), 2);

        {
            CFieldElement fe_B{wp_A};

            EXPECT_EQ(fe_B.GetFieldElement(), wp_A); 

            EXPECT_EQ(fe_A.getUseCount(), 3);
            EXPECT_EQ(fe_B.getUseCount(), 3);

        }
        EXPECT_EQ(fe_A.getUseCount(), 2);
    }
    EXPECT_EQ(fe_A.getUseCount(), 1);

    // using a null wrapped ptr sets a byte array made of 32 null bytes; such a byte array yields
    // a null (but valid) array of bytes as a deserialized field_t
    wrappedFieldPtr wp_null;
    CFieldElement fe_C{wp_null};

    unsigned char nullBuff[Sidechain::SC_FE_SIZE_IN_BYTES] = {};
    ASSERT_TRUE(memcmp(nullBuff, fe_C.GetDataBuffer(), Sidechain::SC_FE_SIZE_IN_BYTES) == 0);

    EXPECT_TRUE(fe_C.IsValid());
    EXPECT_EQ(fe_C.getUseCount(), 1);
    const field_t* const data = fe_C.GetFieldElement().get();
    ASSERT_TRUE(memcmp(data, fe_C.GetDataBuffer(), Sidechain::SC_FE_SIZE_IN_BYTES) == 0);

    // setting byte array resets the wrapped ptr
    fe_C.SetByteArray(SAMPLE_FIELD);
    EXPECT_EQ(fe_C.getUseCount(), 0);
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
    CctpErrorCode code;
    auto tree = ZendooGingerMerkleTree(height, leaves_len, &code);
    ASSERT_TRUE(code == CctpErrorCode::OK);

    // Add leaves to tree
    std::vector<wrappedFieldPtr> vSptr;
    for (int i = 0; i < leaves_len; i++){
        wrappedFieldPtr sptrFe = leaves[i].GetFieldElement();
        tree.append(sptrFe.get(), &code);
        vSptr.push_back(sptrFe);
        ASSERT_TRUE(code == CctpErrorCode::OK);
    }

    // Finalize tree
    tree.finalize_in_place(&code);
    ASSERT_TRUE(code == CctpErrorCode::OK);

    // Compute root and assert equality with expected one
    CFieldElement root = CFieldElement{wrappedFieldPtr{tree.root(&code), CFieldPtrDeleter{}}};
    ASSERT_TRUE(code == CctpErrorCode::OK);
    EXPECT_TRUE(root == expected_root);

    wrappedFieldPtr sptrRoot = root.GetFieldElement();

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
        ASSERT_TRUE(zendoo_verify_ginger_merkle_path(path, height, vSptr.at(i).get(), sptrRoot.get(), &code))
        <<"Merkle Path must be verified";
        ASSERT_TRUE(code == CctpErrorCode::OK);
        zendoo_free_ginger_merkle_path(path);
    }
}

TEST(SidechainsField, NakedZendooFeatures_TreeCommitmentCalculation)
{
    //fPrintToConsole = true;

    //Add txes containing scCreation and fwd transfer + a certificate
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10), 10);

    CMutableTransaction mutTx = scCreationTx;

    auto ccout = CTxScCreationOut(CAmount(10), uint256S("aaa"), CAmount(0), CAmount(0), Sidechain::ScFixedParameters());
    // set mandatory/legal params
    ccout.version = 0;
    ccout.withdrawalEpochLength = 11;
    ccout.wCertVk   = CScVKey{SAMPLE_CERT_DARLIN_VK};
    ccout.wCeasedVk = CScVKey{SAMPLE_CSW_DARLIN_VK};
    ccout.vFieldElementCertificateFieldConfig.push_back(44);
    ccout.customData.push_back(0x77);

    mutTx.vsc_ccout.push_back(ccout);
    mutTx.vft_ccout.push_back(CTxForwardTransferOut(uint256S("bbb"), CAmount(1985), uint256S("badcafe"), uint160S("badcafe")));
    scCreationTx = mutTx;

    uint256 scId = scCreationTx.GetScIdFromScCcOut(0);
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, CAmount(7));

    CScCertificate cert = txCreationUtils::createCertificate(scId,
        /*epochNum*/12, CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/0,
        /*numChangeOut */0, /*bwtTotalAmount*/1, /*numBwt*/1, /*ftScFee*/0, /*mbtrScFee*/0);

    SidechainTxsCommitmentBuilder builder;

    SelectParams(CBaseChainParams::REGTEST);
    const BlockchainTestManager& testManager = BlockchainTestManager::GetInstance();

    ASSERT_TRUE(builder.add(scCreationTx));
    ASSERT_TRUE(builder.add(fwdTx));
    ASSERT_TRUE(builder.add(cert, testManager.CoinsViewCache().get()));

    uint256 scTxCommitmentHash = builder.getCommitment();

    EXPECT_TRUE(scTxCommitmentHash == uint256S("27363f1d4073deecf57dd951362912d5b1d49eb3271026be092a165f29f1975e"))
        <<scTxCommitmentHash.ToString();
}

TEST(SidechainsField, NakedZendooFeatures_EmptyTreeCommitmentCalculation)
{
    //fPrintToConsole = true;
    SidechainTxsCommitmentBuilder builder;

    const CFieldElement& emptyFe = CFieldElement{EMPTY_COMMITMENT_TREE_FIELD};
    uint256 emptySha = emptyFe.GetLegacyHash();
    //Nothing to add

    uint256 scTxCommitmentHash = builder.getCommitment();
    EXPECT_TRUE(SidechainTxsCommitmentBuilder::getEmptyCommitment() == emptySha);
    EXPECT_TRUE(scTxCommitmentHash == emptySha) <<scTxCommitmentHash.ToString() << "\n" << emptySha.ToString();
}

/**
 * @brief This test checks the computation of the commitment tree of a known block.
 * The block is read from a raw byte string stored in a file.
 * This same check is performed on the SDK side to spot any possible difference.
 * 
 * Please, see the unit test blockWithCertificateWithCustomFieldAndBitvectorMixedScVersions
 * on the SDK side for further details and the Python test sc_getscgenesisinfo.py for the
 * generation of the test block.
 * 
 * https://github.com/HorizenOfficial/Sidechains-SDK/commit/460ad1f0984e96925e20be8e6c434d2acda439d4
 */
TEST(SidechainsField, CommitmentComputationFromSerializedBlock)
{
    SelectParams(CBaseChainParams::REGTEST);
    const BlockchainTestManager& testManager = BlockchainTestManager::GetInstance();

    // Read hex string from file
    boost::filesystem::path blockPath = boost::dll::program_location().parent_path() / "/gtest/test-data/block_with_v0_and_v1_certificates.hex";
    std::ifstream t(blockPath.string());
    std::stringstream buffer;
    buffer << t.rdbuf();
    std::string blockData = buffer.str();

    // Convert hex string to byte array
    std::vector<unsigned char> blockDataBytes = ParseHex(blockData);

    // The first raw byte must contain the block version 
    assert(blockDataBytes.front() == BLOCK_VERSION_SC_SUPPORT);

    // Create the data stream from the byte array
    CDataStream dsBlock(SER_DISK, PROTOCOL_VERSION);
    dsBlock << blockDataBytes;

    // Since we are filling the datastream with a byte array (and not an object),
    // the first bytes contain the size of the array itself.
    dsBlock.erase(dsBlock.begin(), dsBlock.begin() + 3);

    // Deserialize the block
    CBlock block;
    dsBlock >> block;

    // Add the information about the sidechains publishing a certificate to the CoinsViewCache
    for (CScCertificate cert : block.vcert)
    {
        CSidechain sidechain;
        uint256 sidechainId = cert.GetScId();

        // 255 bit is the fixed field element configuration used at block generation
        FieldElementCertificateFieldConfig fieldConfig = {255};
        sidechain.fixedParams.vFieldElementCertificateFieldConfig.push_back(fieldConfig);
        sidechain.fixedParams.vFieldElementCertificateFieldConfig.push_back(fieldConfig);

        // (254*4, 151) is the fixed bitvector configuration used at block generation
        BitVectorCertificateFieldConfig bitvectorConfig = {254*4, 151};
        sidechain.fixedParams.vBitVectorCertificateFieldConfig.push_back(bitvectorConfig);

        testManager.StoreSidechainWithCurrentHeight(sidechainId, sidechain, 0);
    }

    // Check the commitment tree
    uint256 scTxCommitmentHash;
    scTxCommitmentHash.SetNull();
    block.BuildScTxsCommitment(testManager.CoinsViewCache().get(), scTxCommitmentHash);

    EXPECT_TRUE(scTxCommitmentHash == uint256S("2a94ae04f2dcb25b274510a4611e1443b088ed2eac9211535105b35cfbd1c543"))
        << scTxCommitmentHash.ToString();
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

    zendoo_free_bws(bws_ret);
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
    zendoo_free_bws(bws_ret1);
    zendoo_free_bws(bws_ret2);
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
    zendoo_free_bws(bws_ret1);
    zendoo_free_bws(bws_ret2);
}

TEST(CctpLibrary, BitVectorMerkleTree)
{
    CctpErrorCode ret_code = CctpErrorCode::OK;

// cctp does an assert(false) if  the size of buffer is too short
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
    zendoo_free_bws(bws_ret1);
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

    // this is the resulting field element
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

    // this is the vector with the raw data which are serialized
    printf("\nCompressed data size = %lu ...\n", bws_ret->len);
    printf("\ncompressed data = [");
    for (int i = 0; i < bws_ret->len; i++)
    {
        printf("%02x", ((const unsigned char*)bws_ret->data)[i]);
    }
    printf("]\n");

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
    zendoo_free_bws(bws_ret);
    zendoo_field_free(fe);
}

TEST(CctpLibrary, BitVectorCertificateFieldNull)
{
    const BitVectorCertificateFieldConfig cfg(1024, 2048);
    BitVectorCertificateField bvField;

    for (uint8_t sidechainVersion = 0; sidechainVersion <= ForkManager::getInstance().getHighestFork()->getMaxSidechainVersion(); sidechainVersion++)
    {
        const CFieldElement& fe = bvField.GetFieldElement(cfg, sidechainVersion);
        EXPECT_FALSE(fe.IsValid());
    }
}

TEST(CctpLibrary, BitVectorCertificateFieldUnsuppComprAlgo)
{
    // unsupported compression algo (header bytes in compressed buffer report used algo)
    const std::vector<unsigned char> bvVec(1024, 0xcc);

    const BitVectorCertificateFieldConfig cfg(1024, 2048);
    BitVectorCertificateField bvField(bvVec);

    for (uint8_t sidechainVersion = 0; sidechainVersion <= ForkManager::getInstance().getHighestFork()->getMaxSidechainVersion(); sidechainVersion++)
    {
        const CFieldElement& fe = bvField.GetFieldElement(cfg, sidechainVersion);
        EXPECT_FALSE(fe.IsValid());
    }
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

    for (uint8_t sidechainVersion = 0; sidechainVersion <= ForkManager::getInstance().getHighestFork()->getMaxSidechainVersion(); sidechainVersion++)
    {
        const CFieldElement& fe = bvField.GetFieldElement(cfg, sidechainVersion);
        EXPECT_FALSE(fe.IsValid());
    }

    zendoo_free_bws(bws_ret1);
}

TEST(CctpLibrary, BitVectorCertificateFieldFullGzip)
{
    CctpErrorCode ret_code = CctpErrorCode::OK;
    srand(time(NULL));

    // uncompressed buffer size, use the max size
    // currently if a value not consistent with field element splitting is used, cctp does an assert(false)
    static const int SC_BV_SIZE_IN_BYTES = BitVectorCertificateFieldConfig::MAX_BIT_VECTOR_SIZE_BITS/8;

    unsigned char* buffer = new unsigned char[SC_BV_SIZE_IN_BYTES];
    for(size_t i = 0; i < SC_BV_SIZE_IN_BYTES; i++)
        buffer[i] = rand() % 256;

    printf("Uncompressed buffer size %d ...\n", SC_BV_SIZE_IN_BYTES);

    CompressionAlgorithm e = CompressionAlgorithm::Gzip;

    BufferWithSize bws_in(buffer, SC_BV_SIZE_IN_BYTES);

    printf("Compressing using gzip...");
    BufferWithSize* bws_ret1 = nullptr;
    bws_ret1 = zendoo_compress_bit_vector(&bws_in, e, &ret_code);
    ASSERT_TRUE(bws_ret1 != nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    printf(" ===> Compressed size %lu\n", bws_ret1->len);

    const std::vector<unsigned char> bvVec(bws_ret1->data, bws_ret1->data + bws_ret1->len);

    int bitVectorSizeBits = SC_BV_SIZE_IN_BYTES*8; // the original size of the buffer
    int maxCompressedSizeBytes = bvVec.size(); // take the compressed data buf as max value 

    // check that we are below the defined limit, which include a small overhed for very sparse bit vectors
    ASSERT_TRUE(maxCompressedSizeBytes < BitVectorCertificateFieldConfig::MAX_COMPRESSED_SIZE_BYTES);

    const BitVectorCertificateFieldConfig cfg(bitVectorSizeBits, maxCompressedSizeBytes);
    BitVectorCertificateField bvField(bvVec);

    for (uint8_t sidechainVersion = 0; sidechainVersion <= ForkManager::getInstance().getHighestFork()->getMaxSidechainVersion(); sidechainVersion++)
    {
        const CFieldElement& fe = bvField.GetFieldElement(cfg, sidechainVersion);
        EXPECT_TRUE(fe.IsValid());
    }

    zendoo_free_bws(bws_ret1);
    delete [] buffer;
}

TEST(CctpLibrary, BitVectorCertificateFieldFullBzip2)
{
    CctpErrorCode ret_code = CctpErrorCode::OK;
    srand(time(NULL));

    // uncompressed buffer size, use the max size
    // currently if a value not consistent with field element splitting is used, cctp does an assert(false)
    static const int SC_BV_SIZE_IN_BYTES = BitVectorCertificateFieldConfig::MAX_BIT_VECTOR_SIZE_BITS/8;

    unsigned char* buffer = new unsigned char[SC_BV_SIZE_IN_BYTES];
    for(size_t i = 0; i < SC_BV_SIZE_IN_BYTES; i++)
        buffer[i] = rand() % 256;

    printf("Uncompressed buffer size %d ...\n", SC_BV_SIZE_IN_BYTES);

    CompressionAlgorithm e = CompressionAlgorithm::Bzip2;

    BufferWithSize bws_in(buffer, SC_BV_SIZE_IN_BYTES);

    printf("Compressing using bzip2...");
    BufferWithSize* bws_ret1 = nullptr;
    bws_ret1 = zendoo_compress_bit_vector(&bws_in, e, &ret_code);
    ASSERT_TRUE(bws_ret1 != nullptr);
    ASSERT_TRUE(ret_code == CctpErrorCode::OK);
    printf(" ===> Compressed size %lu\n", bws_ret1->len);

    const std::vector<unsigned char> bvVec(bws_ret1->data, bws_ret1->data + bws_ret1->len);

    int bitVectorSizeBits = SC_BV_SIZE_IN_BYTES*8; // the original size of the buffer
    int maxCompressedSizeBytes = bvVec.size(); // take the compressed data buf as max value 

    // check that we are below the defined limit, which include a small overhed for very sparse bit vectors
    ASSERT_TRUE(maxCompressedSizeBytes < BitVectorCertificateFieldConfig::MAX_COMPRESSED_SIZE_BYTES);

    const BitVectorCertificateFieldConfig cfg(bitVectorSizeBits, maxCompressedSizeBytes);
    BitVectorCertificateField bvField(bvVec);

    for (uint8_t sidechainVersion = 0; sidechainVersion <= ForkManager::getInstance().getHighestFork()->getMaxSidechainVersion(); sidechainVersion++)
    {
        const CFieldElement& fe = bvField.GetFieldElement(cfg, sidechainVersion);
        EXPECT_TRUE(fe.IsValid());
    }

    zendoo_free_bws(bws_ret1);
    delete [] buffer;
}

TEST(CctpLibrary, CommitmentTreeBuilding)
{
    for (uint8_t sidechainVersion = 0; sidechainVersion <= ForkManager::getInstance().getHighestFork()->getMaxSidechainVersion(); sidechainVersion++)
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

        CTransaction tx = CreateDefaultTx(sidechainVersion);

        const uint256& tx_hash = tx.GetHash();
        BufferWithSize bws_tx_hash(tx_hash.begin(), tx_hash.size());

        printf("tx hash=[%s] ...\n", tx_hash.ToString().c_str());

        uint32_t out_idx = 0;

        for (const CTxScCreationOut& ccout : tx.GetVscCcOut() )
        {
            wrappedFieldPtr sptrScId = CFieldElement(ccout.GetScId()).GetFieldElement();
            field_t* scid_fe = sptrScId.get();

            const uint256& pub_key = ccout.address;
            BufferWithSize bws_pk(pub_key.begin(), pub_key.size());
    
            std::unique_ptr<BufferWithSize> bws_fe_cfg(nullptr);
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
            // mc crypto lib wants a null ptr if we have no fields
            if (bvcfg_size == 0)
                bvcfg.reset();
    
            std::unique_ptr<BufferWithSize> bws_custom_data(nullptr);
            if (!ccout.customData.empty())
            {
                bws_custom_data.reset(new BufferWithSize(
                    (unsigned char*)(&ccout.customData[0]),
                    ccout.customData.size()
                ));
            }
    
            wrappedFieldPtr sptrConstant(nullptr);
            if(ccout.constant.is_initialized())
            {
                sptrConstant = ccout.constant->GetFieldElement();
            }
            field_t* constant_fe = sptrConstant.get();
                
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
            wrappedFieldPtr sptrScId = CFieldElement(ccout.GetScId()).GetFieldElement();
            field_t* scid_fe = sptrScId.get();

            const uint256& fwt_pub_key = ccout.address;
            BufferWithSize bws_fwt_pk((unsigned char*)fwt_pub_key.begin(), fwt_pub_key.size());

            const uint160& fwt_mc_return_address = ccout.mcReturnAddress;
            BufferWithSize bws_fwt_return_address((unsigned char*)fwt_mc_return_address.begin(), fwt_mc_return_address.size());

            printf("Adding a fwt to the commitment tree ...\n");
            bool ret = zendoo_commitment_tree_add_fwt(ct,
                scid_fe,
                ccout.nValue,
                &bws_fwt_pk,
                &bws_fwt_return_address,
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
            wrappedFieldPtr sptrScId = CFieldElement(ccout.GetScId()).GetFieldElement();
            field_t* scid_fe = sptrScId.get();

            int sc_req_data_len = ccout.vScRequestData.size(); 
            std::unique_ptr<const field_t*[]> sc_req_data(new const field_t*[sc_req_data_len]);
            int i = 0;
            std::vector<wrappedFieldPtr> vSptr;
            for (auto entry: ccout.vScRequestData)
            {
                wrappedFieldPtr sptrFe = entry.GetFieldElement();
                sc_req_data[i] = sptrFe.get();
                vSptr.push_back(sptrFe);
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
            wrappedFieldPtr sptrScId = CFieldElement(ccin.scId).GetFieldElement();
            field_t* scid_fe = sptrScId.get();

            const uint160& csw_pk_hash = ccin.pubKeyHash;
            BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());

            wrappedFieldPtr sptrNullifier = ccin.nullifier.GetFieldElement();

            printf("Adding a csw to the commitment tree ...\n");
            bool ret = zendoo_commitment_tree_add_csw(ct,
                scid_fe,
                ccin.nValue,
                sptrNullifier.get(),
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
        wrappedFieldPtr sptrScId = CFieldElement(cert.GetScId()).GetFieldElement();
        field_t* scid_fe = sptrScId.get();
    
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
            bt_list = (const backward_transfer_t*)vbt_list.data();

        size_t bt_list_len = vbt_list.size();

        int custom_fields_len = cert.vFieldElementCertificateField.size() + cert.vBitVectorCertificateField.size(); 

        FieldElementCertificateFieldConfig fieldConfig;
        BitVectorCertificateFieldConfig bitVectorConfig;

        std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
        int i = 0;
        std::vector<wrappedFieldPtr> vSptr;
        for (auto entry: cert.vFieldElementCertificateField)
        {
            CFieldElement fe{entry.GetFieldElement(fieldConfig, sidechainVersion)};
            assert(fe.IsValid());
            wrappedFieldPtr sptrFe = fe.GetFieldElement();
            custom_fields[i] = sptrFe.get();
            vSptr.push_back(sptrFe);
            i++;
        }

        int j = 0;
        for (auto entry: cert.vBitVectorCertificateField)
        {
            CFieldElement fe{entry.GetFieldElement(bitVectorConfig, sidechainVersion)};
            assert(fe.IsValid());
            wrappedFieldPtr sptrFe = fe.GetFieldElement();
            custom_fields[i+j] = sptrFe.get();
            vSptr.push_back(sptrFe);
            j++;
        }

        // mc crypto lib wants a null ptr if we have no fields
        if (custom_fields_len == 0)
        {
            custom_fields.reset();
            ASSERT_EQ(custom_fields.get(), nullptr);
        }

        wrappedFieldPtr sptrCum = cert.endEpochCumScTxCommTreeRoot.GetFieldElement();

        bool ret = zendoo_commitment_tree_add_cert(ct,
            scid_fe,
            epoch_number,
            quality,
            bt_list,
            bt_list_len,
            custom_fields.get(),
            custom_fields_len,
            sptrCum.get(),
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
}

TEST(CctpLibrary, CommitmentTreeBuilding_Negative)
{
    for (uint8_t sidechainVersion = 0; sidechainVersion <= ForkManager::getInstance().getHighestFork()->getMaxSidechainVersion(); sidechainVersion++)
    {
        printf("Creating a commitment tree ...\n");
        CctpErrorCode ret_code = CctpErrorCode::OK;

        commitment_tree_t* ct = zendoo_commitment_tree_create();
        ASSERT_TRUE(ct != nullptr);

        unsigned char field_bytes[CFieldElement::ByteSize()] = {};

        CTransaction tx = CreateDefaultTx(sidechainVersion);

        const uint256& tx_hash = tx.GetHash();
        BufferWithSize bws_tx_hash(tx_hash.begin(), tx_hash.size());

        uint32_t out_idx = 0;

        for (const CTxScCreationOut& ccout : tx.GetVscCcOut() )
        {
            wrappedFieldPtr sptrScId = CFieldElement(ccout.GetScId()).GetFieldElement();
            field_t* scid_fe = sptrScId.get();

            CAmount crAmount = ccout.nValue;

            const uint256& pub_key = ccout.address;
            BufferWithSize bws_pk(pub_key.begin(), pub_key.size());

            uint32_t epoch_len = ccout.withdrawalEpochLength;
            uint8_t  mbtr_len  = ccout.mainchainBackwardTransferRequestDataLength;

            std::unique_ptr<BufferWithSize> bws_fe_cfg(nullptr);
            std::unique_ptr<uint8_t[]> dum(nullptr);
            size_t l = ccout.vFieldElementCertificateFieldConfig.size();
            if (l > 0)
            {
                dum.reset(new uint8_t[l]);
                for (int i = 0; i < l; i++)
                    dum[i] = ccout.vFieldElementCertificateFieldConfig[i].getBitSize();
    
                bws_fe_cfg.reset(new BufferWithSize(dum.get(), l));
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
            // mc crypto lib wants a null ptr if we have no fields
            if (bvcfg_size == 0)
                bvcfg.reset();

            std::unique_ptr<BufferWithSize> bws_custom_data(nullptr);
            if (!ccout.customData.empty())
            {
                bws_custom_data.reset(new BufferWithSize(
                    (unsigned char*)(&ccout.customData[0]),
                    ccout.customData.size()
                ));
            }
    
            wrappedFieldPtr sptrConstant(nullptr);
            if(ccout.constant.is_initialized())
            {
                sptrConstant = ccout.constant->GetFieldElement();
            }
            field_t* constant_fe = sptrConstant.get();
            
            BufferWithSize bws_cert_vk(ccout.wCertVk.GetDataBuffer(), ccout.wCertVk.GetDataSize());
    
            std::unique_ptr<BufferWithSize> bws_csw_vk(nullptr);
            if(ccout.wCeasedVk.is_initialized())
            {
                bws_csw_vk.reset(new BufferWithSize(
                    ccout.wCeasedVk->GetDataBuffer(),
                    ccout.wCeasedVk->GetDataSize()
                ));
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
                bws_fe_cfg.get(),
                bvcfg.get(),
                bvcfg_size,
                ccout.mainchainBackwardTransferRequestScFee, 
                ccout.forwardTransferScFee, 
                bws_custom_data.get(),
                constant_fe, 
                &bws_cert_vk,
                bws_csw_vk.get(),
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
                bws_custom_data.get(),
                constant_fe, 
                &bws_cert_vk,
                bws_csw_vk.get(),
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
                bws_fe_cfg.get(),
                bvcfg.get(),
                bvcfg_size,
                ccout.mainchainBackwardTransferRequestScFee, 
                ccout.forwardTransferScFee, 
                bws_custom_data.get(),
                constant_fe, 
                &bws_cert_vk,
                bws_csw_vk.get(),
                &ret_code
            );
            ASSERT_TRUE(ret == false);
            ASSERT_TRUE(ret_code == CctpErrorCode::NullPtr);
    
            out_idx++;
        }

        printf("Deleting the commitment tree ...\n");
        zendoo_commitment_tree_delete(ct);
    }
}

TEST(CctpLibrary, CommitmentTreeBuilding_Object)
{
    for (uint8_t sidechainVersion = 0; sidechainVersion <= ForkManager::getInstance().getHighestFork()->getMaxSidechainVersion(); sidechainVersion++)
    {
        SidechainTxsCommitmentBuilder cmtObj;

        uint256 cmt = cmtObj.getCommitment();

        printf("cmt = [%s]\n", cmt.ToString().c_str());

        CTransaction tx = CreateDefaultTx(sidechainVersion);

        ASSERT_TRUE(cmtObj.add(tx));

        cmt = cmtObj.getCommitment();
        printf("cmt = [%s]\n", cmt.ToString().c_str());

        CScCertificate cert = CreateDefaultCert();

        SelectParams(CBaseChainParams::REGTEST);
        const BlockchainTestManager& testManager = BlockchainTestManager::GetInstance();

        ASSERT_TRUE(cmtObj.add(cert, testManager.CoinsViewCache().get()));

        cmt = cmtObj.getCommitment();
        printf("cmt = [%s]\n", cmt.ToString().c_str());
    }
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

TEST(CctpLibrary, GetScIdFromNullInputs)
{
    unsigned char expected_res[CFieldElement::ByteSize()] = {
        0xe5, 0x89, 0x89, 0x23, 0xc5, 0x50, 0x1d, 0xbe,
        0xcd, 0x48, 0x45, 0x65, 0x55, 0xcf, 0x92, 0x25,
        0xaa, 0x44, 0xbf, 0x3a, 0x4e, 0x84, 0xbc, 0x20,
        0xec, 0x06, 0x9b, 0x4a, 0x4d, 0xcf, 0x97, 0x2a
    };

    // all zeroes array
    uint256 nullTxId;

    // null as well
    int pos = 0;

    CctpErrorCode code;
    const BufferWithSize bws_tx_hash(nullTxId.begin(), nullTxId.size());

    field_t* scid_fe = zendoo_compute_sc_id(&bws_tx_hash, pos, &code); 

    ASSERT_TRUE(code == CctpErrorCode::OK);
    ASSERT_TRUE(scid_fe != nullptr);

    unsigned char serialized_buffer[CFieldElement::ByteSize()] = {};
    zendoo_serialize_field(scid_fe, serialized_buffer, &code);
    ASSERT_TRUE(code == CctpErrorCode::OK);

    unsigned char* ptr = (unsigned char*)scid_fe;
    printf("           scid_fe = [");
    for (int i = 0; i < CFieldElement::ByteSize(); i++)
        printf("%02x", *ptr++);
    printf("]\n");

    ptr = serialized_buffer;
    printf("serialized_scid_fe = [");
    for (int i = 0; i < CFieldElement::ByteSize(); i++)
    {
        printf("%02x", *ptr);
        ASSERT_TRUE(expected_res[i] == *ptr);
        ptr++;
    }
    printf("]\n");

    const std::vector<unsigned char> tmp((uint8_t*)serialized_buffer, (uint8_t*)serialized_buffer + Sidechain::SC_FE_SIZE_IN_BYTES);
    uint256 scid(tmp);

    CFieldElement fe(scid);
    auto sptrScId = fe.GetFieldElement();
    field_t* fe_ptr = sptrScId.get();
    
    printf("        uint256 fe = [");
    ptr = (unsigned char*)fe_ptr;
    for (int i = 0; i < CFieldElement::ByteSize(); i++)
    {
        printf("%02x", *ptr);
        ptr++;
    }

    ASSERT_TRUE(0 == memcmp(scid_fe, fe_ptr, CFieldElement::ByteSize()));
    zendoo_field_free(scid_fe);
}

void CreateAndVerifyCert(ProvingSystem ps, TestCircuitType tc, bool keyrot)
{
    SelectParams(CBaseChainParams::REGTEST);
    BlockchainTestManager& testManager = BlockchainTestManager::GetInstance();
    testManager.GenerateSidechainTestParameters(ps, tc, keyrot);

    std::cout << "Temp folder for proof verification test: " << testManager.TempFolderPath() << std::endl;

    CCertProofVerifierInput certInput = CreateDefaultCertInput(keyrot, tc);
    certInput.verificationKey = testManager.GetTestVerificationKey(ps, tc);
    certInput.proof = testManager.GenerateTestCertificateProof(certInput, ps, tc);

    ASSERT_TRUE(testManager.VerifyCertificateProof(certInput));
}

void CreateAndVerifyCSW(ProvingSystem ps, TestCircuitType tc)
{
    SelectParams(CBaseChainParams::REGTEST);
    BlockchainTestManager& testManager = BlockchainTestManager::GetInstance();
    testManager.GenerateSidechainTestParameters(ps, tc, false); // no key_rotation possible for CSW

    std::cout << "Temp folder for proof verification test: " << testManager.TempFolderPath() << std::endl;

    CCswProofVerifierInput cswInput = CreateDefaultCswInput(tc);
    cswInput.verificationKey = testManager.GetTestVerificationKey(ps, tc);
    cswInput.proof = testManager.GenerateTestCswProof(cswInput, ps, tc);

    ASSERT_TRUE(testManager.VerifyCswProof(cswInput));
}

/**
 * @brief This test is intended to generate verification parameters,
 * generate a valid certificate proof (Marlin) and verify it through the batch verifier.
 */
TEST(CctpLibrary, CreateAndVerifyMarlinCertificateProof)
{
    CreateAndVerifyCert(ProvingSystem::CoboundaryMarlin, TestCircuitType::Certificate, false);
}

TEST(CctpLibrary, CreateAndVerifyMarlinCertificateNoConstantProof)
{
    CreateAndVerifyCert(ProvingSystem::CoboundaryMarlin, TestCircuitType::CertificateNoConstant, false);
}

TEST(CctpLibrary, CreateAndVerifyMarlinCertificateProofKeyrot)
{
    CreateAndVerifyCert(ProvingSystem::CoboundaryMarlin, TestCircuitType::Certificate, true);
}

TEST(CctpLibrary, CreateAndVerifyMarlinCertificateNoConstantProofKeyrot)
{
    CreateAndVerifyCert(ProvingSystem::CoboundaryMarlin, TestCircuitType::CertificateNoConstant, true);
}

/**
 * @brief This test is intended to generate verification parameters,
 * generate a valid certificate proof (Darlin) and verify it through the batch verifier.
 */
TEST(CctpLibrary, CreateAndVerifyDarlinCertificateProof)
{
    CreateAndVerifyCert(ProvingSystem::Darlin, TestCircuitType::Certificate, false);
}

TEST(CctpLibrary, CreateAndVerifyDarlinCertificateNoConstantProof)
{
    CreateAndVerifyCert(ProvingSystem::Darlin, TestCircuitType::CertificateNoConstant, false);
}

TEST(CctpLibrary, CreateAndVerifyDarlinCertificateProofKeyrot)
{
    CreateAndVerifyCert(ProvingSystem::Darlin, TestCircuitType::Certificate, true);
}

TEST(CctpLibrary, CreateAndVerifyDarlinCertificateNoConstantProofKeyrot)
{
    CreateAndVerifyCert(ProvingSystem::Darlin, TestCircuitType::CertificateNoConstant, true);
}

/**
 * @brief This test is intended to generate verification parameters,
 * generate a valid CSW proof (Marlin) and verify it through the batch verifier.
 */
TEST(CctpLibrary, CreateAndVerifyMarlinCswProof)
{
    CreateAndVerifyCSW(ProvingSystem::CoboundaryMarlin, TestCircuitType::CSW);
}

TEST(CctpLibrary, CreateAndVerifyMarlinCswNoConstantProof)
{
    CreateAndVerifyCSW(ProvingSystem::CoboundaryMarlin, TestCircuitType::CSWNoConstant);
}

/**
 * @brief This test is intended to generate verification parameters,
 * generate a valid CSW proof (Darlin) and verify it through the batch verifier.
 */
TEST(CctpLibrary, CreateAndVerifyDarlinCswProof)
{
    CreateAndVerifyCSW(ProvingSystem::Darlin, TestCircuitType::CSW);
}

TEST(CctpLibrary, CreateAndVerifyDarlinCswNoConstantProof)
{
    CreateAndVerifyCSW(ProvingSystem::Darlin, TestCircuitType::CSWNoConstant);
}

TEST(CctpLibrary, ReadWriteCmtObj)
{
    SidechainTxsCommitmentBuilder cmtObj;

    uint256 cmt = cmtObj.getCommitment();

    CMutableTransaction mtx;
    mtx.nVersion = SC_TX_VERSION;
    mtx.vsc_ccout.resize(1);
    mtx.vsc_ccout[0].nValue = CAmount(1);
    mtx.vsc_ccout[0].address = uint256S("dada");
    mtx.vsc_ccout[0].constant = CFieldElement(ParseHex("034e12b30ad6adf00d043785a590bcd36f0f50c58a2b456dccb296ab035b1d00"));
    mtx.vsc_ccout[0].customData = ParseHex("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    mtx.vsc_ccout[0].withdrawalEpochLength = 123;
    mtx.vsc_ccout[0].wCertVk   = CScVKey{ParseHex("020004000000000000000400000000000000080000000000000c00000000000000043b7a9714b7bdc766cfc3e8eaee811b651079e11b67ed3991f990a1245e41e43a808405289f8ae6294311b5189c04885ef5aa9419f4d8837edf4b31e8a87cfbe30480c71e38705c9be628c0e4f85c9922115fbf0dddef1ee44cfc60b429b6cddb313b802e2b7b324aff5a92f082249c61e21cef4fe1a1c84b0916e2791000501c2e8024800004faa96a7208453523f485a9de708c1f4e305d10020cd5a40f7ffcb7608749110300a85134ec84f629be4707c24f67ca9631725b3efc40e8fd44bd07f75f967580360000842def9b5923c65d78c72ebe96af25fe14f4bd4cc247999b4dd265e352e31f80d7dc7a74ea25ab3e1cd8388d4cf1c4393304447cd94f06435ec07aea4ce1ce180000044aa74466fa675b26d5b7f6a1070b58110c5c7e8f73ad7b0811d416f5c44dab2c80d4a5d62556bc40c7c7a6f18c612eedfb52eae6fe8a836116d0ae3b7b5d49ed3e004f9d974b0c2ada4b85552392cc5f31de706feb34a03c10fd1bc074e9ee4252010085773266a56e49f672caf30d21f039d340bd79ae0f58c17a2866067426003621800004d5669c6b9ccc92b1c6092abf87ffe0af2e3672b1fde13acd3350e3c4ec94b02600fcf0c2ecc48ac392837fd512d840d61ed768ce50e568ad6482c3c9a44f1b7335003d44516a5d9df68abff2fa9399b3367f8f73909c4a08beb225add8a07dc69a33005d90007fe3595185722bd0260c51150f10c979b393bcc9dfa5414a33b161d32980000476e5ee40647dbf3825e6f7104de64828c68c12959af54f1bcc3b34d4a843f402800775c05370b59cda57ff51960de8ba4b5ce4b4f514aa759dec7fa047a9cfe20200b3b12741b0e327ab5d36694b61db3bf91807fff2b467dc62b68707d79da6d50a006d1c68ad8976a89b9e3c3fbd459eb4f73a6e576b2cc77ad105c5e5983011ce3d000004af96850b86746cd60c63c5fcb7dc1db2cce260c9c4ac5758a6bcc1041b85af1d802cb25d70012d4c5c563511198dc5a75de85a3b28a7b28a91e82d81ba6395d40c0066aa7efe1c1cb77f7160026448d794185c2ffeb1fd05fc27e426d3068b88eb090007d2ac5a309c5686ba85907464df62af1673a05f85c5f4d28fefb11ecae34b33800004d5821c9068d1e45f1b391a3b91dd340b12a23f31531b86392be1d9c11a5fef3a80a5c58cf1b79663713b75cb7c8b539b431faf641952753d5d6b9bf028ca91ab2580c6b3e3f4b2613dda332456dc53a8293360b5fdfb79161156c9637dddc1e76f3900cf46b3338a362e7c1d87b1b8448c74e0372e6a4289adc9d2e7a35fc2d256c5378000045bd2b1b11b169ebc2ca245982943f2992415ffc50a612d442cb29b4c13cb732800424fdf0ab99d4e5f52dde25c6ded990d009c201f8837acd6ec1ed89c2429f421001181192eef8f59e7fffbb552024a72dadf09c654507071bc1c89d4aa84c5603600a5be042887c740f273f5efa1521138d1448804c76055b21bfc4b75690e78ce3d00000155da0f9b641df9a3d820ae26f1520911032e611d06feae704d104b06138d3c2400000155da0f9b641df9a3d820ae26f1520911032e611d06feae704d104b06138d3c24000001000000000000000000000000000000000000000000000000000000000000000040000155da0f9b641df9a3d820ae26f1520911032e611d06feae704d104b06138d3c240000")};
    mtx.vft_ccout.resize(0);
    mtx.vmbtr_out.resize(0);
    mtx.vcsw_ccin.resize(0);

    CTransaction tx(mtx);
    std::cout << tx.ToString() << std::endl;

    ASSERT_TRUE(cmtObj.add(tx));

    cmt = cmtObj.getCommitment();
    printf("cmt = [%s]\n", cmt.ToString().c_str());
}

TEST(CctpLibrary, TestVectorsValidity)
{
    auto fe = CFieldElement{SAMPLE_FIELD};
    auto ct = CFieldElement{EMPTY_COMMITMENT_TREE_FIELD};

    auto vk1 = CScVKey{SAMPLE_CERT_DARLIN_VK};
    auto vk2 = CScVKey{SAMPLE_CERT_COBMARLIN_VK};
    auto vk3 = CScVKey{SAMPLE_CSW_DARLIN_VK};
    auto vk4 = CScVKey{SAMPLE_CSW_COBMARLIN_VK};

    auto p1 = CScProof{SAMPLE_CERT_DARLIN_PROOF};
    auto p2 = CScProof{SAMPLE_CERT_COBMARLIN_PROOF};
    auto p3 = CScProof{SAMPLE_CSW_DARLIN_PROOF};
    auto p4 = CScProof{SAMPLE_CSW_COBMARLIN_PROOF};

    EXPECT_TRUE(fe.IsValid());
    EXPECT_TRUE(ct.IsValid());

    EXPECT_TRUE(vk1.IsValid());
    EXPECT_TRUE(vk2.IsValid());
    EXPECT_TRUE(vk3.IsValid());
    EXPECT_TRUE(vk4.IsValid());

    EXPECT_TRUE(p1.IsValid());
    EXPECT_TRUE(p2.IsValid());
    EXPECT_TRUE(p3.IsValid());
    EXPECT_TRUE(p4.IsValid());
}

//TODO: Maybe it's not the correct place for this test
TEST(CctpLibrary, TestInvalidProofVkWhenOversized)
{
    // Oversized Proof
    std::vector<unsigned char> OVERSIZED_CERT_DARLIN_PROOF = SAMPLE_CERT_DARLIN_PROOF;
    OVERSIZED_CERT_DARLIN_PROOF.resize(OVERSIZED_CERT_DARLIN_PROOF.size() + Sidechain::MAX_SC_PROOF_SIZE_IN_BYTES, 0xAB);
    EXPECT_DEATH(CScProof{OVERSIZED_CERT_DARLIN_PROOF}, "");
    
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << OVERSIZED_CERT_DARLIN_PROOF;
    CScProof pOversized;
    stream >> pOversized;
    EXPECT_FALSE(pOversized.IsValid());

    // Not oversized proof but with random bytes appended to proof bytes
    std::vector<unsigned char> INVALID_CERT_DARLIN_PROOF = SAMPLE_CERT_DARLIN_PROOF;
    INVALID_CERT_DARLIN_PROOF.push_back(0xAB);
    EXPECT_NO_FATAL_FAILURE(CScProof{INVALID_CERT_DARLIN_PROOF});

    stream << INVALID_CERT_DARLIN_PROOF;
    CScProof pInvalid;
    stream >> pInvalid;
    EXPECT_FALSE(pInvalid.IsValid());

    // Oversized vk
    std::vector<unsigned char> OVERSIZED_CERT_DARLIN_VK = SAMPLE_CERT_DARLIN_VK;
    OVERSIZED_CERT_DARLIN_VK.resize(OVERSIZED_CERT_DARLIN_VK.size() + Sidechain::MAX_SC_VK_SIZE_IN_BYTES, 0xAB);
    EXPECT_DEATH(CScVKey{OVERSIZED_CERT_DARLIN_VK}, "");

    stream << OVERSIZED_CERT_DARLIN_VK;
    CScVKey vkOversized;
    stream >> vkOversized;
    EXPECT_FALSE(vkOversized.IsValid());

    // Not oversized vk but with random bytes appended to vk bytes
    std::vector<unsigned char> INVALID_CERT_DARLIN_VK = SAMPLE_CERT_DARLIN_VK;
    INVALID_CERT_DARLIN_VK.push_back(0xAB);
    EXPECT_NO_FATAL_FAILURE(CScVKey{INVALID_CERT_DARLIN_VK});

    stream << INVALID_CERT_DARLIN_VK;
    CScVKey vkInvalid;
    stream >> vkInvalid;
    EXPECT_FALSE(vkInvalid.IsValid());

    //TODO: Might be useful to test the same behaviour with bit vector
}

TEST(CctpLibrary, TestGetLeadingZeros)
{
    ASSERT_EQ(8, getLeadingZeroBitsInByte(0));

    for (uint8_t n = 1; n > 0; n++)
    {
        ASSERT_EQ(__builtin_clz(n) % (sizeof(unsigned int) * 8 - CHAR_BIT), getLeadingZeroBitsInByte(n));
    }
}

TEST(CctpLibrary, TestGetTrailingZeros)
{
    ASSERT_EQ(8, getTrailingZeroBitsInByte(0));

    for (uint8_t n = 1; n > 0; n++)
    {
        ASSERT_EQ(__builtin_ctz(n) % (sizeof(unsigned int) * 8 - CHAR_BIT), getTrailingZeroBitsInByte(n));
    }
}

/**
 * @brief Check the correctness of the GetBytesFromBits utility function
 * 
 * In order to keep the test fast, the iteration is limited to the first 2^16 numbers.
 */
TEST(CctpLibrary, TestGetBytesFromBits)
{
    int reminder = 0;

    // Check that the function works properly with the "0" input
    ASSERT_EQ(0, getBytesFromBits(0, reminder));
    ASSERT_EQ(0, reminder);

    // Check that the function works properly with a negative input
    ASSERT_EQ(0, getBytesFromBits(-1, reminder));
    ASSERT_EQ(0, reminder);

    for (uint16_t n = 1; n > 0; n++)
    {
        // Allocate a set of n bits
        boost::dynamic_bitset<uint8_t> bitset(n);

        // Convert the bit set to byte vector
        std::vector<uint8_t> bytes;
        boost::to_block_range(bitset, std::back_inserter(bytes));

        ASSERT_EQ(bytes.size(), getBytesFromBits(n, reminder));

        if (reminder == 0)
        {
            ASSERT_EQ(0, n % CHAR_BIT);
        }
        else
        {
            ASSERT_EQ(reminder, n - (bytes.size() - 1) * CHAR_BIT);
        }
    }
}

TEST(CctpLibrary, TestCustomFieldsValidation_v0)
{
    uint8_t sidechainVersion = 0;

    for (uint8_t i = 2; i < CHAR_BIT; i++)
    {
        for (uint8_t j = 1; j > 0; j++)
        {
            std::vector<unsigned char> rawBytes = { ReverseBitsInByte(j) };
            FieldElementCertificateField certField = FieldElementCertificateField(rawBytes);
            FieldElementCertificateFieldConfig config = FieldElementCertificateFieldConfig(i);
            CFieldElement fe = certField.GetFieldElement(config, sidechainVersion);

            // The Field Element is valid if it matches the configuration (j < 1 << i)
            // and if doesn't exceed the modulus, thus the last two bits cannot be set (j < 1 << 6).
            // Since v0 uses the wrong endianness, "j" must be reversed before checking.
            if (j < 1 << i && ReverseBitsInByte(j) <= 1 << (CHAR_BIT - 2))
            {
                EXPECT_TRUE(fe.IsValid());
            }
            else
            {
                EXPECT_FALSE(fe.IsValid());
            }
        }
    }
}

TEST(CctpLibrary, TestCustomFieldsValidation_v1)
{
    uint8_t sidechainVersion = 1;

    for (uint8_t i = 1; i < CHAR_BIT; i++)
    {
        for (uint8_t j = 1; j > 0; j++)
        {
            std::vector<unsigned char> rawBytes = { j };
            FieldElementCertificateField certField = FieldElementCertificateField(rawBytes);
            FieldElementCertificateFieldConfig config = FieldElementCertificateFieldConfig(i);
            CFieldElement fe = certField.GetFieldElement(config, sidechainVersion);

            // The Field Element is valid if it matches the configuration (j < 1 << i).
            if (j < 1 << i)
            {
                EXPECT_TRUE(fe.IsValid());
            }
            else
            {
                EXPECT_FALSE(fe.IsValid());
            }
        }
    }
}

/**
 * @brief Test the validation of a full (32 bytes) random custom field iteratively
 * changing the last (most significant) byte.
 */
TEST(CctpLibrary, TestFullCustomFieldValidation_v0)
{
    uint8_t sidechainVersion = 0;

    std::vector<unsigned char> rawBytes(std::begin(TEST_CUSTOM_FIELD), std::end(TEST_CUSTOM_FIELD));

    for (uint8_t i = 1; i < CHAR_BIT; i++)
    {
        for (uint8_t j = 1; j > 0; j++)
        {
            rawBytes.back() = ReverseBitsInByte(j);
            FieldElementCertificateField certField = FieldElementCertificateField(rawBytes);
            FieldElementCertificateFieldConfig config = FieldElementCertificateFieldConfig(i + 31 * CHAR_BIT);
            CFieldElement fe = certField.GetFieldElement(config, sidechainVersion);

            // The Field Element is valid if it matches the configuration (j < 1 << i)
            // and if doesn't exceed the modulus, thus the last two bits cannot be set (j < 1 << 6).
            // Since v0 uses the wrong endianness, "j" must be reversed before checking.
            if (j < 1 << i && ReverseBitsInByte(j) < 1 << (CHAR_BIT - 2))
            {
                EXPECT_TRUE(fe.IsValid());
            }
            else
            {
                EXPECT_FALSE(fe.IsValid());
            }
        }
    }
}

/**
 * @brief Test the validation of a full (32 bytes) random custom field iteratively
 * changing the last (most significant) byte.
 */
TEST(CctpLibrary, TestFullCustomFieldValidation_v1)
{
    uint8_t sidechainVersion = 1;

    std::vector<unsigned char> rawBytes(std::begin(TEST_CUSTOM_FIELD), std::end(TEST_CUSTOM_FIELD));

    for (uint8_t i = 1; i < CHAR_BIT; i++)
    {
        for (uint8_t j = 1; j > 0; j++)
        {
            rawBytes.back() = j;
            FieldElementCertificateField certField = FieldElementCertificateField(rawBytes);
            FieldElementCertificateFieldConfig config = FieldElementCertificateFieldConfig(i + 31 * CHAR_BIT);
            CFieldElement fe = certField.GetFieldElement(config, sidechainVersion);

            // The Field Element is valid if it matches the configuration (j < 1 << i)
            // and if doesn't exceed the modulus, thus the last two bits cannot be set (j < 1 << 6).
            if (j < 1 << i && j < 1 << (CHAR_BIT - 2))
            {
                EXPECT_TRUE(fe.IsValid());
            }
            else
            {
                EXPECT_FALSE(fe.IsValid());
            }
        }
    }
}

/**
 * @brief Test the validation of a random custom field
 * with a size equivalent to a long integer (64 bits, 8 bytes)
 * iteratively changing the last (most significant) byte.
 */
TEST(CctpLibrary, TestLongIntCustomFieldValidation_v0)
{
    uint8_t sidechainVersion = 0;

    std::vector<unsigned char> rawBytes(std::begin(TEST_CUSTOM_FIELD), std::end(TEST_CUSTOM_FIELD));
    rawBytes.resize(8);

    for (uint8_t i = 1; i < CHAR_BIT; i++)
    {
        for (uint8_t j = 1; j > 0; j++)
        {
            rawBytes.back() = ReverseBitsInByte(j);
            FieldElementCertificateField certField = FieldElementCertificateField(rawBytes);
            FieldElementCertificateFieldConfig config = FieldElementCertificateFieldConfig(i + 7 * CHAR_BIT);
            CFieldElement fe = certField.GetFieldElement(config, sidechainVersion);

            // The Field Element is valid if it matches the configuration (j < 1 << i)
            // and if doesn't exceed the modulus, thus the last two bits cannot be set (j < 1 << 6).
            // Since v0 uses the wrong endianness, "j" must be reversed before checking.
            if (j < 1 << i && ReverseBitsInByte(j) < 1 << (CHAR_BIT - 2))
            {
                EXPECT_TRUE(fe.IsValid());
            }
            else
            {
                EXPECT_FALSE(fe.IsValid());
            }
        }
    }
}

/**
 * @brief Test the validation of a random custom field
 * with a size equivalent to a long integer (64 bits, 8 bytes)
 * iteratively changing the last (most significant) byte.
 */
TEST(CctpLibrary, TestLongIntCustomFieldValidation_v1)
{
    uint8_t sidechainVersion = 1;

    std::vector<unsigned char> rawBytes(std::begin(TEST_CUSTOM_FIELD), std::end(TEST_CUSTOM_FIELD));
    rawBytes.resize(8);

    for (uint8_t i = 1; i < CHAR_BIT; i++)
    {
        for (uint8_t j = 1; j > 0; j++)
        {
            rawBytes.back() = j;
            FieldElementCertificateField certField = FieldElementCertificateField(rawBytes);
            FieldElementCertificateFieldConfig config = FieldElementCertificateFieldConfig(i + 7 * CHAR_BIT);
            CFieldElement fe = certField.GetFieldElement(config, sidechainVersion);

            // The Field Element is valid if it matches the configuration (j < 1 << i).
            if (j < 1 << i)
            {
                EXPECT_TRUE(fe.IsValid());
            }
            else
            {
                EXPECT_FALSE(fe.IsValid());
            }
        }
    }
}

/**
 * @brief Test the validation of a random custom field
 * with a size equivalent to an integer (32 bits, 4 bytes)
 * iteratively changing the last (most significant) byte.
 */
TEST(CctpLibrary, TestIntCustomFieldValidation_v0)
{
    uint8_t sidechainVersion = 0;

    std::vector<unsigned char> rawBytes(std::begin(TEST_CUSTOM_FIELD), std::end(TEST_CUSTOM_FIELD));
    rawBytes.resize(4);

    for (uint8_t i = 1; i < CHAR_BIT; i++)
    {
        for (uint8_t j = 1; j > 0; j++)
        {
            rawBytes.back() = ReverseBitsInByte(j);
            FieldElementCertificateField certField = FieldElementCertificateField(rawBytes);
            FieldElementCertificateFieldConfig config = FieldElementCertificateFieldConfig(i + 3 * CHAR_BIT);
            CFieldElement fe = certField.GetFieldElement(config, sidechainVersion);

            // The Field Element is valid if it matches the configuration (j < 1 << i)
            // and if doesn't exceed the modulus, thus the last two bits cannot be set (j < 1 << 6).
            // Since v0 uses the wrong endianness, "j" must be reversed before checking.
            if (j < 1 << i && ReverseBitsInByte(j) < 1 << (CHAR_BIT - 2))
            {
                EXPECT_TRUE(fe.IsValid());
            }
            else
            {
                EXPECT_FALSE(fe.IsValid());
            }
        }
    }
}

/**
 * @brief Test the validation of a random custom field
 * with a size equivalent to an integer (32 bits, 4 bytes)
 * iteratively changing the last (most significant) byte.
 */
TEST(CctpLibrary, TestIntCustomFieldValidation_v1)
{
    uint8_t sidechainVersion = 1;

    std::vector<unsigned char> rawBytes(std::begin(TEST_CUSTOM_FIELD), std::end(TEST_CUSTOM_FIELD));
    rawBytes.resize(4);

    for (uint8_t i = 1; i < CHAR_BIT; i++)
    {
        for (uint8_t j = 1; j > 0; j++)
        {
            rawBytes.back() = j;
            FieldElementCertificateField certField = FieldElementCertificateField(rawBytes);
            FieldElementCertificateFieldConfig config = FieldElementCertificateFieldConfig(i + 3 * CHAR_BIT);
            CFieldElement fe = certField.GetFieldElement(config, sidechainVersion);

            // The Field Element is valid if it matches the configuration (j < 1 << i).
            if (j < 1 << i)
            {
                EXPECT_TRUE(fe.IsValid());
            }
            else
            {
                EXPECT_FALSE(fe.IsValid());
            }
        }
    }
}

/**
 * @brief This test is intented to check that the validation mechanism works as expected
 * both in sidechain version 0 and 1 with values around the field modulus.
 */
TEST(CctpLibrary, TestTweedleFrModulusLimit)
{
    FieldElementCertificateFieldConfig config = FieldElementCertificateFieldConfig(255);
    std::vector<unsigned char> rawBytes(std::begin(TWEEDLE_FR_MODULUS), std::end(TWEEDLE_FR_MODULUS));

    /*
     * To decrease the Field Element we can just decrease the least significant byte,
     * but only if there is no overflow (if byte is 0, then 0 - 1 = 0xFF).
     */
    ASSERT_TRUE(rawBytes.front() > 0);
    rawBytes.front()--;

    FieldElementCertificateField certField = FieldElementCertificateField(rawBytes);
    CFieldElement fe = certField.GetFieldElement(config, 0);
    ASSERT_TRUE(fe.IsValid());

    certField = FieldElementCertificateField(rawBytes);
    fe = certField.GetFieldElement(config, 1);
    ASSERT_TRUE(fe.IsValid());

    rawBytes.front()++;

    certField = FieldElementCertificateField(rawBytes);
    fe = certField.GetFieldElement(config, 0);
    ASSERT_FALSE(fe.IsValid());

    certField = FieldElementCertificateField(rawBytes);
    fe = certField.GetFieldElement(config, 1);
    ASSERT_FALSE(fe.IsValid());

    /*
     * Even though the test is working as expected (meaning that both v0 and v1 are able to
     * correctly validate a Field Element containing the value "MODULUS - 1" and instead
     * reject the value "MODULUS"), we cannot be sure that the rawBytes are read with the
     * right endianness. If, by mistake, we are decreasing the wrong byte (not the least
     * significant but the most significant one), the previous checks would pass anyway).
     * 
     * To prove that the endianness is correct we can than run the same checks by keeping
     * the least significant byte and clearing the other ones (expeting success). Then,
     * if we increase the same byte, the checks would pass only if we picked the right
     * byte, otherwise we would end up with a value over the modulus!
     */

    std::vector<unsigned char> clearedBytes = { rawBytes.front() };
    clearedBytes.resize(rawBytes.size(), 0);

    certField = FieldElementCertificateField(clearedBytes);
    fe = certField.GetFieldElement(config, 0);
    ASSERT_TRUE(fe.IsValid());

    certField = FieldElementCertificateField(clearedBytes);
    fe = certField.GetFieldElement(config, 1);
    ASSERT_TRUE(fe.IsValid());

    /*
     * For the same reason as before, before increasing the byte we have to make sure that
     * there is no overflow.
     */
    ASSERT_TRUE(clearedBytes.front() < 0xff);
    clearedBytes.front()++;

    certField = FieldElementCertificateField(clearedBytes);
    fe = certField.GetFieldElement(config, 0);
    ASSERT_TRUE(fe.IsValid());

    certField = FieldElementCertificateField(clearedBytes);
    fe = certField.GetFieldElement(config, 1);
    ASSERT_TRUE(fe.IsValid());

    /*
     * As a countercheck, we should expect a failure if we pick (and increase) the wrong
     * byte after having cleared the other ones.
     */
    std::vector<unsigned char> wrongBytes(rawBytes.size(), 0);
    wrongBytes.back() = rawBytes.back();

    certField = FieldElementCertificateField(wrongBytes);
    fe = certField.GetFieldElement(config, 0);
    ASSERT_TRUE(fe.IsValid());

    certField = FieldElementCertificateField(wrongBytes);
    fe = certField.GetFieldElement(config, 1);
    ASSERT_TRUE(fe.IsValid());


    ASSERT_TRUE(wrongBytes.back() < 0xff);
    wrongBytes.back()++;

    certField = FieldElementCertificateField(wrongBytes);
    fe = certField.GetFieldElement(config, 0);
    ASSERT_FALSE(fe.IsValid());

    certField = FieldElementCertificateField(wrongBytes);
    fe = certField.GetFieldElement(config, 1);
    ASSERT_FALSE(fe.IsValid());
}

/**
 * @brief This test is intented to check that version 0 of the custom fields validation
 * is buggy, while version 1 is fixed.
 * 
 * To do that, we try to validate a "special" field element.
 * 
 */
TEST(CctpLibrary, TestCustomFieldsValidationFix)
{
    FieldElementCertificateFieldConfig config = FieldElementCertificateFieldConfig(9);
    std::vector<unsigned char> rawBytes = { 0, 1 };

    /*
     * This byte array is like [00000000] ... [00000001] [00000000], where the second least
     * significant byte is set to 1. It clearly uses 9 bits, but the version 0 validation
     * would fail processing thinking that it uses 16 bits instead (due to the different
     * endianness).
     */

    FieldElementCertificateField certField = FieldElementCertificateField(rawBytes);
    CFieldElement fe = certField.GetFieldElement(config, 0);
    ASSERT_FALSE(fe.IsValid());

    certField = FieldElementCertificateField(rawBytes);
    fe = certField.GetFieldElement(config, 1);
    ASSERT_TRUE(fe.IsValid());
}

TEST(CctpLibrary, CommitmentBuilder_toomanySC) {
    SidechainTxsCommitmentBuilder cmtObj;
    SidechainTxsCommitmentGuard guardObj;

    CMutableTransaction mtx;
    mtx.nVersion = SC_TX_VERSION;
    mtx.vsc_ccout.resize(0);
    mtx.vft_ccout.resize(0);
    mtx.vmbtr_out.resize(0);
    mtx.vcsw_ccin.resize(0);

    // forward transfers with different sc_ids
    for (int i = 0; i < CCTP_COMMITMENT_BUILDER_SC_LIMIT; i++)
        mtx.vft_ccout.push_back(CTxForwardTransferOut(uint256S("abc" + std::to_string(i + 100)), CAmount(43), uint256S("abba101"), uint160S("abba101")));

    CTransaction tx(mtx);
    // We should be able to push the transaction to the builder and the guard
    ASSERT_TRUE(cmtObj.add(tx));
    ASSERT_TRUE(guardObj.add(tx));

    CMutableTransaction mtx2;
    mtx2.nVersion = SC_TX_VERSION;
    mtx2.vsc_ccout.resize(0);
    mtx2.vft_ccout.resize(0);
    mtx2.vmbtr_out.resize(0);
    mtx2.vcsw_ccin.resize(0);

    mtx2.vft_ccout.push_back(CTxForwardTransferOut(uint256S("cccc"), CAmount(43), uint256S("abba101"), uint160S("abba101")));

    CTransaction tx2(mtx2);
    // Both must fail
    ASSERT_FALSE(cmtObj.add(tx2));
    ASSERT_FALSE(guardObj.add(tx2));

    // check counters
    auto cbsRef  = guardObj.getCBS();
    ASSERT_EQ(cbsRef.cbsaMap.size(), CCTP_COMMITMENT_BUILDER_SC_LIMIT);

    // Adding a FT to an already existing sidechain must not fail
    CMutableTransaction mtx3;
    mtx3.nVersion = SC_TX_VERSION;
    mtx3.vsc_ccout.resize(0);
    mtx3.vft_ccout.resize(0);
    mtx3.vmbtr_out.resize(0);
    mtx3.vcsw_ccin.resize(0);

    mtx3.vft_ccout.push_back(CTxForwardTransferOut(uint256S("abc" + std::to_string(110)), CAmount(12), uint256S("abba101"), uint160S("abba101")));
    CTransaction tx3(mtx3);
    ASSERT_TRUE(cmtObj.add(tx3));
    ASSERT_TRUE(guardObj.add(tx3));

}

TEST(CctpLibrary, CommitmentBuilder_toomanyFT)
{
    SidechainTxsCommitmentBuilder cmtObj;
    SidechainTxsCommitmentGuard guardObj;

    uint256 sidechainId = uint256S("abc");
    CMutableTransaction mtx;
    mtx.nVersion = SC_TX_VERSION;
    mtx.vsc_ccout.resize(0);
    mtx.vft_ccout.resize(0);
    mtx.vmbtr_out.resize(0);
    mtx.vcsw_ccin.resize(0);

    // forward transfers
    for (int i = 0; i < CCTP_COMMITMENT_BUILDER_FT_LIMIT; i++)
        mtx.vft_ccout.push_back(CTxForwardTransferOut(sidechainId, CAmount(43), uint256S("abba101"), uint160S("abba101")));

    CTransaction tx(mtx);
    // We should be able to push the transaction to the builder and the guard
    ASSERT_TRUE(cmtObj.add(tx));
    ASSERT_TRUE(guardObj.add(tx));

    CMutableTransaction mtx2;
    mtx2.nVersion = SC_TX_VERSION;
    mtx2.vsc_ccout.resize(0);
    mtx2.vft_ccout.resize(0);
    mtx2.vmbtr_out.resize(0);
    mtx2.vcsw_ccin.resize(0);

    mtx2.vft_ccout.push_back(CTxForwardTransferOut(sidechainId, CAmount(43), uint256S("abba101"), uint160S("abba101")));

    CTransaction tx2(mtx2);
    // Only the guard will fail
    ASSERT_TRUE(cmtObj.add(tx2));
    ASSERT_FALSE(guardObj.add(tx2));

    // Builder will fail now
    ASSERT_FALSE(cmtObj.add(tx2));

    // check counters
    auto cbsRef  = guardObj.getCBS();
    ASSERT_EQ(cbsRef.cbsaMap.size(), 1);
    ASSERT_EQ(cbsRef.cbscMap.size(), 0);
    ASSERT_EQ(cbsRef.cbsaMap[sidechainId].ft,   CCTP_COMMITMENT_BUILDER_FT_LIMIT);
    ASSERT_EQ(cbsRef.cbsaMap[sidechainId].bwtr, 0);
    ASSERT_EQ(cbsRef.cbsaMap[sidechainId].cert, 0);
}

TEST(CctpLibrary, CommitmentBuilder_toomanyBWTR)
{
    SidechainTxsCommitmentBuilder cmtObj;
    SidechainTxsCommitmentGuard guardObj;

    uint256 sidechainId = uint256S("abc");
    CMutableTransaction mtx;
    mtx.nVersion = SC_TX_VERSION;
    mtx.vsc_ccout.resize(0);
    mtx.vft_ccout.resize(0);
    mtx.vmbtr_out.resize(0);
    mtx.vcsw_ccin.resize(0);

    Sidechain::ScBwtRequestParameters scbwtr_params;
    scbwtr_params.vScRequestData.push_back(CFieldElement{SAMPLE_FIELD});
    scbwtr_params.scFee = 3;

    // backward transfer requests
    for (int i = 0; i < CCTP_COMMITMENT_BUILDER_BWTR_LIMIT; i++)
        mtx.vmbtr_out.push_back(CBwtRequestOut(sidechainId, uint160S("abba101"), scbwtr_params));

    CTransaction tx(mtx);
    // We should be able to push the transaction to the builder and the guard
    ASSERT_TRUE(cmtObj.add(tx));
    ASSERT_TRUE(guardObj.add(tx));

    CMutableTransaction mtx3;
    mtx3.nVersion = SC_TX_VERSION;
    mtx3.vsc_ccout.resize(0);
    mtx3.vft_ccout.resize(0);
    mtx3.vmbtr_out.resize(0);
    mtx3.vcsw_ccin.resize(0);

    mtx3.vmbtr_out.push_back(CBwtRequestOut(sidechainId, uint160S("abba101"), scbwtr_params));

    CTransaction tx3(mtx3);
    // Only the guard will fail
    ASSERT_TRUE(cmtObj.add(tx3));
    ASSERT_FALSE(guardObj.add(tx3));

    // Builder will fail now
    ASSERT_FALSE(cmtObj.add(tx3));

    // check counters
    auto cbsRef  = guardObj.getCBS();
    ASSERT_EQ(cbsRef.cbsaMap.size(), 1);
    ASSERT_EQ(cbsRef.cbscMap.size(), 0);
    ASSERT_EQ(cbsRef.cbsaMap[sidechainId].ft,   0);
    ASSERT_EQ(cbsRef.cbsaMap[sidechainId].bwtr, CCTP_COMMITMENT_BUILDER_BWTR_LIMIT);
    ASSERT_EQ(cbsRef.cbsaMap[sidechainId].cert, 0);
}

TEST(CctpLibrary, CommitmentBuilder_toomanyCERT) {
    SelectParams(CBaseChainParams::REGTEST);
    const BlockchainTestManager& testManager = BlockchainTestManager::GetInstance();

    SidechainTxsCommitmentBuilder cmtObj;
    SidechainTxsCommitmentGuard guardObj;

    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10), 10);
    CMutableTransaction mtx = scCreationTx;
    mtx.nVersion = SC_TX_VERSION;
    mtx.vsc_ccout.resize(0);
    mtx.vft_ccout.resize(0);
    mtx.vmbtr_out.resize(0);
    mtx.vcsw_ccin.resize(0);

    // sc creation transaction
    auto ccout = CTxScCreationOut(CAmount(10), uint256S("aaa"), CAmount(0), CAmount(0), Sidechain::ScFixedParameters());
    ccout.version = 0;
    ccout.withdrawalEpochLength = 100;
    ccout.wCertVk   = CScVKey{SAMPLE_CERT_DARLIN_VK};
    ccout.wCeasedVk = CScVKey{SAMPLE_CSW_DARLIN_VK};
    ccout.vFieldElementCertificateFieldConfig.push_back(44);
    ccout.customData.push_back(0x77);
    mtx.vsc_ccout.push_back(ccout);

    uint256 scId = scCreationTx.GetScIdFromScCcOut(0);

    CScCertificate cert = txCreationUtils::createCertificate(scId,
        /*epochNum*/12, CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/0,
        /*numChangeOut */0, /*bwtTotalAmount*/1, /*numBwt*/1, /*ftScFee*/0, /*mbtrScFee*/0);

    // We should be able to add the certificates to the builder and the guard
    for (int i = 0; i < CCTP_COMMITMENT_BUILDER_CERT_LIMIT; i++) {
        ASSERT_TRUE(cmtObj.add(cert, testManager.CoinsViewCache().get()));
        ASSERT_TRUE(guardObj.add(cert));
    }

    // Only the guard will fail
    ASSERT_TRUE(cmtObj.add(cert, testManager.CoinsViewCache().get()));
    ASSERT_FALSE(guardObj.add(cert));

    // Builder will fail now
    ASSERT_FALSE(cmtObj.add(cert, testManager.CoinsViewCache().get()));

    // check counters
    auto cbsRef  = guardObj.getCBS();
    ASSERT_EQ(cbsRef.cbsaMap.size(), 1);
    ASSERT_EQ(cbsRef.cbscMap.size(), 0);
    ASSERT_EQ(cbsRef.cbsaMap[scId].ft,   0);
    ASSERT_EQ(cbsRef.cbsaMap[scId].bwtr, 0);
    ASSERT_EQ(cbsRef.cbsaMap[scId].cert, CCTP_COMMITMENT_BUILDER_CERT_LIMIT);
}

TEST(CctpLibrary, CommitmentBuilder_toomanyCSW)
{
    const ProvingSystem testProvingSystem = ProvingSystem::Darlin;

    SelectParams(CBaseChainParams::REGTEST);
    BlockchainTestManager::GetInstance().GenerateSidechainTestParameters(testProvingSystem, TestCircuitType::Certificate, false);
    BlockchainTestManager::GetInstance().GenerateSidechainTestParameters(testProvingSystem, TestCircuitType::CSW, false);

    uint256 sidechainId = uint256S("aabba");

    CSidechain sidechain;
    sidechain.creationBlockHeight = 100;
    sidechain.fixedParams.withdrawalEpochLength = 20;
    sidechain.fixedParams.constant = CFieldElement{SAMPLE_FIELD};
    sidechain.fixedParams.version = 0;
    sidechain.lastTopQualityCertHash = uint256S("cccc");
    sidechain.lastTopQualityCertQuality = 100;
    sidechain.lastTopQualityCertReferencedEpoch = -1;
    sidechain.lastTopQualityCertBwtAmount = 50;
    sidechain.balance = CAmount(100);
    sidechain.fixedParams.wCertVk = BlockchainTestManager::GetInstance().GetTestVerificationKey(testProvingSystem, TestCircuitType::Certificate);
    sidechain.fixedParams.wCeasedVk = BlockchainTestManager::GetInstance().GetTestVerificationKey(testProvingSystem, TestCircuitType::CSW);
    
    BlockchainTestManager& testManager = BlockchainTestManager::GetInstance();
    testManager.Reset();

    // Store the test sidechain and extend the blockchain to complete at least one epoch. 
    testManager.StoreSidechainWithCurrentHeight(sidechainId, sidechain, sidechain.creationBlockHeight + sidechain.fixedParams.withdrawalEpochLength);

    CTxCeasedSidechainWithdrawalInput input1 = testManager.CreateCswInput(sidechainId, 1, testProvingSystem);

    SidechainTxsCommitmentBuilder cmtObj;
    SidechainTxsCommitmentGuard guardObj;

    CMutableTransaction mtx;
    mtx.nVersion = SC_TX_VERSION;
    mtx.vsc_ccout.resize(0);
    mtx.vft_ccout.resize(0);
    mtx.vmbtr_out.resize(0);
    mtx.vcsw_ccin.resize(0);

    // ceased sidechain withdrawals
    for (int i = 0; i < CCTP_COMMITMENT_BUILDER_CSW_LIMIT; i++)
        mtx.vcsw_ccin.push_back(input1);

    CTransaction tx(mtx);
    // We should be able to push the transaction to the builder and the guard
    ASSERT_TRUE(cmtObj.add(tx));
    ASSERT_TRUE(guardObj.add(tx));

    CMutableTransaction mtx3;
    mtx3.nVersion = SC_TX_VERSION;
    mtx3.vsc_ccout.resize(0);
    mtx3.vft_ccout.resize(0);
    mtx3.vmbtr_out.resize(0);
    mtx3.vcsw_ccin.resize(0);

    mtx3.vcsw_ccin.push_back(input1);

    CTransaction tx3(mtx3);
    // Only the guard will fail
    ASSERT_TRUE(cmtObj.add(tx3));
    ASSERT_FALSE(guardObj.add(tx3));

    // Builder will fail now
    ASSERT_FALSE(cmtObj.add(tx3));

    // check counters
    auto cbsRef  = guardObj.getCBS();
    ASSERT_EQ(cbsRef.cbsaMap.size(), 0);
    ASSERT_EQ(cbsRef.cbscMap.size(), 1);
    ASSERT_EQ(cbsRef.cbscMap[sidechainId].csw, CCTP_COMMITMENT_BUILDER_CSW_LIMIT);
}

TEST(CctpLibrary, CommitmentBuilder_sameSC_nevermixCSWandFT)
{
    const ProvingSystem testProvingSystem = ProvingSystem::Darlin;

    SelectParams(CBaseChainParams::REGTEST);
    BlockchainTestManager::GetInstance().GenerateSidechainTestParameters(testProvingSystem, TestCircuitType::Certificate, false);
    BlockchainTestManager::GetInstance().GenerateSidechainTestParameters(testProvingSystem, TestCircuitType::CSW, false);

    uint256 sidechainId = uint256S("aabba");

    CSidechain sidechain;
    sidechain.creationBlockHeight = 100;
    sidechain.fixedParams.withdrawalEpochLength = 20;
    sidechain.fixedParams.constant = CFieldElement{SAMPLE_FIELD};
    sidechain.fixedParams.version = 0;
    sidechain.lastTopQualityCertHash = uint256S("cccc");
    sidechain.lastTopQualityCertQuality = 100;
    sidechain.lastTopQualityCertReferencedEpoch = -1;
    sidechain.lastTopQualityCertBwtAmount = 50;
    sidechain.balance = CAmount(100);
    sidechain.fixedParams.wCertVk = BlockchainTestManager::GetInstance().GetTestVerificationKey(testProvingSystem, TestCircuitType::Certificate);
    sidechain.fixedParams.wCeasedVk = BlockchainTestManager::GetInstance().GetTestVerificationKey(testProvingSystem, TestCircuitType::CSW);
    
    BlockchainTestManager& testManager = BlockchainTestManager::GetInstance();
    testManager.Reset();

    // Store the test sidechain and extend the blockchain to complete at least one epoch. 
    testManager.StoreSidechainWithCurrentHeight(sidechainId, sidechain, sidechain.creationBlockHeight + sidechain.fixedParams.withdrawalEpochLength);

    CTxCeasedSidechainWithdrawalInput csw = testManager.CreateCswInput(sidechainId, 1, testProvingSystem);

    // FT and CSW in the same tx
    SidechainTxsCommitmentBuilder cmtObj;
    SidechainTxsCommitmentGuard guardObj;

    CMutableTransaction mtx, mtx2, mtx3;;
    mtx.nVersion = SC_TX_VERSION;
    mtx.vsc_ccout.resize(0);
    mtx.vft_ccout.resize(0);
    mtx.vmbtr_out.resize(0);
    mtx.vcsw_ccin.resize(0);
    mtx2.nVersion = SC_TX_VERSION;
    mtx2.vsc_ccout.resize(0);
    mtx2.vft_ccout.resize(0);
    mtx2.vmbtr_out.resize(0);
    mtx2.vcsw_ccin.resize(0);
    mtx3.nVersion = SC_TX_VERSION;
    mtx3.vsc_ccout.resize(0);
    mtx3.vft_ccout.resize(0);
    mtx3.vmbtr_out.resize(0);
    mtx3.vcsw_ccin.resize(0);

    // forward transfer
    mtx.vft_ccout.push_back(CTxForwardTransferOut(sidechainId, CAmount(43), uint256S("abba101"), uint160S("abba101")));
    // ceased sidechain withdrawals
    mtx.vcsw_ccin.push_back(csw);

    CTransaction tx(mtx);
    // Both must fail
    ASSERT_FALSE(cmtObj.add(tx));
    ASSERT_FALSE(guardObj.add(tx));


    // forward transfer
    mtx2.vft_ccout.push_back(CTxForwardTransferOut(sidechainId, CAmount(43), uint256S("abba101"), uint160S("abba101")));
    CTransaction tx2(mtx2);

    // ceased sidechain withdrawals
    mtx3.vcsw_ccin.push_back(csw);
    CTransaction tx3(mtx3);

    SidechainTxsCommitmentBuilder cmtObj2;
    SidechainTxsCommitmentGuard guardObj2;
    // tx with CSW after tx with FT (builder)
    ASSERT_TRUE(cmtObj2.add(tx2));
    ASSERT_FALSE(cmtObj2.add(tx3));
    // tx with CSW after tx with FT (guard)
    ASSERT_TRUE(guardObj2.add(tx2));
    ASSERT_FALSE(guardObj2.add(tx3));

    SidechainTxsCommitmentBuilder cmtObj3;
    SidechainTxsCommitmentGuard guardObj3;
    // tx with FT after tx with CSW (builder)
    ASSERT_TRUE(cmtObj3.add(tx3));
    ASSERT_FALSE(cmtObj3.add(tx2));
    // tx with FT after tx with CSW (guard)
    ASSERT_TRUE(guardObj3.add(tx3));
    ASSERT_FALSE(guardObj3.add(tx2));

    // Make sure that counters in CommitmentBuilderGuard are not incremented
    auto cbsRef  = guardObj.getCBS();
    auto cbsRef2 = guardObj2.getCBS();
    auto cbsRef3 = guardObj3.getCBS();
    ASSERT_EQ(cbsRef.cbsaMap[sidechainId].ft,  1);
    ASSERT_EQ(cbsRef.cbscMap[sidechainId].csw, 0);
    ASSERT_EQ(cbsRef2.cbsaMap[sidechainId].ft,  1);
    ASSERT_EQ(cbsRef2.cbscMap[sidechainId].csw, 0);
    ASSERT_EQ(cbsRef3.cbsaMap[sidechainId].ft,  0);
    ASSERT_EQ(cbsRef3.cbscMap[sidechainId].csw, 1);
}

TEST(CctpLibrary, CommitmentBuilder_fillFTsendBWTRandCERT)
{
    SelectParams(CBaseChainParams::REGTEST);
    const BlockchainTestManager& testManager = BlockchainTestManager::GetInstance();

    SidechainTxsCommitmentBuilder cmtObj;
    SidechainTxsCommitmentGuard guardObj;

    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10), 10);
    CMutableTransaction mtx = scCreationTx;
    mtx.nVersion = SC_TX_VERSION;
    mtx.vsc_ccout.resize(0);
    mtx.vft_ccout.resize(0);
    mtx.vmbtr_out.resize(0);
    mtx.vcsw_ccin.resize(0);

    // sc creation transaction
    auto ccout = CTxScCreationOut(CAmount(10), uint256S("aaa"), CAmount(0), CAmount(0), Sidechain::ScFixedParameters());
    ccout.version = 0;
    ccout.withdrawalEpochLength = 100;
    ccout.wCertVk   = CScVKey{SAMPLE_CERT_DARLIN_VK};
    ccout.wCeasedVk = CScVKey{SAMPLE_CSW_DARLIN_VK};
    ccout.vFieldElementCertificateFieldConfig.push_back(44);
    ccout.customData.push_back(0x77);
    mtx.vsc_ccout.push_back(ccout);

    uint256 sidechainId = scCreationTx.GetScIdFromScCcOut(0);


    // forward transfers
    for (int i = 0; i < CCTP_COMMITMENT_BUILDER_FT_LIMIT; i++)
        mtx.vft_ccout.push_back(CTxForwardTransferOut(sidechainId, CAmount(43), uint256S("abba101"), uint160S("abba101")));

    CTransaction tx(mtx);
    ASSERT_TRUE(cmtObj.add(tx));
    ASSERT_TRUE(guardObj.add(tx));


    CMutableTransaction mtxbwtr;
    mtxbwtr.nVersion = SC_TX_VERSION;
    mtxbwtr.vsc_ccout.resize(0);
    mtxbwtr.vft_ccout.resize(0);
    mtxbwtr.vmbtr_out.resize(0);
    mtxbwtr.vcsw_ccin.resize(0);

    Sidechain::ScBwtRequestParameters scbwtr_params;
    scbwtr_params.vScRequestData.push_back(CFieldElement{SAMPLE_FIELD});
    scbwtr_params.scFee = 3;

    // backward transfer requests
    for (int i = 0; i < CCTP_COMMITMENT_BUILDER_BWTR_LIMIT; i++)
        mtxbwtr.vmbtr_out.push_back(CBwtRequestOut(sidechainId, uint160S("abba101"), scbwtr_params));
    CTransaction txbwtr(mtxbwtr);
    ASSERT_TRUE(cmtObj.add(txbwtr));
    ASSERT_TRUE(guardObj.add(txbwtr));


    CScCertificate cert = txCreationUtils::createCertificate(sidechainId,
        /*epochNum*/12, CFieldElement{SAMPLE_FIELD}, /*changeTotalAmount*/0,
        /*numChangeOut */0, /*bwtTotalAmount*/1, /*numBwt*/1, /*ftScFee*/0, /*mbtrScFee*/0);

    for (int i = 0; i < CCTP_COMMITMENT_BUILDER_CERT_LIMIT; i++) {
        ASSERT_TRUE(cmtObj.add(cert, testManager.CoinsViewCache().get()));
        ASSERT_TRUE(guardObj.add(cert));
    }

    CMutableTransaction mtx3, mtx4;
    mtx3.nVersion = SC_TX_VERSION;
    mtx3.vsc_ccout.resize(0);
    mtx3.vft_ccout.resize(0);
    mtx3.vmbtr_out.resize(0);
    mtx3.vcsw_ccin.resize(0);
    mtx4.nVersion = SC_TX_VERSION;
    mtx4.vsc_ccout.resize(0);
    mtx4.vft_ccout.resize(0);
    mtx4.vmbtr_out.resize(0);
    mtx4.vcsw_ccin.resize(0);

    mtx3.vft_ccout.push_back(CTxForwardTransferOut(sidechainId, CAmount(43), uint256S("abba101"), uint160S("abba101")));
    CTransaction tx3(mtx3);
    ASSERT_TRUE(cmtObj.add(tx3));
    ASSERT_FALSE(guardObj.add(tx3));

    mtx4.vmbtr_out.push_back(CBwtRequestOut(sidechainId, uint160S("abba101"), scbwtr_params));
    CTransaction tx4(mtx4);
    ASSERT_TRUE(cmtObj.add(tx4));
    ASSERT_FALSE(guardObj.add(tx4));

    ASSERT_TRUE(cmtObj.add(cert, testManager.CoinsViewCache().get()));
    ASSERT_FALSE(guardObj.add(cert));

    // Builder will fail now
    ASSERT_FALSE(cmtObj.add(tx3));
    ASSERT_FALSE(cmtObj.add(tx4));
    ASSERT_FALSE(cmtObj.add(cert, testManager.CoinsViewCache().get()));

    // check counters
    auto cbsRef  = guardObj.getCBS();
    ASSERT_EQ(cbsRef.cbsaMap.size(), 1);
    ASSERT_EQ(cbsRef.cbscMap.size(), 0);
    ASSERT_EQ(cbsRef.cbsaMap[sidechainId].ft,   CCTP_COMMITMENT_BUILDER_FT_LIMIT);
    ASSERT_EQ(cbsRef.cbsaMap[sidechainId].bwtr, CCTP_COMMITMENT_BUILDER_BWTR_LIMIT);
    ASSERT_EQ(cbsRef.cbsaMap[sidechainId].cert, CCTP_COMMITMENT_BUILDER_CERT_LIMIT);
}

TEST(CctpLibrary, CommitmentBuilder_rewind)
{
    SidechainTxsCommitmentGuard guardObj;

    uint256 sidechainId = uint256S("abc");
    uint256 sidechainId2 = uint256S("abcba");
    CMutableTransaction mtx;
    mtx.nVersion = SC_TX_VERSION;
    mtx.vsc_ccout.resize(0);
    mtx.vft_ccout.resize(0);
    mtx.vmbtr_out.resize(0);
    mtx.vcsw_ccin.resize(0);

    const int intialFT = CCTP_COMMITMENT_BUILDER_FT_LIMIT - 3;

    // forward transfers: reaching almost the limit
    for (int i = 0; i < intialFT; i++)  // 4092
        mtx.vft_ccout.push_back(CTxForwardTransferOut(sidechainId, CAmount(43), uint256S("abba101"), uint160S("abba101")));
    // Also add a FT for another sidechain
        mtx.vft_ccout.push_back(CTxForwardTransferOut(sidechainId2, CAmount(43), uint256S("abba101"), uint160S("abba101")));

    CTransaction tx(mtx);
    ASSERT_TRUE(guardObj.add(tx));

    // Each transaction will go beyond the limit, so we will try to restore the guard state as before the failure
    for (int i = 4; i < 15; i++) {
        CMutableTransaction mtx2;
        mtx2.nVersion = SC_TX_VERSION;
        mtx2.vsc_ccout.resize(0);
        mtx2.vft_ccout.resize(0);
        mtx2.vmbtr_out.resize(0);
        mtx2.vcsw_ccin.resize(0);

        for (int j = 0; j < i; j++)
            mtx2.vft_ccout.push_back(CTxForwardTransferOut(sidechainId, CAmount(43), uint256S("abba101"), uint160S("abba101")));

        CTransaction tx2(mtx2);

        // Automatically restore guardObj to a valid state
        ASSERT_FALSE(guardObj.add(tx2, true));

        auto cbsRef  = guardObj.getCBS();
        ASSERT_EQ(cbsRef.cbsaMap[sidechainId].ft, intialFT);
        // And the other sc should have been left intact
        ASSERT_EQ(cbsRef.cbsaMap[sidechainId2].ft, 1);
    }
}

TEST(CctpLibrary, CommitmentBuilder_cleanCBSafterrewind)
{
    SidechainTxsCommitmentGuard guardObj;

    uint256 sidechainId = uint256S("abc");
    uint256 sidechainId2 = uint256S("abcba");
    CMutableTransaction mtx, mtx2;
    mtx.nVersion = SC_TX_VERSION;
    mtx.vsc_ccout.resize(0);
    mtx.vft_ccout.resize(0);
    mtx.vmbtr_out.resize(0);
    mtx.vcsw_ccin.resize(0);
    mtx2.nVersion = SC_TX_VERSION;
    mtx2.vsc_ccout.resize(0);
    mtx2.vft_ccout.resize(0);
    mtx2.vmbtr_out.resize(0);
    mtx2.vcsw_ccin.resize(0);

    for (int i = 0; i < 10; i++)
        mtx.vft_ccout.push_back(CTxForwardTransferOut(sidechainId, CAmount(43), uint256S("abba101"), uint160S("abba101")));

    // forward transfers for different sidechains, sidechainId2 alone should fail
    mtx2.vft_ccout.push_back(CTxForwardTransferOut(sidechainId, CAmount(43), uint256S("abba101"), uint160S("abba101")));
    for (int i = 0; i < CCTP_COMMITMENT_BUILDER_FT_LIMIT + 1; i++)  // 4096
        mtx2.vft_ccout.push_back(CTxForwardTransferOut(sidechainId2, CAmount(43), uint256S("abba101"), uint160S("abba101")));

    CTransaction tx(mtx);
    CTransaction tx2(mtx2);
    ASSERT_TRUE(guardObj.add(tx, true));
    // Transaction must be rejected and cbs state should be restored
    ASSERT_FALSE(guardObj.add(tx2, true));

    // Guard object must not contain anything about sidechainId2
    ASSERT_EQ(guardObj.getCBS().cbsaMap.size(), 1);
    ASSERT_EQ(guardObj.getCBS().cbsaMap.count(sidechainId),  1);
    ASSERT_EQ(guardObj.getCBS().cbsaMap.count(sidechainId2), 0);
    ASSERT_EQ(guardObj.getCBS().cbscMap.size(), 0);
    ASSERT_EQ(guardObj.getCBS().cbscMap.count(sidechainId),  0);
    ASSERT_EQ(guardObj.getCBS().cbscMap.count(sidechainId2), 0);
}