Changelog
=========

Jack Grigg (1):
      Cast uint8* in InterruptibleRecv to char* for recv

MarcoOl94 (25):
      Added fix for deadlock issue (208) and introduced GUnit test to verify it
      leftover from previous commit
      Made lisstransactions filterable by address
      Modified and reintroduced test_foundersreward
      Made changes suggested by @abi87
      Reversed the txes' order returned by listtransactions
      Changed requested by albi87
      Introduced changes requested from albi87
      Reintroduced Account for backward compatibility
      Made changes proposed by @alsala
      modified help of listtransactions
      Refactored wallet.cpp
      Modified test_foundersreward with albi87's changes
      Implemented changes requested by alsala
      Removed useless asserts
      Extended test_checkblock to verify correct send of rewards after the halving
      Removed GetFilteredTransaction and modified OrderedTxItems
      Removed GetFilteredTransaction and modified orederedTxItems to match previous serialization
      Merged listtransactions' test
      Some refactor on listtransactions test
      Some changed comments
      Removed unused variable
      changed passing-parameter in OrderderTxItems
      Fixed test wallet_shieldcoinbase
      Introduced break in orderedTxItems

Miles Manley (1):
      Update libsodium download-path

Sean Bowe (1):
      Fix of CVE-2017-18350

abi87 (5):
      cleaned up useless includes
      minor gtest cleanup
      minor gtest cleanup
      fixed compilation error and cleaned formatting
      removed commented out test deemed useless

cronicc (13):
      Bump version to 2.0.19-1
      Fix socket.settimeout python error on MacOS
      Workaround for 'Connection reset by peer' rpc test failures
      fetch-params.sh bugfix
      Add test for fetch-params.sh using all supported DL methods
      Refactor rpc-tests.sh
      Add --rpc-exclude --rpc-split options for refactored rpc-tests.sh
      Add travis-ci CI/CD pipeline part1
      Set mainnet/testnet checkpoint blocks
      Set deprecation block 736000
      Add test instructions to README.md
      Set version to 2.0.20
      Generate manpages

