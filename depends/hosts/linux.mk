linux_CFLAGS=-pipe
linux_CXXFLAGS=$(linux_CFLAGS)
linux_LDFLAGS=-Wl,--gc-sections

linux_release_CFLAGS=-O3 -fvisibility=hidden
linux_release_CXXFLAGS=$(linux_release_CFLAGS)

linux_debug_CFLAGS=-O1
linux_debug_CXXFLAGS=$(linux_debug_CFLAGS)

linux_debug_CPPFLAGS=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC

x86_64_linux_CC=$(CT_PREFIX)gcc
x86_64_linux_CXX=$(CT_PREFIX)g++
x86_64_linux_AR=$(CT_PREFIX)ar
x86_64_linux_RANLIB=$(CT_PREFIX)ranlib
x86_64_linux_NM=$(CT_PREFIX)nm
x86_64_linux_STRIP=$(CT_PREFIX)strip
