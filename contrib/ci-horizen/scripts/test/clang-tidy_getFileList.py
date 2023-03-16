#!/usr/bin/env python3

import sys
import requests
import subprocess

# Given a commit uuid, this script returns the set of files that should be analyzed by clang-tidy.
# The set of files are those modified in one of the commit between the last one submitted (whose uuid is
# passed as argc[2]), and the last one analyzed by Codacy in the branch hisotry.
# A final filter is applied to consider only .c, .cpp, .h, .hpp and .tcc files.
# Files are returned with the complete path starting from the repository root, and are separated
# by the pipe '|' symbol.
#
# Syntax:
# python clang-tidy_getFileList.py CODACY_API_KEY start-commit-uuid

if len(sys.argv) < 3:
    exit(-1)

provider               = "gh"
remoteOrganizationName = "HorizenOfficial"
repositoryName         = "zen"

API_KEY                = sys.argv[1]
commitUuid             = sys.argv[2]

headers = {
  'Accept': 'application/json',
  'api-token': API_KEY
}

startingUrl = f'https://app.codacy.com/api/v3/analysis/organizations/{provider}/{remoteOrganizationName}/repositories/{repositoryName}/commits/{commitUuid}'
r = requests.get(startingUrl, params = {}, headers = headers)

"""
This will return a json like this:
{
  "commit": {
    "sha": "b43d03a5f8426db26ff26e9449bb8e41dc5f6ae5",
    "id": 705777153,
    "commitTimestamp": "2023-03-15T11:40:20Z",
    "authorName": "Johnny Rockstar",
    "authorEmail": "email@horizenlabs.io",
    "message": "Commit message",
    "startedAnalysis": "2023-03-15T12:52:57.140Z",
    "endedAnalysis": "2023-03-15T12:57:54.716Z",
    "isMergeCommit": false,
    "gitHref": "https://github.com/HorizenOfficial/zen/commit/b43d03a5f8426db26ff26e9449bb8e41dc5f6ff3",
    "parents": [
      "11c69e9b5a97975304e62aa2ad79282e1da34432"
    ]
  },
  "coverage": {
    "totalCoveragePercentage": 65.55,
    "resultReasons": []
  }
}

Commits that have not been analyzed lack both "startedAnalysis" and "endedAnalysis" fields.
'parents' array contains all the direct anchestors commit-uuid in the branch history.
We keep iterating until we find an anchestor that have been analyzed.
"""

# Collect the set of commits that will be analyzed. It starts with the most recent commit
included = set()
included.add(commitUuid)

# Collect the list of parent commits we are iterating on. It start with the parent commits
# of the one passed as argument
parents = []
for x in r.json()['commit']['parents']:
    parents.append(x)

while(len(parents) > 0):
    commitUuid = parents.pop()
    newUrl = f'https://app.codacy.com/api/v3/analysis/organizations/{provider}/{remoteOrganizationName}/repositories/{repositoryName}/commits/{commitUuid}'
    r = requests.get(newUrl, params = {}, headers = headers)
    if 'startedAnalysis' in r.json()['commit'] and 'endedAnalysis' in r.json()['commit']:
        # we found a previous commit that has been analyzed, so stop the iteration without adding to 'included'
        break
    else:
        # this one has not been analyzed, so add to the set...
        included.add(commitUuid)
        # ...and also continue iterating on its ancestors
        for x in r.json()['commit']['parents']:
            parents.append(x)

# For each commit, get the list of affected files using the following command:
#     git diff-tree --no-commit-id --name-only -r $COMMIT
# It returns a '\n'-separated list of files along with their complete path
fileSet = set()
for commit in included:
    execResult = subprocess.run(['git', 'diff-tree', '--no-commit-id', '--name-only', '-r', commit],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True, text=True)
    for filename in execResult.stdout.split(sep='\n'):
        if filename != '':
            fileSet.add(filename)

# Apply a final filter to keep only the files which cland-tidy can analyze
finalList = list(filter(lambda x: x.endswith('.c') | x.endswith('.cpp') | x.endswith('.tcc') | x.endswith('.cc'), fileSet))

# Return the list to clang-tidy-launcher.sh
print("|".join(finalList))
