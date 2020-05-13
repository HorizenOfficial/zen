#ifndef _TEMP_ZENDOO_MC_H
#define _TEMP_ZENDOO_MC_H

#include <stdlib.h>
#include <stdint.h>

typedef struct backward_transfer{
  unsigned char pk_dest[20];
  uint64_t amount;
} backward_transfer_t;

typedef struct field field_t;
typedef struct sc_proof sc_proof_t;
typedef struct sc_vk sc_vk_t;

#ifdef WIN32
    typedef uint16_t path_char_t;
#else
    typedef uint8_t path_char_t;
#endif

field_t* zendoo_deserialize_field(const unsigned char* field_bytes);

void zendoo_field_free(field_t* field);

sc_vk_t* zendoo_deserialize_sc_vk_from_file(
        const path_char_t* vk_path,
        size_t vk_path_len);

void zendoo_sc_vk_free(sc_vk_t* sc_vk);

sc_proof_t* zendoo_deserialize_sc_proof(const unsigned char* sc_proof_bytes);

void zendoo_sc_proof_free(sc_proof_t* sc_proof);

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

#endif // _TEMP_ZENDOO_MC_H