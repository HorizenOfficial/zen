#!/bin/bash

set -eo pipefail

FOLDERNAME="$1"
FILENAME="$2"

if [ "${TRAVIS_OS_NAME}" = "osx" ]; then
  bash -c "${TRAVIS_BUILD_DIR}/contrib/ci-horizen/scripts/common/b2_download_verify_decompress.sh ${FOLDERNAME} ${FILENAME}"
else
  docker run --rm -v "${TRAVIS_BUILD_DIR}/../:/mnt/build" -e LOCAL_USER_ID="$(id -u)" -e LOCAL_GRP_ID="$(id -g)" \
    -e RENAME_FOLDER="${RENAME_FOLDER}" -e RENAME_SUFFIX="${RENAME_SUFFIX}" --env-file <(env | grep 'DOCKER_\|B2_\|TRAVIS_') "${IMAGE_NAME}:${IMAGE_TAG}" \
    bash -c "${DOCKER_HOME}/$(basename ${TRAVIS_BUILD_DIR})/contrib/ci-horizen/scripts/common/b2_download_verify_decompress.sh ${FOLDERNAME} ${FILENAME}"
fi
