#!/bin/bash

set -euo pipefail

docker run --rm -v "${TRAVIS_BUILD_DIR}:/mnt/build" -v "$HOME/.ccache:/mnt/.ccache" -e LOCAL_USER_ID="$(id -u)" \
  -e LOCAL_GRP_ID="$(id -g)" --env-file <(env | grep 'DOCKER_\|MAKEFLAGS') "${IMAGE_NAME}:${IMAGE_TAG}" \
  bash -c 'set -xeuo pipefail && export HOST="$(gcc -dumpmachine)" && export MAKEFLAGS="${MAKEFLAGS:-} -j $(($(nproc)+1))" \
    && cd "${DOCKER_HOME}" && /usr/bin/time -v ./zcutil/build.sh $MAKEFLAGS'
