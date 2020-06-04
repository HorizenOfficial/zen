package=crate_smallvec_zendoo
$(package)_crate_name=smallvec
$(package)_version=0.6.13
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=f7b0758c52e15a8b5e3691eae6cc559f08eee9406e548a4477ba4e67770a82b6
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
