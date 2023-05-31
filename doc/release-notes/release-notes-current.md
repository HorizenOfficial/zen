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

## Contributors
[@JackPiri](https://github.com/JackPiri)
[@a-petrini](https://github.com/a-petrini)
[@drgora](https://github.com/drgora)
[@dullerino](https://github.com/dullerino)

Special thanks to [dullerino](https://github.com/dullerino) for the first contribution to Zen!
