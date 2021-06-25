package=crate_radix_trie_z
$(package)_crate_name=radix_trie
$(package)_version=0.2.1
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=c069c179fcdc6a2fe24d8d18305cf085fdbd4f922c041943e203685d6a1c58fd
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
