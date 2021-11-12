package=crate_pkg-config_z
$(package)_crate_name=pkg-config
$(package)_version=0.3.22
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=12295df4f294471248581bc09bef3c38a5e46f1e36d6a37353621a0c6c357e1f
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
