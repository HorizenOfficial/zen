package=crate_crossbeam-epoch_zendoo
$(package)_crate_name=crossbeam-epoch
$(package)_version=0.9.3
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=2584f639eb95fea8c798496315b297cf81b9b58b6d30ab066a75455333cf4b12
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
