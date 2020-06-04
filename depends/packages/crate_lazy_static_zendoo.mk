package=crate_lazy_static_zendoo
$(package)_crate_name=lazy_static
$(package)_version=1.2.0
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=a374c89b9db55895453a74c1e38861d9deec0b01b405a82516e9d5de4820dea1
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef