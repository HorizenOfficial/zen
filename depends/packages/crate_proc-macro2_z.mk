package=crate_proc-macro2_z
$(package)_crate_name=proc-macro2
$(package)_version=1.0.32
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=ba508cc11742c0dc5c1659771673afbab7a0efab23aa17e854cbab0837ed0b43
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
