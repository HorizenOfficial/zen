package=crate_unroll_z
$(package)_crate_name=unroll
$(package)_version=0.1.5
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=5ad948c1cb799b1a70f836077721a92a35ac177d4daddf4c20a633786d4cf618
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
