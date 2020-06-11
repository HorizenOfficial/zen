package=crate_memoffset_zendoo
$(package)_crate_name=memoffset
$(package)_version=0.5.4
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=b4fc2c02a7e374099d4ee95a193111f72d2110197fe200272371758f6c3643d8
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
