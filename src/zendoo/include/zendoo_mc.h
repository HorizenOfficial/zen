#ifndef ZENDOO_MC_INCLUDE_H_
#define ZENDOO_MC_INCLUDE_H_

#include <stdint.h>
#include <stdlib.h>

extern "C" {

#ifdef WIN32
    typedef uint16_t path_char_t;
#else
    typedef uint8_t path_char_t;
#endif

/* Note: Functions panic if input pointers are NULL.*/

//Field related functions

    typedef struct field field_t;

    /* Get the number of bytes needed to serialize/deserialize a field. */
    size_t zendoo_get_field_size_in_bytes(void);

    /*
     * Serialize a field into `field_bytes` given an opaque pointer `field` to it.
     * It's caller's responsibility to ensure that `field_bytes` size is equal to the one
     * returned by `zendoo_get_field_size_in_bytes`. Panic if serialization was
     * unsuccessful.
     */
    void zendoo_serialize_field(
        const field_t*  field,
        unsigned char*  field_bytes
    );

    /*
     * Deserialize a field from `field_bytes` and return an opaque pointer to it.
     * It's caller's responsibility to ensure that `field_bytes` size is equal to the one
     * returned by `zendoo_get_field_size_in_bytes`. Return NULL if deserialization fails.
     */
    field_t* zendoo_deserialize_field(const unsigned char* field_bytes);

    /*
     * Free the memory from the field pointed by `field`. It's caller responsibility
     * to set `field` to NULL afterwards. If `field` was already null, the function does
     * nothing.
     */
    void zendoo_field_free(field_t* field);

//SC SNARK related functions

    typedef struct backward_transfer{
      unsigned char pk_dest[32];
      uint64_t amount;
    } backward_transfer_t;

    typedef struct sc_proof sc_proof_t;

    typedef struct sc_vk sc_vk_t;

    /* Deserialize a sc_proof from a file at path `vk_path` and return an opaque pointer to it.
     * Return NULL if the file doesn't exist, or if deserialization from it fails.
     */
    sc_vk_t* zendoo_deserialize_sc_vk_from_file(
        const path_char_t* vk_path,
        size_t vk_path_len
    );

    /*
     * Free the memory from the sc_vk pointed by `sc_vk`. It's caller responsibility
     * to set `sc_vk` to NULL afterwards. If `sc_vk` was already null, the function does
     * nothing.
     */
    void zendoo_sc_vk_free(sc_vk_t* sc_vk);

    /* Get the number of bytes needed to serialize/deserialize a sc_proof. */
    size_t zendoo_get_sc_proof_size(void);

    /*  Verify a sc_proof given an opaque pointer `sc_proof` to it, an opaque pointer
     *  to the verification key `sc_vk` and all the data needed to construct
     *  proof's public inputs. Returns `true` if proof verification was
     *  successful, false otherwise, panic if some error occured. NOTE: `constant`
     *  and `proofdata` can be NULL.
     */
    bool zendoo_verify_sc_proof(
        const unsigned char* end_epoch_mc_b_hash,
        const unsigned char* prev_end_epoch_mc_b_hash,
        const backward_transfer_t* bt_list,
        size_t bt_list_len,
        uint64_t quality,
        const field_t* constant,
        const field_t* proofdata,
        const sc_proof_t* sc_proof,
        const sc_vk_t* sc_vk
    );

    /*
     * Serialize a sc_proof into `sc_proof_bytes` given an opaque pointer `sc_proof` to it.
     * It's caller's responsibility to ensure that `sc_proof_bytes` size is equal to the one
     * returned by `zendoo_get_sc_proof_size`. Panic if serialization was unsuccessful
     */
    void zendoo_serialize_sc_proof(
        const sc_proof_t* sc_proof,
        unsigned char* sc_proof_bytes
    );

    /*
     * Deserialize a sc_proof from `sc_proof_bytes` and return an opaque pointer to it.
     * It's caller's responsibility to ensure that `sc_proof_bytes` size is equal to the one
     * returned by `zendoo_get_sc_proof_size`. Panic if deserialization fails.
     */
    sc_proof_t* zendoo_deserialize_sc_proof(const unsigned char* sc_proof_bytes);

    /*
     * Free the memory from the sc_proof pointed by `sc_proof`. It's caller responsibility
     * to set `sc_proof` to NULL afterwards. If `sc_proof` was already NULL, the function does
     * nothing.
     */
    void zendoo_sc_proof_free(sc_proof_t* sc_proof);

//Poseidon hash related functions

    /*
     * Compute the Poseidon Hash of a list of field elements `input` of len `input_len`,
     * passed as a list of opaque pointers. Returns an opaque pointer to the hash output
     * or NULL if some error occurred.
     */
    field_t* zendoo_compute_poseidon_hash(
        const field_t** input,
        size_t input_len
    );

//Poseidon-based Merkle Tree related functions

    typedef struct ginger_mt      ginger_mt_t;
    typedef struct ginger_mt_path ginger_mt_path_t;

    /*
     * Compute a ginger_mt from a list of leaves `leaves` of len `levaes_len`,
     * passed as a list of opaque pointers to `field`s. Return an opaque pointer
     * to a ginger_mt or NULL if some error occurred.
     */
    ginger_mt_t* ginger_mt_new(
        const field_t** leaves,
        size_t leaves_len
    );

    /* Return an opaque pointer to the root of a ginger_mt given an opaque pointer `tree` to it */
    field_t* ginger_mt_get_root(
        const ginger_mt_t* tree
    );

    /* Return an opaque pointer to a ginger_mt_path given:
     * - An opaque pointer `leaf` to a field representing a leaf;
     * - The index of the leaf;
     * - An opaque pointer `tree` to a ginger_mt.
     * Return NULL if some error occurred.
     */
    ginger_mt_path_t* ginger_mt_get_merkle_path(
        const field_t* leaf,
        size_t leaf_index,
        const ginger_mt_t* tree
    );

    /*
     * Verify that a ginger_mt_path, for a given leaf and merkle root, all passed
     * as opaque pointers to this function is correct. Return `true` if the ginger_mt_path
     * is correct and `false` otherwise (or if some error occurred).
     */
    bool ginger_mt_verify_merkle_path(
        const field_t* leaf,
        const field_t* mr,
        const ginger_mt_path_t* path
    );

    /*
    * Free the memory from the ginger_mt pointed by `tree`. It's caller responsibility
    * to set `tree` to NULL afterwards. If `tree` was already NULL, the function does
    * nothing.
    */
    void ginger_mt_free(
        ginger_mt_t* tree
    );

    /*
    * Free the memory from the ginger_mt_path pointed by `path`. It's caller responsibility
    * to set `path` to NULL afterwards. If `path` was already NULL, the function does
    * nothing.
    */
    void ginger_mt_path_free(
        ginger_mt_path_t* path
    );

//Test functions

    /* Get an opaque pointer to a random field element */
    field_t* zendoo_get_random_field(void);

    /* Return `true` if the fields pointed by `field_1` and `field_2` are
     * equal, and `false` otherwise.
     */
    bool zendoo_field_assert_eq(
        const field_t* field_1,
        const field_t* field_2
    );
}

#endif // ZENDOO_MC_INCLUDE_H_