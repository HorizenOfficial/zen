#!/bin/bash

set -euo pipefail

FOLDERNAME="$1"
FILENAME="$2"
gzip_cmd="gzip"

if command -v pigz > /dev/null; then
  gzip_cmd="pigz"
fi

tar -hcf - -C "${FOLDERNAME}" . | $gzip_cmd -c | tee >(sha256sum | cut -d " " -f1 | xargs -I {} echo {}"  ${FILENAME}" > ~/"${FILENAME}.sha256") > ~/"${FILENAME}"
b2 authorize-account
b2 upload-file "${B2_BUCKET_NAME}" ~/"${FILENAME}.sha256" "${FILENAME}.sha256"
b2 upload-file --threads 20 "${B2_BUCKET_NAME}" ~/"${FILENAME}" "${FILENAME}"
