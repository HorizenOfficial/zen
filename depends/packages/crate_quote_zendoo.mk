package=crate_quote_zendoo
$(package)_crate_name=quote
$(package)_version=0.6.13
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=6ce23b6b870e8f94f81fb0a363d65d86675884b34a09043c81e5562f11c1f8e1
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
