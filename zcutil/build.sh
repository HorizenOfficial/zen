#!/usr/bin/env bash
set -eu -o pipefail

function cmd_pref() {
    if type -p "$2" > /dev/null; then
        eval "$1=$2"
    else
        eval "$1=$3"
    fi
}

# If a g-prefixed version of the command exists, use it preferentially.
function gprefix() {
    cmd_pref "$1" "g$2" "$2"
}

gprefix READLINK readlink
cd "$(dirname "$("$READLINK" -f "$0")")/.."

# Allow user overrides to $MAKE. Typical usage for users who need it:
#   MAKE=gmake ./zcutil/build.sh -j$(nproc)
if [[ -z "${MAKE-}" ]]; then
    MAKE=make
fi

# Allow overrides to $BUILD and $HOST for porters. Most users will not need it.
#   BUILD=i686-pc-linux-gnu ./zcutil/build.sh
if [[ -z "${BUILD-}" ]]; then
    BUILD="$(./depends/config.guess)"
fi
if [[ -z "${HOST-}" ]]; then
    HOST="$BUILD"
fi

# Allow users to set arbitrary compile flags. Most users will not need this.
if [[ -z "${CONFIGURE_FLAGS-}" ]]; then
    CONFIGURE_FLAGS=""
fi

if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:

$0 --help
  Show this help message and exit.

$0 [ --enable-lcov || --disable-tests ] [ --disable-mining ] [ --enable-proton ] [ --legacy-cpu ] [--enable-address-indexing] [--use-clang] [ MAKEARGS... ]
    Build Zen and most of its transitive dependencies from
    source. MAKEARGS are applied to both dependencies and Zen itself.

  If --enable-lcov is passed, Zen is configured to add coverage
  instrumentation, thus enabling "make cov" to work.
  If --disable-tests is passed instead, the Zen tests are not built.

  If --disable-mining is passed, Zen is configured to not build any mining
  code. It must be passed after the test arguments, if present.

  If --enable-proton is passed, Zen is configured to build the Apache Qpid Proton
  library required for AMQP support. This library is not built by default.
  It must be passed after the test/mining/Rust arguments, if present.

  If --legacy-cpu is passed, libzendoo is built without bmi2 and adx compiler flags.
  These CPU flags were introduced in Intel Broadwell and AMD Excavator architectures.

  If --enable-address-indexing is passed, Zen is configured to build the code related
  to address indexing. Such feature is typically used by the Explorer to keep track
  of additional information that are not useful for normal nodes.

  If --use-clang is passed, Zen is compiled using Clang instead of GCC.
EOF
    exit 0
fi

set -x

# If --enable-lcov is the first argument, enable lcov coverage support:
LCOV_ARG=''
HARDENING_ARG='--enable-hardening'
TEST_ARG=''
if [ "x${1:-}" = 'x--enable-lcov' ]
then
    LCOV_ARG='--enable-lcov'
    HARDENING_ARG='--disable-hardening'
    shift
elif [ "x${1:-}" = 'x--disable-tests' ]
then
    TEST_ARG='--enable-tests=no'
    shift
fi

# If --disable-mining is the next argument, disable mining code:
MINING_ARG=''
if [ "x${1:-}" = 'x--disable-mining' ]
then
    MINING_ARG='--enable-mining=no'
    shift
fi

# If --enable-proton is the next argument, enable building Proton code:
PROTON_ARG='--enable-proton=no'
if [ "x${1:-}" = 'x--enable-proton' ]
then
    PROTON_ARG=''
    shift
fi

# If --legacy-cpu is the next argument, build libzendoo without +bmi2,+adx:
LIBZENDOO_LEGACY_CPU='false'
if [ "x${1:-}" = 'x--legacy-cpu' ]; then
    LIBZENDOO_LEGACY_CPU='true'
    shift
fi

set +x
# Check if CPU supports required flags, if not warn the user and exit.
if [ "$LIBZENDOO_LEGACY_CPU" = "false" ]; then
  CPU_FLAGS=''
  if command -v lscpu > /dev/null 2>&1; then
    CPU_FLAGS="$(lscpu)"
  elif [ -f "/proc/cpuinfo" ]; then
    CPU_FLAGS="$(</proc/cpuinfo)"
  else
    echo 'Warning: unable to detect CPU flags, please make sure bmi2 and adx are supported on this host.'
  fi
  if [ -n "$CPU_FLAGS" ] && ( ! grep -q 'bmi2' <<< "$CPU_FLAGS" || ! grep -q 'adx' <<< "$CPU_FLAGS" ); then
    echo "Error: bmi2 and adx CPU flags are not supported on this host, please build with './zcutil/build.sh --legacy-cpu'."
    exit 1
  fi
fi
set -x

# If --enable-address-indexing is the next argument, enable building AddressIndexing code:
ADDRESSINDEXING_ARG=''
if [ "x${1:-}" = 'x--enable-address-indexing' ]
then
    ADDRESSINDEXING_ARG='--enable-address-indexing'
    shift
fi

# If --use-clang is true, use Clang as the compiler (instead of gcc):
CLANG_ARG='false'
if [ "x${1:-}" = 'x--use-clang' ]; then
    CLANG_ARG='true'
    shift
fi

eval "$MAKE" --version
as --version
ld -v

HOST="$HOST" BUILD="$BUILD" NO_PROTON="$PROTON_ARG" LIBZENDOO_LEGACY_CPU="$LIBZENDOO_LEGACY_CPU" CLANG_ARG="$CLANG_ARG" "$MAKE" "$@" -C ./depends/ V=1
./autogen.sh
CONFIG_SITE="$PWD/depends/$HOST/share/config.site" ./configure "$HARDENING_ARG" "$LCOV_ARG" "$TEST_ARG" "$MINING_ARG" "$PROTON_ARG" "$ADDRESSINDEXING_ARG" $CONFIGURE_FLAGS CXXFLAGS='-g'
"$MAKE" "$@" V=1
