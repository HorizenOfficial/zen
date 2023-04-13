package=libzendoo
$(package)_version=0.3.0
$(package)_download_path=https://github.com/HorizenOfficial/zendoo-mc-cryptolib/archive/
$(package)_file_name=$(package)-$($(package)_git_commit).tar.gz
$(package)_download_file=$($(package)_git_commit).tar.gz
$(package)_sha256_hash=2304b80f5442f96f91b6cdc696b795a9844342fb279dadc47d4edf4359be52f3
$(package)_git_commit=273e6998d5454208dd889a12a0c41525d6647492
$(package)_dependencies=rust $(rust_crates_z)
$(package)_patches=cargo.config

ifeq ($(host_os),mingw32)
$(package)_library_file=target/x86_64-pc-windows-gnu/release/libzendoo_mc.a
# libmcTestCall.a must be compiled with the host compiler
else
$(package)_library_file=target/release/libzendoo_mc.a
endif

$(package)_mctest_library_file=target/release/libmcTestCall.a

ifeq ($(LIBZENDOO_LEGACY_CPU),true)
$(package)_target_feature=
else
$(package)_target_feature=-C target-feature=+bmi2,+adx
endif

ifeq ($(host_os),darwin)
DOWNSTREAM_CARGO_FLAGS=MACOSX_DEPLOYMENT_TARGET=$(OSX_MIN_VERSION)
DOWNSTREAM_CXX_FLAGS=CXX_FLAGS="-mmacosx-version-min=$(OSX_MIN_VERSION) -stdlib=libc++ -isysroot $(shell xcrun --show-sdk-path)"
$(package)_target_feature+=-C link-arg=-mmacosx-version-min=$(OSX_MIN_VERSION)
endif

define $(package)_set_vars
$(package)_build_opts=  --release  --all-features
$(package)_build_opts_mingw32=--target=x86_64-pc-windows-gnu
endef

define $(package)_preprocess_cmds
  mkdir -p .cargo && \
  cat $($(package)_patch_dir)/cargo.config | sed 's|CRATE_REGISTRY|$(host_prefix)/$(CRATE_REGISTRY)|' | sed 's|DUMMY_LINKER|$(default_build_CC)|g' > .cargo/config
endef

define $(package)_build_cmds
  $(DOWNSTREAM_CARGO_FLAGS) RUSTFLAGS="$($(package)_target_feature)" cargo build $($(package)_build_opts) && \
  CXX="$(firstword $($(package)_cxx))" $(DOWNSTREAM_CXX_FLAGS) $($(package)_build_env) make mcTestLib -C mc_test
endef


define $(package)_stage_cmds
  mkdir $($(package)_staging_dir)$(host_prefix)/lib/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/zendoo/ && \
  cp $($(package)_library_file) $($(package)_mctest_library_file) $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp include/zendoo_mc.h include/mcTestCall.h $($(package)_staging_dir)$(host_prefix)/include/zendoo
endef
