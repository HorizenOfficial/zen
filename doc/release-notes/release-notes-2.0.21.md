Notable changes
===============

Removal of time adjustment
-------------------------------------------------------------

Prior to v2.0.21, `zend` would adjust the local time that it used by up
to 70 minutes, according to a median of the times sent by the first 200 peers
to connect to it. This mechanism was inherently insecure, since an adversary
making multiple connections to the node could effectively control its time
within that +/- 70 minute window (this is called a "timejacking attack").

In the v2.0.21 release, in addition to other mitigations for timejacking attacks,
as a simplification the time adjustment code has now been completely removed.
Node operators should instead simply ensure that local time is set
reasonably accurately.

If it appears that the node has a significantly different time than its peers,
a warning will still be logged and indicated on the metrics screen if enabled.

Changelog
=========

MarcoOl94 (4):
      Added additional parameter to z_sendmany for change address needed by SBH
      Added new test case for multisig address in z_sendmany.py and some code style fix
      Added new parameter to z_sendmany's help
      renamed change parameter of z_sendmany in sendChangeToSource

cronicc (8):
      Set fork6 activation heights
      Add changelog
      Set mainnet/testnet checkpoint blocks
      Set deprecation block 831936
      Set version to 2.0.21
      Add release-notes
      Regenerate man pages
      Fixup: z_sendmany help message lines were misordered in https://github.com/ZencashOfficial/zen/pull/249

root (7):
      add timeblock check
      remove comments
      fix init and update python test framework
      remove getadjusttime, use gettime
      add gtest futuretimestamp forkmanager
      revert getpeerinfo. fix getnetworkinfo
      Fix undefined behavior in CScriptNum

