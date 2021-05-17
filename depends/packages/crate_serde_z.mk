package=crate_serde_zendoo
$(package)_crate_name=serde
$(package)_version=1.0.126
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=ec7505abeacaec74ae4778d9d9328fe5a5d04253220a85c4ee022239fc996d03
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
