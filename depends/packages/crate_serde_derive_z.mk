package=crate_serde_derive_zendoo
$(package)_crate_name=serde_derive
$(package)_version=1.0.126
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=963a7dbc9895aeac7ac90e74f34a5d5261828f79df35cbed41e10189d3804d43
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
