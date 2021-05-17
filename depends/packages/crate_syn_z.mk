package=crate_syn_zendoo
$(package)_crate_name=syn
$(package)_version=1.0.72
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=a1e8cdbefb79a9a5a65e0db8b47b723ee907b7c7f8496c76a1770b5c310bab82
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
