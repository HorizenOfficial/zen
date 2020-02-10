#!/bin/bash

set -euo pipefail

HASH_PACKAGE_LIST_REGISTRY=""
HASH_PACKAGE_LIST_LOCAL="differs from registry"

if [ -z "${TRAVIS_TAG}" ]; then
  docker build --force-rm --pull --no-cache -t "${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE}" "${IMAGE_PATH}"
  if [ "${DOCKER_IS_DEB}" = "true" ]; then
    HASH_PACKAGE_LIST_LOCAL=$(docker run --rm --entrypoint='' "${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE}" bash -c 'dpkg -l | sha256sum | cut -d " " -f 1')
    HASH_PACKAGE_LIST_REGISTRY=$(docker run --rm --entrypoint='' "${IMAGE_NAME}:${IMAGE_LATEST_TAG}" bash -c 'dpkg -l | sha256sum | cut -d " " -f 1')
    echo "$HASH_PACKAGE_LIST_REGISTRY $HASH_PACKAGE_LIST_LOCAL"
    docker image rm "${IMAGE_NAME}:${IMAGE_LATEST_TAG}"
  fi
  if [ "${HASH_PACKAGE_LIST_REGISTRY}" = "${HASH_PACKAGE_LIST_LOCAL}" ] && [ "$DOCKER_FORCE_DEPLOY" != "true" ]; then
    echo "Deleting locally built image, no changes detected between ${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE} and ${IMAGE_NAME}:${IMAGE_LATEST_TAG}"
    docker image rm "${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE}"
  else
    docker tag "${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE}" "${IMAGE_NAME}:${IMAGE_LATEST_TAG}"
  fi
fi
