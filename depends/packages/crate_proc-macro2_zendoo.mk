package=crate_proc-macro2_zendoo
$(package)_crate_name=proc-macro2
$(package)_version=0.4.30
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=cf3d2011ab5c909338f7887f4fc896d35932e29146c12c8d01da6b22a80ba759
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
