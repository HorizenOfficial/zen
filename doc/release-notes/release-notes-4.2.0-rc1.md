zend v4.2.0-rc1
=========

This version is expected to deprecate on Mainnet at block **#1502084**, which will occur on **February 6th 2024 at approximately 12:00 PM UTC**. Please, update to a newer version before February 6th.

A **hard fork** is going to activate on Testnet at block **#1396500**, on **January 3rd 2024 at approximately 12:00 PM UTC**.

## Important Notes
- PR [#624](https://github.com/HorizenOfficial/zen/pull/624) is a follow-up in the gradual path of shielded pool deprecation, it introduces a hard fork requiring the unshielding of private funds to target only script addresses. After the hard fork, unshielding to P2PKH addresses or to P2PK addresses will not be allowed. For this reason, the following RPC commands have been (partially) deprecated: `z_sendmany` and `z_mergetoaddress` (check inline documentation for additional details).

## New Features and Improvements
- Unshielding to script addresses only: a hard-fork is introduced which results in unshielding txs to target only P2SH addresses [#624](https://github.com/HorizenOfficial/zen/pull/624)

## Bugfixes

## Minor Changes

## Contributors

[@JackPiri](https://github.com/JackPiri)
[mantle0x](https://github.com/mantle0x)

Special thanks to [mantle0x](https://github.com/mantle0x) for the first contribution to Zen!
