zend v4.1.1
=========

This version is expected to deprecate on Mainnet at block **#1502084**, which will occur on **February 6th 2024 at approximately 12:00 PM UTC**. Please, update to a newer version before February 6th.

## Important Notes

### Proof verifier logging
Starting from this release, `zend` includes a logging mechanism specific for the proof verifier, meaning that there will be a new log file named `mc_crypto.log` inside the Zen data folder. This new log file can be configured through the `mc_crypto_log_config.yaml`, which, during the first restart of the node after the upgrade, will be automatically created inside the same folder.

## New Features and Improvements
* Improved the mechanism to limit the size of the mempool [#611](https://github.com/HorizenOfficial/zen/pull/611)
* Updated `libzendoo` (aka MC Crypto Lib) to version `0.5.0`. It brings a new logging file in the Zen data folder specific for the SNARK proof verifier [#616](https://github.com/HorizenOfficial/zen/pull/616)
* Updated OpenSSL dependency to version `1.1.1w` [#617](https://github.com/HorizenOfficial/zen/pull/617)

## Bugfixes
* Fixed the build on CentOS [#605](https://github.com/HorizenOfficial/zen/pull/605)

## Minor Changes
* Refactoring of the networking sub-system: added connection manager class CConnman [#573](https://github.com/HorizenOfficial/zen/pull/573)
* Refactoring of Python test `sc_cert_getblocktemplate.py` [#581](https://github.com/HorizenOfficial/zen/pull/581)
* Improved the management of connection inside the Python test framework [#586](https://github.com/HorizenOfficial/zen/pull/586)
* Improved the validation of `-maxsendbuffer` and `-maxreceivebuffer` startup parameters [#588](https://github.com/HorizenOfficial/zen/pull/588)
* Fixed logging when running unit tests related to the `alertNotify()` function [#589](https://github.com/HorizenOfficial/zen/pull/589)
* Improved the validation of P2P message headers [#591](https://github.com/HorizenOfficial/zen/pull/591)
* Increased coverage of tests for the mempool size limit [#596](https://github.com/HorizenOfficial/zen/pull/596)
* Minor static analysis fixes [#590](https://github.com/HorizenOfficial/zen/pull/590) [599](https://github.com/HorizenOfficial/zen/pull/599) [#606](https://github.com/HorizenOfficial/zen/pull/606)
* Increased robustness and flexibility of release scripts [#600](https://github.com/HorizenOfficial/zen/pull/600)
* Improvement of the Python test `mempool_size_limit.py` [#601](https://github.com/HorizenOfficial/zen/pull/601)
* Minor refactoring of the usage of the `mempool` pointer [#602](https://github.com/HorizenOfficial/zen/pull/602)
* Minor refactoring of the code inside `ThreadSocketHandler()` [#608](https://github.com/HorizenOfficial/zen/pull/608)
* Removed any reference to `boost::thread` from the networking code [#612](https://github.com/HorizenOfficial/zen/pull/612)
* Copyright header update [#613](https://github.com/HorizenOfficial/zen/pull/613)
* Fixed a sporadic error on the CI related to `sc_cert_nonceasing` and improved a few more tests [#618](https://github.com/HorizenOfficial/zen/pull/618)
* Fixed the startup warning message that is shown when configuration files are missing [#619](https://github.com/HorizenOfficial/zen/pull/619)

## Contributors
[@a-petrini](https://github.com/a-petrini)
[@JackPiri](https://github.com/JackPiri)
[@ptagl](https://github.com/ptagl)
[@drgora](https://github.com/drgora)
[@titusen](https://github.com/titusen)
