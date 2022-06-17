#!/usr/bin/env bash
#
# Copyright (c) 2019-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#

export LC_ALL=C

ENABLED_CHECKS=(
    "Class '.*' has a constructor with 1 argument that is not explicit."
    "Struct '.*' has a constructor with 1 argument that is not explicit."
)

IGNORED_WARNINGS=(
    ".*long line \[size\|P3\] Line with .* characters exceeds limit of .*"
    ".*long variable name \[naming\|P3\] Length of variable name .* is .*, which is longer than the threshold of .*"
)

if ! command -v oclint > /dev/null; then
    echo "Skipping oclint linting since oclint is not installed."
    exit 0
fi

function join_array {
    local IFS="$1"
    shift
    echo "$*"
}

ENABLED_CHECKS_REGEXP=$(join_array "|" "${ENABLED_CHECKS[@]}")
IGNORED_WARNINGS_REGEXP=$(join_array "|" "${IGNORED_WARNINGS[@]}")
WARNINGS=$(oclint-json-compilation-database -e src/leveldb -e src/secp256k1 -e src/univalue -e src/snark -e depends 2>&1)
    # grep -E "$ENABLED_CHECKS_REGEXP" | \
    # grep -vE "${IGNORED_WARNINGS_REGEXP}")
if [[ ${WARNINGS} != "" ]]; then
    echo "${WARNINGS}"
    echo
    echo "Advice not applicable in this specific case? Add an exception by updating"
    echo "IGNORED_WARNINGS in $0"
    # Uncomment to enforce the developer note policy "By default, declare single-argument constructors `explicit`"
    # exit 1
fi
exit 0
