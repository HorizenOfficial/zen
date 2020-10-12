Changelog
=========

Adam Brown (1):
      [doc] Update port in tor.md

Adam Weiss (1):
      Buffer log messages and explicitly open logs

Alberto Garoffolo (1):
      moved initial syncing flag in main

Alberto Sala (27):
      Added first code for RPC getblockfinalityindex method. Added code for relaying forks which could be candidate for neighbours active chain
      fork propagation fixes
      Added method for finding fork tips. Added also gtest first implementation
      Preliminary version candidate
      More fix and minor modifications
      More fix on getheaders
      Added helper method for adding in global tip set
      Fix for getheaders in case of forks longer than 160 elements
      More fix for getheaders in case of forks longer than 160 elements
      Fix for populating global tips repo
      More fix for populating global tips repo
      Removed usleep and checked size of locator before access
      Fixes after first round of testing: added missing check on ptr, removed obsolete gtest, fixed py test
      Removed verbose logs and moved some dbg method to util
      Preliminary version for gtests
      Fix for test files after the merge
      Fix for getblockfinalityindex
      Added rpc method getglobaltips
      Removed debug log categories from zen.conf in py tests; minor modification to blockdelay py test; minor files cleanup
      Removed #if 0 macro; fixes for global gtest execution
      Added some more py test in the ext execution list; minor fix in blockdelay.py test
      Fixed issue #4 "zen-cli help not working anymore"
      Fix for issue #167: int64_t used in place of int; added a limit on tip age for checking finality; added a new gtest
      Introduced a minimum age for the referenced block in the scriptPubKey for CHECKBLOCKATHEIGHT part
      Moved check of minimum age of referenced block from solver method to its interested caller
      Added new init option allownonstandardtx and exposed cbh-related switches to testnet as well
      Modified HelpMessageOpt for new switches

Alex van der Peet (1):
      New RPC command disconnectnode

Allan Niemerg (1):
      Pause mining during joinsplit creation

Amgad Abdelhafez (2):
      Update timedata.cpp
      Update timedata.cpp

Anthony Towns (1):
      Add configure check for -latomic

Ariel (1):
      add examples to z_getoperationresult

Ariel Gabizon (5):
      make-release.py: Versioning changes for 1.0.11-rc1.
      make-release.py: Updated manpages for 1.0.11-rc1.
      make-release.py: Updated release notes and changelog for 1.0.11-rc1.
      add load-wallet benchmark
      boost::format -> tinyformat

Bjorn Hjortsberg (2):
      Do not warn on built in declaration mismatch
      Remove deprecated exception specification

Bob McElrath (1):
      Add explicit shared_ptr constructor due to C++11 error

Boris Hajduk (1):
      documentatin z_validateaddress was missing param

Boris P (1):
      add new node

Bruno Arueira (1):
      Removes out bitcoin mention in favor for zcash

Casey Rodarmor (1):
      Don't share objects between TestInstances

Code Particle (8):
      Integrated commit - Update ANSI art - 03/24/2017
      Integrated Commit Update folder locations - 03/04/2017
      Integrated Commit - Remove Zcash references in bitcoind.cpp - 03/05/2017
      Integrated Commit - Update binary names to Zen - 03/05/2017 - additional changes were required
      Integrated Commit - Updated resource files for Zen - 03/05/2017
      Integrated Commit - Remove Zcash references in bitcoin-cli.cpp - 03/05/2017
      Integrated Commit - Updated Network Magic Start Values - 03/19/2017.
      [VERIFIED] - Fixed compilation and test suite issues.

Cory Fields (17):
      locking: teach Clang's -Wthread-safety to cope with our scoped lock macros
      locking: add a quick example of GUARDED_BY
      libevent: add depends
      libevent: Windows reuseaddr workaround in depends
      httpserver: explicitly detach worker threads
      c++11: don't throw from the reverselock destructor
      c++11: CAccountingEntry must be defined before use in a list
      c++11: fix libbdb build against libc++ in c++11 mode
      depends: use c++11
      depends: bump OSX toolchain
      build: Split hardening/fPIE options out
      build: define base filenames for use elsewhere in the buildsystem
      build: quiet annoying warnings without adding new ones
      build: fix Windows builds without pkg-config
      build: force a c++ standard to be specified
      build: warn about variable length arrays
      build: add --enable-werror option

Daira Hopwood (40):
      Don't assume sizes of unsigned short and unsigned int in GetSizeOfCompactSize and WriteCompactSize. Fixes #2137
      Remove src/qt.
      License updates for removal of src/qt.
      Correct license text for LGPL.
      Remove QT gunk from Makefiles.
      Remove some more QT-related stragglers.
      Update documentation for QT removal.
      Update which libraries are allowed to be linked to zcashd by symbol-check.py.
      Remove NO_QT make option.
      .gitignore cache/ and venv-mnf/
      Remove unused packages and patches.
      Delete -rootcertificates from bash completion script.
      Line-wrap privacy notice. Use <> around URL and end sentence with '.'. Include privacy notice in help text for zcashd -help.
      Update version numbers.
      Improvement to release process doc.
      Generate man pages.
      Update authors, release notes, and Debian package metadata.
      Delete old protocol version constants and simplify code that used them. fixes #2244
      Don't rely on a finite upper bound on fee rate or priority.
      Simplify JoinSplit priority calculation. refs 1896
      Add check for JoinSplit priority as calculated by CCoinsViewCache::GetPriority.
      Fix an error reporting bug due to BrokenPipeError and ConnectionResetError not existing in Python 2. refs #2263
      Alert 1002 (versions 1.0.0-1.0.2 inclusive).
      Alert 1003 (versions 1.0.3-1.0.8 inclusive).
      Disable building Proton by default.
      Remove an unneeded version workaround as per @str4d's review comment.
      Remove unneeded lax ECDSA signature verification.
      Strict DER signatures are always enforced; remove the flag and code that used it.
      Repair tests for strict DER signatures. While we're at it, repair a similar test for CLTV, and make the repaired RPC tests run by default.
      Make transaction test failures print the comments preceding the test JSON.
      Fix a comment that was made stale before launch by #1016 (commit 542da61).
      Delete test that is redundant and inapplicable to Zcash.
      Clean up imports to be pyflakes-checkable. fixes #2450
      For unused variables reported by pyflakes, either remove the variable, suppress the warning, or fix a bug (if the wrong variable was used). refs #2450
      Cosmetics (trailing whitespace, comment conventions, etc.)
      Alert 1004 (version 1.0.10 only)
      Remove UPnP support. fixes #2500
      Change wording in Security Warnings section of README.md.
      Document our criteria for adding CI workers. closes #2499
      Use https: for BDB backup download URL.

Daniel Cousens (1):
      torcontrol: only output disconnect if -debug=tor

Daniel Kraft (1):
      Fix univalue handling of \u0000 characters.

Duke Leto (1):
      Update performance-measurements.sh

Florian Schmaus (1):
      Add BITCOIND_SIGTERM_TIMEOUT to OpenRC init scripts

Forrest Voight (1):
      When processing RPC commands during warmup phase, parse the request object before returning an error so that id value can be used in the response.

Franck De Girolami (1):
      Complete pass on fork management Added new null transaction fork and new unit and rpc tests Fixed existing unit tests and rpc tests Further refactored zen code change community rewards and secure node rewards, add super node rewards

FranckDG (219):
      fix for compile error
      Removed references to nMinerThreads (merge error?) and fPowAllowMinDifficultyBlocks (removed from latest zcash code)
      Integrated Commit: Update base58 tests to use new prefix #5871062
      Integrated Commit: Update DoS_tests for OpenSSL #f27001a
      Integrated Commit: Disable key_tests for now (FIXME) #1cee593
      Integrated Commit: Updated testnet multisig addresses #bd76b58
      Integrated Commit: Update regtest chainparams #e5c99a9b
      Integrated Commit: Obtain TLS configuration from zen.conf and generate TLS certificates automatically #f9a65c52
      Integrated Commit: Extend CLI support for cert location and show metrics #55e36467
      Integrated Commit: Update routing secrecy for Tor #4efd19f2
      Integrated Commit: Update routing secrecy for Tor #4543107a
      Integrated Commit: Various fixes (NEEDS CLEANUP) #564bee1a
      Integrated Commit: Bugfix: Blocks not syncing #edaa1200
      Integrated Commit: Disable InitialBlockDownload() for pre-chainsplit blocks #0c479520
      Integrated Commit: Remove Zcash references in rpcserver.cpp #22f28d16
      Integrated Commit: Update Core/DAO allocations and add 110 #a9358796
      Integrated Commit: Update zen.conf example. #4af49736
      Integrated Commit: Stay non-blocking for SSL_read #0836b27e
      Integrated Commit:  Update chainparams.cpp #596ecdcf
      Integrated Commit: Adds section on building on CentOS #91e22fee
      Integrated Commit: fixes typo... #951cfcea
      Integrated Commit: Ignore Zen binaries #7eef0592
      Integrated Commit: Updages build scripts to Zen rather then Zcash where possible #dc821c4b
      Integrated Commit: Adds 'Development tools' group #812ae675
      Integrated Commit: Change network identifier and versions #c782bcfe
      Integrated Commit: Bugfix: socket loop in OpenSSL #bc7dadac
      Integrated Commit: Don't disconnect on undefined non-blocking behavior #9edcc066
      Integrated Commit: Bugfix: Clean stack for OP_CHECKBLOCKATHEIGHT #d345ccbc
      Integrated Commit: Update default port for ZEN #d888a776
      Integrated Commit: Fix CBitcoinAddress to support both pre-chainsplit and post-chainsplit prefixes #a90d521e
      Integrated Commit: Bugfix: allow pre-fork addresses to sign transactions #77822968
      Integrated Commit: change testnet port #8e3bdaea
      Integrated Commit: update build-mac #94f2b273
      Integrated Commit: Bugfix: Call SocketSendData only with lock #ad13aea7
      Integrated Commit: Bugfix: OP_CHECKBLOCKATHEIGHT clear stack #58dcfd9a
      Integrated Commit: Bugfix: Do not continue/initiate handshake in message thread #d4a5d189
      Integrated Commit: Bugfix: Only poll establish_tls_connection() once. #f385cc00
      Integrated Commit: #33 Re-insert x509 verification #315a4b64
      removed references to nMinerThreads in chainparams.cpp as it no longer exists
      Integrated Commit: Bugfix: High CPU usage in socket thread #0f94edf0
      Integrated Commit: Bugfix: Metrics display not showing OpenSSL stats correctly #1f5021da
      Integrated Commit: Bugfix #36 - QA OpenSSL Implementation #1134484c
      Integrated Commit: SSL_read() transparently continues TLS handshaking #6501e680
      Integrated Commit: SSL_write() transparently continues TLS handshaking #8dd73d90
      Integrated Commit: Bugfix: Move TLS handshake initialization to OpenNetworkConnection() #5a531869
      Integrated Commit: Bugfix: Add TLS handshake to ThreadSocketHandler() #3e9ee67e
      Integrated Commit: Bugfix: Print parsing and remove partial writes #e0f0ca2c
      Integrated Commit: Bugfix: Isolate TLS handshake initialization #f4a1320a
      Integrated Commit: fixup! Bugfix: Isolate TLS handshake initialization #ae5fb9cf
      Integrated Commit: Bugfix: Off by one error in getblocksubsidy #a8b1ae89
      Integrated Commit: Bugfix: Ensure complete writes #c79b7b4a
      Integrated Commit: Update build-win.sh #bf58693c
      Integrated Commit: Update README.md #d2e0cb1e
      Integrated Commit: Disable OP_CHECKBLOCKATHEIGHT for coinbase txs #9c5a6e2e
      Integrated Commit: Bugfix: High CPU usage in establish_tls_connection() #21064ab1
      Integrated Commit: Bugfix: Operate messaging stack independent of TLS status #e7dc3a3a
      Integrated Commit: Bugfix: SocketSendData should return when SSL_write() unavailable #edd93f2b
      Integrated Commit: Bugfix: Run establish_tls_connection() once in ThreadSocketHandler #9b89e549
      Integrated Commit: Bugfix: Don't block optimistic writes. #40e4d559
      Integrated Commit: fixup! Bugfix: Don't block optimistic writes. #e169d03a
      Integrated Commit: Bugfix: Ensure read/write is not called without instantiation #320ee39a
      Integrated Commit: Bugfix: Remove establish_tls_connection() from LOCK #65ac970a
      Integrated Commit: Bugfix: Enable partial writes and force close bad nodes #92eeee09
      Integrated Commit: fixup! Bugfix: Enable partial writes and force close bad nodes #e54afaf3
      Integrated Commit: Revert "fixup! Bugfix: Enable partial writes and force close bad nodes" #21372500
      Integrated Commit: fixup! fixup! Bugfix: Enable partial writes and force close bad nodes #aee77dfd
      Integrated Commit: Remote TLS functions #53245644
      Integrated Commit: Change Zcash references in build-debian-package.sh #8832a559
      Integrated Commit: Update debian control file #e220d06b
      Integrated Commit: Make replay protection indefinite. #75867a1c
      Integrated Commit: Move conf file (#1) #89dfdda9
      Integrated Commit: Create bitcoind.cpp #6105c7b3
      Integrated Commit: Reject blocks containing transactions without OP_CHECKBLOCKATHEIGHT past 117000 (#3) #624c552f
      Integrated Commit: Bump readme version #b4315d99
      Integrated Commit: Update slack info #c400f067
      Integrated Commit: Update zen.conf #c7d6c188
      Integrated Commit: Updates qa directory to work with zen (#12) #af79e89d
      Integrated Commit: Fix wallet warning (pre chainsplit txs are considered as invalid which is not true) (#13) #e40ff3c1
      Integrated Commit: Fixed #14: add the ability to send P2SH transactions #c2a434da
      Integrated Commit: Disable P2SH transactions until HF #19b7538e
      Integrated Commit: Bip 0115 integration (#16) #1861aee2
      Integrated Commit: Coinbase protection bugfix #5ce57d7e
      Integrated Commit: Add windows instructions (#10) #5e021b8c
      Fixed integration issues between BIP0115 and the latest zcash code
      Integrated Commit: Update README.md #8c8bc771
      Integrated Commit: Update seeders (#2) #23050279
      Integrated Commit: Update rpcmisc.cpp #e5883881
      Integrated Commit: Bump Version to 2.0.9-3 #4dc309ca
      Integrated Commit: Update seed nodes #053060dd
      Integrated Commit: Update chainparams.cpp #d17f162c
      Integrated Commit: Update chainparams.cpp #92d67350
      Integrated Commit: Fix OP_CHECKBLOCKATHEIGHT validation for tx inputs #5e39ad5e
      Integrated Commit: Bugfix: fix op-checkblockatheight validation for txs that are anchored to the genesis block #64214d05
      Integrated Commit: Allow to send coinbase founders reward coins to transparent address. Prepare a hardfork (#22) #01e928bf
      Integrated Commit: Refactor fork deployment. Collect all HF/SF constants in consensusParams #020bd3f8
      Integrated Commit: Change founders reward from 8.5% to 12% #c3554f83
      Integrated Commit: Add new set of founders reward addresses #2b753daa
      Integrated Commit: Set hard fork block height for testnet #8bc808e1
      Integrated Commit: Set new multisig founders reward address for testnet #a2b197df
      Integrated Commit: Update rpcmisc.cpp #5d92c62d
      Integrated Commit: Update rpcmisc.cpp #80e1d695
      Integrated Commit: Add additional checks for op-checkblockatheight arguments #9b923086
      Integrated Commit: Refactor op-checkblockatheight validation #91ac0a54
      Integrated Commit: Remove unnecessary check #8c7b9352
      Added a few functions to chainparams to help with founder reward tests. Tests are disabled for now and will be reenabled and fully fixed once all commits have been merged in.
      Integrated Commit: Make community reward permanent #f9629f30
      Integrated Commit: Bugfix. Add additional unit tests for P2SH functionality. #3d3e039e
      Integrated Commit: Update ISSUE_TEMPLATE.md #61675de9
      Integrated Commit: Update clientversion.h #f4589c9a
      Integrated Commit: Update copyright version info... #6ba0595b
      Integrated Commit: Removes zdeveloper from readme #e4a99045
      Integrated Commit: Update zcash-cli.1 #ba6f003f
      Integrated Commit: Update zcashd.1 #15f45a45
      Integrated Commit: Update COPYING #a1f48b2b
      Integrated Commit: Update util.cpp #0b34458a
      Integrated Commit: Update control #4f41034f
      Integrated Commit: Update gitian-linux.yml #dc8e6711
      Integrated Commit: Update DL link to our own fork of libsnark #44d202e0
      Integrated Commit: Update DL link to our own fork of libgmp #7294e634
      Integrated Commit: Fix comments #f7d5865a
      Integrated Commit: Set CF addressChangeInterval for main net #8822896c
      Integrated Commit: Updates emaile addresses to point at admin@zensystem.io rather then personal ones. #08a51125
      Integrated Commit: Update control #e30e8b1a
      Integrated Commit: Fix unit tests for OP_CHECKBLOCKATHEIGHT #25b9a41b
      Integrated Commit: Fix OP_CHECKBLOCKATHEIGHT validation for vouts #6f3cc47c
      Integrated Commit: Set HF block for main net #d008e839
      Integrated Commit: Bump Version to 2.0.9-4 #412e4079
      Integrated Commit: Update README.md #9ea86673
      Integrated Commit: Update gitian-linux.yml #1a492e66
      Integrated Commit: Use our mirror of the trusted setup #02d3b92b
      Integrated Commit: TLS mode in the network module #5b1189f6
      Integrated Commit: Set new community fund addresses #ca0371bc
      Integrated Commit: todos for renaming variables to Treasury Funds #7b777cb2
      Integrated Commit: Fix rpc_wallet_tests #5abf1b3d
      Integrated Commit: Do not warn on built in declaration mismatch #c4db0463
      Integrated Commit: Add unit tests for ContextualCheckBlock (checking correctness of the community rewards) #7ef3498b
      Integrated Commit: Rename Founders Reward to Community Fund #3d46fa1e
      Integrated Commit: Update README.md #f1b8f572
      Integrated Commit: Fix watchonly addresses. Fix functionality that was broken by modifeid GetScriptForDestination #eb885b43
      Fixed compilation issue from latest commits integration
      Integrated Commit: Fix pull-tester: fix path to zend and zen-cli. Fix config to be able to run it for Zen. Set chainsplit index for regtest. #a2ade7d2
      Integrated Commit: Checkpoint HF block (139200) #e608d425
      Integrated Commit: Implemented support of the non-TLS (unencrypted) connections; Refactored TLS implementation #c5b1bfff
      Integrated Commit: Fix pull-tester: fix tests #7ad2333f
      Integrated Commit: Repair tests for strict DER signatures. While we're at it #889cdcb1
      Integrated Commit: Show number of TLS connections in stats. Add TLS status to getpeerinfo RPC call. #9e2ed418
      Integrated Commit: Implemented generation of node credentials (RSA keys and self-signed certificates for them) #30d34d21
      Integrated Commit: Fix Win build #65730666
      Integrated Commit: Add unit test for network layer (stress test for self-connection) #b3a7a672
      Integrated Commit: Added synchronization around hSocket and ssl #e4bbb9a5
      Integrated Commit: Additional synchronization; Handling SSL_shutdown correctly #8ebd4be8
      Integrated Commit: Certificates validation #fc73d8db
      Integrated Commit: Fix pull-tester: fix tests. Disable tests that are failed and cant be fixed currently because of some issues in zcash. #ac22a5a1
      Integrated Commit: Fix pull-tester: smartfees.py (taken from bitcoin) #6e8228fe
      Integrated Commit: Revert zcash-specific tests #c1cc92be
      Integrated Commit: Zenify docs #1462d9c3
      Integrated Commit: Trusted directories and root certificates management; Private key encryption; Refactored credentials preparation #deb56fe8
      Integrated Commit: initial working arm64 version #7b51bb12
      Integrated Commit: Add aarch64 instructions to README.md #b373a7ba
      Integrated Commit: Update security-warnings.md #ff66c725
      Integrated Commit: revert ARM changes #a61cf72e
      Integrated Commit: Update libsodium.mk #1fe1ccc3
      Integrated Commit: Refactored certs and net related code #a9da9133
      Integrated Commit: TLS: Clean up the code #a4d5b91e
      Integrated Commit: TLS: add tls certificate status to getpeerinfo and getnetworkinfo #5ef497d7
      Integrated Commit: Use zensystem.io depends mirror #69fabeb5
      Integrated Commit: Update test-depends-sources-mirror.py #5aab5420
      Integrated Commit: Update OpenSSL to 1.1.0f #25e19591
      Integrated Commit: Changes emails from ZCash to zencash Changes admin@zensystem.io to info@zensystem.io #4956fd03
      Integrated Commit: Update chainparams.cpp #ade861b1
      Integrated Commit: TLS: Clean up debug logs #6e1a51aa
      Integrated Commit: v2.0.10 #d3cad1d0
      Integrated Commit: fixup #5823aeca
      Integrated Commit: Use tagged release for backup DL #4e47e5ab
      Integrated Commit: fixup configure.ac #2b712e8f
      Integrated Commit: fix handling of arm64/amd64 arch in debian package script #92d4ae15
      Integrated Commit: Log output fix #582ddbff
      Integrated Commit: TLS: fix runtime exception in LogPrintf #0eb1b256
      Integrated Commit: Disable to build rpc_wallet_tests.cpp for Win (it fails for Win probably due to mingw issues) #e767843c
      Integrated Commit: Update libsodium.mk #a798ced8
      Integrated Commit: fixup #2356e6fd
      fixed boost.mk
      Integrated Commit: fix paths in full-test-suite.sh #467bb0aa
      Integrated Commit: enable GenerateParams test #3f77448f
      Integrated Commit: fix binary name in bitcoin-util-test.json #b12fd5ed
      Integrated Commit: fix binary name in performance-measurements.sh #8c686096
      Integrated Commit: Added description for versioning. #af80a3fb
      Integrated Commit: Auto merge of #2335 - str4d:2333-validation-speed #0a228771
      Integrated Commit: Auto merge of #2297 - str4d:2274-apoptosis #e7285fc1
      Integrated Commit: Add CONTRIBUTING.md #d5c02215
      Fixed auto-merge issues
      Revert "Fixed auto-merge issues"
      Revert "Integrated Commit: Auto merge of #2335 - str4d:2333-validation-speed #0a228771"
      Integrated Commit: Add option to limit debug.log size #54909514
      Integrated Commit: fix wording after script changes #d215e203
      Integrated Commit: Fix the logo art that appears when you launch zend #72 #823203a4
      Integrated Commit: Fix CentOS compile dependencies #430a373a
      Integrated Commit: Disable last part of test_wallet.cpp temporarily #db3b7236
      Integrated Commit: Add `listaddresses` rpc command to go with z_listaddresses #5db3633c
      Integrated Commit: More zen #6eada0c0
      Integrated Commit: Fix examples; add  rpc test #b2a82191
      Integrated Commit: bump versions and auto-deprecation #b0dff43d
      Integrated Commit: Update README.md #c9d158dc
      Integrated Commit: drop version down to 2.0.10-1 #0781e5f8
      Integrated Commit: Upgrade OpenSSL to 1.1.0g #728fb425
      Integrated Commit: Clear OpenSSL error queue #3e0ee66f
      Integrated Commit: Bump auto-deprecation #f83d7df3
      Integrated Commit: Updates Discord link in README.md #00e81433
      Integrated Commit: Removes second Discord link from README.md - Thanks @cronic for finding this. #a7e5de5d
      Integrated Commit: Changes zcash to zen. #dad810df
      Integrated Commit: Implement RPC shield_coinbase #2448. #a86d2f70
      Integrated Commit: Closes #2639. Adds optional limit parameter with a default value of 50. #5fbdc0c9
      Integrated Commit: Closes #2446 by adding generated field to listunspent. #ed2b1c95
      Integrated Commit: Updates shieldcoinbase tests to use Zen CF amount #6aafeb0b
      Integrated Commit: Modified for macOS High Sierra Build #af45c7aa
      Integrated Commit: add macos shieldconbase support #dc39545d
      updated developer notes with new requirements
      moved zen-specific utiltls files to zen folder and namespace
      [WIP] implemented new core classes for fork management with new unit tests [WIP] Modified makefiles to have a separate libzencash [WIP] Removed Zencash-specific chain params: hfFixReplayProtectionHeight, hfFixPS3SHHeight, sfReplayProtectionHeight, hfCommunityFundHeight, nChainsplitIndex, nChainsplitTime and fDisableCoinbaseProtectionForCommunityFund(always true - unused). [WIP] increased fork index by 1 for original chainsplitfork and sfReplayProtection. These forks were non-inclusive (the starting block of the fork was previously not included in the fork) as opposed to newer forks that are inclusive. [WIP] refactor pass to force all calls through ForkManager directly.

Gavin Andresen (2):
      configure --enable-debug changes
      Testing infrastructure: mocktime fixes

Gregory Maxwell (3):
      Avoid a compile error on hosts with libevent too old for EVENT_LOG_WARN.
      Do not absolutely protect local peers from eviction.
      Decide eviction group ties based on time.

Homu (1):
      Auto merge of #3890 - zebambam:add_dns_rebinding_warnings_security_3841, r=mdr0id

Ian Kelling (1):
      Docs: add details to -rpcclienttimeout doc

Igor Mikheiko (1):
      Added additional path check for default config file when app installed from .deb package #91

JOSEPH NICHOLAS R. ALCANTARA (1):
      Merge pull request #135 from HorizenOfficial/TorDocumentation

Jack Gavigan (2):
      Removed markdown from COPYING
      Updated the Bitcoin Core copyright statement

Jack Grigg (209):
      Fix incorrect locking in CCryptoKeyStore
      Remove reference to -reindex-chainstate
      Treat metrics screen as non-interactive for now
      Adjust gen-manpages.sh for Zcash, use in Debian builds
      Regenerate and collate Zcash manpages, delete Bitcoin ones
      Update release process with gen-manpages.sh
      Adjust blockheaderToJSON() for Zcash block header
      Adjust fundrawtransaction RPC test for Zcash
      Re-encode t-addrs in disablewallet.py with Zcash prefixes
      BTC -> ZEC in paytxfee RPC docs
      Update default RPC port in help strings
      Fix typo in listbanned RPC keys
      Add anchor to output of getblock
      Migrate IncrementalMerkleTree memory usage calls
      Add tests for getmempoolinfo
      Usability improvements for z_importkey
      Implement an AtomicTimer
      Use AtomicTimer for more accurate local solution rate
      Metrics: Move local solution rate into stats
      Metrics: Improve mining status
      Expand on reasons for mining being paused
      Simplify z_importkey by making rescan a string
      Fix prioritisetransaction RPC test
      Revert "Closes #1680, temporary fix for rpc deadlock inherited from upstream."
      Add libevent to zcash-gtest
      [depends] libevent 2.1.8
      Test boolean fallback in z_importkey
      Require that z_importkey height parameter be in valid range
      Update LocalSolPS test
      Add AtomicTimer tests
      Revert "Revert "rpc-tests: re-enable rpc-tests for Windows""
      Wrap error string
      Fix typo
      torcontrol: Improve comments
      torcontrol: Add unit tests for Tor reply parsers
      torcontrol: Fix ParseTorReplyMapping
      torcontrol: Check for reading errors in ReadBinaryFile
      torcontrol: Log invalid parameters in Tor reply strings where meaningful
      Use AtomicTimer for metrics screen thread count
      Use a larger -rpcclienttimeout for slow performance measurements
      torcontrol: Handle escapes in Tor QuotedStrings
      torcontrol: Add missing copyright header
      Convert Zcash versions to Debian format
      [manpage] Handle build numbers in versions
      Address Daira's comments
      Address Daira's further comments
      Correctly handle three-digit octals with leading digit 4-7
      Check that >3-digit octals are truncated.
      Implement automatic shutdown of deprecated Zcash versions
      Wrap messages nicely on metrics screen
      Regenerate miner tests
      Add a benchmark for calling ConnectBlock on a block with many inputs
      Remove additional sources of determinism from benchmark archive
      Revert "Fix secp256k1 test compilation"
      Squashed 'src/secp256k1/' changes from 22f60a6..84973d3
      Fix potential overflows in ECDSA DER parsers
      Rename FALLBACK_DOWNLOAD_PATH to PRIORITY_DOWNLOAD_PATH
      Add test for incorrect consensus logic
      Correct consensus logic in ContextualCheckInputs
      Add comments
      Update Debian copyright list
      Specify ECDSA constant sizes as constants
      Remove redundant `= 0` initialisations
      Ensure that ECDSA constant sizes are correctly-sized
      Add test for -mempooltxinputlimit
      Hold an ECCVerifyHandle in zcash-gtest
      Additional testing of -mempooltxinputlimit
      Fix comment
      Use sendfrom for both t-addr calls
      make-release.py: Versioning changes for 1.0.10.
      make-release.py: Updated manpages for 1.0.10.
      make-release.py: Updated release notes and changelog for 1.0.10.
      Move temporary release notes to final ones
      Disable building Proton in Gitian
      Pull in temporary release notes during the release process
      Ansible playbook for installing Zcash dependencies and Buildbot worker
      Variable overrides for Debian, Ubuntu and Fedora
      Variable overrides for FreeBSD
      Simplify Python installation, inform user if they need to manually configure
      Add test for issue #2444
      Add Buildbot worker setup to Ansible playbook
      Add steps for setting up a latent worker on Amazon EC2
      Add pyblake2 to required Python modules
      Remove Buildbot version from host file
      Add a separate Buildbot host info template for EC2
      Add pyflakes to required Python modules
      Add block download progress to metrics UI
      Correct and extend EstimateNetHeightInner tests
      Improve network height estimation
      ci-workers: Enable pipelining, and use root to set admin and host details
      Squashed 'src/snark/' content from commit 9ada3f8
      Add libsnark compile flag to not copy DEPINST to PREFIX
      Variable overrides for Arch Linux
      Rationalize currency unit to "ZEC"
      ci-workers: Fail if Python is not version 2.7
      ci-workers: Variable overrides and process tweaks for CentOS 7
      make-release.py: Versioning changes for 1.0.11.
      make-release.py: Updated manpages for 1.0.11.
      make-release.py: Updated release notes and changelog for 1.0.11.
      Add build progress to the release script if progressbar module is available
      Add hotfix support to release script
      Document the hotfix release process
      Enforce sequential hotfix versioning
      Benchmark time to call sendtoaddress with many UTXOs
      Fix bug in benchmark data generation script
      Adjust instructions for UTXO dataset creation
      Add GitHub release notes to release process
      Clarify branching and force-building operations in hotfix process
      Update user guide translations as part of release process
      make-release.py: Send stderr to stdout
      List dependencies for release script in release process doc
      Additional test cases for importprivkey RPC test
      make-release.py: Versioning changes for 1.0.12-rc1.
      make-release.py: Updated manpages for 1.0.12-rc1.
      make-release.py: Updated release notes and changelog for 1.0.12-rc1.
      Fix pyflakes warnings in RPC tests
      Individualise performance-measurements.sh errors for debugging
      Fix incorrect failure in memory benchmark
      make-release.py: Versioning changes for 1.0.12.
      make-release.py: Updated manpages for 1.0.12.
      make-release.py: Updated release notes and changelog for 1.0.12.
      Add Ansible playbook for grind workers
      Add connections in BIP65 and BIP66 tests to the test manager
      Add benchmark for listunspent
      [Test] MiniNode: Implement JSDescription parsing
      [Test] MiniNode: Implement v2 CTransaction parsing
      [Test] MiniNode: Implement Zcash block parsing
      [Test] MiniNode: Update protocol version and network magics
      [Test] MiniNode: Use Zcash PoW
      [Test] MiniNode: Fix coinbase creation
      [Test] MiniNode: Coerce OP_PUSHDATA bytearrays to bytes
      [Test] MiniNode: Implement Zcash coinbase
      Fix BIP65 and BIP66 tests
      Un-indent RPC test output in test runner
      Replace full-test-suite.sh with a new test suite driver script
      Move ensure-no-dot-so-in-depends.py into full_test_suite.py
      Move check-security-hardening.sh into full_test_suite.py
      Add memory benchmark for validatelargetx
      Migrate libsnark test code to Google Test
      Remove test code corresponding to removed code
      Add alt_bn128 to QAP and Merkle tree gadget tests
      Update libsnark LDLIBS
      Add "make check" to libsnark that runs the Google Tests
      Add "make libsnark-tests" that runs libsnark's "make check"
      Changes to get test_r1cs_ppzksnark passing
      Add bitcoin-util-test.py to full_test_suite.py
      Add stdout notice if any stage fails
      Add libsnark to "make clean"
      Ensure that libsnark is built first, so its headers are available
      Remove OpenSSL libraries from libsnark LDLIBS
      Add libsnark tests to full_test_suite.py
      Add --list-stages argument to full_test_suite.py
      Fix NPE in rpc_wallet_tests
      make-release.py: Versioning changes for 1.0.13-rc1.
      make-release.py: Updated manpages for 1.0.13-rc1.
      make-release.py: Updated release notes and changelog for 1.0.13-rc1.
      Change auto-senescence cycle to 16 weeks
      Move libsnark from DIST_SUBDIRS into EXTRA_DIST
      Pass correct dependencies path to libsnark from both Gitian and build.sh
      Mark libsnark includes as library includes
      Add the tar-pax option to automake
      make-release.py: Versioning changes for 1.0.13-rc2.
      make-release.py: Updated manpages for 1.0.13-rc2.
      make-release.py: Updated release notes and changelog for 1.0.13-rc2.
      make-release.py: Versioning changes for 1.0.13.
      make-release.py: Updated manpages for 1.0.13.
      make-release.py: Updated release notes and changelog for 1.0.13.
      Squashed 'src/secp256k1/' changes from 84973d3..6ad5cdb
      Use g-prefixed coreutils commands if they are available
      Replace hard-coded defaults for HOST and BUILD with config.guess
      Remove manual -std=c++11 flag
      Replace "install -D" with "mkdir -p && install"
      Check if OpenMP is available before using it
      [libsnark] Use POSIX-compliant ar arguments
      Include endian-ness compatibility layer in Equihash implementation
      build: Split hardening/fPIE options out in Zcash-specific binaries
      Change --enable-werror to apply to all warnings, use it in build.sh
      Move Zcash flags into configure.ac
      ViewingKey -> ReceivingKey per zcash/zips#117
      Implement viewing key storage in the keystore
      Factor out common logic from CZCPaymentAddress and CZCSpendingKey
      Track net value entering and exiting the Sprout circuit
      Add Sprout value pool to getblock and getblockchaininfo
      Apply -fstack-protector-all to libsnark
      Add Rust and Proton to configure options printout
      Clarify operator precedence in serialization of nSproutValue
      Remove nSproutValue TODO from CDiskBlockIndex
      Add Base58 encoding of viewing keys
      Implement viewing key storage in the wallet
      Add RPC methods for exporting/importing viewing keys
      Update wallet logic to account for viewing keys
      Add watch-only support to Zcash RPC methods
      Modify zcrawkeygen RPC method to set "zcviewingkey" to the viewing key
      Cleanup: Add braces for clarity
      Add cautions to z_getbalance and z_gettotalbalance help text about viewing keys
      Scope the ECDSA constant sizes to CPubKey / CKey classes
      Add release notes for incoming viewing keys
      Create release notes starting from the previous non-beta non-RC release
      release-notes.py: Remove unnecessary parameter
      Regenerate previous release notes to conform to new format
      Exclude beta and RC release notes from author tallies
      Fix pyflakes warnings in zkey_import_export RPC test
      make-release.py: Versioning changes for 1.0.14-rc1.
      make-release.py: Updated manpages for 1.0.14-rc1.
      make-release.py: Updated release notes and changelog for 1.0.14-rc1.
      Update release process
      make-release.py: Versioning changes for 1.0.14.
      make-release.py: Updated manpages for 1.0.14.
      make-release.py: Updated release notes and changelog for 1.0.14.

Jake Tarren (11):
      Cleans up ZEN_MOD_* statements
      Updates 'zen' to 'horizen' where possible
      Updates to use Horizen where applicable
      Updates logo to Horizen logo
      Removes all ZEN_MOD_* statements
      Removes build error caused by inline comment
      Improves the wording of the configuration copying messages a bit.
      Fixes issues with package name while building APT package
      Squashed commit of the following:
      Updates Issue Reporting template
      Updates URLs

Jason Davies (2):
      Fix deprecation policy comment.
      Replace "bitcoin" with "Zcash".

Jay Graber (22):
      Add rpc test for prioritisetransaction
      Inc num of txs in test mempool
      Update release to 1.0.7, generate manpages
      Add 1.0.7 release notes and update authors.md
      Update debian changelog for 1.0.7 release process
      Update README for 1.0.7 release
      Document returned results of submitblock
      Add -t to git fetch for release-notes.py
      Update version to 1.0.7-1
      Update auto-generated manpages to 1.0.7-1
      Add updated release notes for v1.0.7-1
      Update debian package changelog for 1.0.7+1 (use + instead of - for debian versioning)
      Edit release-process.md for clarity
      Add security warning to zcash-cli --help and --version message output
      Add security warning to zcashd metrics display
      Add security message to license text, rm url from translation string
      Change help text examples to use Zcash addresses
      Poll on getblocktemplate result rather than use bare sleep to avoid race condition.
      s/zcash/Zcash
      Add cli and rpc examples for z_sendmany
      Fix cli help result for z_shieldcoinbase
      Add rpc test that exercises z_importkey

Jonas Schnelli (25):
      [net] extend core functionallity for ban/unban/listban
      [RPC] add setban/listbanned/clearbanned RPC commands
      [QA] add setban/listbanned/clearbanned tests
      [net] remove unused return type bool from CNode::Ban()
      [RPC] extend setban to allow subnets
      rename json field "bannedtill" to "banned_until"
      setban: rewrite to UniValue, allow absolute bantime
      fix CSubNet comparison operator
      setban: add RPCErrorCode
      add RPC tests for setban & disconnectnode
      fix missing lock in CNode::ClearBanned()
      setban: add IPv6 tests
      fix lock issue for QT node diconnect and RPC disconnectnode
      fundrawtransaction tests
      UniValue: don't escape solidus, keep espacing of reverse solidus
      [REST] add JSON support for /rest/headers/
      [QA] fix possible reorg issue in rawtransaction.py/fundrawtransaction.py RPC test
      [QA] remove rawtransactions.py from the extended test list
      [QA] add testcases for parsing strings as values
      [bitcoin-cli] improve error output
      fix and extend CBitcoinExtKeyBase template
      extend bip32 tests to cover Base58c/CExtKey decode
      don't try to decode invalid encoded ext keys
      Fix torcontrol.cpp unused private field warning
      Add compile and link options echo to configure

Jonathan "Duke" Leto (2):
      Fix bug where performance-measurements.sh fails hards when given no args
      Update zeromq

Jorge Timón (1):
      Consensus: Refactor: Separate Consensus::CheckTxInputs and GetSpendHeight in CheckInputs

Joshua Yabut (2):
      Integrated Commit: Update COPYING 9a19ff9
      Update ISSUE_TEMPLATE.md

João Barbosa (1):
      Improve shutdown process

Karl-Johan Alm (4):
      Added std::unique_ptr<> wrappers with deleters for libevent modules.
      Switched bitcoin-cli.cpp to use RAII unique pointers with deleters.
      Added some simple tests for the RAII-style events.
      Added EVENT_CFLAGS to test makefile to explicitly include libevent headers.

Kent (1):
      Update README.md

Kevin Pan (1):
      "getblocktemplate" could work without wallet

Larry Ruane (1):
      Update boost from v1.69.0 to v1.70.0. #3947

Leo Arias (1):
      Fix the path to the example configuration

Luke Dashjr (6):
      Fix various warnings
      Skip RAII event tests if libevent is built without event_set_mem_functions
      depends: Use curl for fetching on Linux
      Travis: Use curl rather than wget for Mac SDK
      Bugfix: depends/Travis: Use --location (follow redirects) and --fail [on HTTP error response] with curl
      Travis: Use Blue Box VMs for IPv6 loopback support

MarcoFalke (4):
      [doc] [tor] Clarify when to use bind
      torcontrol debug: Change to a blanket message that covers both cases
      Fix url in .travis.yml
      [depends] builders: No need to set -L and --location for curl

Marius Kjærstad (1):
      Changed http:// to https:// on some links

Matt Corallo (4):
      Small tweaks to CCoinControl for fundrawtransaction
      Add FundTransaction method to wallet
      Add fundrawtransaction RPC method
      Assert on probable deadlocks if the second lock isnt try_lock

Matt Quinn (1):
      Consolidate individual references to the current maximum peer connection value of 125 into a single constant declaration.

Murilo Santana (1):
      Fix sha256sum on busybox by using -c instead of --check

Nathan Wilcox (50):
      Bump version numbers for v1.0.8-1.
      Commit the changes from gen-manpages.sh, except manually tweak the version strings.
      Fix a release-process.md bug; commit results changelog + debian updates.
      Undo a documentation error due to an automated tool.
      Fix the previous incorrect fix to the manpage.
      [Direct master commit] Fix a release snafu in debian version string.
      Show toolchain versions in build.sh.
      Start on a make-release.py script; currently just arg parsing and unittests [unittests fail].
      Update version spec by altering test; also update regex to pass single 0 digits in major/minor/patch.
      Add another case from debian-style versions.
      Add all of the zcash release tags in my current repo as positive test vector.
      Add support for beta/rc release versions.
      Add version sorting, assert that RELEASE_PREV is the most recent release.
      Make SystemExit errors less redundant in output; verify clean git status on master.
      Always run unittests prior to actual runs.
      Make --help output clean by not running self-test.
      Add an option to run against a different repo directory.
      Make sure to pull the latest master.
      Exit instead of raising an unexpected exception, since it's already logged.
      Implement `PathPatcher` abstraction, `clientversion.h` rewrite, and build numbering w/ unittests.
      Implement the IS_RELEASE rule for betas.
      Generalize buildnum patching for both `clientversion.h` and `configure.ac`.
      Modify the `APPROX_RELEASE_HEIGHT`.
      Remove portions of `./doc/release-process.md` now implemented in `make-release.py`.
      Switch from `sh_out_logged` to `sh_log`.
      Shorten the arg log line.
      Commit the version changes and build.
      Generate manpages; commit that; improve error output in sh_log.
      Polish logging a bit more.
      Tidy up / systematize logging output a bit more.
      First full-release-branch version of script; rewrite large swatch of release-process.md. [Manually tested.]
      Enable set -u mode.
      Fix a variable name typo.
      Reuse zcash_rpc.
      Do not use `-rpcwait` on all `zcash_rpc` invocations, only block when starting zcashd.
      Fix `release-process.md` doc usage for `make-release.py` to have correct arguments and order.
      Include release version in commit comments.
      Examine all future versions which are assumed to follow the same Version parser schema.
      Consider both beta and rc versions to be `IS_RELEASE == false`.
      Add a few more version strings to positive parser test.
      Define the deprecation policy for 1.0.9.
      Clarify that the feature is automated *shutdown*.
      make-release.py: Versioning changes for 1.0.9.
      make-release.py: Updated manpages for 1.0.9.
      make-release.py: Updated release notes and changelog for 1.0.9.
      key_import_export rpc-test: verify that UTXO view co-evolves for nodes sharing a key.
      Add a new rpc-test-specified requirement: `importprivkey` outputs the associated address. (Test fails.)
      [tests pass] Output address on new key import.
      Add a new requirement that `importprivkey` API is idempotent.
      [tests pass] Ensure `importprivkey` outputs the address in case key is already imported.

Nathaniel Mahieu (1):
      Clarify documentation for running a tor node

Oleksandr Iozhytsia (1):
      CreateNewBlock function performance improved.-blockmaxcomplexity and -deprecatedgetblocktemplate parameters added. RPC tests: getblocktemplate_blockmaxcomplexity and getblocktemplate_priority tests added.

Paige Peterson (4):
      wallet backup instructions
      typo and rewording edits
      str4d and Ariel's suggestions
      specify exportdir being within homedirectory

Patrick Strateman (1):
      Remove vfReachable and modify IsReachable to only use vfLimited.

Paul Georgiou (1):
      Update Linearize tool to support Windows paths

Pavel Janík (3):
      Implement REST mempool API, add test and documentation.
      Prevent -Wshadow warnings with gcc versions 4.8.5, 5.3.1 and 6.2.1.
      Make some global variables less-global (static)

Pavel Vasin (1):
      remove unused inv from ConnectTip()

PeaStew (7):
      Remove ZCash from error message
      ZcashMiner -> ZenMiner in many strings and thread name
      ZCash -> Zen in string
      ZCash -> Zen or zcashd -> zend in message strings
      ZCash -> Zen in message strings rpcrawtransaction.cpp
      ZCash -> Zen or zcashd -> zend in message strings rpcmining.cpp
      ZCash -> Zen in message strings rpcnet.cpp

Per Grön (2):
      Deduplicate test utility method wait_and_assert_operationid_status
      Print result of RPC call in test only when PYTHON_DEBUG is set

Peter Todd (4):
      Add getblockheader RPC call
      Improve comment explaining purpose of MAX_MONEY constant
      Better error message if Tor version too old
      Connect to Tor hidden services by default

Philip Kaufmann (3):
      use const references where appropriate
      [init] add -blockversion help and extend -upnp help
      make CAddrMan::size() return the correct type of size_t

Pieter Wuille (8):
      Do not ask a UI question from bitcoind
      Add DummySignatureCreator which just creates zeroed sigs
      Reduce checkpoints' effect on consensus.
      Implement accurate memory accounting for mempool
      Separate core memory usage computation in core_memusage.h
      Fix interrupted HTTP RPC connection workaround for Python 3.5+
      Update key.cpp to new secp256k1 API
      Switch to libsecp256k1-based validation for ECDSA

René Nyffenegger (1):
      Use AC_ARG_VAR to set ARFLAGS.

Reza Barazesh (4):
      implemented the block delay penalty [WIP]
      implemented the block delay penalty [WIP]
      fixed a bug related to pindexBestHeader
      [WIP] rpc tests + gtest first draft + bug fixes

Ross Nicoll (1):
      Rationalize currency unit to "BTC"

Sean Bowe (10):
      Introduce librustzcash and Rust to depends system.
      Allow Rust-language related assets to be disabled with `--disable-rust`.
      Check that pairings work properly when the G1 point is at infinity.
      Revert "Remove an unneeded version workaround as per @str4d's review comment."
      Revert "Delete old protocol version constants and simplify code that used them."
      Remove libsnark from depends system and integrate it into build system.
      Remove crusty old "loadVerifyingKey"/"loadProvingKey" APIs and associated invariants.
      Refactor proof generation function.
      Add streaming prover.
      Integrate low memory prover.

Simon Liu (52):
      Alert 1000
      Alert 1001
      Add assert to check alert message length is valid
      Fix bug where test was generating but not saving keys to wallet on disk.
      Update founders reward addresses for testnet
      Keep first three original testnet fr addresses so existing coinbase transactions on testnet remain valid during upgrade.  New addresses will be used starting from block 53127.
      Closes #2083 and #2088. Update release process documentation
      Closes #2084. Fix incorrect year in timestamp.
      Closes #2112 where z_getoperationresult could return stale status.
      Add mainnet checkpoint at block 67500
      Add testnet checkpoint at block 38000
      Closes #1969. Default fee now sufficient for large shielded tx.
      Part of #1969. Changing min fee calculation also changes the dust threshold.
      Part of #1969. Update tests to avoid error 'absurdly high fee' from change in min fee calc.
      Remove stale Qt comments and dead code
      Remove QT translation support files
      Remove redundant gui options from build scripts
      Closes #2186. RPC getblock now accepts height or hash.
      Add AMQP 1.0 support via Apache Qpid Proton C++ API 0.17.0
      Add --disable-proton flag to build.sh.  Proton has build/linker issues with gcc 4.9.2 and requires gcc 5.x.
      Fix proton build issue with debian jessie, as used on CI servers.
      Change regtest port to 18344.  Closes #2269.
      Patch to build Proton with minimal dependencies.
      Fix intermediate vpub_new leakage in multi joinsplit tx (#1360)
      Add option 'mempooltxinputlimit' so the mempool can reject a transaction based on the number of transparent inputs.
      Check mempooltxinputlimit when creating a transaction to avoid local mempool rejection.
      Partial revert & fix for commit 9e84b5a ; code block in wrong location.
      Fix #b1eb4f2 so test checks sendfrom as originally intended.
      make-release.py: Versioning changes for 1.0.10-1.
      make-release.py: Updated manpages for 1.0.10-1.
      make-release.py: Updated release notes and changelog for 1.0.10-1.
      Closes #2446 by adding generated field to listunspent.
      Fixes #2519. When sending from a zaddr, minconf cannot be zero.
      Fixes #2480. Null entry in map was dereferenced leading to a segfault.
      Closes #2583. Exclude watch-only utxos from z_sendmany coin selection.
      Set up a clean chain. Delete redundant method wait_until_miner_sees() via use of sync_all().
      Implement RPC shield_coinbase #2448.
      Update which lock to synchronize on when calling GetBestAnchor().
      Closes #2637. Make z_shieldcoinbase an experimental feature where it can be enabled with: zcashd -experimentalfeatures -zshieldcoinbase.
      Replace 'bitcoin address' with 'zcash address'.
      Closes #2639. z_shieldcoinbase is now supported, no longer experimental.
      Closes #2263 fixing broken pipe error.
      Closes #2576. Update link to security info on z.cash website.
      Closes #2639. Adds optional limit parameter with a default value of 50.
      Fix an issue where qa test wallet_shieldcoinbase could hang.
      Add payment disclosure as experimental feature.
      RPC dumpwallet and z_exportwallet updated to no longer allow overwriting an existing file.
      Add documentation for shielding coinbase utxos.
      Add documentation for payment disclosure.
      Closes #2759. Fixes broken pipe error with QA test wallet.py.
      Closes #2746. Payment disclosure blobs now use 'zpd:' prefix.
      Upgrade OpenSSL to 1.1.0h

Smrtz (3):
      Updates alert private key.  To test: `./src/test/test_bitcoin -t Alert_tests`
      Adds `//ZEN MOD START` and `END` comments.
      Removes ZCash security message from  output.

Stefano (1):
      test max complexity modified

Stephen (1):
      Add paytxfee to getwalletinfo, warnings to getnetworkinfo

Taylor Hornby (8):
      Update OpenSSL from 1.1.0h to 1.1.1a. #3786
      Update boost from v1.66.0 to v1.69.0. #3786
      Update Rust from v1.28.0 to v1.32.0. #3786
      Update Proton from 0.17.0 to 0.26.0. #3816, #3786
      Patch Proton for a minimal build. #3786
      Fix OpenSSL reproducible build regression
      Fix proton patch regression. #3916
      Patch out proton::url deprecation as workaround for build warnings

Wladimir J. van der Laan (51):
      rpc: make `gettxoutsettinfo` run lock-free
      test: Move reindex test to standard tests
      rpc: Remove chain-specific RequireRPCPassword
      univalue: Avoid unnecessary roundtrip through double for numbers
      rpc: Accept strings in AmountFromValue
      Fix crash in validateaddress with -disablewallet
      Improve proxy initialization
      tests: Extend RPC proxy tests
      build: Remove -DBOOST_SPIRIT_THREADSAFE
      tests: Fix bitcoin-tx signing testcase
      doc: remove documentation for rpcssl
      qa: Remove -rpckeepalive tests from httpbasics
      Remove rpc_boostasiotocnetaddr test
      build: build-system changes for libevent
      tests: GET requests cannot have request body, use POST in rest.py
      evhttpd implementation
      Implement RPCTimerHandler for Qt RPC console
      Document options for new HTTP/RPC server in --help
      Fix race condition between starting HTTP server thread and setting EventBase()
      Move windows socket init to utility function
      Revert "rpc-tests: re-enable rpc-tests for Windows"
      init: Ignore SIGPIPE
      http: Disable libevent debug logging, if not explicitly enabled
      rpc: Split option -rpctimeout into -rpcservertimeout and -rpcclienttimeout
      Make RPC tests cope with server-side timeout between requests
      chain: define enum used as bit field as uint32_t
      auto_ptr → unique_ptr
      bitcoin-cli: More detailed error reporting
      depends: Add libevent compatibility patch for windows
      bitcoin-cli: Make error message less confusing
      test: Avoid ConnectionResetErrors during RPC tests
      net: Automatically create hidden service, listen on Tor
      torcontrol improvements and fixes
      doc: update docs for Tor listening
      tests: Disable Tor interaction
      Fix memleak in TorController [rework]
      tor: Change auth order to only use HASHEDPASSWORD if -torpassword
      torcontrol: Explicitly request RSA1024 private key
      Use real number of cores for default -par, ignore virtual cores
      Remove ChainParams::DefaultMinerThreads
      rpc: Add WWW-Authenticate header to 401 response
      Make HTTP server shutdown more graceful
      http: Wait for worker threads to exit
      http: Force-exit event loop after predefined time
      http: speed up shutdown
      build: Enable C++11 build, require C++11 compiler
      build: update ax_cxx_compile_stdcxx to serial 4
      test: Remove java comparison tool
      build: Remove check for `openssl/ec.h`
      devtools: Check for high-entropy ASLR in 64-bit PE executables
      build: supply `-Wl,--high-entropy-va`

ca333 (1):
      update libsodium dl-path

calebogden (1):
      Fixing typos on security-check.py and torcontrol.cpp

codeparticle (2):
      [core-upgrade] - Merged PR 14 from zencashio: TX Replay Protection
      additional changes from PR 14 that were in the merge itself

cronicc (74):
      Fix primary depends DL url
      Update build-debian-package.sh
      Fix fetch-params.sh
      Fixup copy std config
      Zenify payment-api.md
      Fix typo
      Fixup2 copy std config
      Add MacOS support to fetch-params.sh
      Add # ZEN_MOD to fetch-params.sh
      Add new logo ascii art
      Remove libsnark.mk
      Revert "Integrated Commit: Update libsodium.mk #1fe1ccc3"
      Reintroduce check for missing config
      Set Hard Fork heights in fork4_nulltransactionfork.cpp
      Set Hard Fork height in rpc_wallet_tests.cpp
      Fix rpc_z_shieldcoinbase_internals test
      Set 2.0.14-rc1 deprecation to the same as 2.0.11
      Set different libsnark OPTFLAGS if arch is aarch64
      Update ARM instructions in README.md
      Update build-arm.sh
      Update contrib/debian
      Fix merge conflict fragments in /doc
      Zenify manpage buildsystem
      Update build-debian-package.sh
      Zenify bash-completion
      Regenerate man pages
      Update zen.manpages
      Update build-debian-package.sh
      Zcash->Zen in deprecation msg
      Add test for Super/SecureNode address and reward
      Clean up after each rpc-test again
      Make full_test_suite.py work with zen
      Fix make check-security on Linux
      Add Mainnet tests to test_forkmanager.cpp
      Set 16 week deprecation
      Bump Version to 2.0.14
      Update README.md
      Style fix: identation, remove whitespace
      Bump version to 2.0.15-beta1
      Update manpages for 2.0.15
      Bump version to 2.0.15-rc1
      Update OpenSSL to 1.1.0i
      fixup, bad merge
      Update deprecation
      Bump Version to 2.0.15
      Improved Groth16 implementation of Sprout circuit
      Set HardFork heights
      Adjust tests with HF heights
      Bump version to 2.0.16-rc1
      Set libsnark ARM64 specific flags
      Fix no-dot-so test on ARM64
      Fix ARM64 timeout error in wallet_protectcoinbase.py
      Set approx. release height
      Bump version to 2.0.16
      Bump version to 2.0.16-1, change copyright year
      Bump version in README.md
      Add --rpc-extended argument
      (wallet) Check that the commitment matches the note plaintext provided by the sender.
      Reword -blockmaxcomplexity and -deprecatedgetblocktemplate help text
      Update man pages for v2.0.17
      Bump Version to v2.0.17-rc1
      Add mainnet and testnet checkpoint blocks
      Bump Version to v2.0.17
      Fix issue #159 - 2.0.17-rc1 windows build failing
      Set approx. release height, next deprecation at block 555555
      More robust wget resume support, add aria2 dl method with parallel connections
      Update OpenSSL from 1.1.1a to 1.1.1c
      Regenerate man pages
      Add new mainnet and testnet checkpoint blocks
      Handle '[log] showSignature = true' git global config
      Set deprecation block 610000, ~16weeks in the future
      Rename zcash to zen
      Bump version to v2.0.18
      Changes to responsible_disclosure.md for Horizen

daniel (1):
      add powerpc build support for openssl lib

dexX7 (1):
      Return all available information via validateaddress

emilrus (1):
      Replace bitcoind with zcashd

fanquake (4):
      [depends] libevent 2.1.7rc
      [build-aux] Update Boost & check macros to latest serials
      [depends] Add -stdlib=libc++ to darwin CXX flags
      [depends] Set OSX_MIN_VERSION to 10.8

fgius (1):
      Added new python test in order to test the solution for 51% attack

g666 (2):
      removed test not used
      conflict merge resolved

hellcatz (10):
      Update chainparams.cpp
      Update common.h
      Update httpserver.cpp
      Update init.cpp
      Update main.cpp
      Update metrics.cpp
      Update rpcblockchain.cpp
      Update rpcwallet.cpp
      Update walletdb.cpp
      Update rpcwallet.cpp

instagibbs (1):
      Add common failure cases for rpc server connection failure

joshuayabut (20):
      Integrated commit: Update Testnet DNS Seeds
      Integrated Commit: Core/DAO allocation
      Integrated Commit: Add nChainsplitIndex and nChainsplitTime Checks
      Integrated Commit: Remove genesis builder code
      Integrated Commit: Remove fMinerTestModeForFoundersRewardScript field
      Integrated Commit: Set testnet chainsplit at block 70,000 and LIVE NOW
      Integrated Commit: Disable relaying of pre-chainsplit transactions
      Integrated Commit: Update RPC ports
      Integrated Commit: Chainsplit: Allow blocks up to 10 days old to be the chain tip
      Integrated Commit: Update README.md 199bc65
      Integrated Commit: Update depends update URL to Zen 55f82c4
      Integrated Commit: fixup! Update binary names to Zen b0fac5c - Note that most changes from that commit were already merged from a previous PR.
      Integrated Commit: Fixup debug.log enabled 8455ec0
      Integrated Commit: Add block checkpoint at 96577 e83ff75
      Integrated Commit: Modify Zen base58 prefixes & abbreviations; update DNS seeds d9d6453
      #12 - Bugfix for out of bounds read.
      Integrated Commit: Add BIP9 Softfork Capability for OP_CHECKBLOCKATHEIGHT b01498b
      Integrated Commit: fixup! Update metric screen 1932f31
      Integrated Commit: Remove BIP9 OP_CHECKBLOCKATHEIGHT 4f63f28
      Integrated Commit: Add OpenSSL support (WIP) #7afdebd

koljenovic (1):
      Add mempool TX count metric to splash screen (#143)

kozyilmaz (4):
      [macOS] system linker does not support “--version” option but only “-v”
      option to disable building libraries (zcutil/build.sh)
      support per platform filename and hash setting for dependencies
      empty spaces in PATH variable cause build failure

kpcyrd (2):
      Fetch params from ipfs if possible
      Prefer wget over ipfs

mruddy (1):
      add tests for the decodescript rpc. add mention of the rpc regression tests to the testing seciton of the main readme.

nickolay (1):
      Created getblocktemplate performance test

nomnombtc (9):
      add script to generate manpages with help2man
      add gen-manpages.sh description to README.md
      add autogenerated manpages by help2man
      add doc/man/Makefile.am to include manpages
      add doc/man to subdir if configure flag --enable-man is set
      add conditional for --enable-man, default is yes
      change help string --enable-man to --disable-man
      regenerated all manpages with commit tag stripped, also add bitcoin-tx
      improved gen-manpages.sh, includes bitcoin-tx and strips commit tag, now also runs binaries from build dir by default, added variables for more control

paveljanik (1):
      [TRIVIAL] Fix typo: exactmath -> exactmatch

pier (1):
      Update README.md

pierstab (29):
      minor merge fixes, first pass Mac OS support
      re-add proton lib
      add build-mac-clang to build with clang, its temporary
      add missing zen tags
      add missing zen tags
      fixes the bug implementation of the replay protection CheckBlockHash
      add hashReserved to LoadBlockIndexGuts
      fix typo
      minor fix gtest forkmanager for change testnet height fork and empty CF before first fork
      first pass code review
      split coinbase from 30 to 10-10-10 to prepare for sidechain, fix release version, todo gtest
      code clean up, fix typo
      minor changes
      fix iscommunityfund
      fix reward/miner/foundation
      Revert "Merge branch 'master' into development"
      fix https://github.com/HorizenOfficial/zencash/issues/46 and https://github.com/HorizenOfficial/zencash/issues/46
      minor fixes
      refine CF,SN,XN addresses for testnet, reduce set for mainnet
      change the getblocktemplate , add securenode, supernode in the coinbasetxn object, add the check in the related test
      add debug print to util.py syncblock
      add sync check in getblockdelay, add specific logs for block delay and sync status change
      add test block delay main win
      changes in delay penalty rpc test, remove threshold from the penalty formula
      minor fix in rpc block delay penalty test
      fix typo broken build
      fix one op_cbah condition in the solver, added new line char to some debug messages
      add windows and mac os specific message when is not possible to copy the default configuration file, add some new line to the error message in the interpreter
      fix win escape

practicalswift (1):
      Net: Fix resource leak in ReadBinaryFile(...)

pstab (2):
      changes to 51% mitigation test
      minor fixes and changes in the gtest 51% penalty mitigation

str4d (2):
      Update tests to check actual infinity as well as INF_FEERATE
      Add unit test for security issue 2017-04-11.a

syd (15):
      Upgrade googletest to 1.8.0
      Get the sec-hard tests to run correctly.
      Update libsodium from 1.0.11 to 1.0.15
      Remove Boost conditional compilation.
      Update to address @daira comments wrt fixing configure.ac
      Get rid of consensus.fPowAllowMinDifficultyBlocks.
      Don't compile libgtest.a when building libsnark.
      Add gtests to .gitignore
      Get rid of fp3 from libsnark, it is not used.
      InitGoogleMock instead of InitGoogleTest per CR
      Get rid of underscore prefixes for include guards.
      Rename bash completion files so that they refer to zcash and not bitcoin.
      Fix libsnark test failure.
      Fix libsnark dependency build.
      Remove OSX and Windows files from Makefile + share directory.

tarrenj (8):
      Update proton.mk
      Update README.md
      Update clientversion.h
      Update README.md
      Update README.md
      Update README.md
      Update developer-notes.md
      Update developer-notes.md

unsystemizer (1):
      Clarify `listenonion`

zathras-crypto (1):
      Exempt unspendable transaction outputs from dust checks

zebambam (3):
      Added documentation warnings about DNS rebinding attacks, issue #3841
      Added responsible disclosure statement for issue #3869
      Minor speling changes

