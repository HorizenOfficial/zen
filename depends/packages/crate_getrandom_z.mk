package=crate_getrandom_z
$(package)_crate_name=getrandom
$(package)_version=0.2.3
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=7fcd999463524c52659517fe2cea98493cfe485d10565e7b0fb07dbba7ad2753
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef