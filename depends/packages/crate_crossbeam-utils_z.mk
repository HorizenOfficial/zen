package=crate_crossbeam-utils_z
$(package)_crate_name=crossbeam-utils
$(package)_version=0.8.5
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=d82cfc11ce7f2c3faef78d8a684447b40d503d9681acebed6cb728d45940c4db
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
