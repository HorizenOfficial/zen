Changelog
=========

Paolo Tagliaferri (46):
      Fixed an issue with the validation of custom fields
      Fixed failing Python tests due to malformed custom fields
      Increased unit test coverage for custom fields validation
      Implemented a "custom" function to compute trailing zero bits in a byte
      Added a unit test for the GetBytesFromBits() utility function
      Removed UT failing on Mac OS due to different rand() implementation
      Added a flag to getblocktemplate to include merkle roots in the JSON
      Optimized computation of Merkle trees in getblocktemplate RPC command
      Prepared the management of Fork 9 (sidechain version)
      Added the sidechain version in CTxScCreationOut and in ScFixedParameters
      Added the sidechain version as a parameter for custom fields validation
      Sidechain version fork height management
      Added regressions tests for the sidechain version management
      Added a Python test to check the sidechain version fork point
      Set the "version" parameter in "sc_create" RPC command as mandatory
      Made the "version" argument mandatory in the sc_create() RPC command
      Made "version" argument mandatory in "createrawtransaction" sc creation
      Added a test for sc_create() RPC command without version
      Added "sidechain version" to "getscinfo()" RPC command
      Used the constant SC_VERSION_FORK_HEIGHT in some Python tests
      Set sc_version to 0xff by default to detect not initialized variables
      Fixed tests
      Extended Python tests to verify the persistence of the sc version info
      Fixed typo and removed unused code
      Restored the serialization of a field in CSidechain objects
      Implemented additional UTs for custom fields validation
      Fixed typo
      Fixed a wrong check for the sidechain version fork point in sc_create
      Added a test for the creation of sidechains with totally invalid version
      Fixed typo
      Reverted "reserved" CSidechain field to 0 instead of 0xffffffff
      Fixed a breaking change in the ScFixedParameters serialization
      Fixed an issue with the Python blockchain helper class
      Added GetSidechainVersions websocket API
      Added Python tests for GetSidechainVersions websocket API
      Added a check when serializing "version" and "withdrawalEpochLength"
      Added version of sidechains that sent a certificate in getscgenesisinfo
      Fixed a bug in getscgenesisinfo RPC command
      Added a Python test for sc_getscgenesisinfo RPC command
      Minor fix for FieldElementCertificateFieldConfig toString()
      Fixed execution permissions for sc_getscgenesisinfo.py test
      Struct sSidechainVersion_tag renamed as ScVersionInfo
      Added some utility test functions
      Increased fork point activation height for sidechain version
      Updated community fund addresses with fork point 9 (sidechain version)
      Renamed "version" field in getscinfo RPC command

cronicc (11):
      Add 'merkleTree' and 'scTxsCommitment' to 'getblocktemplate'
      Set fork activation heights, set new treasury addresses:
      Address review comments
      Set clientversion 3.1.0
      Update checkpoint blocks
      Update OpenSSL to 1.1.1n
      Update Debian package info
      Set deprecation height 1111262/2022-10-20
      Update responsible_disclosure.md and rename to SECURITY.md
      Update man pages
      Add release notes

