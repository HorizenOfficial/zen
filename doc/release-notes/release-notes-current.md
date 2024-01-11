zend v4.1.99
=========

This version is expected to deprecate on Mainnet at block **#XXXXXXX**, which will occur on **Month Xth XXXX at approximately XX:XX PM UTC**. Please, update to a newer version before Month XXth.

Zend `4.1.1` is going to deprecate on Mainnet at block **#1502084**, which will occur on **February 6th 2024 at approximately 12:00 PM UTC**. Please, update to a `v5.0.0` version before February 6th.

Zend `5.0.0` is going to deprecate on Mainnet at block **#xxxxxxx**, which will occur on **xxx xxth, 2024 at approximately xx:xx PM UTC**.

A **hard fork** is going to activate on Mainnet at block **#xxxxxxx**, which will occur on **xxx xxth, 2024 at approximately xx:xx PM UTC**

The same **hard fork** will activate on testnet at block **#1404200**, which will occur on **January 16th at approximately 12:00:00 UTC**.

## Important Notes

### Shielded pool removal
- PR [#628](https://github.com/HorizenOfficial/zen/pull/628) implements [ZenIP-42207](https://github.com/HorizenOfficial/ZenIPs/blob/master/zenip-42207.md), which introduces a hard fork fully disabling the shielded pool from `zend`. After the hard fork, it will no longer be possible to submit and process transactions involving shielded (z-) addresses, thus transparent-to-shielded, shielded-to-shielded and shielded-to-transparent transactions will be completely disallowed.
- PR [#628](https://github.com/HorizenOfficial/zen/pull/628) as a consequence of [ZenIP-42207](https://github.com/HorizenOfficial/ZenIPs/blob/master/zenip-42207.md), the following RPC commands
    - `z_sendmany`
    - `z_mergetoaddress`

    have been partially disabled, as after the hard fork they are only allowed to submit transparent-to-transparent transactions (check inline documentation for additional details).

## New Features and Improvements

## Bugfixes

## Minor Changes
- PR [#628](https://github.com/HorizenOfficial/zen/pull/628) all the RPC commands interacting with the shielded pool - hence, all the RPC commands starting with `z_` or `zc_` (for instance `z_getbalanace` or `zc_raw_joinsplit`) - are now *deprecated*. A warning message has been included in the help body of these commands.

## Contributors

* [@JackPiri](https://github.com/JackPiri)
* [@a-petrini](https://github.com/a-petrini)
* [@drgora](https://github.com/drgora)
* [@ptagl](https://github.com/ptagl)
* [@mantle0x](https://github.com/mantle0x)

Special thanks to [@mantle0x](https://github.com/mantle0x) for the first contribution to Zen!
