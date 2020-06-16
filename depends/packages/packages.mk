rust_crates := \
  crate_aes \
  crate_aesni \
  crate_aes_soft \
  crate_arrayvec \
  crate_bellman \
  crate_bitflags \
  crate_bit_vec \
  crate_blake2_rfc \
  crate_block_cipher_trait \
  crate_byte_tools \
  crate_byteorder \
  crate_constant_time_eq \
  crate_crossbeam \
  crate_digest \
  crate_fpe \
  crate_fuchsia_zircon \
  crate_fuchsia_zircon_sys \
  crate_futures_cpupool \
  crate_futures \
  crate_generic_array \
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
	crate_rand_zendoo \
	crate_lazy_static_zendoo \
	crate_libc_zendoo \
	crate_winapi_zendoo \
	crate_getrandom_zendoo \
	crate_maybe-uninit_zendoo \
	crate_c2-chacha_zendoo \
	crate_cfg-if_zendoo \
	crate_cloudabi_zendoo \
	crate_crossbeam-deque_zendoo \
	crate_crossbeam-queue_zendoo \
	crate_crossbeam-epoch_zendoo \
	crate_crossbeam-utils_zendoo \
	crate_derivative_zendoo \
	crate_memoffset_zendoo \
	crate_hex_zendoo \
	crate_syn_zendoo \
	crate_byteorder_zendoo \
	crate_either_zendoo \
	crate_num_cpus_zendoo \
	crate_ppv-lite86_zendoo \
	crate_quote_zendoo \
	crate_rand_core_zendoo \
	crate_rand_chacha_zendoo \
	crate_rayon_zendoo \
	crate_rayon-core_zendoo \
	crate_scopeguard_zendoo \
	crate_smallvec_zendoo \
	crate_proc-macro2_zendoo \
	crate_unicode-xid_zendoo \
	crate_autocfg_zendoo \
	crate_rand_hc_zendoo \
	crate_winapi_i686_pc_windows_gnu \
	crate_winapi_x86_64_pc_windows_gnu \
	crate_wasi \
	crate_hermit-abi_zendoo \
	crate_radix_trie_zendoo \
	crate_endian-type_zendoo \
	crate_nibble_vec_zendoo
	
#rust_packages := rust $(rust_crates) 
rust_packages := rust $(rust_crates) librustzcash
rust_packages_zendoo := rust $(rust_crates_zendoo) libzendoo
proton_packages := proton
zcash_packages := libgmp libsodium
packages := boost openssl libevent zeromq $(zcash_packages) googletest
native_packages := native_ccache

wallet_packages=bdb
