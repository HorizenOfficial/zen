package=crate_libc_z
$(package)_crate_name=libc
$(package)_version=0.2.97
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=12b8adadd720df158f4d70dfe7ccc6adb0472d7c55ca83445f6a5ab3e36f8fb6
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef