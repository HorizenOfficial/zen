package=crate_num_traits
$(package)_crate_name=num-traits
$(package)_version=0.2.15
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=578ede34cf02f8924ab9447f50c28075b4d3e5b269972345e7e0372b38c6cdcd
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
