package=crate_syn_z
$(package)_crate_name=syn
$(package)_version=1.0.81
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=f2afee18b8beb5a596ecb4a2dce128c719b4ba399d34126b9e4396e3f9860966
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
