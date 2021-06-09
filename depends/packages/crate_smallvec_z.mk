package=crate_smallvec_z
$(package)_crate_name=smallvec
$(package)_version=0.6.14
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=b97fcaeba89edba30f044a10c6a3cc39df9c3f17d7cd829dd1446cab35f890e0
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
