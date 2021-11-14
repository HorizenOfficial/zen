package=crate_cc_z
$(package)_crate_name=cc
$(package)_version=1.0.72
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=22a9137b95ea06864e018375b72adfb7db6e6f68cfc8df5a04d00288050485ee
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
