#!/bin/bash

# Get the current commit hash
export COMMIT=$(git log -1 --format="%H")

export CODACY_API_TOKEN="${CODACY_API_TOKEN_COVERAGE}"
export CODACY_ORGANIZATION_PROVIDER="gh"
export CODACY_USERNAME="HorizenOfficial"
export CODACY_PROJECT_NAME="zen"

bash <(curl -Ls https://coverage.codacy.com/get.sh) final --commit-uuid "${COMMIT}"

curl -v -XPOST -L -H "project-token: ${CODACY_TOKEN}" \
	-H "Content-type: application/json" \
	"https://api.codacy.com/2.0/commit/${COMMIT}/resultsFinal"
