Notable changes
===============

* -reindexfast - Rebuild block chain index from current blk000??.dat files on startup, skipping expensive checks for blocks below checkpoints. Faster sync speed by skipping expensive checks for blocks below checkpoints. #350
* Improve tracking of penalized chains by adding optional argument --with-penalties=true/false to 'getchaintips' RPC call #348
* Added tests for block halving #342
* Update OpenSSL dependency #365
* Modify contrib/linearize to work with zend #362

Changelog
=========

Alberto Sala (8):
      Added regtest only option for setting halving period; added py test for halving height
      Fix for gtest/bitcoin ut
      Added regtest only option for setting halving period; added py test for halving height
      Fix for gtest/bitcoin ut
      Fix for script_test.py
      Changes after code review
      Regenerated regtest addresses for communitis subsidy coinbase quotas and added relevant privkey in comments
      Modifications after final code review

Paolo Tagliaferri (3):
      Improved the processing of blocks when zk-SNARKs verification is not needed (e.g. when using checkpoints).
      Fixed the creation of new blk??????.dat files.
      Removed a minor unit test that could not be compiled for Windows target.

abi87 (28):
      Fixes following code review
      Minor renaming before -reindex-fast implementation
      Added UT file for reindex
      Added UTs for LoadBlocksFromExternalFile
      Minor refactoring for readability
      reindexU UTs: added UT to store and retrieve genesis to MapBlockIndex
      Refactored generation of equihash for blockHeaders
      Reindex UTs: tested case for multiple blocks per file + minor refactoring of CreateNewBlock
      Extended asserts in reindex UTs
      reindex: refactored blocks loading from LoadBlocksFromExternalFile
      Reindex: minor fix of function signature
      Reindex: minor fixes to LoadBlocksFromExternalFile
      Draft of introduction of header-first block load
      reindex: Added Uts for out-of-order blocks
      reindex: added UTs for duplicated blocks in files
      Minimal changes for reindex-fast: removed refactoring of blocks loading
      reindex: fixed block processing following headers one
      Added UT for headers and block back-to-back processing
      reindex-fast: added global option to cli
      Fixes following code review
      Fixed CheckBlockIndex for regtest checks
      Minor preparatory refactoring
      Refactoring to extract logic of block to overran chainActive tip
      Added extra fields in getchaintips
      Adding py tests for verbose getchaintips
      reintroduced trace as for code review indications
      Reverted unneeded changes to zen/delay.cpp
      Reverted minor change over readability disagreement

cronicc (14):
      Exclude failing RPC tests on MacOS Travis worker
      Fix regression B2 CLI v2.1.0 with python2
      Modify block header offsets for zen
      Update documentation
      Speed up download of stage artifacts by using backblaze directly
      Fix transient failure on MacOS:
      2nd try, fix MacOS ZcashParams travis-ci cache dir
      Update OpenSSL to 1.1.1i
      Update OpenSSL to 1.1.1j
      Update domain to horizen.io
      Set mainnet/testnet checkpoint blocks
      Set next deprecation block 1027500 * next depreaction October 2021, adjusted to 30 weeks
      Set version to 2.0.23, set copyright year 2021
      Regenerate man pages

lander86 (3):
      freezing the b2 version and fixing pigz command on MacOs
      fix ipfs image url
      fix indentation and minor fixes

