#include "base58.h" // DecodeBase58
#include <sc/sidechaintypes.h>
#include <utilstrencodings.h>
#include <zendoo/zendoo_mc.h>

#include <cassert>
#include <iostream>
#include <string>

/*
 *  Usage:
 *
 *  1) ./mcTest "generate" "cert/cert_no_const/csw/csw_no_const" "darlin/cob_marlin" "params_dir" "segment_size" "num_constraints"
 *  Generates SNARK pk and vk for a test cert/csw circuit using darlin/coboundary_marlin proving system;
 *
 *  2) ./mcTest "create" "cert/cert_no_const" "darlin/cob_marlin" <"-v"> <"-zk"> "proof_path" "params_dir" "segment_size"
 *  "sc_id" "epoch_number" "quality" ["constant"] "end_cum_comm_tree_root", "btr_fee",
 *  "ft_min_amount" "num_constraints"
 *  "bt_list_len", "mc_dest_addr_0" "amount_0" "mc_dest_addr_1" "amount_1" ... "mc_dest_addr_n" "amount_n",
 *  "custom_fields_list_len", "custom_field_0", ... , "custom_field_1"
 *  Generates a TestCertificateProof.
 *  NOTE: "constant" param must be present if "cert" has been passed; If "cert_no_const" has been passed,
 *        instead, "constant" param must not be present.
 *
 *  3) ./mcTest "create" "csw/csw_no_const" "darlin/cob_marlin" <"-v"> <"-zk"> "proof_path" "params_dir" "segment_size",
 *  "amount" "sc_id" "nullifier" "mc_address" "end_cum_comm_tree_root" "num_constraints" <"cert_data_hash">, [constant]
 *  Generates a TestCSWProof. cert_data_hash is optional.
 *  NOTE: "constant" param must be present if "cert" has been passed; If "cert_no_const" has been passed,
 *        instead, "constant" param must not be present.
 */

void create_verify_test_cert_proof(std::string ps_type_raw, std::string cert_type_raw, int argc, char** argv)
{
    int arg = 4;
    bool verify = false;
    if (std::string(argv[arg]) == "-v") {
        arg++;
        verify = true;
    }

    bool zk = false;
    if (std::string(argv[arg]) == "-zk") {
        arg++;
        zk = true;
    }

    // Get ProvingSystemType
    ProvingSystem ps_type;
    if (ps_type_raw == "darlin") {
        ps_type = ProvingSystem::Darlin;
    } else if (ps_type_raw == "cob_marlin") {
        ps_type = ProvingSystem::CoboundaryMarlin;
    } else {
        abort(); // Invalid ProvingSystemType
    }

    // Parse inputs
    // Parse paths
    auto proof_path = std::string(argv[arg++]);
    size_t proof_path_len = proof_path.size();

    auto params_path = std::string(argv[arg++]);

    CctpErrorCode ret_code = CctpErrorCode::OK;

    std::string pk_name;
    std::string vk_name;

    if (cert_type_raw == "cert") {
        pk_name = std::string("_cert_test_pk");
        vk_name = std::string("_cert_test_vk");
    } else {
        pk_name = std::string("_cert_no_const_test_pk");
        vk_name = std::string("_cert_no_const_test_vk");
    }

    // Deserialize pk
    auto pk_path = params_path + ps_type_raw + pk_name;
    size_t pk_path_len = pk_path.size();

    sc_pk_t* pk = zendoo_deserialize_sc_pk_from_file(
        (path_char_t*)pk_path.c_str(),
        pk_path_len,
        true,
        &ret_code);
    assert(pk != NULL);
    assert(ret_code == CctpErrorCode::OK);

    // Parse segment_size
    uint32_t segment_size = strtoull(argv[arg++], NULL, 0);

    // Init DLOG keys
    assert(zendoo_init_dlog_keys(Sidechain::SEGMENT_SIZE, &ret_code));
    assert(ret_code == CctpErrorCode::OK);

    // Parse sc_id
    assert(IsHex(argv[arg]));
    auto sc_id = ParseHex(argv[arg++]);
    assert(sc_id.size() == 32);
    field_t* sc_id_f = zendoo_deserialize_field(sc_id.data(), &ret_code);
    assert(sc_id_f != NULL);
    assert(ret_code == CctpErrorCode::OK);

    // Parse epoch number and quality
    uint32_t epoch_number = strtoull(argv[arg++], NULL, 0);
    uint64_t quality = strtoull(argv[arg++], NULL, 0);

    // Parse constant if present
    field_t* constant_f = NULL;
    if (cert_type_raw == "cert") {
        assert(IsHex(argv[arg]));
        auto constant = ParseHex(argv[arg++]);
        assert(constant.size() == 32);
        constant_f = zendoo_deserialize_field(constant.data(), &ret_code);
        assert(constant_f != NULL);
        assert(ret_code == CctpErrorCode::OK);
    }

    // Parse end_cum_comm_tree_root
    assert(IsHex(argv[arg]));
    auto end_cum_comm_tree_root = ParseHex(argv[arg++]);
    assert(end_cum_comm_tree_root.size() == 32);
    field_t* end_cum_comm_tree_root_f = zendoo_deserialize_field(end_cum_comm_tree_root.data(), &ret_code);
    assert(end_cum_comm_tree_root_f != NULL);
    assert(ret_code == CctpErrorCode::OK);

    // Parse btr_fee and ft_min_amount
    uint64_t btr_fee = strtoull(argv[arg++], NULL, 0);
    uint64_t ft_min_amount = strtoull(argv[arg++], NULL, 0);

    // Parse num_constraints
    uint32_t num_constraints = strtoull(argv[arg++], NULL, 0);

    // Create bt_list
    // Inputs must be (mc_dest_addr, amount) pairs from which construct backward_transfer_t objects
    uint32_t bt_list_length = strtoull(argv[arg++], NULL, 0);

    // Parse backward transfer list
    backward_transfer_t* bt_list_ptr = NULL;
    std::vector<backward_transfer_t> bt_list;
    if (bt_list_length != 0) {
        bt_list.reserve(bt_list_length);
        for (int i = 0; i < bt_list_length; i++) {
            backward_transfer_t bt;

            std::vector<unsigned char> vchData;
            assert(DecodeBase58(argv[arg++], vchData));
            uint160 pk_dest;
            memcpy(&pk_dest, &vchData[2], 20);

            /*uint160 pk_dest;
            CBitcoinAddress address(argv[arg++]);
            CKeyID keyId;
            assert(!address.GetKeyID(keyId));
            pk_dest = keyId;
            assert(pk_dest.size() == 20);*/
            std::copy(pk_dest.begin(), pk_dest.end(), std::begin(bt.pk_dest));

            uint64_t amount = strtoull(argv[arg++], NULL, 0);
            assert(amount >= 0);
            bt.amount = amount;

            bt_list.push_back(bt);
        }
        bt_list_ptr = bt_list.data();
    }

    // Create custom_fields
    uint32_t custom_fields_list_length = strtoull(argv[arg++], NULL, 0);

    // Parse backward transfer list
    field_t** custom_fields_ptr = NULL;
    std::vector<field_t*> custom_fields_list;

    if (custom_fields_list_length != 0) {
        custom_fields_list.reserve(custom_fields_list_length);
        for (int i = 0; i < custom_fields_list_length; i++) {
            // Parse custom field
            assert(IsHex(argv[arg]));
            auto custom_field = ParseHex(argv[arg++]);
            assert(custom_field.size() == 32);
            field_t* custom_field_f = zendoo_deserialize_field(custom_field.data(), &ret_code);
            assert(custom_field_f != NULL);
            assert(ret_code == CctpErrorCode::OK);

            custom_fields_list.push_back(custom_field_f);
        }
        custom_fields_ptr = custom_fields_list.data();
    }

    // Generate proof and vk
    assert(zendoo_create_cert_test_proof(
        zk,
        constant_f,
        sc_id_f,
        epoch_number,
        quality,
        bt_list_ptr,
        bt_list_length,
        (const field_t**)custom_fields_ptr,
        custom_fields_list_length,
        end_cum_comm_tree_root_f,
        btr_fee,
        ft_min_amount,
        pk,
        (path_char_t*)proof_path.c_str(),
        proof_path_len,
        num_constraints,
        &ret_code,
        true,
        &segment_size));
    assert(ret_code == CctpErrorCode::OK);

    // If -v was specified we verify the proof just created
    if (verify) {
        // Deserialize proof
        sc_proof_t* proof = zendoo_deserialize_sc_proof_from_file(
            (path_char_t*)proof_path.c_str(),
            proof_path_len,
            true,
            &ret_code);
        assert(proof != NULL);
        assert(ret_code == CctpErrorCode::OK);

        // Deserialize vk
        auto vk_path = params_path + ps_type_raw + vk_name;
        size_t vk_path_len = vk_path.size();
        sc_vk_t* vk = zendoo_deserialize_sc_vk_from_file(
            (path_char_t*)vk_path.c_str(),
            vk_path_len,
            true,
            &ret_code);
        assert(vk != NULL);
        assert(ret_code == CctpErrorCode::OK);

        // Verify proof
        assert(zendoo_verify_certificate_proof(
            constant_f,
            sc_id_f,
            epoch_number,
            quality,
            bt_list.data(),
            bt_list_length,
            (const field_t**)custom_fields_list.data(),
            custom_fields_list_length,
            end_cum_comm_tree_root_f,
            btr_fee,
            ft_min_amount,
            proof,
            vk,
            &ret_code));
        assert(ret_code == CctpErrorCode::OK);

        // Negative test
        auto wrong_epoch_number = epoch_number + 1;
        assert(!zendoo_verify_certificate_proof(
            constant_f,
            sc_id_f,
            wrong_epoch_number,
            quality,
            bt_list.data(),
            bt_list_length,
            (const field_t**)custom_fields_list.data(),
            custom_fields_list_length,
            end_cum_comm_tree_root_f,
            btr_fee,
            ft_min_amount,
            proof,
            vk,
            &ret_code));
        assert(ret_code == CctpErrorCode::OK);

        zendoo_sc_proof_free(proof);
        zendoo_sc_vk_free(vk);
    }

    zendoo_sc_pk_free(pk);
    zendoo_field_free(constant_f);
    zendoo_field_free(sc_id_f);
    zendoo_field_free(end_cum_comm_tree_root_f);

    for (int i = 0; i < custom_fields_list_length; i++) {
        zendoo_field_free(custom_fields_list[i]);
    }
}

void create_verify_test_csw_proof(std::string ps_type_raw, std::string csw_type_raw, int argc, char** argv)
{
    int arg = 4;
    bool verify = false;
    if (std::string(argv[arg]) == "-v") {
        arg++;
        verify = true;
    }

    bool zk = false;
    if (std::string(argv[arg]) == "-zk") {
        arg++;
        zk = true;
    }

    // Get ProvingSystemType
    ProvingSystem ps_type;
    if (ps_type_raw == "darlin") {
        ps_type = ProvingSystem::Darlin;
    } else if (ps_type_raw == "cob_marlin") {
        ps_type = ProvingSystem::CoboundaryMarlin;
    } else {
        abort(); // Invalid ProvingSystemType
    }

    // Parse inputs
    // Parse paths
    auto proof_path = std::string(argv[arg++]);
    size_t proof_path_len = proof_path.size();

    auto params_path = std::string(argv[arg++]);
    CctpErrorCode ret_code = CctpErrorCode::OK;

    std::string pk_name;
    std::string vk_name;

    if (csw_type_raw == "csw") {
        pk_name = std::string("_csw_test_pk");
        vk_name = std::string("_csw_test_vk");
    } else {
        pk_name = std::string("_csw_no_const_test_pk");
        vk_name = std::string("_csw_no_const_test_vk");
    }

    // Deserialize pk
    auto pk_path = params_path + ps_type_raw + pk_name;
    size_t pk_path_len = pk_path.size();
    sc_pk_t* pk = zendoo_deserialize_sc_pk_from_file(
        (path_char_t*)pk_path.c_str(),
        pk_path_len,
        true,
        &ret_code);
    assert(pk != NULL);
    assert(ret_code == CctpErrorCode::OK);

    // Parse segment_size
    uint32_t segment_size = strtoull(argv[arg++], NULL, 0);

    // Init DLOG keys
    assert(zendoo_init_dlog_keys(Sidechain::SEGMENT_SIZE, &ret_code));
    assert(ret_code == CctpErrorCode::OK);

    // Parse amount
    uint64_t amount = strtoull(argv[arg++], NULL, 0);

    // Parse sc_id
    assert(IsHex(argv[arg]));
    auto sc_id = ParseHex(argv[arg++]);
    assert(sc_id.size() == 32);
    field_t* sc_id_f = zendoo_deserialize_field(sc_id.data(), &ret_code);
    assert(sc_id_f != NULL);
    assert(ret_code == CctpErrorCode::OK);

    // Parse nullifier
    assert(IsHex(argv[arg]));
    auto nullifier = ParseHex(argv[arg++]);
    assert(nullifier.size() == 32);
    field_t* nullifier_f = zendoo_deserialize_field(nullifier.data(), &ret_code);
    assert(nullifier_f != NULL);
    assert(ret_code == CctpErrorCode::OK);

    // Parse mc_address
    // Extract pubKeyHash from the address
    std::vector<unsigned char> vchData;
    assert(DecodeBase58(argv[arg++], vchData));
    uint160 pk_dest;
    memcpy(&pk_dest, &vchData[2], 20);

    /*uint160 pk_dest;
    CBitcoinAddress address(argv[arg++]);
    CKeyID keyId;
    assert(!address.GetKeyID(keyId));
    pk_dest = keyId;
    assert(pk_dest.size() == 20);*/

    auto mc_pk_hash = BufferWithSize(pk_dest.begin(), pk_dest.size());

    // Parse end_cum_comm_tree_root
    assert(IsHex(argv[arg]));
    auto end_cum_comm_tree_root = ParseHex(argv[arg++]);
    assert(end_cum_comm_tree_root.size() == 32);
    field_t* end_cum_comm_tree_root_f = zendoo_deserialize_field(end_cum_comm_tree_root.data(), &ret_code);
    assert(end_cum_comm_tree_root_f != NULL);
    assert(ret_code == CctpErrorCode::OK);

    // Parse num_constraints
    uint32_t num_constraints = strtoull(argv[arg++], NULL, 0);

    // Parse cert_data_hash if present
    field_t* cert_data_hash_f;
    assert(arg <= argc);
    if (std::string(argv[arg]) == "NO_CERT_DATA_HASH") {
        arg++;
        cert_data_hash_f = NULL;
    } else {
        assert(IsHex(argv[arg]));
        auto cert_data_hash = ParseHex(argv[arg++]);
        assert(cert_data_hash.size() == 32);
        cert_data_hash_f = zendoo_deserialize_field(cert_data_hash.data(), &ret_code);
        assert(cert_data_hash_f != NULL);
        assert(ret_code == CctpErrorCode::OK);
    }

    // Parse constant if present
    field_t* constant_f = NULL;
    if (csw_type_raw == "csw") {
        assert(arg <= argc);
        assert(IsHex(argv[arg]));
        auto constant = ParseHex(argv[arg++]);
        assert(constant.size() == 32);
        constant_f = zendoo_deserialize_field(constant.data(), &ret_code);
        assert(constant_f != NULL);
        assert(ret_code == CctpErrorCode::OK);
    }

    // Generate proof and vk
    assert(zendoo_create_csw_test_proof(
        zk,
        amount,
        constant_f,
        sc_id_f,
        nullifier_f,
        &mc_pk_hash,
        cert_data_hash_f,
        end_cum_comm_tree_root_f,
        pk,
        (path_char_t*)proof_path.c_str(),
        proof_path_len,
        num_constraints,
        &ret_code,
        true,
        &segment_size));
    assert(ret_code == CctpErrorCode::OK);

    // If -v was specified we verify the proof just created
    if (verify) {
        // Deserialize proof
        sc_proof_t* proof = zendoo_deserialize_sc_proof_from_file(
            (path_char_t*)proof_path.c_str(),
            proof_path_len,
            true,
            &ret_code);
        assert(proof != NULL);
        assert(ret_code == CctpErrorCode::OK);

        // Deserialize vk
        auto vk_path = params_path + ps_type_raw + vk_name;
        size_t vk_path_len = vk_path.size();

        sc_vk_t* vk = zendoo_deserialize_sc_vk_from_file(
            (path_char_t*)vk_path.c_str(),
            vk_path_len,
            true,
            &ret_code);
        assert(vk != NULL);
        assert(ret_code == CctpErrorCode::OK);

        // Verify proof
        assert(zendoo_verify_csw_proof(
            amount,
            constant_f,
            sc_id_f,
            nullifier_f,
            &mc_pk_hash,
            cert_data_hash_f,
            end_cum_comm_tree_root_f,
            proof,
            vk,
            &ret_code));
        assert(ret_code == CctpErrorCode::OK);

        // Negative test
        auto wrong_amount = amount + 1;
        assert(!zendoo_verify_csw_proof(
            wrong_amount,
            constant_f,
            sc_id_f,
            nullifier_f,
            &mc_pk_hash,
            cert_data_hash_f,
            end_cum_comm_tree_root_f,
            proof,
            vk,
            &ret_code));
        assert(ret_code == CctpErrorCode::OK);

        zendoo_sc_proof_free(proof);
        zendoo_sc_vk_free(vk);
    }

    zendoo_sc_pk_free(pk);
    zendoo_field_free(constant_f);
    zendoo_field_free(sc_id_f);
    zendoo_field_free(nullifier_f);
    zendoo_field_free(cert_data_hash_f);
    zendoo_field_free(end_cum_comm_tree_root_f);
}

void create_verify(int argc, char** argv)
{
    // Get ProvingSystemType
    auto ps_type_raw = std::string(argv[3]);

    auto circ_type_raw = std::string(argv[2]);
    if (circ_type_raw == "cert") {
        assert(argc >= 17);
        create_verify_test_cert_proof(ps_type_raw, circ_type_raw, argc, argv);
    } else if (circ_type_raw == "cert_no_const") {
        assert(argc >= 16);
        create_verify_test_cert_proof(ps_type_raw, circ_type_raw, argc, argv);
    } else if (circ_type_raw == "csw") {
        assert(argc >= 14 && argc <= 17);
        create_verify_test_csw_proof(ps_type_raw, circ_type_raw, argc, argv);
    } else if (circ_type_raw == "csw_no_const") {
        assert(argc >= 13 && argc <= 16);
        create_verify_test_csw_proof(ps_type_raw, circ_type_raw, argc, argv);
    } else {
        abort(); // Invalid TestCircuitType
    }
}

void generate(char** argv)
{
    // Get TestCircuitType
    auto circ_type_raw = std::string(argv[2]);
    TestCircuitType circ_type;
    if (circ_type_raw == "cert") {
        circ_type = TestCircuitType::Certificate;
    } else if (circ_type_raw == "cert_no_const") {
        circ_type = TestCircuitType::CertificateNoConstant;
    } else if (circ_type_raw == "csw") {
        circ_type = TestCircuitType::CSW;
    } else if (circ_type_raw == "csw_no_const") {
        circ_type = TestCircuitType::CSWNoConstant;
    } else {
        abort(); // Invalid TestCircuitType
    }

    // Get ProvingSystemType
    auto ps_type_raw = std::string(argv[3]);
    ProvingSystem ps_type;
    if (ps_type_raw == "darlin") {
        ps_type = ProvingSystem::Darlin;
    } else if (ps_type_raw == "cob_marlin") {
        ps_type = ProvingSystem::CoboundaryMarlin;
    } else {
        abort(); // Invalid ProvingSystemType
    }

    // Get Path
    auto path = std::string(argv[4]);

    // Parse segment size
    uint32_t segment_size = strtoull(argv[5], NULL, 0);

    // Init DLOG keys
    CctpErrorCode ret_code = CctpErrorCode::OK;
    assert(zendoo_init_dlog_keys(Sidechain::SEGMENT_SIZE, &ret_code));
    assert(ret_code == CctpErrorCode::OK);

    // Generate proving and verifying key
    uint32_t num_constraints = strtoull(argv[6], NULL, 0);
    assert(zendoo_generate_mc_test_params(
        circ_type,
        ps_type,
        num_constraints,
        (path_char_t*)path.c_str(),
        path.size(),
        &ret_code,
        true,
        true,
        &segment_size));
    assert(ret_code == CctpErrorCode::OK);
}

int main(int argc, char** argv)
{
    if (std::string(argv[1]) == "generate") {
        assert(argc == 7);
        generate(argv);
    } else if (std::string(argv[1]) == "create") {
        assert(argc >= 13);
        create_verify(argc, argv);
    } else {
        abort();
    }
}
