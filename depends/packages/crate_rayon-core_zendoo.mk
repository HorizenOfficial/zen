package=crate_rayon-core_zendoo
$(package)_crate_name=rayon-core
$(package)_version=1.7.0
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=08a89b46efaf957e52b18062fb2f4660f8b8a4dde1807ca002690868ef2c85a9
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
