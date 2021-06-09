package=crate_generic_array_z
$(package)_crate_name=generic-array
$(package)_version=0.12.4
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=ffdf9f34f1447443d37393cc6c2b8313aebddcd96906caf34e54c68d8e57d7bd
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
