package=crate_syn_zendoo
$(package)_crate_name=syn
$(package)_version=1.0.71
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=ad184cc9470f9117b2ac6817bfe297307418819ba40552f9b3846f05c33d5373
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
