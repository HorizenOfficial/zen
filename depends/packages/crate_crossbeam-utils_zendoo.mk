package=crate_crossbeam-utils_zendoo
$(package)_crate_name=crossbeam-utils
$(package)_version=0.8.3
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=e7e9d99fa91428effe99c5c6d4634cdeba32b8cf784fc428a2a687f61a952c49
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
