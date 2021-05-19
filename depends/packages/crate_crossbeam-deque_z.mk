package=crate_crossbeam-deque_z
$(package)_crate_name=crossbeam-deque
$(package)_version=0.8.0
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=94af6efb46fef72616855b036a624cf27ba656ffc9be1b9a3c931cfc7749a9a9
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
