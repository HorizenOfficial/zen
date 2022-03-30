#!/bin/bash

set -euo pipefail

export CMD="$1"
export ARGS="$2"

if [ "${TRAVIS_OS_NAME}" = "osx" ]; then
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
