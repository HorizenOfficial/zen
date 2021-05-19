package=crate_bzip2_sys_z
$(package)_crate_name=bzip2-sys
$(package)_version=0.1.10+1.0.8
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=17fa3d1ac1ca21c5c4e36a97f3c3eb25084576f6fc47bf0139c1123434216c6c
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
