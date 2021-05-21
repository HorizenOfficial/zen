package=crate_typenum_z
$(package)_crate_name=typenum
$(package)_version=1.13.0
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=879f6906492a7cd215bfa4cf595b600146ccfac0c79bcbd1f3000162af5e8b06
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
