package=crate_scopeguard
$(package)_crate_name=scopeguard
$(package)_version=0.3.3
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=94258f53601af11e6a49f722422f6e3425c52b06245a5cf9bc09908b174f5e27
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
