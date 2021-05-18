package=crate_cloudabi_z
$(package)_crate_name=cloudabi
$(package)_version=0.0.3
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=ddfc5b9aa5d4507acaf872de71051dfd0e309860e88966e1051e462a077aac4f
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
