package=crate_bit_vec_z
$(package)_crate_name=bit-vec
$(package)_version=0.6.3
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=349f9b6a179ed607305526ca489b34ad0a41aed5f7980fa90eb03160b69598fb
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
