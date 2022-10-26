.NOTPARALLEL :

SOURCES_PATH ?= $(BASEDIR)/sources
BASE_CACHE ?= $(BASEDIR)/built
SDK_PATH ?= $(BASEDIR)/SDKs
NO_WALLET ?=
NO_UPNP ?=
PRIORITY_DOWNLOAD_PATH ?= https://downloads.horizen.io/file/depends-sources

BUILD ?= $(shell ./config.guess)
HOST ?= $(BUILD)
PATCHES_PATH = $(BASEDIR)/patches
BASEDIR = $(CURDIR)
HASH_LENGTH:=11
DOWNLOAD_CONNECT_TIMEOUT:=10
DOWNLOAD_RETRIES:=3
CRATE_REGISTRY:=vendored-sources

host:=$(BUILD)
ifneq ($(HOST),)
host:=$(HOST)
host_toolchain:=$(HOST)-
endif

ifneq ($(DEBUG),)
release_type=debug
else
release_type=release
endif

base_build_dir=$(BASEDIR)/work/build
base_staging_dir=$(BASEDIR)/work/staging
base_download_dir=$(BASEDIR)/work/download
canonical_host:=$(shell ./config.sub $(HOST))
build:=$(shell ./config.sub $(BUILD))

build_arch =$(firstword $(subst -, ,$(build)))
build_vendor=$(word 2,$(subst -, ,$(build)))
full_build_os:=$(subst $(build_arch)-$(build_vendor)-,,$(build))
build_os:=$(findstring linux,$(full_build_os))
build_os+=$(findstring darwin,$(full_build_os))
build_os:=$(strip $(build_os))
ifeq ($(build_os),)
build_os=$(full_build_os)
endif

host_arch=$(firstword $(subst -, ,$(canonical_host)))
host_vendor=$(word 2,$(subst -, ,$(canonical_host)))
full_host_os:=$(subst $(host_arch)-$(host_vendor)-,,$(canonical_host))
host_os:=$(findstring linux,$(full_host_os))
host_os+=$(findstring darwin,$(full_host_os))
host_os+=$(findstring mingw32,$(full_host_os))
host_os:=$(strip $(host_os))
ifeq ($(host_os),)
host_os=$(full_host_os)
endif

$(host_arch)_$(host_os)_prefix=$(BASEDIR)/$(host)
$(host_arch)_$(host_os)_host=$(host)
host_prefix=$($(host_arch)_$(host_os)_prefix)
build_prefix=$(host_prefix)/native
build_host=$(build)

AT_$(V):=
AT_:=@
AT:=$(AT_$(V))

all: install

ifeq ($(CLANG_ARG),true)
include hosts/$(host_os)_clang.mk
else
include hosts/$(host_os).mk
endif

include hosts/default.mk
include builders/$(build_os).mk
include builders/default.mk
include packages/packages.mk

wallet_packages_$(NO_WALLET) = $(wallet_packages)
proton_packages_$(NO_PROTON) = $(proton_packages)

packages += $($(host_arch)_$(host_os)_packages) $($(host_os)_packages) $(rust_packages) $(proton_packages_) $(wallet_packages_) $(rust_packages_zendoo)
native_packages += $($(host_arch)_$(host_os)_native_packages) $($(host_os)_native_packages)
all_packages = $(packages) $(native_packages)

meta_depends = Makefile funcs.mk builders/default.mk hosts/default.mk hosts/$(host_os).mk builders/$(build_os).mk cargo-checksum.sh

$(host_arch)_$(host_os)_native_toolchain?=$($(host_os)_native_toolchain)

include funcs.mk

toolchain_path=$($($(host_arch)_$(host_os)_native_toolchain)_prefixbin)
final_build_id_long+=$(shell $(build_SHA256SUM) config.site.in)
final_build_id+=$(shell echo -n $(final_build_id_long) | $(build_SHA256SUM) | cut -c-$(HASH_LENGTH))

$(host_prefix)/.stamp_$(final_build_id): $(all_packages)
	$(AT)rm -rf $(@D)
	$(AT)mkdir -p $(@D)
	$(AT)echo copying packages: $^
	$(AT)echo to: $(@D)

$(host_prefix)/.stamp-%_$(final_build_id): $(host_prefix)/.stamp_$(final_build_id) $*
	$(AT)cd $(@D); tar xf $($*_cached)
	$(AT)touch $@

$(host_prefix)/share/config.site : config.site.in $(foreach package,$(all_packages),$(host_prefix)/.stamp-$(package)_$(final_build_id))
	$(AT)@mkdir -p $(@D)
	$(AT)sed -e 's|@HOST@|$(host)|' \
            -e 's|@CC@|$(toolchain_path)$(host_CC)|' \
            -e 's|@CXX@|$(toolchain_path)$(host_CXX)|' \
            -e 's|@AR@|$(toolchain_path)$(host_AR)|' \
            -e 's|@RANLIB@|$(toolchain_path)$(host_RANLIB)|' \
            -e 's|@NM@|$(toolchain_path)$(host_NM)|' \
            -e 's|@STRIP@|$(toolchain_path)$(host_STRIP)|' \
            -e 's|@build_os@|$(build_os)|' \
            -e 's|@host_os@|$(host_os)|' \
            -e 's|@CFLAGS@|$(strip $(host_CFLAGS) $(host_$(release_type)_CFLAGS))|' \
            -e 's|@CXXFLAGS@|$(strip $(host_CXXFLAGS) $(host_$(release_type)_CXXFLAGS))|' \
            -e 's|@CPPFLAGS@|$(strip $(host_CPPFLAGS) $(host_$(release_type)_CPPFLAGS))|' \
            -e 's|@LDFLAGS@|$(strip $(host_LDFLAGS) $(host_$(release_type)_LDFLAGS))|' \
            -e 's|@rust_target@|$(if $(rust_rust_target_$(canonical_host)),$(rust_rust_target_$(canonical_host)),$(canonical_host))|' \
            -e 's|@no_wallet@|$(NO_WALLET)|' \
            -e 's|@debug@|$(DEBUG)|' \
            $< > $@
	$(AT)touch $@


define check_or_remove_cached
mkdir -p $(BASE_CACHE)/$(host)/$* && cd $(BASE_CACHE)/$(host)/$*; \
$(build_SHA256SUM) -c $($*_cached_checksum) >/dev/null 2>/dev/null || \
if test -f "$($*_cached)";  then echo "Checksum mismatch for $*. Forcing rebuild."; \
rm -f $($*_cached_checksum) $($*_cached); fi;
endef

define check_or_remove_sources
mkdir -p $($*_source_dir); cd $($*_source_dir); \
$(build_SHA256SUM) -c $($*_fetched) >/dev/null 2>/dev/null || \
if test -f $($*_all_sources); then echo "Checksum missing or mismatched for $* source. Forcing re-download."; \
rm -f $($*_all_sources) $($(1)_fetched); fi;
endef

cache-%:
	@$(call check_or_remove_cached,$*)

source-%:
	@$(call check_or_remove_sources,$*)

check-packages: $(foreach package,$(all_packages),cache-$(package))

check-sources: $(foreach package,$(all_packages),source-$(package))

$(host_prefix)/share/config.site: check-packages

check-packages: check-sources

install: check-packages $(host_prefix)/share/config.site


download-one: check-sources $(all_sources)

download-osx:
	@$(MAKE) -s HOST=x86_64-apple-darwin11 download-one
download-linux:
	@$(MAKE) -s HOST=x86_64-unknown-linux-gnu download-one
download-win:
	@$(MAKE) -s HOST=x86_64-w64-mingw32 download-one
download: download-osx download-linux download-win

.PHONY: install cached download-one download-osx download-linux download-win download check-packages check-sources
