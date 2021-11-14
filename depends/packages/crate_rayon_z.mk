package=crate_rayon_z
$(package)_crate_name=rayon
$(package)_version=1.5.1
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=c06aca804d41dbc8ba42dfd964f0d01334eceb64314b9ecf7c5fad5188a06d90
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
