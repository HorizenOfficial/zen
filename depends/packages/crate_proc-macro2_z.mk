package=crate_proc-macro2_z
$(package)_crate_name=proc-macro2
$(package)_version=1.0.26
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=a152013215dca273577e18d2bf00fa862b89b24169fb78c4c95aeb07992c9cec
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
