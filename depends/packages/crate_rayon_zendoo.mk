package=crate_rayon_zendoo
$(package)_crate_name=rayon
$(package)_version=1.5.0
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=8b0d8e0819fadc20c74ea8373106ead0600e3a67ef1fe8da56e39b9ae7275674
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
