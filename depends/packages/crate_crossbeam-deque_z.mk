package=crate_crossbeam-deque_z
$(package)_crate_name=crossbeam-deque
$(package)_version=0.8.1
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=6455c0ca19f0d2fbf751b908d5c55c1f5cbc65e03c4225427254b46890bdde1e
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
