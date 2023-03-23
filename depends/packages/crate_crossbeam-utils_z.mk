package=crate_crossbeam-utils_z
$(package)_crate_name=crossbeam-utils
$(package)_version=0.8.7
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=b5e5bed1f1c269533fa816a0a5492b3545209a205ca1a54842be180eb63a16a6
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
