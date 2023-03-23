package=libzendoo
$(package)_version=0.3.0-rc1
$(package)_download_path=https://github.com/HorizenOfficial/zendoo-mc-cryptolib/archive/
$(package)_file_name=$(package)-$($(package)_git_commit).tar.gz
$(package)_download_file=$($(package)_git_commit).tar.gz
$(package)_sha256_hash=5f895dde11609bfdbe822f0eb3bda3d6e24051ab970f2b21f318f93d7b7b7b89
$(package)_git_commit=fdcef283c81d2adf31a36bf6f2577474e09c2216
$(package)_dependencies=rust $(rust_crates_z)
$(package)_patches=cargo.config

ifeq ($(CLANG_ARG),true)
$(package)_compiler=$(CT_PREFIX)clang
else
$(package)_compiler=$(CT_PREFIX)gcc
endif

RUST_CC=CC=$($(package)_compiler)

ifeq ($(host_os),mingw32)
$(package)_library_file=target/x86_64-pc-windows-gnu/release/libzendoo_mc.a
# libmcTestCall.a must be compiled with the host compiler
LIB_CXX=CXX=$(HOST)-g++
# Unset custom CC
RUST_CC=
else
$(package)_library_file=target/release/libzendoo_mc.a
endif

$(package)_mctest_library_file=target/release/libmcTestCall.a

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
  mkdir -p .cargo && \
  cat $($(package)_patch_dir)/cargo.config | sed 's|CRATE_REGISTRY|$(host_prefix)/$(CRATE_REGISTRY)|' | sed 's|DUMMY_LINKER|$($(package)_compiler)|g' > .cargo/config
endef

define $(package)_build_cmds
  $(RUST_CC) RUSTFLAGS="$($(package)_target_feature)" cargo build $($(package)_build_opts) && \
  $(LIB_CXX) $($(package)_build_env) make mcTestLib -C mc_test
endef


define $(package)_stage_cmds
  mkdir $($(package)_staging_dir)$(host_prefix)/lib/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/zendoo/ && \
  cp $($(package)_library_file) $($(package)_mctest_library_file) $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp include/zendoo_mc.h include/mcTestCall.h $($(package)_staging_dir)$(host_prefix)/include/zendoo
endef
