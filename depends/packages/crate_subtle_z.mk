package=crate_subtle_z
$(package)_crate_name=subtle
$(package)_version=1.0.0
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=2d67a5a62ba6e01cb2192ff309324cb4875d0c451d55fe2319433abe7a05a8ee
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
