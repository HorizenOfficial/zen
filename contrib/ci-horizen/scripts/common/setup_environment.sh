#!/bin/bash

set -euo pipefail

NEED_DOCKER_CREDS="false"
NEED_B2_CREDS="false"
NEED_GH_CREDS="false"
NEED_MAC_SIGN_CREDS="false"
NEED_WIN_SIGN_CREDS="false"
NEED_PGP_SIGN_CREDS="false"

export B2_DOWNLOAD_URL="https://f001.backblazeb2.com/file/${B2_BUCKET_NAME}/"

if [[ $TRAVIS_TAG =~ ^.*-bitcore$ ]] || [[ $TRAVIS_BRANCH =~ ^AddressIndexing.*$ ]]; then
  export ENABLE_ADDRESS_INDEX=1
  export MAKEFLAGS="${MAKEFLAGS:-} --enable-address-indexing"
fi

if [ "${TRAVIS_OS_NAME}" = "linux" ]; then
  export DOCKER_UPDATE_PACKAGES="binfmt-support containerd.io docker-ce docker-ce-cli qemu-user-static"
  export UPDATE_PACKAGES="ca-certificates curl jq openssl"
  export PIP_UPDATE_PACKAGES="python-pip python-setuptools python-wheel python-wheel-common"
  export PIP_INSTALL=""
  export PIP3_INSTALL=""
  export IMAGE_NAME=zencash/zen-builder
  export IMAGE_BASE_TAG="${DOCKER_ARCH}-${DOCKER_TARGET_OS}-${DOCKER_FROM}"
  export IMAGE_LATEST_TAG="${IMAGE_BASE_TAG}-latest"
  export DOCKER_HOME="/home/zenbuilder/build"
  if [ -z "${TRAVIS_TAG}" ]; then
    export IMAGE_TAG="${IMAGE_LATEST_TAG}"
  else
    # shellcheck disable=SC2001,SC2155
    export IMAGE_TAG="${IMAGE_BASE_TAG}-$(sed 's/[^-._a-zA-Z0-9]/-/g' <<< "${TRAVIS_TAG}")"
  fi
  if [ "${TRAVIS_BUILD_STAGE_NAME}" = "Prepare" ]; then
    # shellcheck disable=SC2155
    export DATE="$(date '+%Y-%m-%d')"
    export IMAGE_PATH="${TRAVIS_BUILD_DIR}/contrib/ci-horizen/dockerfiles/${DOCKER_ARCH}/${DOCKER_TARGET_OS}/${DOCKER_FROM}"
    NEED_DOCKER_CREDS="true"
  fi
  if [ "${TRAVIS_BUILD_STAGE_NAME}" = "Build" ]; then
    export B2_UL_COMPRESS_FOLDER="${DOCKER_HOME}"
    # shellcheck disable=SC2001,SC2155
    export B2_UL_FILENAME="${DOCKER_ARCH}-${DOCKER_TARGET_OS}-${DOCKER_FROM}-${TRAVIS_BUILD_ID}-${TRAVIS_COMMIT}$(sed 's/ //g' <<< "${MAKEFLAGS:-}").tar.gz"
    NEED_B2_CREDS="true"
  fi
  if [ "${TRAVIS_BUILD_STAGE_NAME}" = "Test" ]; then
    # shellcheck disable=SC2155
    export B2_DL_DECOMPRESS_FOLDER="${DOCKER_HOME}/$(basename "${TRAVIS_BUILD_DIR}")"
    # shellcheck disable=SC2001,SC2155
    export B2_DL_FILENAME="${DOCKER_ARCH}-${DOCKER_TARGET_OS}-${DOCKER_FROM}-${TRAVIS_BUILD_ID}-${TRAVIS_COMMIT}$(sed 's/ //g' <<< "${MAKEFLAGS:-}").tar.gz"
  fi
  if [ "${TRAVIS_BUILD_STAGE_NAME}" = "Package" ]; then
    NEED_B2_CREDS="true"
  fi
  if [ "${TRAVIS_BUILD_STAGE_NAME}" = "Sign" ]; then
    NEED_B2_CREDS="true"
    NEED_GH_CREDS="true"
    NEED_PGP_SIGN_CREDS="true"
    if [ "${DOCKER_TARGET_OS}" = "windows" ]; then
      NEED_WIN_SIGN_CREDS="true"
    fi
  fi
  if [ "${TRAVIS_BUILD_STAGE_NAME}" = "Deploy" ]; then
    export DOCKER_UPDATE_PACKAGES=""
    NEED_GH_CREDS="true"
    NEED_PGP_SIGN_CREDS="true"
  fi
  # due to new ratelimiting on hub.docker.com always login, we use a service account that has no push permissions
  echo "$DOCKER_READONLY_PASSWORD" | docker login -u "$DOCKER_READONLY_USERNAME" --password-stdin
fi

if [ "${TRAVIS_OS_NAME}" = "osx" ]; then
  export UPDATE_PACKAGES=""
  export PIP_INSTALL=""
  export PIP3_INSTALL=""
  if [ "${TRAVIS_BUILD_STAGE_NAME}" = "Prepare" ]; then
    export PIP_INSTALL="${PIP_INSTALL}"
    export PIP3_INSTALL="${PIP3_INSTALL} b2"
    export CLONE_REPO="https://github.com/HorizenOfficial/zencash-apple.git"
    export CLONE_TARGET="${HOME}/zencash-apple"
    export B2_UL_COMPRESS_FOLDER="${CLONE_TARGET}"
    export B2_UL_FILENAME="${TRAVIS_CPU_ARCH}-${TRAVIS_OS_NAME}-${TRAVIS_OSX_IMAGE}-${TRAVIS_BUILD_ID}-${TRAVIS_COMMIT}-zencash-apple.tar.gz"
    NEED_B2_CREDS="true"
  fi
  if [ "${TRAVIS_BUILD_STAGE_NAME}" = "Build" ]; then
    export PIP_INSTALL="${PIP_INSTALL}"
    export PIP3_INSTALL="${PIP3_INSTALL} b2"
    export B2_DL_DECOMPRESS_FOLDER="${HOME}/zencash-apple"
    export B2_DL_FILENAME="${TRAVIS_CPU_ARCH}-${TRAVIS_OS_NAME}-${TRAVIS_OSX_IMAGE}-${TRAVIS_BUILD_ID}-${TRAVIS_COMMIT}-zencash-apple.tar.gz"
    export B2_UL_COMPRESS_FOLDER="${TRAVIS_BUILD_DIR}"
    # shellcheck disable=SC2001,SC2155
    export B2_UL_FILENAME="${TRAVIS_CPU_ARCH}-${TRAVIS_OS_NAME}-${TRAVIS_OSX_IMAGE}-${TRAVIS_BUILD_ID}-${TRAVIS_COMMIT}$(sed 's/ //g' <<< "${MAKEFLAGS:-}").tar.gz"
    NEED_B2_CREDS="true"
  fi
  if [ "${TRAVIS_BUILD_STAGE_NAME}" = "Test" ]; then
    export PIP3_INSTALL="${PIP3_INSTALL} pyblake2 pyzmq websocket-client2 pyzmq"
    export B2_DL_DECOMPRESS_FOLDER="${TRAVIS_BUILD_DIR}"
    # shellcheck disable=SC2001,SC2155
    export B2_DL_FILENAME="${TRAVIS_CPU_ARCH}-${TRAVIS_OS_NAME}-${TRAVIS_OSX_IMAGE}-${TRAVIS_BUILD_ID}-${TRAVIS_COMMIT}$(sed 's/ //g' <<< "${MAKEFLAGS:-}").tar.gz"
    mkdir -p "$HOME/ZcashParams"
    ln -sf "$HOME/ZcashParams" "$HOME/Library/Application Support/ZcashParams"
  fi
  if [ "${TRAVIS_BUILD_STAGE_NAME}" = "Package" ]; then
    NEED_B2_CREDS="true"
  fi
  if [ "${TRAVIS_BUILD_STAGE_NAME}" = "Sign" ]; then
    NEED_B2_CREDS="true"
    NEED_GH_CREDS="true"
    NEED_PGP_SIGN_CREDS="true"
    NEED_MAC_SIGN_CREDS="true"
  fi
fi

# clear credentials when not needed
if [ "${NEED_DOCKER_CREDS}" = "false" ]; then
  export DOCKER_USERNAME=""
  export DOCKER_PASSWORD=""
  unset DOCKER_USERNAME
  unset DOCKER_PASSWORD
fi

if [ "${NEED_B2_CREDS}" = "false" ]; then
  export B2_APPLICATION_KEY_ID=""
  export B2_APPLICATION_KEY=""
  unset B2_APPLICATION_KEY_ID
  unset B2_APPLICATION_KEY
fi

if [ "${NEED_GH_CREDS}" = "false" ]; then
  export GH_USER=""
  export GH_AUTH=""
  unset GH_USER
  unset GH_AUTH
fi

if [ "${NEED_MAC_SIGN_CREDS}" = "false" ]; then
  export CERT_ARCHIVE_URL=""
  export CERT_ARCHIVE_PASSWORD=""
  export MAC_CERT_PASSWORD=""
  unset CERT_ARCHIVE_URL
  unset CERT_ARCHIVE_PASSWORD
  unset MAC_CERT_PASSWORD
fi

if [ "${NEED_WIN_SIGN_CREDS}" = "false" ]; then
  export CERT_ARCHIVE_URL=""
  export CERT_ARCHIVE_PASSWORD=""
  export WIN_CERT_PASSWORD=""
  unset CERT_ARCHIVE_URL
  unset CERT_ARCHIVE_PASSWORD
  unset WIN_CERT_PASSWORD
fi

if [ "${NEED_PGP_SIGN_CREDS}" = "false" ]; then
  export PGP_KEY_PASSWORD=""
  unset PGP_KEY_PASSWORD
fi

# clear credentials after use
export DOCKER_READONLY_USERNAME=""
export DOCKER_READONLY_PASSWORD=""
unset DOCKER_READONLY_USERNAME
unset DOCKER_READONLY_PASSWORD

set +u
