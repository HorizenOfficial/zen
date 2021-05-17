package=crate_winapi_zendoo
$(package)_crate_name=winapi
$(package)_version=0.3.6
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=92c1eb33641e276cfa214a0522acad57be5c56b10cb348b3c5117db75f3ac4b0
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
