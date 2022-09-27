#!/bin/bash

set -euo pipefail

if [[ "$OSTYPE" == "darwin"* ]]; then
  PARAMS_DIR="$HOME/Library/Application Support/ZcashParams"
else
  PARAMS_DIR="$HOME/.zcash-params"
fi

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

# download using aria2c
if [ -d "${PARAMS_DIR}" ]; then
  rm -f "${PARAMS_DIR}"/*
fi
ZC_DISABLE_ARIA2="" ZC_DISABLE_CURL=1 ZC_DISABLE_IPFS=1 ZC_DISABLE_WGET=1 "${SCRIPT_DIR}"/../../zcutil/fetch-params.sh

# download using curl
if [ -d "${PARAMS_DIR}" ]; then
  rm -f "${PARAMS_DIR}"/*
fi
ZC_DISABLE_ARIA2=1 ZC_DISABLE_CURL="" ZC_DISABLE_IPFS=1 ZC_DISABLE_WGET=1 "${SCRIPT_DIR}"/../../zcutil/fetch-params.sh

# download using wget
if [ -d "${PARAMS_DIR}" ]; then
  rm -f "${PARAMS_DIR}"/*
fi
ZC_DISABLE_ARIA2=1 ZC_DISABLE_CURL=1 ZC_DISABLE_IPFS=1 ZC_DISABLE_WGET="" "${SCRIPT_DIR}"/../../zcutil/fetch-params.sh

