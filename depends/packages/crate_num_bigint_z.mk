package=crate_num_bigint
$(package)_crate_name=num-bigint
$(package)_version=0.4.3
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=f93ab6289c7b344a8a9f60f88d80aa20032336fe78da341afc91c8a2341fc75f
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
