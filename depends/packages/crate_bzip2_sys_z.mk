package=crate_bzip2_sys_z
$(package)_crate_name=bzip2-sys
$(package)_version=0.1.11+1.0.8
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=736a955f3fa7875102d57c82b8cac37ec45224a07fd32d58f9f7a186b6cd4cdc
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
