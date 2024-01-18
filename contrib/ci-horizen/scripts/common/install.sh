#!/bin/bash

set -euo pipefail

if [ "${TRAVIS_OS_NAME}" = "linux" ]; then
  sudo apt-get update
  # shellcheck disable=SC2086
  sudo apt-get -y --no-install-recommends install ${UPDATE_PACKAGES}
  if [ -n "${DOCKER_UPDATE_PACKAGES}" ]; then
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
    sudo add-apt-repository "deb [arch=${TRAVIS_CPU_ARCH}] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
    sudo apt-get update
    sudo systemctl stop containerd.service
    sudo systemctl stop docker.service
    sudo systemctl stop docker.socket
    sudo apt-get purge containerd docker.io runc
    sudo apt-get autoremove --purge
    sudo rm -f /etc/default/docker
    # shellcheck disable=SC2086
    sudo apt-get -y --no-install-recommends --reinstall -o Dpkg::Options::="--force-confnew" install ${DOCKER_UPDATE_PACKAGES}
    ls /proc/sys/fs/binfmt_misc/
    if [ "${TRAVIS_CPU_ARCH}" = "amd64" ]; then
      docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
      docker image rm multiarch/qemu-user-static:latest
    fi
  fi
  if [ -n "${PIP3_INSTALL}" ]; then
    # shellcheck disable=SC2086
    sudo pip3 install --upgrade ${PIP3_INSTALL}
  fi
fi

if [ "${TRAVIS_OS_NAME}" = "osx" ]; then
  if [ -n "${PIP3_INSTALL}" ]; then
    # shellcheck disable=SC2086
    sudo pip3 install --upgrade ${PIP3_INSTALL}
  fi
fi
