linux_CFLAGS=-pipe
linux_CXXFLAGS=$(linux_CFLAGS)
linux_LDFLAGS=-Wl,--gc-sections

linux_release_CFLAGS=-O1
linux_release_CXXFLAGS=$(linux_release_CFLAGS)

linux_debug_CFLAGS=-O1
linux_debug_CXXFLAGS=$(linux_debug_CFLAGS)

linux_debug_CPPFLAGS=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC

ifeq (86,$(findstring 86,$(build_arch)))
i686_linux_CC=$(CT_PREFIX)gcc -m32
i686_linux_CXX=$(CT_PREFIX)g++ -m32
i686_linux_AR=$(CT_PREFIX)ar
i686_linux_RANLIB=$(CT_PREFIX)ranlib
i686_linux_NM=$(CT_PREFIX)nm
i686_linux_STRIP=$(CT_PREFIX)strip

x86_64_linux_CC=$(CT_PREFIX)gcc -m64
x86_64_linux_CXX=$(CT_PREFIX)g++ -m64
x86_64_linux_AR=$(CT_PREFIX)ar
x86_64_linux_RANLIB=$(CT_PREFIX)ranlib
x86_64_linux_NM=$(CT_PREFIX)nm
x86_64_linux_STRIP=$(CT_PREFIX)strip
else
i686_linux_CC=$(default_host_CC) -m32
i686_linux_CXX=$(default_host_CXX) -m32
x86_64_linux_CC=$(default_host_CC) -m64
x86_64_linux_CXX=$(default_host_CXX) -m64
endif
