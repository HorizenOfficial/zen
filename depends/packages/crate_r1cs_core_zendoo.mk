package=crate_r1cs_core_zendoo
$(package)_crate_name=r1cs-core
$(package)_version=0.1.0
#$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)

#$(package)_download_path=https://github.com/ZencashOfficial/ginger-lib/archive/
#$(package)_download_path=https://github.com/ark2038/ginger-lib/archive/
$(package)_download_path=https://github.com/ark2038/r1cs-core/archive/


#$(package)_file_name=$(package)-$($(package)_git_commit).tar.gz
$(package)_download_file=$($(package)_git_commit).tar.gz

$(package)_file_name=$($(package)_crate_name)-$($(package)_version).tar.gz

$(package)_sha256_hash=9d67d268369d600cc74d7a13c0694d08c169a8202cb4ef82b789fc88984f1f1c
$(package)_git_commit=ccb88e90f2c2713c7799bd02f8c1361dc4685eaa

#$(package)_sha256_hash=fe3afdb6223a3f5700bf972ea30a735634d938820a6e1877c56753357cc9309e
#$(package)_git_commit=3f3d29a118d2342ba56d6b8146dc49f220d1222f

$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_unpackaged_crate_checksum,$(package)) 
  #&& \
  cp .cargo-checksum.json algebra/.cargo-checksum.json && \
  cp .cargo-checksum.json proof-systems/.cargo-checksum.json && \
  cp .cargo-checksum.json primitives/.cargo-checksum.json && \
  cp .cargo-checksum.json bench-utils/.cargo-checksum.json
endef

##  cat $(host_prefix)/$(CRATE_REGISTRY)/$($(package)_crate_name)/.cargo-checksum.json
#mkdir $(host_prefix)/$(CRATE_REGISTRY)/$($(package)_crate_name)/algebra && \
#&& \cp $(host_prefix)/$(CRATE_REGISTRY)/$($(package)_crate_name)/.cargo-checksum.json $(host_prefix)/$(CRATE_REGISTRY)/$($(package)_crate_name)/algebra/ 

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef