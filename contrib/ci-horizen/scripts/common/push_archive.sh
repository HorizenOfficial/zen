#!/bin/bash

set -euo pipefail

FOLDERNAME="$1"
FILENAME="$2"

if [ "${TRAVIS_OS_NAME}" = "osx" ]; then
  # work around the "mac os tar bug", e.g. https://github.com/rust-lang/cargo/issues/8603
  sudo /usr/sbin/purge
  bash -c "${TRAVIS_BUILD_DIR}/contrib/ci-horizen/scripts/common/b2_compress_checksum_upload.sh $FOLDERNAME $FILENAME"
else
  docker run --rm -v "${TRAVIS_BUILD_DIR}:/mnt/build" -e LOCAL_USER_ID="$(id -u)" -e LOCAL_GRP_ID="$(id -g)"  --env-file <(env | grep 'DOCKER_\|B2_\|TRAVIS_') "${IMAGE_NAME}:${IMAGE_TAG}" \
    bash -c "${DOCKER_HOME}/contrib/ci-horizen/scripts/common/b2_compress_checksum_upload.sh $FOLDERNAME $FILENAME"
fi
