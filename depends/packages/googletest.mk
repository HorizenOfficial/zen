package=googletest
$(package)_version=1.13.0
$(package)_download_path=https://github.com/google/$(package)/archive/refs/tags
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_download_file=v$($(package)_version).tar.gz
$(package)_sha256_hash=ad7fdba11ea011c1d925b3289cf4af2c66a352e18d4c7264392fead75e919363

define $(package)_set_vars
$(package)_cxxflags+=-std=c++17
$(package)_cxxflags_linux=-fPIC
endef

define $(package)_build_cmds
  mkdir -p ./build && \
  cd ./build && \
  cmake .. && \
  $(MAKE) CC="$($(package)_cc)" CXX="$($(package)_cxx)" AR="$($(package)_ar)" CXXFLAGS="$($(package)_cxxflags)"
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_dir)$(host_prefix)/lib && \
  install ./build/lib/libgmock.a $($(package)_staging_dir)$(host_prefix)/lib/libgmock.a && \
  install ./build/lib/libgtest.a $($(package)_staging_dir)$(host_prefix)/lib/libgtest.a && \
  cp -a ./googlemock/include $($(package)_staging_dir)$(host_prefix)/ && \
  cp -a ./googletest/include $($(package)_staging_dir)$(host_prefix)/
endef
