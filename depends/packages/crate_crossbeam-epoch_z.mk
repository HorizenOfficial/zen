package=crate_crossbeam-epoch_z
$(package)_crate_name=crossbeam-epoch
$(package)_version=0.9.5
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=4ec02e091aa634e2c3ada4a392989e7c3116673ef0ac5b72232439094d73b7fd
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
