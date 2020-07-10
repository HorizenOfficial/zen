#!/bin/bash

set +u -eo pipefail

FOLDERNAME="$1"
FILENAME="$2"

if [ "${RENAME_FOLDER}" = "true" ] && [ ! -z "${RENAME_SUFFIX}" ]; then
  mv "${FOLDERNAME}" "${FOLDERNAME}${RENAME_SUFFIX}"
fi
if command -v aria2c 2>&1 > /dev/null; then
  aria2c --file-allocation=none --max-tries=3 --continue=true "${B2_DOWNLOAD_URL}${FILENAME}.sha256" -d "${HOME}" || FAILURE="true"
  aria2c --file-allocation=none -s4 -x4 --max-tries=3 --continue=true "${B2_DOWNLOAD_URL}${FILENAME}" -d "${HOME}" || FAILURE="true"
else
  wget --quiet --retry-connrefused --waitretry=3 --timeout=90 --continue "${B2_DOWNLOAD_URL}${FILENAME}.sha256" -O "${HOME}/${FILENAME}.sha256" || FAILURE="true"
  wget --quiet --retry-connrefused --waitretry=3 --timeout=90 --continue "${B2_DOWNLOAD_URL}${FILENAME}" -O "${HOME}/${FILENAME}" || FAILURE="true"
fi
if [ "${FAILURE}" = "true" ]; then
  if [ "${ALLOW_FAILURE}" = "true" ]; then
    exit 0
  else
    exit 1
  fi
else
  cd "${HOME}"
  sha256sum -c "${FILENAME}.sha256"
  mkdir -p "${FOLDERNAME}"
  pigz -cd "${FILENAME}" | tar -xf - -C "${FOLDERNAME}"
  if [ "${DELETE_DL_FILE}" = "true" ]; then
    rm -f "${HOME}/${FILENAME}" "${HOME}/${FILENAME}.sha256"
  fi
fi
