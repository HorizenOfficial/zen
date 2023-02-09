package=crate_num_integer
$(package)_crate_name=num-integer
$(package)_version=0.1.45
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=225d3389fb3509a24c93f5c29eb6bde2586b98d9f016636dff58d7c6f7569cd9
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
