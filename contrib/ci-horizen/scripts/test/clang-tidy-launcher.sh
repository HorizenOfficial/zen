#!/bin/bash

# Get the current commit hash
export COMMIT=`git log -1 --format="%H"`
# Get the names of the files touched by this commit
export FILELIST=`git diff-tree --no-commit-id --name-only -r $COMMIT`

# Filter out all files which do not end with .cpp, .h and .cc
FILTEREDFILELIST=()
for i in $FILELIST;
do
    if [[ $i =~ .*\.cpp$ ]] || [[ $i =~ .*\.h$ ]] || [[ $i =~ .*\.tcc$ ]];
    then
        FILTEREDFILELIST+=($i)
    fi
done

echo "°°°°°°°°°°°°°°°°°°°°"
echo "Starting static analysis with clang-tidy for commit $COMMIT on the following files:"
for i in ${FILTEREDFILELIST[@]}
do
    echo $i
done
echo "°°°°°°°°°°°°°°°°°°°°"

# Run Clang-Tidy on the filtered list
# Recommended checks - amongs those available
# --checks='modernize*, readability*, hicpp*, cppcoreguidelines*, bugprone*, performance*'
clang-tidy --checks='cppcoreguidelines*, performance*, bugprone*' ${FILTEREDFILELIST[@]} | \
# Convert the Clang-Tidy output to a format that the Codacy API accepts
codacy-clang-tidy-linux-1.3.5 | \
# Send the results to Codacy
curl -v -XPOST -L -H "project-token: $CODACY_TOKEN" \
    -H "Content-type: application/json" -d @- \
    "https://api.codacy.com/2.0/commit/$COMMIT/issuesRemoteResults"

# Signal that Codacy can use the sent results and start a new analysis
curl -v -XPOST -L -H "project-token: $CODACY_TOKEN" \
	-H "Content-type: application/json" \
	"https://api.codacy.com/2.0/commit/$COMMIT/resultsFinal"

echo
echo "°°°°°°°°°°°°°°°°°°°°"
echo "Static analysis done"
echo "°°°°°°°°°°°°°°°°°°°°"
