#!/usr/bin/env bash

set -eu -o pipefail

MAKE=make
BUILD=aarch64-unknown-linux-gnu
HOST=aarch64-unknown-linux-gnu
CXX=aarch64-unknown-linux-gnu-g++
CC=aarch64-unknown-linux-gnu-gcc
PREFIX="$(pwd)/depends/$HOST"

if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:

$0 --help
  Show this help message and exit.

$0 [ --enable-lcov || --disable-tests ] [ --disable-mining ] [ --disable-rust ] [ MAKEARGS... ]
  Build Zen and most of its transitive dependencies from
  source. MAKEARGS are applied to both dependencies and Zen itself.

  If --enable-lcov is passed, Zen is configured to add coverage
  instrumentation, thus enabling "make cov" to work.
  If --disable-tests is passed instead, the Zen tests are not built.

  If --disable-mining is passed, Zen is configured to not build any mining
  code. It must be passed after the test arguments, if present.

  If --disable-rust is passed, Zen is configured to not build any Rust language
  assets. It must be passed after mining/test arguments, if present.
EOF
    exit 0
fi

set -x
cd "$(dirname "$(readlink -f "$0")")/.."

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

# If --disable-rust is the next argument, disable Rust code:
RUST_ARG=''
if [ "x${1:-}" = 'x--disable-rust' ]
then
    RUST_ARG='--enable-rust=no'
    shift
fi
PREFIX="$(pwd)/depends/$BUILD/"

#echo '================================================'
#echo "HOST=" "$HOST" "BUILD=" "$BUILD" "NO_RUST=" "$RUST_ARG"
#echo "$MAKE" "$@" "-C ./depends/ V=1"
#echo '================================================'

HOST="$HOST" BUILD="$BUILD" NO_RUST="$RUST_ARG" "$MAKE" "$@" -C ./depends/ V=1
./autogen.sh
CC="$CC" CXX="$CXX" ./configure --prefix="${PREFIX}" --host="$HOST" --build="$BUILD" "$RUST_ARG" "$HARDENING_ARG" "$LCOV_ARG" "$TEST_ARG" "$MINING_ARG" CXXFLAGS='-fwrapv -fno-strict-aliasing -Wno-stack-protector '
"$MAKE" "$@" V=1
