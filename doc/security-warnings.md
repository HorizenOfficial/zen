Security Warnings
====================

Security Audit
--------------

Zen has been subjected to a formal third-party security review. For security
announcements, audit results and other general security information, see
http://blog.horizen.io/.

Wallet Encryption
-----------------

Wallet encryption is disabled, for several reasons:

- Shielded transactions, which are no longer supported, these reasons are kept for historical reference only
    - Encrypted wallets are unable to correctly detect shielded spends (due to the
  nature of unlinkability of JoinSplits) and can incorrectly show larger
  available shielded balances until the next time the wallet is unlocked. This
  problem was not limited to failing to recognize the spend; it was possible for
  the shown balance to increase by the amount of change from a spend, without
  deducting the spent amount.
  - While encrypted wallets prevent spending of funds, they do not maintain the
  shielding properties of JoinSplits (due to the need to detect spends). That
  is, someone with access to an encrypted wallet.dat has full visibility of
  your entire transaction graph (other than newly-detected spends, which suffer
  from the earlier issue).

- We were concerned about the resistance of the algorithm used to derive wallet
  encryption keys (inherited from [Bitcoin](https://bitcoin.org/en/secure-your-wallet))
  to dictionary attacks by a powerful attacker. If and when we re-enable wallet 
  encryption, it is likely to be with a modern passphrase-based key derivation 
  algorithm designed for greater resistance to dictionary attack, such as Argon2i.

You should use full-disk encryption (or encryption of your home directory) to
protect your wallet at rest, and should assume (even unprivileged) users who are
runnng on your OS can read your wallet.dat file.

REST Interface
--------------

The REST interface is a feature inherited from upstream Bitcoin.  By default,
it is disabled. We do not recommend you enable it until it has undergone a
security review.

RPC Interface
---------------

Users should choose a strong RPC password. If no RPC username and password are set, `zend` will not start and will print an error message with a suggestion for a strong random password. If the client knows the RPC password, they have at least full access to the node. In addition, certain RPC commands can be misused to overwrite files and/or take over the account that is running `zend`. (In the future we may restrict these commands, but full node access – including the ability to spend from and export keys held by the wallet – would still be possible unless wallet methods are disabled.)

Users should also refrain from changing the default setting that only allows RPC connections from localhost. Allowing connections from remote hosts would enable a MITM to execute arbitrary RPC commands, which could lead to compromise of the account running `zend` and loss of funds. For multi-user services that use one or more `zend` instances on the backend, the parameters passed in by users should be controlled to prevent confused-deputy attacks which could spend from any keys held by that `zend`.

Logging z_* RPC calls (deprecated)
---------------------

The option `-debug=zrpc` covers logging of the z_* calls.  This will reveal information about private notes which you might prefer not to disclose.  For example, when calling `z_sendmany` to create a shielded transaction, input notes are consumed and new output notes are created.

The option `-debug=zrpcunsafe` covers logging of sensitive information in z_* calls which you would only need for debugging and audit purposes.  For example, if you want to examine the memo field of a note being spent.

Private spending keys for z addresses are never logged.

Potentially-Missing Required Modifications
------------------------------------------

In addition to potential mistakes in the code we added to Zen, and
potential mistakes in our modifications to Zen, it is also possible
that there were potential changes we were supposed to make to Zen but
didn't, either because we didn't even consider making those changes, or we ran
out of time.
