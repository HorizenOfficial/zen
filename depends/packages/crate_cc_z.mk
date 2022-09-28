package=crate_cc_z
$(package)_crate_name=cc
$(package)_version=1.0.71
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=79c2681d6594606957bbb8631c4b90a7fcaaa72cdb714743a437b156d6a7eedd
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
