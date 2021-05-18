package=crate_miniz_oxide_z
$(package)_crate_name=miniz_oxide
$(package)_version=0.4.4
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=a92518e98c078586bc6c934028adcca4c92a53d6a958196de835170a01d84e4b
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
