#!/bin/bash

set -euo pipefail

cd "${B2_DL_DECOMPRESS_FOLDER}"
source environment

cd "${TRAVIS_BUILD_DIR}"
export MAKEFLAGS="-j $(($(nproc)+1))"
export LIBTOOLIZE=glibtoolize
# travis-ci has a 4MB log size limitation after which any job is terminated
# due to stdout verbosity we only display stderr
# stdout and stderr are saved in build.log, the last 100 lines are displayed on error
time ./zcutil/build-mac-clang.sh --disable-libs $MAKEFLAGS > >(tee -a "${TRAVIS_BUILD_DIR}/build.log" > /dev/null) 2> >(tee -a "${TRAVIS_BUILD_DIR}/build.log" >&2) || (tail -n 100 "${TRAVIS_BUILD_DIR}/build.log"; false)
