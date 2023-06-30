package=librustzcash
$(package)_version=0.1
$(package)_download_path=https://github.com/zcash/$(package)/archive/
$(package)_file_name=$(package)-$($(package)_git_commit).tar.gz
$(package)_download_file=$($(package)_git_commit).tar.gz
$(package)_sha256_hash=e9a488a8bbecf7fb237a32dadd65133211ef61616d44cf55609e029837a41004
$(package)_git_commit=f5e5cb24e1bd756a02fc4a3fd2b824238ccd15ad
$(package)_dependencies=rust $(rust_crates)
$(package)_patches=cargo.config

ifeq ($(host_os),mingw32)
$(package)_library_file=target/x86_64-pc-windows-gnu/release/librustzcash.a
else
$(package)_library_file=target/release/librustzcash.a
endif

define $(package)_set_vars
$(package)_build_opts=--frozen --release
$(package)_build_opts_mingw32=--target=x86_64-pc-windows-gnu
endef

define $(package)_preprocess_cmds
  mkdir .cargo && \
  cat $($(package)_patch_dir)/cargo.config | sed 's|CRATE_REGISTRY|$(host_prefix)/$(CRATE_REGISTRY)|' | sed 's|DUMMY_LINKER|$(default_build_CC)|g'  > .cargo/config && \
  cat Cargo.toml | sed '/lto/d' | sed '/panic/d' > toml.temp && \
  cat toml.temp >  Cargo.toml && \
  rm toml.temp 
endef

define $(package)_build_cmds
  # Shutting down warning generated with the newer rust version 
  #   --> src/rustzcash.rs:61:38
  #    |
  # 61 |     static ref JUBJUB: JubjubBls12 = { JubjubBls12::new() };
  #    |                                      ^^                  ^^
  #    |
  #    = note: `#[warn(unused_braces)]` on by default
  # 
  #   warning: use of deprecated constant `std::sync::ONCE_INIT`: the `new` function is now preferred
  #   --> src/rustzcash.rs:60:1
  #    |
  # 60 | / lazy_static! {
  # 61 | |     static ref JUBJUB: JubjubBls12 = { JubjubBls12::new() };
  # 62 | | }
  #    | |_^
  #    |
  # 
  RUSTFLAGS="-A unused_braces -A deprecated" cargo build $($(package)_build_opts)
endef

define $(package)_stage_cmds
  mkdir $($(package)_staging_dir)$(host_prefix)/lib/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/ && \
  cp $($(package)_library_file) $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp include/librustzcash.h $($(package)_staging_dir)$(host_prefix)/include/
endef
