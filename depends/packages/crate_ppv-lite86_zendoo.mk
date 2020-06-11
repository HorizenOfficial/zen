package=crate_ppv-lite86_zendoo
$(package)_crate_name=ppv-lite86
$(package)_version=0.2.8
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=237a5ed80e274dbc66f86bd59c1e25edc039660be53194b5fe0a482e0f2612ea
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
