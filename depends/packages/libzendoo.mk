package=libzendoo
$(package)_version=0.1.0
$(package)_download_path=https://github.com/ZencashOfficial/zendoo-mc-cryptolib/archive/
$(package)_file_name=$(package)-$($(package)_git_commit).tar.gz
$(package)_download_file=$($(package)_git_commit).tar.gz
$(package)_sha256_hash=5c620ee5be7b5f869740d013740a6a536ba15d22b5a27a1c91227b0fbad316fd
$(package)_git_commit=29ef2f20cd42ecc86014f0325e37025cbb6b06d2
$(package)_dependencies=rust $(rust_crates_zendoo)
$(package)_patches=cargo.config

ifeq ($(host_os),mingw32)
$(package)_library_file=target/x86_64-pc-windows-gnu/release/libzendoo_mc.lib
else
$(package)_library_file=target/release/libzendoo_mc.a
endif

define $(package)_set_vars
$(package)_build_opts=  --release  --all-features
$(package)_build_opts_mingw32=--target=x86_64-pc-windows-gnu
endef

define $(package)_preprocess_cmds
  mkdir .cargo && \
  cat $($(package)_patch_dir)/cargo.config | sed 's|CRATE_REGISTRY|$(host_prefix)/$(CRATE_REGISTRY)|' > .cargo/config
endef

define $(package)_build_cmds
  cargo build $($(package)_build_opts)
endef


define $(package)_stage_cmds
  mkdir $($(package)_staging_dir)$(host_prefix)/lib/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/zendoo/ && \
  cp $($(package)_library_file) $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp include/zendoo_mc.h $($(package)_staging_dir)$(host_prefix)/include/zendoo && \
  cp include/error.h $($(package)_staging_dir)$(host_prefix)/include/zendoo && \
  cp include/hex_utils.h $($(package)_staging_dir)$(host_prefix)/include/zendoo 
endef