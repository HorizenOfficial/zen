#!/bin/bash

# Get the current commit hash
export COMMIT=$(git log -1 --format="%H")

echo "°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°"
echo "  Starting code linting with clang-tidy for commit $COMMIT"
echo "°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°"
echo

echo "Analyzing all files"

# Recommended checks - amongs those available
# --checks='modernize*, readability*, hicpp*, cppcoreguidelines*, bugprone*, performance*'
run-clang-tidy -quiet -header-filter=".*" -checks='bugprone*, cppcoreguidelines*' | \
# Filter out all the ANSI escape sequences that colour the report (--use-color is hardwired in run-clang-tidy-14)
sed -e 's/\x1b\[[0-9;]*m//g' | \
# Select only the warning lines
grep "warning:" | \
# Remove all the dependencies
grep -v "include/boost" | grep -v "include/openssl" | grep -v "include/sodium" | grep -v "include/leveldb" | grep -v "include/event2" | grep -v "include/gtest" | grep -v "include/gmock" | \
# Convert the Clang-Tidy output to a format that the Codacy API accepts
codacy-clang-tidy-linux-1.3.7 | \
# Send the results to Codacy
curl -v -XPOST -L -H "project-token: ${CODACY_PROJECT_TOKEN}" \
    -H "Content-type: application/json" -d @- \
    "https://api.codacy.com/2.0/commit/${COMMIT}/issuesRemoteResults"

echo
echo "°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°"
echo "                                      Code linting done                                      "
echo "°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°"
