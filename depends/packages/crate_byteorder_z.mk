package=crate_byteorder_z
$(package)_crate_name=byteorder
$(package)_version=1.4.3
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=14c189c53d098945499cdfa7ecc63567cf3886b3332b312a5b4585d8d3a6a610
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
