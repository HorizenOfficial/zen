package=libzendoo
$(package)_version=0.2.0.1
$(package)_download_path=https://github.com/HorizenOfficial/zendoo-mc-cryptolib/archive/
$(package)_file_name=$(package)-$($(package)_git_commit).tar.gz
$(package)_download_file=$($(package)_git_commit).tar.gz
$(package)_sha256_hash=40dfbbc020e846f156ecb20d9d94c1f13e8d45357054abb1cd0f969f8fdb9975
$(package)_git_commit=7a96af9aee9adbcb43666251590425d98124ee94
$(package)_dependencies=rust
$(package)_patches=cargo.config

ifeq ($(CLANG_ARG),true)
$(package)_compiler=$(CT_PREFIX)clang
RUST_CC=CC=$($(package)_compiler)
else
$(package)_compiler=$(CT_PREFIX)gcc
RUST_CC=CC=$($(package)_compiler)
endif

ifeq ($(host_os),mingw32)
$(package)_library_file=target/x86_64-pc-windows-gnu/release/libzendoo_mc.a
# Unset custom CC
RUST_CC=
else
$(package)_library_file=target/release/libzendoo_mc.a
endif

ifeq ($(LIBZENDOO_LEGACY_CPU),true)
$(package)_target_feature=
else
$(package)_target_feature=-C target-feature=+bmi2,+adx
endif

define $(package)_set_vars
$(package)_build_opts=  --release  --all-features
$(package)_build_opts_mingw32=--target=x86_64-pc-windows-gnu
endef

define $(package)_preprocess_cmds
  mkdir .cargo && \
  cat $($(package)_patch_dir)/cargo.config | sed 's|CRATE_REGISTRY|$(host_prefix)/$(CRATE_REGISTRY)|' | sed 's|DUMMY_LINKER|$($(package)_compiler)|g' > .cargo/config
endef

define $(package)_build_cmds
  $(RUST_CC) RUSTFLAGS="$($(package)_target_feature)" cargo build $($(package)_build_opts)
endef


define $(package)_stage_cmds
  mkdir $($(package)_staging_dir)$(host_prefix)/lib/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/zendoo/ && \
  cp $($(package)_library_file) $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp include/zendoo_mc.h $($(package)_staging_dir)$(host_prefix)/include/zendoo
endef
