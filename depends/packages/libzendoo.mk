package=libzendoo
$(package)_version=0.1.0
$(package)_download_path=https://github.com/HorizenOfficial/zendoo-mc-cryptolib/archive/
$(package)_file_name=$(package)-$($(package)_git_commit).tar.gz
$(package)_download_file=$($(package)_git_commit).tar.gz
$(package)_sha256_hash=35c4820d99f5e903aac5639f246171910477112f64af9e8b369c5fc8c47f98f8
$(package)_git_commit=59462f6f9ad7c5aa557e1ae8434578b620a43812
$(package)_dependencies=rust $(rust_crates_z)
$(package)_patches=cargo.config

ifeq ($(host_os),mingw32)
$(package)_library_file=target/x86_64-pc-windows-gnu/release/zendoo_mc.lib
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
  RUSTFLAGS="-C target-feature=+bmi2,+adx --emit=asm" cargo build $($(package)_build_opts)
endef


define $(package)_stage_cmds
  mkdir $($(package)_staging_dir)$(host_prefix)/lib/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/zendoo/ && \
  cp $($(package)_library_file) $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp include/zendoo_mc.h $($(package)_staging_dir)$(host_prefix)/include/zendoo
endef
