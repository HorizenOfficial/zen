package=crate_serde_derive_z
$(package)_crate_name=serde_derive
$(package)_version=1.0.130
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=d7bc1a1ab1961464eae040d96713baa5a724a8152c1222492465b54322ec508b
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
