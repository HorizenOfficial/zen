package=crate_hermit-abi_zendoo
$(package)_crate_name=hermit-abi
$(package)_version=0.1.13
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=91780f809e750b0a89f5544be56617ff6b1227ee485bcb06ebe10cdf89bd3b71
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
