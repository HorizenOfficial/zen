package=crate_crossbeam-utils_zendoo
$(package)_crate_name=crossbeam-utils
$(package)_version=0.8.4
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=4feb231f0d4d6af81aed15928e58ecf5816aa62a2393e2c82f46973e92a9a278
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
