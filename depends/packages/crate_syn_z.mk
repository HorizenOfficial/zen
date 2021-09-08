package=crate_syn_z
$(package)_crate_name=syn
$(package)_version=1.0.75
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=b7f58f7e8eaa0009c5fec437aabf511bd9933e4b2d7407bd05273c01a8906ea7
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
