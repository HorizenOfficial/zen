package=crate_crc32fast_z
$(package)_crate_name=crc32fast
$(package)_version=1.2.1
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=81156fece84ab6a9f2afdb109ce3ae577e42b1228441eded99bd77f627953b1a
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
