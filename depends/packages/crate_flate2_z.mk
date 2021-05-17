package=crate_flate2_zendoo
$(package)_crate_name=flate2
$(package)_version=1.0.20
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=cd3aec53de10fe96d7d8c565eb17f2c687bb5518a2ec453b5b1252964526abe0
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
