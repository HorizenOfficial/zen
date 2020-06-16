package=crate_rayon_zendoo
$(package)_crate_name=rayon
$(package)_version=1.3.0
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=db6ce3297f9c85e16621bb8cca38a06779ffc31bb8184e1be4bed2be4678a098
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
