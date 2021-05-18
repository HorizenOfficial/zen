package=crate_unicode-xid_z
$(package)_crate_name=unicode-xid
$(package)_version=0.2.2
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=8ccb82d61f80a663efe1f787a51b16b5a51e3314d6ac365b08639f52387b33f3
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
