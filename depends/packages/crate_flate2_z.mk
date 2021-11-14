package=crate_flate2_z
$(package)_crate_name=flate2
$(package)_version=1.0.22
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=1e6988e897c1c9c485f43b47a529cef42fde0547f9d8d41a7062518f1d8fc53f
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
