#!/bin/bash

set -euo pipefail

HASH_PACKAGE_LIST_REGISTRY=""
HASH_PACKAGE_LIST_LOCAL="differs from registry"

if [ -z "${TRAVIS_TAG}" ]; then

  # Build the new image
  docker build --force-rm --pull --no-cache -t "${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE}" "${IMAGE_PATH}"

  # Remove unused docker images
  docker image prune -f

  # Check if the "latest" tagged image already exists in the remote registry
  LATEST_IN_REMOTE_REGISTY=$(docker manifest inspect "${IMAGE_NAME}:${IMAGE_LATEST_TAG}" > /dev/null ; echo $?)

  # Check if the latest tagged image is already in the local registry
  LATEST_IN_LOCAL_REGISTY="$(docker images -q ${IMAGE_NAME}:${IMAGE_LATEST_TAG} 2> /dev/null)"

  if [ "${DOCKER_IS_DEB}" = "true" ]; then

    # Get hash value for dated tagged image just built
    HASH_PACKAGE_LIST_LOCAL=$(docker run --rm --entrypoint='' "${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE}" bash -c 'dpkg -l | sha256sum | cut -d " " -f 1')

    if [ "${LATEST_IN_REMOTE_REGISTY}" == "0" ]; then

      echo "${IMAGE_NAME}:${IMAGE_LATEST_TAG} - Already Exists in remote registry"

      # If the latest is already in the local registry then delete it so the remote image will be downloaded next
      if [ -n "${LATEST_IN_LOCAL_REGISTY}" ]; then
        echo "Deleting latest from local registry ${IMAGE_NAME}:${IMAGE_LATEST_TAG}"
        docker image rm "${IMAGE_NAME}:${IMAGE_LATEST_TAG}"
      fi

      # Get hash value for image in remote registry 
      HASH_PACKAGE_LIST_REGISTRY=$(docker run --rm --entrypoint='' "${IMAGE_NAME}:${IMAGE_LATEST_TAG}" bash -c 'dpkg -l | sha256sum | cut -d " " -f 1')
    else
      echo "${IMAGE_NAME}:${IMAGE_LATEST_TAG} - Does Not Already Exists in remote registry"
    fi
  fi

  # If the newly built image matches the latest already in the registry then delete the new image
  if [ "${HASH_PACKAGE_LIST_REGISTRY}" = "${HASH_PACKAGE_LIST_LOCAL}" ] && [ "$DOCKER_FORCE_DEPLOY" != "true" ]; then

    echo "No changes detected: Deleting locally built image (${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE})" 

    docker image rm "${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE}"
  else

    # Tag the new image with the latest tag
    # echo "Tagging ${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE} to ${IMAGE_NAME}:${IMAGE_LATEST_TAG}"
    docker tag "${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE}" "${IMAGE_NAME}:${IMAGE_LATEST_TAG}"

    # Delete the dated tag image
    docker image rm "${IMAGE_NAME}:${IMAGE_BASE_TAG}-${DATE}"

  fi
else
     echo "TRAVIS_TAG not found"
fi
