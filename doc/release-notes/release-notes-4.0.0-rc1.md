zend v4.0.0-RC1
=========

This is a release candidate of `zend` meant to be used primarily on testnet.
As the previous version of `zend` (3.2.1), this RC1 is expected to deprecate at block **#1328320**, which will occur on **April 7th, 2023 at approximately 12:00 PM UTC**. Please, update to `v4.0.0` before April 7th (the final release will be available soon).

A **hard fork** is expected to activate on testnet at block **#1228700**, which will occur on **March 13th at approximately 4:00 PM**.

## New Features and Improvements
* Sidechains version 2 (introducing non-ceasable behavior and key rotation) [#512](https://github.com/HorizenOfficial/zen/pull/512)
* Added some light pre-checks for the sidechain transaction commitment tree to improve performance [#515](https://github.com/HorizenOfficial/zen/pull/515)

## Bugfixes and Minor Changes
* Fixed an issue that was causing the build procedure to fail when the path of the project folder was too long [#470](https://github.com/HorizenOfficial/zen/pull/470)
* Added on the Travis CI the possibility to dynamically enable/disable the MacOS build on some branches [#489](https://github.com/HorizenOfficial/zen/pull/489)
* Fixed a bug affecting the z_mergetoaddress RPC command that was causing a wrong estimation of transaction size potentially leading to the creation of invalid transactions (rejected by the mempool for exceeding the max size) [#479](https://github.com/HorizenOfficial/zen/pull/479)
* Added the usage of Travis cache for compiling zend dependencies in order to speed up the CI [#498](https://github.com/HorizenOfficial/zen/pull/498)
* Minor fixes for help-documentation of some RPC methods [#501](https://github.com/HorizenOfficial/zen/pull/501)
* Updated MC Crypto Lib dependency to version 0.3.0-rc1 [#506](https://github.com/HorizenOfficial/zen/pull/506)

## Contributors
* [@AndreaLanfranchi](https://github.com/AndreaLanfranchi)
* [@a-petrini](https://github.com/a-petrini)
* [@cronicc](https://github.com/cronicc)
* [@DanieleDiBenedetto](https://github.com/DanieleDiBenedetto)
* [@drgora](https://github.com/drgora)
* [@JackPiri](https://github.com/JackPiri)
* [@ptagl](https://github.com/ptagl)
