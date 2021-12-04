#!/bin/bash

set -euo pipefail

export CMD="$1"
export ARGS="$2"

if [ "${TRAVIS_OS_NAME}" = "osx" ]; then
  # workaround until https://github.com/zcash/zcash/pull/3538 is pulled in from upstream
  # or the work porting the test suite to python3 is pulled in
  find "${TRAVIS_BUILD_DIR}/qa" "${TRAVIS_BUILD_DIR}/src/test" -type f -name '*.py' -print0 | LC_ALL=C xargs -0 sed -i '' 's/env python2/env python/'
  bash -c 'set -xeuo pipefail && export HOST=$(gcc -dumpmachine) && export MAKEFLAGS="${MAKEFLAGS:-} -j $(($(nproc)+1))" \
    && cd "${TRAVIS_BUILD_DIR}" && ./zcutil/fetch-params.sh && time "${CMD}" ${ARGS}'
else
  docker network create --ipv6 --subnet=fd00::/48 dockerbridge
  docker run --rm -v "${TRAVIS_BUILD_DIR}:/mnt/build" -v "$HOME/.zcash-params:/mnt/.zcash-params" --tmpfs /tmp \
    -e LOCAL_USER_ID="$(id -u)" -e LOCAL_GRP_ID="$(id -g)" -e CMD -e ARGS \
    --env-file <(env | grep 'DOCKER_\|B2_\|TEST_\|TRAVIS_\|MAKEFLAGS') --network=dockerbridge "${IMAGE_NAME}:${IMAGE_TAG}" \
    bash -c 'set -xeuo pipefail && export HOST=$(gcc -dumpmachine) \
      && export MAKEFLAGS="${MAKEFLAGS:-} -j $(($(nproc)+1))" && cd "${DOCKER_HOME}" && ./zcutil/fetch-params.sh && time "${CMD}" ${ARGS}'
fi
