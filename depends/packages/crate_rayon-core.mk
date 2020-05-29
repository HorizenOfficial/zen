package=crate_rayon-core
$(package)_crate_name=rayon-core
$(package)_version=1.4.1
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=b055d1e92aba6877574d8fe604a63c8b5df60f60e5982bf7ccbb1338ea527356
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
