#!/bin/bash

set -euo pipefail

git clone "${CLONE_REPO}" "${CLONE_TARGET}"
cd "${CLONE_TARGET}"
# shellcheck disable=SC1091
source environment
make
