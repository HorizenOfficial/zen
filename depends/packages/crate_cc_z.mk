package=crate_cc_z
$(package)_crate_name=cc
$(package)_version=1.0.68
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=4a72c244c1ff497a746a7e1fb3d14bd08420ecda70c8f25c7112f2781652d787
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
