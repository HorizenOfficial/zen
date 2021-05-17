package=crate_libc_zendoo
$(package)_crate_name=libc
$(package)_version=0.2.70
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=3baa92041a6fec78c687fa0cc2b3fae8884f743d672cf551bed1d6dac6988d0f
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef