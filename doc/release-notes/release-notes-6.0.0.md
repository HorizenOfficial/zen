zend v6.0.0
=========

This version is expected to deprecate on Mainnet at block **#1872068**, which will occur on **November 13th 2025, at approximately 06:00 AM UTC**.

A **hard fork** is going to activate on **Mainnet** at block **#1807300**, which will occur on **Jul 23rd 2025, at approximately 04:30 AM UTC**

## Important Notes

This version is designed to support the Horizen migration by introducing the following features:

1) A hard fork (HF) that prevents the propagation of all transactions except coinbase transactions across the network. (Note: certificate processing will continue to function as usual.)

2) A new optional configuration flag that accepts a block height value. Once the node reaches this specified height, it will cease processing any new incoming blocks.

- PR [#662](https://github.com/HorizenOfficial/zen/pull/662) HF for zend migration and new optional flag for stop block processing at a set height

## Bugfixes and Minor Changes

- PR [#659](https://github.com/HorizenOfficial/zen/pull/659) remove consecutive duplicate words from docs

## Contributors
* [@alsala](https://github.com/alsala)
* [@drgora](https://github.com/drgora)
* [@zeroprooff](https://github.com/zeroprooff)


