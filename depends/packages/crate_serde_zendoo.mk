package=crate_serde_zendoo
$(package)_crate_name=serde
$(package)_version=1.0.125
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=558dc50e1a5a5fa7112ca2ce4effcb321b0300c0d4ccf0776a9f60cd89031171
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
