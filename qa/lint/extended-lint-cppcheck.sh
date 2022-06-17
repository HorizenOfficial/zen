#!/usr/bin/env bash
#
# Copyright (c) 2019-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#

export LC_ALL=C

if ! command -v cppcheck > /dev/null; then
    echo "Skipping cppcheck linting since cppcheck is not installed. Install by running \"apt install cppcheck\""
    exit 0
fi

function join_array {
    local IFS="$1"
    shift
    echo "$*"
}

# cppcheck-suppressions-list.txt is required since "-i" only ignores source files, but not headers, see "cppcheck --help" for further details.
WARNINGS=$(cppcheck --enable=all -j "$(getconf _NPROCESSORS_ONLN)" --language=c++ --std=c++17 --template=gcc --project=compile_commands.json -q -isrc/leveldb -isrc/secp256k1 -isrc/snark -isrc/univalue --suppressions-list=qa/lint/cppcheck-suppressions-list.txt 2>&1)
if [[ ${WARNINGS} != "" ]]; then
    echo "${WARNINGS}"
    echo
    echo "Advice not applicable in this specific case? Add an exception by updating"
    echo "IGNORED_WARNINGS in $0"
    # Uncomment to enforce the developer note policy "By default, declare single-argument constructors `explicit`"
    # exit 1
fi
exit 0
