package=crate_quote_z
$(package)_crate_name=quote
$(package)_version=1.0.9
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=c3d0b9745dc2debf507c8422de05d7226cc1f0644216dfdfead988f9b1ab32a7
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
