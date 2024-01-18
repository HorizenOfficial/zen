zend v5.0.0
=========

This version is expected to deprecate on Mainnet at block **#1554150**, which will occur on **May 7th 2024, at approximately 10:00 AM UTC**. Please, update to a newer version before May 7th.

A **hard fork** is going to activate on Mainnet at block **#1502800**, which will occur on **Feb 7th 2024, at approximately 12:00 PM UTC**

## Important Notes

### Shielded pool removal
- PR [#628](https://github.com/HorizenOfficial/zen/pull/628) implements [ZenIP-42207](https://github.com/HorizenOfficial/ZenIPs/blob/master/zenip-42207.md), which introduces a hard fork fully disabling the shielded pool from `zend`. After the hard fork, it will no longer be possible to submit and process transactions involving shielded (z-) addresses, thus transparent-to-shielded, shielded-to-shielded and shielded-to-transparent transactions will be completely disallowed.
- PR [#628](https://github.com/HorizenOfficial/zen/pull/628) as a consequence of [ZenIP-42207](https://github.com/HorizenOfficial/ZenIPs/blob/master/zenip-42207.md), the following RPC commands
    - `z_sendmany`
    - `z_mergetoaddress`

    have been partially disabled, as after the hard fork they are only allowed to submit transparent-to-transparent transactions (check inline documentation for additional details).

## Bugfixes and Minor Changes
- All the RPC commands interacting with the shielded pool - hence, all the RPC commands starting with `z_` or `zc_` (for instance `z_getbalanace` or `zc_raw_joinsplit`) - are now *deprecated*; a warning message has been included in the help body of these commands [#628](https://github.com/HorizenOfficial/zen/pull/628)
- Fix for build error on recent compilers, including gcc v13 and clang v14, and with recent glibc (version 2.36); alignment of compiler optimization flags on clang and gcc on Linux [#629](https://github.com/HorizenOfficial/zen/pull/629)

## Contributors
* [@JackPiri](https://github.com/JackPiri)
* [@a-petrini](https://github.com/a-petrini)
* [@drgora](https://github.com/drgora)
* [@ptagl](https://github.com/ptagl)
* [@mantle0x](https://github.com/mantle0x)

Special thanks to [@mantle0x](https://github.com/mantle0x) for the first contribution to Zen!
