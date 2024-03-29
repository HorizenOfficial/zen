package=libevent
$(package)_version=2.1.8
$(package)_download_path=https://github.com/libevent/libevent/archive/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_download_file=release-$($(package)_version)-stable.tar.gz
$(package)_sha256_hash=316ddb401745ac5d222d7c529ef1eada12f58f6376a66c1118eee803cb70f83d
$(package)_patches=detect_arc4random_addrandom.patch

define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/detect_arc4random_addrandom.patch && \
  ./autogen.sh
endef

define $(package)_set_vars
  $(package)_config_opts=--disable-shared --disable-openssl --disable-libevent-regress
  $(package)_config_opts_release=--disable-debug-mode
  $(package)_config_opts_linux=--with-pic
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
endef
