#!/bin/bash

set -euo pipefail

if [ -z "${TRAVIS_TAG}" ]; then
  echo "$DOCKER_PASSWORD" | docker login -u "$DOCKER_USERNAME" --password-stdin
  for image in $(docker image ls --filter=reference="${IMAGE_NAME}:${IMAGE_TAG}" --format "{{.Repository}}:{{.Tag}}"); do
    echo "Pushing $image to registry"
    docker push "$image"
  done
else
  AUTH_URL="https://auth.docker.io/token"
  AUTH_SERVICE="registry.docker.io"
  AUTH_SCOPE="repository:${IMAGE_NAME}:pull,push"
  AUTH_OFFLINE_TOKEN="1"
  AUTH_CLIENT_ID="shell"
  REGISTRY="https://registry.hub.docker.com/v2"
  CONTENT_TYPE="application/vnd.docker.distribution.manifest.v2+json"
  TOKEN=$(curl -s -H "Content-Type: application/json" -u "${DOCKER_USERNAME}:${DOCKER_PASSWORD}" "${AUTH_URL}?service=${AUTH_SERVICE}&scope=${AUTH_SCOPE}&offline_token=${AUTH_OFFLINE_TOKEN}&client_id=${AUTH_CLIENT_ID}" | jq -r .token)
  TAG_OLD="${IMAGE_LATEST_TAG}"
  TAG_NEW="${IMAGE_TAG}"
  echo "Tagging image ${IMAGE_NAME}:${TAG_OLD} as ${IMAGE_NAME}:${TAG_NEW}"
  MANIFEST=$(curl -s -H "Accept: ${CONTENT_TYPE}" -H "Authorization: Bearer ${TOKEN}" "${REGISTRY}/${IMAGE_NAME}/manifests/${TAG_OLD}")
  curl -s -X PUT -H "Content-Type: ${CONTENT_TYPE}" -H "Authorization: Bearer ${TOKEN}" -d "${MANIFEST}" "${REGISTRY}/${IMAGE_NAME}/manifests/${TAG_NEW}"
fi
