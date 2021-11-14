package=crate_serde_z
$(package)_crate_name=serde
$(package)_version=1.0.130
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=f12d06de37cf59146fbdecab66aa99f9fe4f78722e3607577a5375d66bd0c913
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
