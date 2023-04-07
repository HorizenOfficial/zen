zend v4.0.0-RC2
=========

As the previous version of `zend` (3.3.1), this RC2 is expected to deprecate at block **#1359120**, which will occur on **May 31st, 2023 at approximately 12:00 PM UTC**. Please, update to `v4.0.0` before May 31st (the final release will be available soon).

A **hard fork** has activated on testnet at block **#1228700**, which has occurred on **March 13th at 14:37:43 GMT+0000**.

## New Features and Improvements
* Sidechains version 2 (introducing non-ceasable behavior and key rotation) [#512](https://github.com/HorizenOfficial/zen/pull/512)
* Added some light pre-checks for the sidechain transaction commitment tree to improve performance [#515](https://github.com/HorizenOfficial/zen/pull/515)
* Added load balancer to Python test suite for speeding up CI flow [#524](https://github.com/HorizenOfficial/zen/pull/524)[#529](https://github.com/HorizenOfficial/zen/pull/529)
* Removal of ENABLE_ADDRESS_INDEXING directive for unifying code flows and binaries [#531](https://github.com/HorizenOfficial/zen/pull/531)

## Bugfixes and Minor Changes
* Fixed an issue that was causing the build procedure to fail when the path of the project folder was too long [#470](https://github.com/HorizenOfficial/zen/pull/470)
* Added on the Travis CI the possibility to dynamically enable/disable the MacOS build on some branches [#489](https://github.com/HorizenOfficial/zen/pull/489)
* Fixed a bug affecting the z_mergetoaddress RPC command that was causing a wrong estimation of transaction size potentially leading to the creation of invalid transactions (rejected by the mempool for exceeding the max size) [#479](https://github.com/HorizenOfficial/zen/pull/479)
* Added the usage of Travis cache for compiling zend dependencies in order to speed up the CI [#498](https://github.com/HorizenOfficial/zen/pull/498)
* Minor fixes for help-documentation of some RPC methods [#501](https://github.com/HorizenOfficial/zen/pull/501)
* Updated MC Crypto Lib dependency to version 0.3.0-rc1 [#506](https://github.com/HorizenOfficial/zen/pull/506)
* Fixed blocks/txs synchronization between nodes in listtransactions.py test [#508](https://github.com/HorizenOfficial/zen/pull/508)
* Update documentation and security information [#514](https://github.com/HorizenOfficial/zen/pull/514)
* Always run MacOS jobs when building a tag [#518](https://github.com/HorizenOfficial/zen/pull/518)
* Fixed some unit tests that were failing when running in debug mode [#359](https://github.com/HorizenOfficial/zen/pull/359)
* Refactoring of BlockchainHelper used in Python tests [#519](https://github.com/HorizenOfficial/zen/pull/519)
* Refactoring removing old todos [#520](https://github.com/HorizenOfficial/zen/pull/520)
* Fixed blocks/txs synchronization between nodes in mempool_double_spend.py test [#528](https://github.com/HorizenOfficial/zen/pull/528)
* Updated MC Crypto Lib dependency to version 0.3.0 [#527](https://github.com/HorizenOfficial/zen/pull/527)

## Contributors
* [@AndreaLanfranchi](https://github.com/AndreaLanfranchi)
* [@a-petrini](https://github.com/a-petrini)
* [@cronicc](https://github.com/cronicc)
* [@DanieleDiBenedetto](https://github.com/DanieleDiBenedetto)
* [@drgora](https://github.com/drgora)
* [@JackPiri](https://github.com/JackPiri)
* [@ptagl](https://github.com/ptagl)

