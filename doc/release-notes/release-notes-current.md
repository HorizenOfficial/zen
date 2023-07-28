zend v4.0.99
=========

## Important Notes
- PR [#526](https://github.com/HorizenOfficial/zen/pull/526) implements [ZenIP-42204](https://github.com/HorizenOfficial/ZenIPs/blob/master/zenip-42204.md), which introduces a hard fork disabling the possibility to move transparent funds to shield addresses. After the hard fork, only shielded-to-shielded and shielded-to-transparent transactions will be allowed. For this reason, the following RPC commands have been (partially or fully) deprecated: `z_sendmany`, `z_shieldcoinbase` and `z_mergetoaddress` (check inline documentation for additional details).
- PR [#539](https://github.com/HorizenOfficial/zen/pull/539) modifies the data structure returned by RPC method `getrawmempool`
- PR [#541](https://github.com/HorizenOfficial/zen/pull/541) fixes an issue that made explorers, relying on `zend` RPC commands, not to show old transactions including P2PK scripts. The fix has an effect only on transactions processed after the update, so in case of explorers it is recommended to run a (fast) reindex to properly handle also previously received transactions. However, reindexing is not mandatory.
- Zend `4.0.0` was the last version supporting Ubuntu Bionic, whose End of Life started on May 31, 2023. PR [#553](https://github.com/HorizenOfficial/zen/pull/553) removes Ubuntu Bionic from Travis, and from that date the minimal supported version will be Ubuntu Focal.

## New Features and Improvements
- Implementation of [ZenIP-42204](https://github.com/HorizenOfficial/ZenIPs/blob/master/zenip-42204.md): a hard-fork is introduced which results in shielding txs being deprecated [#526](https://github.com/HorizenOfficial/zen/pull/526)
- Removal of the p2p alert system [#540](https://github.com/HorizenOfficial/zen/pull/540)
- Added support for Pay-To-Public-Key (P2PK) scripts [#541](https://github.com/HorizenOfficial/zen/pull/541)
- Set a maximum limit to the size of the mempool (400 MB by default, configurable through `-maxmempool` startup parameter)[#574](https://github.com/HorizenOfficial/zen/pull/574)

## Bugfixes and Minor Changes
- Fix compilation warnings for GCC (v12) and clang (v15), update boost to v1.81 [#513](https://github.com/HorizenOfficial/zen/pull/513)
- Refactoring of error code handling for proof verifier, UI improvements for reindex (fast) [#504](https://github.com/HorizenOfficial/zen/pull/504)
- Fix compilation errors for recent macOS versions [#536](https://github.com/HorizenOfficial/zen/pull/536)
- Fix for preventing already received and spent txs to be asked for again [#537](https://github.com/HorizenOfficial/zen/pull/537)
- Fix for limitedmap [#538](https://github.com/HorizenOfficial/zen/pull/538)
- Fix for return value of function mempoolToJSON [#539](https://github.com/HorizenOfficial/zen/pull/539)
- Fix logfile output for "Leaving block file" statement [#530](https://github.com/HorizenOfficial/zen/pull/530)
- Fix undefined behavior of a bitshift executed on a signed integer [#552](https://github.com/HorizenOfficial/zen/pull/552)
- Improve `debug.log` file reopening procedure [#552](https://github.com/HorizenOfficial/zen/pull/552)
- Introduce a maximum dns look up for one entry [#561](https://github.com/HorizenOfficial/zen/pull/561)
- Fix `shieldedpooldeprecation_rpc.py` and `wallet_mergetoaddress_2.py` Python tests [#544](https://github.com/HorizenOfficial/zen/pull/544)
- Make `fReindex` and `fReindexFast` flag variables thread safe [#555](https://github.com/HorizenOfficial/zen/pull/555)
- Remove `zencash-apple` as a dependency to build Zend on MacOS [#558](https://github.com/HorizenOfficial/zen/pull/558)
- Remove some unnecessary log lines from the command line interface [#559](https://github.com/HorizenOfficial/zen/pull/559)
- Update "contributing" documentation and the GitHub issue template [#562](https://github.com/HorizenOfficial/zen/pull/562)
- Remove the need to manually specify libzendoo dependencies one by one [#565](https://github.com/HorizenOfficial/zen/pull/565)
- General cleaning of code (static analysis) [#564](https://github.com/HorizenOfficial/zen/pull/564)
- Minor refactoring of `sc_cert_getblocktemplate.py` [#568](https://github.com/HorizenOfficial/zen/pull/568)
- Refactoring of the network functions in the Python test framework [#563](https://github.com/HorizenOfficial/zen/pull/563)
- Update `cargo` version from `1.51` to `1.70` [#569](https://github.com/HorizenOfficial/zen/pull/569)
- Remove `pyblake2` Python dependency [#570](https://github.com/HorizenOfficial/zen/pull/570)
- Avoid unnecessary copies when passing parameters to functions (static analysis) [#567](https://github.com/HorizenOfficial/zen/pull/567)
- Improve syntax of `autogen.sh` (static analysis) [#571](https://github.com/HorizenOfficial/zen/pull/571)
- Updated MacOS version on Travis CI from `12.3` to `14.2` [#572](https://github.com/HorizenOfficial/zen/pull/572)
- Minor performance improvement during the processing of `inv` and `getdata` P2P messages [#575](https://github.com/HorizenOfficial/zen/pull/575)
- Minor change of the P2P inactivity timeout check [#579](https://github.com/HorizenOfficial/zen/pull/579)
- Improved the management of SSL disconnections [#566](https://github.com/HorizenOfficial/zen/pull/566)
- Added the possibility to pass parameters to the `run_until_fails.py` helper script [#578](https://github.com/HorizenOfficial/zen/pull/578)
- Avoid relaying transactions coming from whitelisted peers that would make the local node be banned [#580](https://github.com/HorizenOfficial/zen/pull/580)

## Contributors
[@JackPiri](https://github.com/JackPiri)
[@a-petrini](https://github.com/a-petrini)
[@drgora](https://github.com/drgora)
[@dullerino](https://github.com/dullerino)
[@titusen](https://github.com/titusen)
[@la10736](https://github.com/la10736)
[@AndreaLanfranchi](https://github.com/AndreaLanfranchi)
[@ptagl](https://github.com/ptagl)

Special thanks to [dullerino](https://github.com/dullerino) for the first contribution to Zen!
