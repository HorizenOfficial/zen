Notable changes
===============

* Soft Fork at mainnet block 835968/testnet block 735700 with minor changes to OP_CBAH handling
* Extend OP_CBAH unit tests
* Security hardening of TLS P2P code, only allow PFS ciphers, limit TLS versions to 1.2/1.3
* Extend TLS unit tests
* Extend the 'getblock' RPC command with additional verbosity level, adding transaction information in the format of the getrawtransaction RPC call
* Add fetch-params.ps1 powershell script for Windows trusted setup DL and verification
* Fix for gcc10 compatibility
* Updates of dependencies OpenSSL, Univalue, libsodium
* Rename GH organization to HorizenOfficial

Changelog
=========

Alberto Sala (31):
      WIP:Replay protection fixes
      Added gtest for new rpfixfork in forkmanager
      Added py test for tx_null_replay with different msg sizes pre-post rp fix fork
      Added some ut for checkblockatheight scripts
      First implementation for verifying cbh script before cpu intensive sign verification
      Added py test for replay protection fixes
      Moved code for checking rp data from tx checker obj into a suited function commonly used; removed noisy traces
      Minor changes to py test
      Fixes and comments after albene code review
      Removed status handling from rp attribute class and modified checker function
      Removed redundant check for rp attributes
      Added bool data member in rpAttributes to be set when cbh opcode is found in a script
      Restored exact comparison against sizeof uint256 for hash in Solver; renamed test and fixed type in func name
      WIP: preliminary modifications and traces, to be continued
      Prevented TLS1.0/1.1 and added ciphers for PFS support in TLS 1.2; Moved net cleanup func from global obj dtor to static func; - Handled to error in TLS separatedly from others in order not to use non-TLS connection if this happens; Used non deprecated api in creating ssl ctx; added fix for Secure Client-Initiated Renegotiation DoS threat; added py test; added more traces and a new 'tls' category
      Non-tls compatibility achieved via a build defined macro, is now set via -tlsfallbacknontls zend option
      tls py test: using ciphers supported by several openssl versions for portability
      Modifications after code review
      Modifications after code review
      Removed useless option setting
      Modification to the list of cipher to support in TLS1.2
      Fixed typo on tlsprotocols.py test
      Removed #if0 block
      Fix wrong bool condition assignment
      Removed unused var in GetChance() func
      Absorbing pr https://github.com/HorizenOfficial/zen/pull/321 with minor change
      Added checks for minimal encoding height in rp
      Added UTs for rp data encodings
      Further modif and UT tests  for minimal encoding
      Modified check of minimal push using the same code of interpreter.cpp
      Added test of tx with non minimal encoding in py script

Jack Grigg (5):
      Add test vectors for small-order Ed25519 pubkeys
      Patch libsodium 1.0.15 pubkey validation onto 1.0.18
      Patch libsodium 1.0.15 signature validation onto 1.0.18
      test: Minor tweaks to comments in LibsodiumPubkeyValidation
      depends: Remove comments from libsodium signature validation patch

MarcoOl94 (2):
      Porting getblock verbose improvement
      Modified help of getblock

PowerVANO (1):
      Added setup_zend.bat and fetch-params.ps1

Taylor Hornby (1):
      Avoid names starting with __.

abi87 (6):
      Cleanup headers and forward declarations
      Added py test description
      First changes following final code review
      Renamed enum to allow for windows compilation
      Fixed socket fd check for portability
      Removed pass-by-value

alsala (4):
      tls py test: fixed setting of unsupported cipher; fixed tls1.3 protocol connection
      Fixed typo in a comment
      Update src/net.h
      Update src/init.cpp

ca333 (1):
      update libsodium to v1.0.18

cronicc (19):
      Update openssl from 1.1.1d to 1.1.1g
      Update univalue to v1.1.1
      Fix MacOS build issue "conversion from 'size_t' (aka 'unsigned long') to 'const UniValue' is ambiguous"
      Replace calls to deprecated method push_back(Pair()) with pushKV()
      Disable aria2 installation on MacOS and use wget as alternative downloader
      CI fixes:
      Rename org to HorizenOfficial
      Remove redundant Building section from README
      Update src/net.cpp
      Update src/net.cpp
      Fix typo
      Set rpfix fork activation height
      Extend fetch-params.ps1:
      Set mainnet/testnet checkpoint blocks
      Set next deprecation block 920000
      Set version to 2.0.22, set copyright year 2020
      Rename threads from zcash to horizen
      Regenerate man pages
      Add release-notes

​Felix Ippolitov (1):
      Fix build with gcc10

