Notable changes
===============

UTXO and note merging
---------------------

In order to simplify the process of combining many small UTXOs and notes into a
few larger ones, a new RPC method `z_mergetoaddress` has been added. It merges
funds from t-addresses, z-addresses, or both, and sends them to a single
t-address or z-address.

Unlike most other RPC methods, `z_mergetoaddress` operates over a particular
quantity of UTXOs and notes, instead of a particular amount of ZEN. By default,
it will merge 50 UTXOs and 10 notes at a time; these limits can be adjusted with
the parameters `transparent_limit` and `shielded_limit`.

`z_mergetoaddress` also returns the number of UTXOs and notes remaining in the
given addresses, which can be used to automate the merging process (for example,
merging until the number of UTXOs falls below some value).

UTXO memory accounting
----------------------

The default -dbcache has been changed in this release to 450MiB. Users can set -dbcache to a higher value (e.g. to keep the UTXO set more fully cached in memory). Users on low-memory systems (such as systems with 1GB or less) should consider specifying a lower value for this parameter.

Additional information relating to running on low-memory systems can be found here: [reducing-memory-usage.md](https://github.com/HorizenOfficial/zen/blob/master/doc/reducing-memory-usage.md).

Changelog
=========

Alessandro Petrini (15):
      update IsInitialBlockDownload in main.cpp
      Introduced gtest for IsInitialBlockDownload
      IsInitialBlockDownload: lockIBDState no longer global; added nullptr tests
      improved checkIBDState test: activeChain builder; nullptr test; for loop
      test_blockdownload: removed activeChain init
      tlsmanager: corrected misspelled warnings
      improvements to initialblockdownload test
      removed additional newline from main.cpp
      improved initial block download test
      miner threads joined when shutdown is requested
      added interruption points to default miner for premature interruption when shutdown is requested
      change parameters order in assert_equal()
      Add colorize function for improving readability of printed logs
      Simplify the usage of colored mark_logs function
      Improved clarity of the assert_equal returned message, minor mod to mark_logs

Brad Miller (1):
      Implement note locking for z_mergetoaddress

Daniele Rogora (6):
      Update check for libgmp in configure.ac
      Allow easy cross-compilation with the CT_PREFIX env var
      Use linker garbage collection by default
      Remove DecodeBase58 duplication
      If compiling with clang, use it also to compile rust dependencies
      Update zeromq to version 4.3.4.

Jack Grigg (6):
      Extend CWallet::GetFilteredNotes to enable filtering on a set of addresses
      Implement z_mergetoaddress for combining UTXOs and notes
      Gate z_mergetoaddress as an experimental feature
      Add z_mergetoaddress to release notes
      wallet_mergetoaddress: Add additional syncs to prevent race conditions
      Test calling z_mergetoaddress to merge notes while a note merge is ongoing

Jonathan "Duke" Leto (2):
      Return JoinSplit and JoinSplitOutput indexes in z_listreceivedbyaddress
      Add tests for new JoinSplit keys returned by z_listreceivedbyaddress

Paolo Tagliaferri (38):
      Porting of Python test framework from v2 to v3
      Fixed some tests failing after migrating to Python 3
      Fix for Travis CI to run regression tests with Python 3
      Added pyzmq Python module to CI
      Minor fix for running tests on MacOS
      Upgraded some utility Python scripts to use version 3
      Updated some shebang lines from Python 2 to Python 3
      Fixed some failing tests
      Updated the Python version for fundaddress.py test
      Fixed version of pip package "websocket-client"
      Extended the sc_getscgenesisinfo.py test to generate data for SDK test
      Implemented a unit test for the computation of the commitment tree root
      Added a way to get the hex file independently from current working dir
      Fixed the generation of random field elements in BlockchainHelper
      BlockchainHelper upgrade to Python 3
      Upgraded Boost dependency from version 1.70.0 to 1.74.0
      Set b2sdk pip package to version 1.14.1
      Updated GMP lib to latest version
      Added support for C++ 17 compilation
      Switched to compilation with C++ 17
      Compile dependencies with C++ 17
      Switched from random_shuffle to shuffle
      Fixed some conversion operators for C++ 17 compatibility
      Update Libzendoo to v0.2.0.1
      Minor changes to random.h and random.cpp after code review
      Added the "dependencies" doc file
      Fixed the link for hexdump (util-linux)
      Fixed the Zendoo compilation with -Werror flag
      Added scripts for static code analysis with cppcheck and oclint
      Added the --use-clang flag to conditionally build with CLANG
      Fixed Windows cross-compilation
      Added documentation and improved linting scripts
      Added a note to the static analysis doc for "--use-clang" compatibility
      Fixed a check for the "-wall" flag in the configure script
      Remove inclusions and exclusions through grep from the linting scripts
      Added a help string for --use-clang in build.sh
      Applied suggestions from code review
      Fixed a sporadic error with Python test

Simon Liu (4):
      Fixes https://github.com/zcash/zcash/issues/2793. Backport commit f33afd3 to increase dbcache default.
      Add documentation about dbcache.
      Add note about dbcache to release notes.
      Closes https://github.com/zcash/zcash/issues/2910. Add z_listunspent RPC call.

Todd VanGundy (1):
      Add 2204 ci worker: Added -Wnonull flag for C compiles Removed ipfs Added pycryptodome to elminate dependency on RIPEMD160

cronicc (21):
      Remove python2 env workaround for MacOS
      Remove all python2/pip2 packages and dependencies
      Add python3-wheel to Dockerfiles
      Revert "Merge pull request #458 from HorizenOfficial/ap/nCeasingSC"
      Allow sending to the same recipient address multiple times in z_sendmany:
      Fix: 'z_sendmany error=Witness for note commitment is null' after z-addr import
      Replace push_back(Pair()) with pushKV()
      Adjust for our Groth16 implementation
      Adjust for our Groth16 implementation, replace push_back(Pair()) with pushKV()
      Adjust values for zen reward schedule
      Make z_mergetoaddress compatible with changes introduced in Zendoo
      Port wallet_mergetoaddress.py to python3
      Add TODO
      Update z_listreceivedbyaddress help text with js index info
      Only call SetBestChain() if blocks were actually rescanned
      Update OpenSSL to 1.1.1q
      Set clientversion 3.2.0
      Update checkpoint blocks
      Update Debian package info
      Set deprecation height 1303000/2023-02-21
      Update man pages

tvangundy-hl (1):
      Removed commented code

