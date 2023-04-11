#!/bin/bash

# Get the current commit hash
export COMMIT=$(git log -1 --format="%H")

bash <(curl -Ls https://coverage.codacy.com/get.sh) final --commit-uuid "${COMMIT}"

curl -v -XPOST -L -H "project-token: ${CODACY_PROJECT_TOKEN}" \
	-H "Content-type: application/json" \
	"https://api.codacy.com/2.0/commit/${COMMIT}/resultsFinal"
