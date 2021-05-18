package=crate_cfg_if_z
$(package)_crate_name=cfg-if
$(package)_version=1.0.0
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=baf1de4339761588bc0619e3cbc0120ee582ebb74b53b4efbf79117bd2da40fd
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
