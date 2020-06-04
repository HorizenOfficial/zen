package=crate_radix_trie_zendoo
$(package)_crate_name=radix_trie
$(package)_version=0.1.6
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=3d3681b28cd95acfb0560ea9441f82d6a4504fa3b15b97bd7b6e952131820e95
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
