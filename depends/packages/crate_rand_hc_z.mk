package=crate_rand_hc_z
$(package)_crate_name=rand_hc
$(package)_version=0.3.1
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=d51e9f596de227fda2ea6c84607f5558e196eeaf43c986b724ba4fb8fdf497e7
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
