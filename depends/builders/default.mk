default_build_CC = $(CT_PREFIX)gcc
default_build_CXX = $(CT_PREFIX)g++
default_build_AR = $(CT_PREFIX)ar
default_build_RANLIB = $(CT_PREFIX)ranlib
default_build_STRIP = $(CT_PREFIX)strip
default_build_NM = $(CT_PREFIX)nm
default_build_OTOOL = otool
default_build_INSTALL_NAME_TOOL = install_name_tool

define add_build_tool_func
build_$(build_os)_$1 ?= $$(default_build_$1)
build_$(build_arch)_$(build_os)_$1 ?= $$(build_$(build_os)_$1)
build_$1=$$(build_$(build_arch)_$(build_os)_$1)
endef
$(foreach var,CC CXX AR RANLIB NM STRIP SHA256SUM DOWNLOAD OTOOL INSTALL_NAME_TOOL,$(eval $(call add_build_tool_func,$(var))))
define add_build_flags_func
build_$(build_arch)_$(build_os)_$1 += $(build_$(build_os)_$1)
build_$1=$$(build_$(build_arch)_$(build_os)_$1)
endef
$(foreach flags, CFLAGS CXXFLAGS LDFLAGS, $(eval $(call add_build_flags_func,$(flags))))
