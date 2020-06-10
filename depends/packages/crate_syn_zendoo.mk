package=crate_syn_zendoo
$(package)_crate_name=syn
$(package)_version=0.15.44
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=9ca4b3b69a77cbe1ffc9e198781b7acb0c7365a883670e8f1c1bc66fba79a5c5
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
