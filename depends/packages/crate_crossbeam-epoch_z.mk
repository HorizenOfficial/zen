package=crate_crossbeam-epoch_zendoo
$(package)_crate_name=crossbeam-epoch
$(package)_version=0.9.4
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=52fb27eab85b17fbb9f6fd667089e07d6a2eb8743d02639ee7f6a7a7729c9c94
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
