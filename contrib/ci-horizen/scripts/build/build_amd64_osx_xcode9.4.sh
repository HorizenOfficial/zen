#!/bin/bash

set -euo pipefail

cd "${TRAVIS_BUILD_DIR}"
export MAKEFLAGS="${MAKEFLAGS:-} -j $(($(nproc)+1))"
export LIBTOOLIZE=glibtoolize
# travis-ci has a 4MB log size limitation after which any job is terminated
# due to stdout verbosity we only display stderr
# stdout and stderr are saved in build.log, the last 100 lines are displayed on error
# shellcheck disable=SC2086
time ./zcutil/build-mac-clang.sh $MAKEFLAGS > >(tee -a "${TRAVIS_BUILD_DIR}/build.log" > /dev/null) 2> >(tee -a "${TRAVIS_BUILD_DIR}/build.log" >&2) || (tail -n 100 "${TRAVIS_BUILD_DIR}/build.log"; false)
