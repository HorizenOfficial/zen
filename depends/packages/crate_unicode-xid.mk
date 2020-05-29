package=crate_unicode-xid
$(package)_crate_name=unicode-xid
$(package)_version=0.0.4
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=8c1f860d7d29cf02cb2f3f359fd35991af3d30bac52c57d265a3c461074cb4dc
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
