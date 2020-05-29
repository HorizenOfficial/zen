rust_crates := \
  crate_aes \
  crate_derivative \
  crate_hex \
  crate_rayon \
  crate_itertools \
  crate_quote \
  crate_syn \
  crate_either \
  crate_rayon-core \
  crate_unicode-xid \
  crate_cfg-if \
  crate_wasi \
  crate_ppv-lite86 \
  crate_crossbeam-deque \
  crate_crossbeam-epoch \
  crate_crossbeam-utils \
  crate_smallvec \
  crate_memoffset \
  crate_scopeguard \
  crate_fuchsia-cprng \
  crate_cloudabi \
  crate_c2-chacha \
  crate_aesni \
  crate_aes_soft \
  crate_arrayvec \
  crate_bellman \
  crate_bitflags \
  crate_bellman \
  crate_bit_vec \
  crate_blake2_rfc \
  crate_block_cipher_trait \
  crate_byte_tools \
  crate_byteorder \
  crate_constant_time_eq \
  crate_crossbeam \
  crate_digest \
  crate_ff_derive \
  crate_ff \
  crate_fpe \
  crate_fuchsia_zircon \
  crate_fuchsia_zircon_sys \
  crate_futures_cpupool \
  crate_futures \
  crate_generic_array \
  crate_group \
  crate_lazy_static \
  crate_libc \
  crate_nodrop \
  crate_num_bigint \
  crate_num_cpus \
  crate_num_integer \
  crate_num_traits \
  crate_opaque_debug \
  crate_pairing \
  crate_rand \
  crate_sapling_crypto \
  crate_stream_cipher \
  crate_typenum \
  crate_winapi_i686_pc_windows_gnu \
  crate_winapi \
  crate_winapi_x86_64_pc_windows_gnu \
  crate_zip32

rust_crates_zendoo := \
  create_rand_zendoo
  
#rust_packages := rust $(rust_crates) 
rust_packages := rust $(rust_crates) librustzcash
proton_packages := proton
zcash_packages := libgmp libsodium
packages := boost openssl libevent zeromq $(zcash_packages) googletest
native_packages := native_ccache

wallet_packages=bdb
