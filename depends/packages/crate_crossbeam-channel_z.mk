package=crate_crossbeam-channel_z
$(package)_crate_name=crossbeam-channel
$(package)_version=0.5.1
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=06ed27e177f16d65f0f0c22a213e17c696ace5dd64b14258b52f9417ccb52db4
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
