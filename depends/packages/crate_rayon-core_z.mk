package=crate_rayon-core_z
$(package)_crate_name=rayon-core
$(package)_version=1.9.0
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=9ab346ac5921dc62ffa9f89b7a773907511cdfa5490c572ae9be1be33e8afa4a
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
