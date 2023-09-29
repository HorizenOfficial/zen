#include "equi.h"

void genhash(const crypto_generichash_blake2b_state *ctx, u32 idx, uchar *hash) {
  crypto_generichash_blake2b_state state = *ctx;
  u32 leb = htole32(idx / HASHESPERBLAKE);
  crypto_generichash_blake2b_update(&state, (uchar *)&leb, sizeof(u32));
  uchar blakehash[HASHOUT];
  crypto_generichash_blake2b_final(&state, blakehash, HASHOUT);
  memcpy(hash, blakehash + (idx % HASHESPERBLAKE) * WN/8, WN/8);
}

int verifyrec(const crypto_generichash_blake2b_state *ctx, u32 *indices, uchar *hash, int r) {
  if (r == 0) {
    genhash(ctx, *indices, hash);
    return POW_OK;
  }
  u32 *indices1 = indices + (1 << (r-1));
  if (*indices >= *indices1)
    return POW_OUT_OF_ORDER;
  uchar hash0[WN/8], hash1[WN/8];
  int vrf0 = verifyrec(ctx, indices,  hash0, r-1);
  if (vrf0 != POW_OK)
    return vrf0;
  int vrf1 = verifyrec(ctx, indices1, hash1, r-1);
  if (vrf1 != POW_OK)
    return vrf1;
  for (int i=0; i < WN/8; i++)
    hash[i] = hash0[i] ^ hash1[i];
  int i, b = r * DIGITBITS;
  for (i = 0; i < b/8; i++)
    if (hash[i])
      return POW_NONZERO_XOR;
  if ((b%8) && hash[i] >> (8-(b%8)))
    return POW_NONZERO_XOR;
  return POW_OK;
}

int compu32(const void *pa, const void *pb) {
  u32 a = *(u32 *)pa, b = *(u32 *)pb;
  return a<b ? -1 : a==b ? 0 : +1;
}

bool duped(proof prf) {
  proof sortprf;
  memcpy(sortprf, prf, sizeof(proof));
  qsort(sortprf, PROOFSIZE, sizeof(u32), &compu32);
  for (u32 i=1; i<PROOFSIZE; i++)
    if (sortprf[i] <= sortprf[i-1])
      return true;
  return false;
}

int verify(u32 indices[PROOFSIZE], const crypto_generichash_blake2b_state *ctx) {
  if (duped(indices))
    return POW_DUPLICATE;
  uchar hash[WN/8];
  return verifyrec(ctx, indices, hash, WK);
}

