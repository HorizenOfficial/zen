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

rust_crates_z := \
    crate_adler_z \
    crate_bit_vec_z \
    crate_blake2_z \
    crate_bzip2_z \
    crate_bzip2_sys_z \
    crate_byte_tools_z \
    crate_cc_z \
    crate_crc32fast_z \
    crate_crypto_mac_z \
    crate_digest_z \
    crate_flate2_z \
	crate_rand_z \
	crate_lazy_static_z \
	crate_libc_z \
	crate_winapi_z \
	crate_getrandom_z \
	crate_maybe-uninit_z \
	crate_c2-chacha_z \
	crate_cfg_if_z \
	crate_cloudabi_z \
	crate_crossbeam-channel_z \
	crate_crossbeam-deque_z \
	crate_crossbeam-queue_z \
	crate_crossbeam-epoch_z \
	crate_crossbeam-utils_z \
	crate_derivative_z \
	crate_generic_array_z \
	crate_memoffset_z \
	crate_hex_z \
	crate_syn_z \
	crate_byteorder_z \
	crate_either_z \
    crate_miniz_oxide_z \
	crate_num_cpus_z \
	crate_opaque_debug_z \
	crate_ppv-lite86_z \
	crate_quote_z \
	crate_rand_core_z \
	crate_rand_chacha_z \
	crate_rayon_z \
	crate_rayon-core_z \
	crate_scopeguard_z \
	crate_smallvec_z \
	crate_pkg-config_z \
	crate_proc-macro2_z \
	crate_autocfg_z \
	crate_rand_hc_z \
	crate_semver_z \
	crate_semver-parser_z \
	crate_serde_z \
	crate_serde_derive_z \
	crate_sha1_z \
	crate_subtle_z \
	crate_typenum_z \
	crate_unroll_z \
	crate_unicode_xid_z \
	crate_winapi_i686_pc_windows_gnu \
	crate_winapi_x86_64_pc_windows_gnu \
	crate_wasi \
	crate_hermit-abi_z \
	crate_radix_trie_z \
	crate_rustc_version_z \
	crate_endian-type_z \
	crate_nibble_vec_z \
	crate_num_bigint_z \
	crate_num_integer_z \
	crate_num_traits_z
	
#rust_packages := rust $(rust_crates) 
rust_packages := rust $(rust_crates) librustzcash
rust_packages_zendoo := rust $(rust_crates_z) libzendoo
proton_packages := proton
zcash_packages := libgmp libsodium
packages := boost openssl libevent zeromq $(zcash_packages) googletest
native_packages := native_ccache

wallet_packages=bdb
