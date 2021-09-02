package=crate_proc-macro2_z
$(package)_crate_name=proc-macro2
$(package)_version=1.0.29
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=b9f5105d4fdaab20335ca9565e106a5d9b82b6219b5ba735731124ac6711d23d
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
