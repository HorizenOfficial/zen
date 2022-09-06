#include "zendoo/zendoo_mc.h"
#include "utilstrencodings.h"
#include "sc/sidechaintypes.h"
#include "base58.h" // DecodeBase58

#include <iostream>
#include <cassert>
#include <string>
#include <unistd.h>

char const* usage = R"(
    OPERATION [OPTIONS...] params_directory [CREATE_PARAMETERS... (CERT_PAR/CSW_PAR)]

    OPERATION:
    generate         generates SNARK pk and vk for a test
    create           creates a TestCertificateProof/TestCSWProof.

    OPTIONS:
    -c circuit       circuit type {cert, csw} (default: cert)
    -k constant      constant field element (default: no constant)
    -p ps            proving system type {darlin, cob_marlin} (default: cob_marlin)
    -s segsize       segment size (default: 512)
    -n constraints   number of constraints (default: 1024)
    -r               use keyrotation (default: false)
    -v               verify the proof created (default: false)
    -z               use zero knowledge (default: false)

    CREATE_PARAMETERS (must be given in the exact order):
    output_proof_file sc_id end_cum_comm_tree_root cert_datahash

    CERT_PAR
    epoch_number quality btr_fee ft_min_amount bt_list_len mc_dest_addr_0 amount_0 mc_dest_addr_1 amount_1 mc_dest_addr_n amount_n custom_fields_list_len custom_field_0 custom_field_1

    CSW_PAR
    nullifier mc_address

    Notes: cert_datahash serves different purposes in different contexts.
    1) when creating a certificate, it represents the datahash of the last certificate, as needed to support keyrotation
    2) when creating a csw, it represents the datahash of the certificate
)";

enum class Operation {
    GENERATE,
    CREATE
};

struct Parameters {
    Operation       op              = Operation::GENERATE;
    TestCircuitType circ            = TestCircuitType::CertificateNoConstant;
    char const*     ps_type_raw     = "cob_marlin";
    ProvingSystem   ps              = ProvingSystem::CoboundaryMarlin;
    bool            keyrot          = false;
    bool            has_constant    = false;
    uint32_t        segment_size    = 512;
    uint32_t        num_constraints = 1024;
    char const*     params_dir      = nullptr;
    char const*     proof_path      = nullptr;
};

struct CreateParameters : Parameters {
    bool     verify                 = false;
    bool     zk                     = false;
    field_t* scid                   = nullptr;
    uint32_t epoch_number           = 0;
    uint64_t quality                = 0;
    field_t* constant               = nullptr;
    field_t* end_cum_comm_tree_root = nullptr;
    field_t* cert_datahash          = nullptr;
    uint64_t btr_fee                = 0;
    uint64_t ft_min_amount          = 0;
    uint64_t amount                 = 0;
    field_t* nullifier              = nullptr;

    BufferWithSize                   mc_pk_hash;
    std::vector<backward_transfer_t> bt_list;
    std::vector<field_t*>            custom_fields_list;

};

void printUsage(char const* self) {
    std::cerr << "Usage:" << std::endl << self << " " << usage << std::endl;
    exit(-1);
}

void printError(char const* func, uint32_t line, char const* msg...) {
    va_list args;
    va_start(args, msg);
    fprintf(stderr, "%s:%u - ", func, line);
    vfprintf(stderr, msg, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(-1);
}

void printError(char const* func, uint32_t line, Parameters const* pars, char const* msg...) {
    if (pars != nullptr) {
        fprintf(stderr, "PARAMETERS:          "
                        "\ncirc            = %d;"
                        "\nps_type_raw     = %s;"
                        "\nps              = %d;"
                        "\nkeyrot          = %s;"
                        "\nhas_constant    = %s;"
                        "\nsegment_size    = %u;"
                        "\nnum_constraints = %u;"
                        "\nparams_dir      = %s;"
                        "\nproof_path      = %s;"
                        "\n\n",
                        (int)pars->circ,
                        pars->ps_type_raw,
                        (int)pars->ps,
                        pars->keyrot ? "true" : "false",
                        pars->has_constant ? "true" : "false",
                        pars->segment_size,
                        pars->num_constraints,
                        pars->params_dir,
                        pars->proof_path
               );

        if (pars->op == Operation::CREATE) {
            CreateParameters const* cpars = static_cast<CreateParameters const*>(pars);
            fprintf(stderr, "CREATION PARAMETERS:       "
                            "\nverify                 = %s"
                            "\nzk                     = %s"
                            "\nscid                   = %p"
                            "\nepoch_number           = %u"
                            "\nquality                = %lu"
                            "\nconstant               = %p"
                            "\nend_cum_comm_tree_root = %p"
                            "\ncert_datahash          = %p"
                            "\nbtr_fee                = %lu"
                            "\nft_min_amount          = %lu"
                            "\namount                 = %lu"
                            "\nnullifier              = %p"
                            "\nbt_list size           = %lu"
                            "\ncustom_fields_list size= %lu"
                            "\n\n",
                            cpars->verify ? "true" : "false",
                            cpars->zk ? "true" : "false",
                            cpars->scid,
                            cpars->epoch_number,
                            cpars->quality,
                            cpars->constant,
                            cpars->end_cum_comm_tree_root,
                            cpars->cert_datahash,
                            cpars->btr_fee,
                            cpars->ft_min_amount,
                            cpars->amount,
                            cpars->nullifier,
                            cpars->bt_list.size(),
                            cpars->custom_fields_list.size()
                   );
        }
    }

    printError(func, line, msg);
}

void init(Parameters const& pars) {
    // Load DLOG keys
    CctpErrorCode ret_code = CctpErrorCode::OK;
    zendoo_init_dlog_keys(Sidechain::SEGMENT_SIZE, &ret_code);
    if (ret_code != CctpErrorCode::OK) {
        printError(__func__, __LINE__, "Failed initializing dlog keys");
    }
}

std::string get_key_path(Parameters const& pars, bool is_verification) {
    std::stringstream res;
    res << pars.params_dir << pars.ps_type_raw;
    switch (pars.circ) {
    case TestCircuitType::Certificate:
        res << "_cert_test_";
        break;

    case TestCircuitType::CertificateNoConstant:
        res << "_cert_no_const_test_";
        break;
    
    case TestCircuitType::CSW:
        res << "_csw_test_";
        break;

    case TestCircuitType::CSWNoConstant:
        res << "_csw_no_const_test_";
        break;

    default:
        printError(__func__, __LINE__, "Unknown circuit");
    }
    res << (is_verification ? "vk" : "pk");
    return res.str();
}

TestCircuitType get_circuit_type(std::string const& circ_raw, bool constant) {
    if (circ_raw == "cert") {
        return constant ? TestCircuitType::Certificate : TestCircuitType::CertificateNoConstant;
    }
    if (circ_raw == "csw") {
        return constant ? TestCircuitType::CSW : TestCircuitType::CSWNoConstant;
    }

    printError(__func__, __LINE__, "Unknown circuit: %s", circ_raw);
    return TestCircuitType::Undefined;
}

ProvingSystem get_proving_system_type(char const* ps_type_raw) {
    if (strcmp(ps_type_raw, "darlin") == 0)     return ProvingSystem::Darlin;
    if (strcmp(ps_type_raw, "cob_marlin") == 0) return ProvingSystem::CoboundaryMarlin;

    return ProvingSystem::Undefined;
}

void parse_field(char const* argv, field_t*& res) {
    CctpErrorCode ret_code = CctpErrorCode::OK;
    if (!IsHex(argv)) {
        printError(__func__, __LINE__, "Cannot parse as hex: %s", argv);
    }
    std::vector<unsigned char> temp = ParseHex(argv);
    assert(temp.size() == 32);
    res = zendoo_deserialize_field(temp.data(), &ret_code);
    if (res == nullptr || ret_code != CctpErrorCode::OK) {
        printError(__func__, __LINE__, "Failed deserializing field element: %s", argv);
    }
}

CreateParameters parse_args(int argc, char** argv, Operation op) {
    CreateParameters res;
    char opt;
    char const * circuit;

    res.op = op;

    while ((opt = getopt(argc, argv, "c:k:p:s:n:rvz")) != -1) {
        switch (opt) {
        case 'c':
            circuit = optarg;
            break;
        case 'k':
            res.has_constant = true;
            if (strcmp(optarg, "CONSTANT_PLACEHOLDER") != 0) {
                parse_field(optarg, res.constant);
            }
            break;
        case 'p':
            res.ps_type_raw = optarg;
            res.ps = get_proving_system_type(optarg);
            break;
        case 's':
            res.segment_size = strtoull(optarg, nullptr, 0);
            break;
        case 'n':
            res.num_constraints = strtoull(optarg, nullptr, 0);
            break;
        case 'r':
            res.keyrot = true;
            break;
        case 'v':
            res.verify = true;
            break;
        case 'z':
            res.zk = true;
            break;
        }
    }

    assert(optind < argc);
    res.circ = get_circuit_type(circuit, res.has_constant);

    assert(optind < argc);
    res.params_dir = argv[optind++];

    if (op == Operation::CREATE) {
        // output_file
        assert(optind < argc);
        res.proof_path = argv[optind++];

        // sidechain_id
        assert(optind < argc);
        parse_field(argv[optind++], res.scid);

        // end_cum_comm_tree_root
        assert(optind < argc);
        parse_field(argv[optind++], res.end_cum_comm_tree_root);

        // cert_datahash
        assert(optind < argc);
        if (strcmp(argv[optind], "NO_PREV_CERT_HASH") == 0) { // for cert
            res.cert_datahash = nullptr;
        } else if (strcmp(argv[optind], "NO_CERT_DATA_HASH") == 0) { //for CSW
            res.cert_datahash = nullptr;
        } else if (strcmp(argv[optind], "PHANTOM_PREV_CERT_HASH") == 0) {
            res.cert_datahash = zendoo_get_phantom_cert_data_hash();
            assert(res.cert_datahash != nullptr);
        } else {
            parse_field(argv[optind], res.cert_datahash);
        }
        optind++;

        if (res.circ == TestCircuitType::Certificate ||
            res.circ == TestCircuitType::CertificateNoConstant) {
            // epoch_number
            assert(optind < argc);
            res.epoch_number = strtoull(argv[optind++], nullptr, 0);

            // quality 
            assert(optind < argc);
            res.quality = strtoull(argv[optind++], nullptr, 0);

            // btr_fee
            assert(optind < argc);
            res.btr_fee = strtoull(argv[optind++], nullptr, 0);

            // ft_min_amount
            assert(optind < argc);
            res.ft_min_amount = strtoull(argv[optind++], nullptr, 0);

            // Create bt_list
            // Inputs must be (mc_dest_addr, amount) pairs from which construct backward_transfer_t objects
            uint32_t bt_list_length = strtoull(argv[optind++], nullptr, 0);
            // Parse backward transfer list
            res.bt_list.resize(bt_list_length);

            for (uint32_t i = 0; i < bt_list_length; ++i) {
                backward_transfer_t& bt = res.bt_list[i];

                std::vector<unsigned char> vchData;
                bool res = DecodeBase58(argv[optind++], vchData);
                assert(res);
                memcpy(&bt.pk_dest, &vchData[2], 20);

                uint64_t amount = strtoull(argv[optind++], nullptr, 0);
                assert(amount >= 0);
                bt.amount = amount;
            }
        
            // Create custom_fields
            uint32_t custom_fields_list_length = strtoull(argv[optind++], nullptr, 0);
            res.custom_fields_list.resize(custom_fields_list_length);

            for (uint32_t i = 0; i < custom_fields_list_length; ++i) {
                field_t*& field = res.custom_fields_list[i];
                parse_field(argv[optind++], field);
                assert(field != nullptr);
            }
        }
        else if (res.circ == TestCircuitType::CSW ||
                 res.circ == TestCircuitType::CSWNoConstant) {
            res.amount = strtoull(argv[optind++], nullptr, 0);
            assert(res.amount >= 0);

            // nullifier 
            assert(optind < argc);
            parse_field(argv[optind++], res.nullifier);

            // Parse mc_address
            // Extract pubKeyHash from the address
            std::vector<unsigned char> vchData;
            if (!DecodeBase58(argv[optind++], vchData)) {
                printError(__func__, __LINE__, "Failed decoding base58 mc pk hash.");
            }
            unsigned char* buf = new unsigned char[20];
            memcpy(buf, &vchData[2], 20);
            res.mc_pk_hash = BufferWithSize(buf, 20);
        }
    }
    return res;
}

void generate(Parameters const& pars) {
    init(pars);
    CctpErrorCode ret_code = CctpErrorCode::OK;
    bool res = zendoo_generate_mc_test_params(pars.circ,
                                        pars.ps,
                                        pars.num_constraints,
                                        pars.keyrot,
                                        (path_char_t const*)pars.params_dir,
                                        strlen(pars.params_dir),
                                        &ret_code);
    if (!res || ret_code != CctpErrorCode::OK) {
        printError(__func__, __LINE__, &pars, "Failed generating mc_test_params. Error code %d", ret_code);
    }
}

void create_verify_test_cert_proof(CreateParameters const& pars) {
    assert(pars.proof_path);

    CctpErrorCode ret_code = CctpErrorCode::OK;

    // Deserialize pk
    std::string pk_path = get_key_path(pars, false);
    sc_pk_t* pk = zendoo_deserialize_sc_pk_from_file(
        (path_char_t*)pk_path.c_str(),
        pk_path.size(),
        true,
        &ret_code
    );
    if (pk == nullptr || ret_code != CctpErrorCode::OK) {
        printError(__func__, __LINE__, "Failed deserializing sc pk. Error code %d", ret_code);
    }

    // Generate proof and vk
    bool res = zendoo_create_cert_test_proof(
        pars.zk,
        pars.constant,
        pars.scid,
        pars.epoch_number,
        pars.quality,
        pars.bt_list.data(),
        pars.bt_list.size(),
        (const field_t**)pars.custom_fields_list.data(),
        pars.custom_fields_list.size(),
        pars.end_cum_comm_tree_root,
        pars.btr_fee,
        pars.ft_min_amount,
        pk,
        (path_char_t*)pars.proof_path,
        strlen(pars.proof_path),
        pars.num_constraints,
        pars.cert_datahash,
        &ret_code
    );
    if (!res || ret_code != CctpErrorCode::OK) {
        printError(__func__, __LINE__, &pars, "Failed creating cert test proof. Error code %d", ret_code);
    }

    // If -v was specified we verify the proof just created
    if (pars.verify) {
        // Deserialize proof
        sc_proof_t* proof = zendoo_deserialize_sc_proof_from_file(
            (path_char_t*)pars.proof_path,
            strlen(pars.proof_path),
            true,
            &ret_code
        );
        if (proof == nullptr || ret_code != CctpErrorCode::OK) {
            printError(__func__, __LINE__, "Failed deserializing sc proof. Error code %d", ret_code);
        }

        // Deserialize vk
        std::string vk_path = get_key_path(pars, true);
        sc_vk_t* vk = zendoo_deserialize_sc_vk_from_file(
            (path_char_t*)vk_path.c_str(),
            vk_path.size(),
            true,
            &ret_code
        );
        if (vk == nullptr || ret_code != CctpErrorCode::OK) {
            printError(__func__, __LINE__, "Failed deserializing sc vk. Error code %d", ret_code);
        }

        // Verify proof
        res = zendoo_verify_certificate_proof(
            pars.constant,
            pars.scid,
            pars.epoch_number,
            pars.quality,
            pars.bt_list.data(),
            pars.bt_list.size(),
            (const field_t**)pars.custom_fields_list.data(),
            pars.custom_fields_list.size(),
            pars.end_cum_comm_tree_root,
            pars.btr_fee,
            pars.ft_min_amount,
            proof,
            vk,
            pars.cert_datahash,
            &ret_code
        );
        if (!res || ret_code != CctpErrorCode::OK) {
            printError(__func__, __LINE__, &pars, "Failed verifying cert test proof. Error code %d", ret_code);
        }

        // Negative test
        uint32_t wrong_epoch_number = pars.epoch_number + 1;
        res = zendoo_verify_certificate_proof(
            pars.constant,
            pars.scid,
            wrong_epoch_number,
            pars.quality,
            pars.bt_list.data(),
            pars.bt_list.size(),
            (const field_t**)pars.custom_fields_list.data(),
            pars.custom_fields_list.size(),
            pars.end_cum_comm_tree_root,
            pars.btr_fee,
            pars.ft_min_amount,
            proof,
            vk,
            pars.cert_datahash,
            &ret_code
        );
        if (res || ret_code != CctpErrorCode::OK) {
            printError(__func__, __LINE__, &pars, "Failed failing with wrong cert test proof. Error code %d", ret_code);
        }

        zendoo_sc_proof_free(proof);
        zendoo_sc_vk_free(vk);
    }

    zendoo_sc_pk_free(pk);
    zendoo_field_free(pars.constant);
    zendoo_field_free(pars.scid);
    zendoo_field_free(pars.end_cum_comm_tree_root);
    zendoo_field_free(pars.cert_datahash);

    for (field_t* f: pars.custom_fields_list) {
        zendoo_field_free(f);
    }
}

void create_verify_test_csw_proof(CreateParameters const& pars) {
    assert(pars.proof_path);

    CctpErrorCode ret_code = CctpErrorCode::OK;

    // Deserialize pk
    std::string pk_path = get_key_path(pars, false);
    sc_pk_t* pk = zendoo_deserialize_sc_pk_from_file(
        (path_char_t*)pk_path.c_str(),
        pk_path.size(),
        true,
        &ret_code
    );
    if (pk == nullptr || ret_code != CctpErrorCode::OK) {
        printError(__func__, __LINE__, "Failed deserializing sc pk. Error code %d", ret_code);
    }

    // Generate proof and vk
    bool res = zendoo_create_csw_test_proof(
        pars.zk,
        pars.amount,
        pars.constant,
        pars.scid,
        pars.nullifier,
        &pars.mc_pk_hash,
        pars.cert_datahash,
        pars.end_cum_comm_tree_root,
        pk,
        (path_char_t*)pars.proof_path,
        strlen(pars.proof_path),
        pars.num_constraints,
        &ret_code
    );
    if (!res || ret_code != CctpErrorCode::OK) {
        printError(__func__, __LINE__, &pars, "Failed verifying csw test proof. Error code %d", ret_code);
    }

    // If -v was specified we verify the proof just created
    if (pars.verify) {
        // Deserialize proof
        sc_proof_t* proof = zendoo_deserialize_sc_proof_from_file(
            (path_char_t*)pars.proof_path,
            strlen(pars.proof_path),
            true,
            &ret_code
        );
        if (proof == nullptr || ret_code != CctpErrorCode::OK) {
            printError(__func__, __LINE__, "Failed deserializing sc proof. Error code %d", ret_code);
        }

        // Deserialize vk
        std::string vk_path = get_key_path(pars, true);
        sc_vk_t* vk = zendoo_deserialize_sc_vk_from_file(
            (path_char_t*)vk_path.c_str(),
            vk_path.size(),
            true,
            &ret_code
        );
        if (vk == nullptr || ret_code != CctpErrorCode::OK) {
            printError(__func__, __LINE__, "Failed deserializing sc vk. Error code %d", ret_code);
        }

        // Verify proof
        res = zendoo_verify_csw_proof(
            pars.amount,
            pars.constant,
            pars.scid,
            pars.nullifier,
            &pars.mc_pk_hash,
            pars.cert_datahash,
            pars.end_cum_comm_tree_root,
            proof,
            vk,
            &ret_code
        );
        if (!res || ret_code != CctpErrorCode::OK) {
            printError(__func__, __LINE__, &pars, "Failed verifying csw test proof. Error code %d", ret_code);
        }

        // Negative test
        uint64_t wrong_amount = pars.amount + 1;
        res = zendoo_verify_csw_proof(
            wrong_amount,
            pars.constant,
            pars.scid,
            pars.nullifier,
            &pars.mc_pk_hash,
            pars.cert_datahash,
            pars.end_cum_comm_tree_root,
            proof,
            vk,
            &ret_code
        );
        if (res || ret_code != CctpErrorCode::OK) {
            printError(__func__, __LINE__, &pars, "Failed failing with wrong csw test proof. Error code %d", ret_code);
        }

        zendoo_sc_proof_free(proof);
        zendoo_sc_vk_free(vk);
    }

    zendoo_sc_pk_free(pk);
    zendoo_field_free(pars.constant);
    zendoo_field_free(pars.scid);
    zendoo_field_free(pars.nullifier);
    zendoo_field_free(pars.cert_datahash);
    zendoo_field_free(pars.end_cum_comm_tree_root);
    free((void*)pars.mc_pk_hash.data);
}

void create_verify(CreateParameters const& pars)
{
    init(pars);
    switch (pars.circ) {
    case TestCircuitType::Certificate:
    case TestCircuitType::CertificateNoConstant:
        create_verify_test_cert_proof(pars);
        break;
    
    case TestCircuitType::CSW:
    case TestCircuitType::CSWNoConstant:
        create_verify_test_csw_proof(pars);
        break;
    default:
        printError(__func__, __LINE__, "Unknown circuit");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) printUsage(argv[0]);

    if (strcmp(argv[1], "generate") == 0) {
        generate(parse_args(argc-1, argv+1, Operation::GENERATE));
    } else if (strcmp(argv[1], "create") == 0) {
        create_verify(parse_args(argc-1, argv+1, Operation::CREATE));
    } else {
        printError(__func__, __LINE__, "Unsupported operation: %s", argv[1]);
    }
}

