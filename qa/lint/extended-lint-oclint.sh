#!/usr/bin/env bash
#
# Copyright (c) 2019-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#

export LC_ALL=C

if ! command -v oclint > /dev/null; then
    echo "Skipping oclint linting since oclint is not installed."
    exit 0
fi


# To disable specific rules, use "-- -disable-rule=<rule name", for example "-- -disable-rule=LongLine".
# The list of all rules can be found by running "oclint . -list-enabled-rules".
oclint-json-compilation-database -e src/leveldb -e src/secp256k1 -e src/univalue -e src/snark -e depends 2>&1
