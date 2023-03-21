#!/bin/bash

# Get the current commit hash
export COMMIT=$(git log -1 --format="%H")

declare FILTEREDFILELIST

echo "°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°"
echo "  Starting code linting with clang-tidy for commit $COMMIT."
echo "°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°"
echo ""

# Call clang-tidy_getFileList.py to extract a list of meaningful file
originalIFS="$IFS"; IFS='|'
read -a FILTEREDFILELIST <<< $(./contrib/ci-horizen/scripts/test/clang-tidy_getFileList.py "${CODACY_API_TOKEN_COVERAGE}" "${COMMIT}")
IFS="$originalIFS"

if [ -n "$FILTEREDFILELIST" ] ;
then
    echo "Analyzing the following files:"
    for i in ${FILTEREDFILELIST[@]}
    do
        echo "${i}"
    done
    echo ""

    declare headerList
    # Build the list of header file to be included in the analysis. Note that currently the logic
    # behind '-header-filter' regex is broken, so it is not possible to exclude a path.
    # The purpose of this workaround is to avoid analyzing the majority of headers belonging to
    # external libraries, i.e. those in the ./depends folder
    read headerList <<< $(./contrib/ci-horizen/scripts/test/clang-tidy_getHeaders.py)

    # Run clang-tidy on the filtered list
    # Recommended checks - amongs those available
    # --checks='modernize*, readability*, hicpp*, cppcoreguidelines*, bugprone*, performance*'
    run-clang-tidy -header-filter="${headerList}" -checks='cppcoreguidelines*, performance*, bugprone*' "${FILTEREDFILELIST[@]}" | \
    # Convert the Clang-Tidy output to a format that the Codacy API accepts
    codacy-clang-tidy-linux-1.3.7 | \
    # Send the results to Codacy
    curl -v -XPOST -L -H "project-token: ${CODACY_PROJECT_TOKEN}" \
       -H "Content-type: application/json" -d @- \
       "https://api.codacy.com/2.0/commit/${COMMIT}/issuesRemoteResults"
else
    # Skip clang-tidy if there are no meaningful files
    echo "Skipping clang-tidy: no files to be analyzed"
fi

echo
echo "°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°"
echo "                     Code linting done"
echo "°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°"
