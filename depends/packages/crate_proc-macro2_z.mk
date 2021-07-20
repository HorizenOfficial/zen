package=crate_proc-macro2_z
$(package)_crate_name=proc-macro2
$(package)_version=1.0.27
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=f0d8caf72986c1a598726adc988bb5984792ef84f5ee5aa50209145ee8077038
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
