package=crate_bzip2_z
$(package)_crate_name=bzip2
$(package)_version=0.4.4
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=bdb116a6ef3f6c3698828873ad02c3014b3c85cadb88496095628e3ef1e347f8
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
