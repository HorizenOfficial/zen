package=crate_pkg-config_z
$(package)_crate_name=pkg-config
$(package)_version=0.3.19
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=3831453b3449ceb48b6d9c7ad7c96d5ea673e9b470a1dc578c2ce6521230884c
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
