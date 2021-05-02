package=crate_serde_derive_zendoo
$(package)_crate_name=serde_derive
$(package)_version=1.0.125
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=b093b7a2bb58203b5da3056c05b4ec1fed827dcfdb37347a8841695263b3d06d
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
