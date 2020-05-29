package=librustzcash
$(package)_version=0.1
$(package)_download_path=https://github.com/ark2038/$(package)/archive/
$(package)_file_name=master.tar.gz
$(package)_download_file=$($(package)_git_commit).tar.gz
$(package)_sha256_hash=e9a488a8bbecf7fb237a32dadd65133211ef61616d44cf55609e029837a41004
$(package)_git_commit=f5e5cb24e1bd756a02fc4a3fd2b824238ccd15ad
$(package)_dependencies=rust $(rust_crates)
$(package)_patches=cargo.config

define $(package)_preprocess_cmds
  $(call generate_unpackaged_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef

