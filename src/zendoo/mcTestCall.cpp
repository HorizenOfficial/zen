#include <zendoo/zendoo_mc.h>
#include <utilstrencodings.h>
#include <uint256.h>
#include <iostream>
#include <cassert>
#include <string>

/*
 *  Usage:
 *       1) ./mcTest "generate" "params_dir"
 *       2) ./mcTest "create" <"-v"> "proof_path" "params_dir" "end_epoch_mc_b_hash" "prev_end_epoch_mc_b_hash" "quality"
 *           "constant" "pk_dest_0" "amount_0" "pk_dest_1" "amount_1" ... "pk_dest_n" "amount_n"
 */


void create_verify(int argc, char** argv)
{
    int arg = 2;
    bool verify = false;
    if (std::string(argv[2]) == "-v"){
        arg++;
        verify = true;
    }

    // Parse inputs
    auto proof_path = std::string(argv[arg++]);
    size_t proof_path_len = proof_path.size();

    auto pk_path = std::string(argv[arg]) + std::string("test_mc_pk");
    auto vk_path = std::string(argv[arg++]) + std::string("test_mc_vk");
    size_t pk_path_len = vk_path.size();
    size_t vk_path_len = pk_path_len;

    assert(IsHex(argv[arg]));
    auto end_epoch_mc_b_hash = uint256S(argv[arg++]);

    assert(IsHex(argv[arg]));
    auto prev_end_epoch_mc_b_hash = uint256S(argv[arg++]);

    uint64_t quality = strtoull(argv[arg++], NULL, 0);
    assert(quality >= 0);

    assert(IsHex(argv[arg]));
    auto constant = ParseHex(argv[arg++]);
    assert(constant.size() == 96);
    field_t* constant_f = zendoo_deserialize_field(constant.data());
    assert(constant_f != NULL);

    // Create bt_list
    // Inputs must be (pk_dest, amount) pairs from which construct backward_transfer objects
    assert((argc - arg) % 2 == 0);
    int bt_list_length = (argc - arg)/2;
    assert(bt_list_length >= 0);

    // Parse backward transfer list
    std::vector<backward_transfer_t> bt_list;
    bt_list.reserve(bt_list_length);
    for(int i = 0; i < bt_list_length; i ++){
        backward_transfer_t bt;

        assert(IsHex(argv[arg]));
        uint160 pk_dest;
        pk_dest.SetHex(argv[arg++]);
        std::copy(pk_dest.begin(), pk_dest.end(), std::begin(bt.pk_dest));

        uint64_t amount = strtoull(argv[arg++], NULL, 0);
        assert(amount >= 0);
        bt.amount = amount;

        bt_list.push_back(bt);
    }

    // Generate proof and vk
    assert(zendoo_create_mc_test_proof(
        end_epoch_mc_b_hash.begin(),
        prev_end_epoch_mc_b_hash.begin(),
        bt_list.data(),
        bt_list_length,
        quality,
        constant_f,
        (path_char_t*)pk_path.c_str(),
        pk_path_len,
        (path_char_t*)proof_path.c_str(),
        proof_path_len
    ));

    // If -v was specified we verify the proof just created
    if(verify) {

        // Deserialize proof
        sc_proof_t* proof = zendoo_deserialize_sc_proof_from_file(
            (path_char_t*)proof_path.c_str(),
            proof_path_len
        );
        assert(proof != NULL);

        // Deserialize vk
        sc_vk_t* vk = zendoo_deserialize_sc_vk_from_file(
            (path_char_t*)vk_path.c_str(),
            vk_path_len
        );
        assert(vk != NULL);

        // Verify proof
        assert(zendoo_verify_sc_proof(
            end_epoch_mc_b_hash.begin(),
            prev_end_epoch_mc_b_hash.begin(),
            bt_list.data(),
            bt_list_length,
            quality,
            constant_f,
            NULL,
            proof,
            vk
        ));

        zendoo_sc_proof_free(proof);
        zendoo_sc_vk_free(vk);
    }

    zendoo_field_free(constant_f);
}

void generate(char** argv)
{
    std::string path = std::string(argv[2]);
    assert(zendoo_generate_mc_test_params((path_char_t*)path.c_str(), path.size()));
}


int main(int argc, char** argv)
{
    if(std::string(argv[1]) == "generate") {
        assert(argc == 3);
        generate(argv);
    } else if (std::string(argv[1]) == "create"){
        assert(argc > 7);
        create_verify(argc, argv);
    } else {
        abort();
    }
}