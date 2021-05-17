package=crate_nibble_vec_zendoo
$(package)_crate_name=nibble_vec
$(package)_version=0.0.4
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=c8d77f3db4bce033f4d04db08079b2ef1c3d02b44e86f25d08886fafa7756ffa
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
