package=crate_bzip2_zendoo
$(package)_crate_name=bzip2
$(package)_version=0.4.2
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=abf8012c8a15d5df745fcf258d93e6149dcf102882c8d8702d9cff778eab43a8
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
