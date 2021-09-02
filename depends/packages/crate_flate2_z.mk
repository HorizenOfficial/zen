package=crate_flate2_z
$(package)_crate_name=flate2
$(package)_version=1.0.21
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=80edafed416a46fb378521624fab1cfa2eb514784fd8921adbe8a8d8321da811
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
