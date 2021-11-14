package=crate_hermit-abi_z
$(package)_crate_name=hermit-abi
$(package)_version=0.1.19
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=62b467343b94ba476dcb2500d242dadbb39557df889310ac77c5d99100aaac33
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef
