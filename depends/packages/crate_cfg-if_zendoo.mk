package=crate_cfg-if_zendoo
$(package)_crate_name=cfg-if
$(package)_version=0.1.2
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=d4c819a1287eb618df47cc647173c5c4c66ba19d888a6e50d605672aed3140de
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
