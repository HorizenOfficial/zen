Contents
===========
This directory contains tools for developers working on this repository.


github-merge.sh
==================

A small script to automate merging pull-requests securely and sign them with GPG.

For example:

  ./github-merge.sh bitcoin/bitcoin 3077

(in any git repository) will help you merge pull request #3077 for the
bitcoin/bitcoin repository.

What it does:
* Fetch master and the pull request.
* Locally construct a merge commit.
* Show the diff that merge results in.
* Ask you to verify the resulting source tree (so you can do a make
check or whatever).
* Ask you whether to GPG sign the merge commit.
* Ask you whether to push the result upstream.

This means that there are no potential race conditions (where a
pullreq gets updated while you're reviewing it, but before you click
merge), and when using GPG signatures, that even a compromised github
couldn't mess with the sources.

Setup
--------
Configuring the github-merge tool for the bitcoin repository is done in the following way:

    git config githubmerge.repository bitcoin/bitcoin
    git config githubmerge.testcmd "make -j4 check" (adapt to whatever you want to use for testing)
    git config --global user.signingkey mykeyid (if you want to GPG sign)


update-copyright-headers.py
===========================

This script takes care of updating the copyright headers in the source files of the repository.
The script works by checking git history for each file and detecting if there were any changes
within a date range, file by file.

If the header is already available, it's just updated (if needed), otherwise a new line is
appended to the headers section (at the beginning of the file).

Example usage:
```python contrib/devtools/updated-copyright-headers.py```

By default the script updates the header `The Horizen Foundation` taking into account changes
since 2023-09-28. The behavior can be modified by passing optional arguments, for instance:
```python contrib/devtools/update-copyright-headers.py --since 2018-01-18 --until 2023-09-27 --copyright-holder "Zen Blockchain Foundation"```


symbol-check.py
==================

A script to check that the (Linux) executables produced by gitian only contain
allowed gcc, glibc and libstdc++ version symbols.  This makes sure they are
still compatible with the minimum supported Linux distribution versions.

Example usage after a gitian build:

    find ../gitian-builder/build -type f -executable | xargs python contrib/devtools/symbol-check.py 

If only supported symbols are used the return value will be 0 and the output will be empty.

If there are 'unsupported' symbols, the return value will be 1 a list like this will be printed:

    .../64/test_bitcoin: symbol memcpy from unsupported version GLIBC_2.14
    .../64/test_bitcoin: symbol __fdelt_chk from unsupported version GLIBC_2.15
    .../64/test_bitcoin: symbol std::out_of_range::~out_of_range() from unsupported version GLIBCXX_3.4.15
    .../64/test_bitcoin: symbol _ZNSt8__detail15_List_nod from unsupported version GLIBCXX_3.4.15


update-translations.py
=========================

Run this script from the root of the repository to update all translations from transifex.
It will do the following automatically:

- fetch all translations
- post-process them into valid and committable format
- add missing translations to the build system (TODO)

See doc/translation-process.md for more information.


gen-manpages.sh
==================

A small script to automatically create manpages in ../../doc/man by running the release binaries with the -help option.
This requires help2man which can be found at: https://www.gnu.org/software/help2man/


git-subtree-check.sh
=======================

Run this script from the root of the repository to verify that a subtree matches the contents of
the commit it claims to have been updated to.

To use, make sure that you have fetched the upstream repository branch in which the subtree is
maintained:
* for src/secp256k1: https://github.com/bitcoin/secp256k1.git (branch master)
* for sec/leveldb: https://github.com/bitcoin/leveldb.git (branch bitcoin-fork)

Usage: git-subtree-check.sh DIR COMMIT
COMMIT may be omitted, in which case HEAD is used.


release_management/
======================

Inside this folder the following are provided:

- a script (release_preparation.py),
- a template config file (release_preparation_config.yaml)
- various config files associated to past releases preparations (configs/release_preparation_config-X.Y.Z.yaml)

Run the script for preparing a new release.
It is suggested to run the script from a terminal located at repository root. 
The script accepts one command line parameter, being the path of the config file containing all the inputs
to the script (it is suggested to put this file in `./contrib/devtools/release_management/configs` folder, naming it
as `release_preparation_config-X.Y.Z.yaml`). If the path of the config file is not provided as command line parameter,
the script itself explicitly requests the user to input it; if the user does not input it, then an interactive
session is started, in which the input of each preparation step is requested to the user.
A successful run of the script would create a new branch starting from currently selected commit of `main`
branch (a different branch can be configured) and would commit one by one each release preparation step.

After script completion the user is required to:

- check no error code is returned by the script
- push from release preparation local branch to remote
- create release branch on remote
- open a PR merging release preparation into release branch and wait for approval
- once the release preparation branch is merged into the release branch, create annotated tag (vX.Y.Z)


Inside this folder the following are provided:

- a script (release_backport.py),
- a template config file (release_backport_config.yaml)
- various config files associated to past releases preparations (configs/release_backport_config-X.Y.Z.yaml)

Run the script for backporting a release.
It is suggested to run the script from a terminal located at repository root. 
The script accepts one command line parameter, being the path of the config file containing all the inputs
to the script (it is suggested to put this file in `./contrib/devtools/release_management/configs` folder, naming it
as `release_backport_config-X.Y.Z.yaml`). If the path of the config file is not provided as command line parameter,
the script itself explicitly requests the user to input it; if the user does not input it, then an interactive
session is started, in which the input of each preparation step is requested to the user.
A successful run of the script would create a new branch starting from currently selected commit of `main`
branch (a different branch can be configured) and would perform one single commit containing all the release
backport steps.

After script completion the user is required to:

- check no error code is returned by the script
- push from release backport local branch to remote
- open a PR merging release backport into `main` branch and wait for approval