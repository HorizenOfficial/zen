Changelog
=========

Alberto Sala (355):
      Removed visitor pattern from rpc cmd; WIP- regression ok
      Fixed remaining items in the backlog of code review
      Modifications after albene codereview
      WIP: Fixes for issue #36
      getrawtransaction and decoderawtransaction can handle both tx and cert; added py test for it
      Avoid adding empty objs in result of getscinfo when filtering sc
      Added verbose optional parameter to getscinfo
      Added support for from/to to the getscinfo cmd; also result format changed according to issue44 requirement
      Updated all py tests for supporting the getscinfo rpc cmd changes
      Added py test for getscinfo rpc cmd changes
      Added info on minimum value for withdrawalepoch length in rpc cmds for creating a sidechain
      Changed default value for upper limit in the range of getscinfo from 50 to -1
      Fixed typo in param name
      Minor include cleanup
      WIP: modifications to py test framework for using websockets
      Restore sc_cert_base.py to latest version; clean up ws_messages.py
      Added regtest only option for setting halving period; added py test for halving height
      WIP:Replay protection fixes
      Added gtest for new rpfixfork in forkmanager
      Added py test for tx_null_replay with different msg sizes pre-post rp fix fork
      Added some ut for checkblockatheight scripts
      First implementation for verifying cbh script before cpu intensive sign verification
      Added py test for replay protection fixes
      Moved code for checking rp data from tx checker obj into a suited function commonly used; removed noisy traces
      Minor changes to py test
      Fixes and comments after albene code review
      Removed status handling from rp attribute class and modified checker function
      Removed redundant check for rp attributes
      Added bool data member in rpAttributes to be set when cbh opcode is found in a script
      Restored exact comparison against sizeof uint256 for hash in Solver; renamed test and fixed type in func name
      WIP: preliminary modifications and traces, to be continued
      Prevented TLS1.0/1.1 and added ciphers for PFS support in TLS 1.2; Moved net cleanup func from global obj dtor to static func; - Handled to error in TLS separatedly from others in order not to use non-TLS connection if this happens; Used non deprecated api in creating ssl ctx; added fix for Secure Client-Initiated Renegotiation DoS threat; added py test; added more traces and a new 'tls' category
      Non-tls compatibility achieved via a build defined macro, is now set via -tlsfallbacknontls zend option
      tls py test: using ciphers supported by several openssl versions for portability
      Modifications after code review
      Modifications after code review
      Removed useless option setting
      Modification to the list of cipher to support in TLS1.2
      Fixed typo on tlsprotocols.py test
      Removed #if0 block
      Fix wrong bool condition assignment
      Removed unused var in GetChance() func
      Absorbing pr https://github.com/HorizenOfficial/zen/pull/321 with minor change
      Fix for gtest/bitcoin ut
      Added checks for minimal encoding height in rp
      Added UTs for rp data encodings
      Further modif and UT tests  for minimal encoding
      Modified check of minimal push using the same code of interpreter.cpp
      Added test of tx with non minimal encoding in py script
      Added regtest only option for setting halving period; added py test for halving height
      Fix for gtest/bitcoin ut
      Fix for script_test.py
      Changes after code review
      Merge branch 'speedup_SelectCoins' into merge_prs; Help msg cleanup for getblock rpc cmd
      WIP: handling of certificate quality
      Regenerated regtest addresses for communitis subsidy coinbase quotas and added relevant privkey in comments
      Added quality attribute in scinfo and undo data structure; updateed handling of certs with different quality in mempool
      Added removal of lower quality certs from mempool; first version for handling multiple certs in blockchain
      Preliminary fix in mempool for the case of cert2 depending on cert1 with q2==q1 and fee2>fee1
      fix in mempool for the case of cert2 depending on cert1 with q2==q1 and fee2>fee1; now cert2 is refused and cert1 is kept instead
      Handled the case of cert2 depending on cert1 with q1>q2 in mempool
      Fix for removing conflicted certs based on quality comparison
      Added first implementation for multiple certs quality in blocks and undo struct; change internal data in mempool for container of certificates
      improved code for multiple certs quality in blocks and interaction with mempool; added some fix from albene first prelimnary code review
      Modifications after final code review
      Fixed fee value in py script
      Fixed fee value in py script
      Added lock for new mempool funcs
      Fixed typo in deque handling for mempool search algo
      Fixes for UT that were failing with assert condition
      Added ordering by quality for certs when mining; added check for cert ordering in checkblock; fix for py test
      Fix for removeOutOfEpochCertificates
      Added handling of quality also in wallet balance
      Modification in syn voiding logic for wallet
      Added init value for topCommittedCertBwtAmount
      Removed wrong asser() check for removal of conflicting cert; modified mempool quality py testcase
      Modifications after code review
      Added unconfirmed data info, if any, to the RPC getscinfo cmd
      py test clean up
      Modifaications after code review
      Fixed ceasing sc height limit
      Modified unit tests; chenged check in concerned rpc cmds
      Removed check on ceased sc in isCertApplicableToState(); modified isCeasingAtHeight(); added new py test
      WIP:Replay protection fixes
      Added gtest for new rpfixfork in forkmanager
      Added py test for tx_null_replay with different msg sizes pre-post rp fix fork
      Added some ut for checkblockatheight scripts
      First implementation for verifying cbh script before cpu intensive sign verification
      Added py test for replay protection fixes
      Moved code for checking rp data from tx checker obj into a suited function commonly used; removed noisy traces
      Minor changes to py test
      Fixes and comments after albene code review
      Removed status handling from rp attribute class and modified checker function
      Removed redundant check for rp attributes
      Added bool data member in rpAttributes to be set when cbh opcode is found in a script
      Restored exact comparison against sizeof uint256 for hash in Solver; renamed test and fixed type in func name
      WIP: preliminary modifications and traces, to be continued
      Prevented TLS1.0/1.1 and added ciphers for PFS support in TLS 1.2; Moved net cleanup func from global obj dtor to static func; - Handled to error in TLS separatedly from others in order not to use non-TLS connection if this happens; Used non deprecated api in creating ssl ctx; added fix for Secure Client-Initiated Renegotiation DoS threat; added py test; added more traces and a new 'tls' category
      Non-tls compatibility achieved via a build defined macro, is now set via -tlsfallbacknontls zend option
      tls py test: using ciphers supported by several openssl versions for portability
      Modifications after code review
      Modifications after code review
      Removed useless option setting
      Modification to the list of cipher to support in TLS1.2
      Fixed typo on tlsprotocols.py test
      Removed #if0 block
      Fix wrong bool condition assignment
      Removed unused var in GetChance() func
      Absorbing pr https://github.com/HorizenOfficial/zen/pull/321 with minor change
      Added checks for minimal encoding height in rp
      Added UTs for rp data encodings
      Further modif and UT tests  for minimal encoding
      Modified check of minimal push using the same code of interpreter.cpp
      Added test of tx with non minimal encoding in py script
      Added api for getting view sc state
      Used recently added api for getting view sc state in place of the old one
      Minor modifications after code review
      Modifications after albene code review
      fix for build after merge
      WIP: new and modified rpc cmds for handling of bwt requests
      WIP: other modifications; full reg not yet run
      Added py test for bwt requests
      Added some description to the py test
      Removed unused code
      First pass after albene code review; one py test not working yet
      Fix for sc cert raw py test
      Modified fundrawtransaction cmd in order to support Ceased Sc withdrawal req in tx
      Fix for getting correct sc state; removed list of ptr when doing a remove call in mempool
      Added comment for deprecation of isCeasedAtHeight method
      Fixes for build after rebase
      Commented out sc-commitment-tree value check in py test
      WIP: new parameter for scCreationOutput
      Added vec of cfg for custom cert data in the set of sc creation cmd; added initial version of py test
      WIP: Added dummy classes for cert custom field implementations
      Added custom fileds for cert in interpreter sign serializer
      Inititial implementation for data members of newly introduced classes
      Minor fix after rebase; added first methods skeleton for custom data check
      WIP: added raw cmd options for creating custom fields in cert
      Added modification to high level rpc cmd for sending cert
      Splitted sc types h into header and impl; adapted ws impl for sending certificate with new fields
      More step added into py test, still some one to add
      Added check of cert custom field validity; fix for rpc cmd
      completed impl of py test for custom cert fields; minor fixes in src code
      Fixed wrong format specifier in macro for logging
      Modified py test for testing csw and nullifier handling
      Added rpc method for getting cert hash data
      Added py test for the removal of sc txes from mempool on reverting a sc ceasing evt
      Added py test for the removal of sc txes from mempool on reverting a sc ceasing evt (double committed in branch mbtr_introduction)
      Fix for getcertdatahash rpc cmd; modifications for sc_cert_ceasing_split py test
      WIP: started branch for cumulative committment tree hash; still not fully linking
      Fix for linking step
      Added check for block version when computing cum hash
      Changed AcceptToMempool flags from bool to enum class type; some UT testing AcceptCertToMemPool has been switched off until a fix is imported
      Added cumulative sc commitment tree hash to output of getscgenesisinfo; added field element type and a method for computing poseidon hash
      Modified code for accessing cumulative cmmitment tree hash data before verifying cert proof; also removed useless creation block hash from sidechain info db
      Introducing sched evt for mbtr
      Renamed rpc cmd for sendign mbtr; added unconf contribution in getscinfo for sc creation and mbtr
      Revert previous version of sc_rawcertificate.py file, erroneously pushed
      Fix for py tests which were referencing a no more existing sc attribute
      Fixed condition on version check for cblockindex serialization
      Removed dbg lines
      Added test in py for mbtr mempool removal
      Added py tests for checking mempool cleanup upon block connection and disconnction
      Restored the original order and change name to type defs in the signature of Accept*ToMemoryPool functions
      Added mock implementation to cert GetDataHash() in order to have real data; Fixed height param when calling removeStale method in case of connect block; fixes to py tests
      Fixed check of subs window when checking cert timing; fixed py test
      Modified some py test allowing the handling of a generic epoch length
      Modifications after Sasha code review
      Renamed vars in py test as in src code
      Added description in rpc cmd for cert custom fields cfg vectors in sc creation cmd
      Fixes for UT
      Fixed sc_csw_nullifier.py after branch merge
      Fixes to py test after merge; also fixed createrawtransaction and getactivecertdatahash rpc cmds
      Fix for UTs after merge
      Fixed chain height in failing UT
      Modifications after code review
      Fixed failing UT; last modifications for code review
      Fixes after latest merge
      Fixed formatting in log
      Fixed GetDataHash; adapted remaining UT
      Removed raw_input line from py test
      Added accessor method for field_t ptr to field element class
      WIP: first integration stages with a zendoo-mc-cryptolib based on ginger_algebra_ocl
      WIP: first working version with UT for comp/uncomp bit vector rust apis
      Fixed py test; added valid field element returned for BitVectorCertificateField::GetFieldElement(); renamed cust field tags in createrawcertificate rpc cmd
      Added call to bit vector merkle tree api in UT
      Added negative test for bit vector merkle tree UT
      Adapted bit vector UTs to the new crypto lib interface
      Added UT for merkle tree root computation with precomputed in/out data
      Added first UT for cctp commitment tree, namely add_scc
      Modifications after code review
      Added get committment to cctp UT
      Added accessor method for internal buffer data for CZendooCctpObject types
      Added add_cert call and some negative UTs
      Added implementation to committment builder obj; added UT
      Temporary fix for rendering scid compatible with tweedle field element; fixed some UT; improved modularity of commitment builder obj
      Added checker method for verifying alignemnt between rust lib abd c header file; added some more UT
      Added one more case in a negative UT
      Removed wrongly left testing code which prevents zend startup
      WIP: added endEpochCumScTxCommTreeRoot attribute for certificate; added method in coins db for checking its consistency; moved proof verification in a suited coinss db method
      WIP: modified rpc commands, py test still to be done
      Added error codes and their handling in check methods; Modified concerned UT and py tests in order to support new implementation
      Added first mplementation for the handling of active cert data in CSW input
      Added signature for new attributes of CSW and updated related bitcoin test; modified UT/py test and added new ones
      Added handling of cum comm tree root to ws cert cmd
      Missing indentation in py util
      Added method for getting cumulative ScTXCommTree hashes used in csw proof
      Fixed some build error in the preliminary modifications
      Changed interface of IsScTxApplicableToState() for returning a code instead of a bool
      Using cfg.getBitVectorSizeBits() instead of const value
      Modifications after albene code review
      Fixed bitcoin sighash tests
      Included 0 as a valid rand value for some optional vectors in bitcoin sighash test
      Fixed err msg in py test
      Fixed py test after merge: full reg now is ok
      Removed useless #if 1/#endif
      Added missing cert members in CTransactionSignatureSerializer; fixed bitcoin test sighash
      Fixed UT after merge with branch proof_sys_integration_base
      Fixed build
      Modifications after Sasha review
      Restored missing key/value pair in rpc misc cmd result
      Added a check that prevents signing a tx with SIGHASH_ANYONECANPAY set if there is a csw input
      Restored extra check on nInputs including also potential use of fAnyoneCanPay with csw
      Reverted part of the implementation related to mempool batch verification
      Removed gtest impl not used anymore
      Removed unused code and refactored part of the calls for verifying proofs
      Fix to issue #112
      Added ceasing cum comm tree to csw and actCertData moved in the csw input
      Minor changes from code review
      First fixes to py test and src after merge
      Fixes from Sasha code review; further fixes for regression
      Added to Is<Tx/Cert>ApplicableToState() an optional bool ptr indicating to the caller if the sender should be banned; fix to sighash UT
      Fixed rawtransactions.py
      implementation of multiple proving sys type rebased on sidechains_dev
      Modifications after code review
      fixed wrong if/else when processing incoming tx msg
      WIP: modifications to ScTxCommTree cctp apis and first pass on proving cctp apis
      WIP: Added call to cctp/mccryptolib proving sys
      After having added all inputs to be verified added missing call to mc crypto lib API batch_verify_all()
      Fixed constant expressing max size in bits of bit vector
      Added sample cctp vector for UT; first fixes for failing UTs
      Modification for pointing to mc crypto lib branch sync_with_cctp_lib_2 commit 5c39055d
      Fix toooooooooo UT test for recent mc crypto modifications
      Modified generation of sc id adding serialization of field element; added UT
      Fix for using valid ptrs from cctp obj; added debug funcs for buffers with size
      Removed endEpochBlockHash data since it is not used anymore in the proving sys; set null buffer for optional data in cctp api when not populated
      Fixed wrapper ptr scope when calling add_scc cctp api
      WIP: set segment size 1 << 9, to be fix; first modifications for py tests with prov sys
      WIP: Modification to csw async proof py test
      fix for queue of csw proof inputs; further modifications to py test
      Fix to several py test; UT proving sys test vector updated
      Fix to assigmnt op in custom field obj: now using accessor method for getting fe obj when needed; fix to related py test adding valid bit vector elements
      added sample vk test vectors to UT catalogue; fixed buffer in dump utility; fixed sc_csw_nullifier.py test
      Added missing contribution of bit vector custom fields to the cert commitment tree calculation
      Added dbg func for bt and bt_list objs to bw passed along the mc crypto lib if
      Added handling of optional parameter in create raw tx with csws having null cert data hash; added py test
      Restored creation of tmp path in UT testmanager
      Ported fix related to p2p mempool msg from PR107 into sidechains_integration_step4 branch; a variant of the UT logic with lower impact on code base has been imported as well
      Fixes for errors in windows build
      Workaround for having correct fee rate in case of csw with fundrawtransaction; this will be superseeded with the fix for issue #95
      Fix for misalignemnt between cert custom fields and their cfg in rpc cmd send_certificate
      Added py test for mining a block with a configurable number of cert/csw proofs; this test is not included in the regression list since can be very time consuming
      Added traces for checking BatchVerify execution time
      Fix after Paolo's code review
      Modification to sc_big_block.py for reusing the same cert proof for all SCs and using a higher complexity for proof generation
      Modifications and fix after merge with sidechains_integration_step4; first round - build ok
      Modifications and fix after merge with sidechains_integration_step4; second round, modifications for UT and py test regression tests OK
      Removed duplicate code after merge
      Modifications after cronicc code review
      Fix and py test for issue #95: added csw input to CreateTransaction wallet function in order to handle properly csw in the fundrawtransaction cmd
      Modification after albertog78 code review
      Fix after Sasha code review
      Added some more tunable parameter in sc_big_block.py test
      Adapted sc_big_block-py to new sc fork height
      Sync with mc_cryptolib: updated csw and cert circuits
      Optimization: Vk and Proof cctp objs now call deserialization api with semantic checks enabled only the very first time; sync with mc crypto lob supporting this calls
      Moved commitment tree computation and proof batch verification calls in ConnectBlock() after end of input checks; fix and modifications of bench tracing lines
      Fixed bench trace; noisy traces commented out
      Added zend options for tuning size and delay params for proof batch verification queue
      Added -scproofqueuesize=0 option for sc bench py test speedup
      Modification after Paolo's code review
      Changed size limit for cert/csw vks and proofs according to issue #123; aligned with mc-crypto-lib on current head of branch priority_verification
      Added wrapped ptr as data member in classes derived from cctp obj, using it for calling deserialization mc crypto api just one time
      aligned with mc-crypto-lib on current head of branch priority_verification
      Aligned py test framework constants for cert/csw vks and proofs size with definitions of sidechaintypes.h
      Added option for having a 0 minimum queue size for sc batch proofs in all concerned py tests, this is for execution time speedup; modifications to getblocktemplate_proposals.py for testing separatedly both invalid commitment tree fe and a wrong one
      Set scproofqueuesize=10 in sc_async_proof_verifier.py
      Fix compilation issue after merge
      Modified qa/rpc-tests/sc_csw_fundrawtransaction.py after merge
      aligned with mc-crypto-lib on current head of branch cert_sc_id
      Updated mc crypto lib vk test vectors for UT usage; added a test for verifying all test vectors
      Updated UT for TreeCommitmentCalculation
      Fix after regression tests; added sc_big_block.py, tuned for quick execution, to the list of py test
      Removed unused code and added return code in log for mc crypto lib api call in GetDataHash() method
      Added a bool argument in CBlockUndo ctor for avoiding modifying db before sc fork height
      Added check of wallet ptr before using it, that prevents unwanted dereferencing when using -disablewallet option in mining
      Added EOL in some missing LogPrint
      Fix after build; WIP
      miner sets hashScTxsCommitment field in block (formerly hashReserved) only if it supports sidechains (issue #135)
      Added test coverage in UT for fork8 sidechain fork (issue #127)
      Added a section in py test for testing that hashReserved block field can also have non null contents in the last block before sidechains fork (issue #138)
      Fix after build; build OK
      Fix for failing UT
      Modification to run_sc_tests.sh after cronic comment; also added stdbuf for having unbuffered output on console
      Using an enum instead of a bool in CBlockUndo ctor
      Modification after Sasha code review
      Increased block size up to 4M; txes are allowed to occupy only a subset (default half) of the block size while this limit does not apply to certs
      Using block priority size for both tx and certs in miner logic
      Modifications after code review and some preliminary test with block and cert size
      Added py test for checking block tx partition handling
      Fix to py mc_test for the correct rounding of amounts in proof creation; added a py test
      Adding an overhead large enough to the max size of compressed bit vector for the case when compressed data are larger than original ones; added UT for bzip2 algo
      Fix to mingw configuration to overcome an unresolved symbol in build for windows (issue #152)
      Fix to py test after merges
      Added a RAII guard for pausing rust low prio threads when connecting a block
      Modification after code review
      removed unused var after rebase
      Fix path for test data file in py test
      Fixed sc_block_partitions.py test after merge
      Fix to py test after merges
      Fix path for test data file in py test
      Added py test for reproducing the behaviour
      WIP: added a constant in chain params for getting the number of recent SC active data certificates whose scFee values are to be include in a list for checking ft/mbtr sc fees and deciding if a tx should be removed from mempool when a certificate is included in a block just connected; the check is done agaist the minimum scFee in such list
      Added handling of mbtr scfee; modified check in ConnectBlock call
      Added enum class for telling which type of check must be applied for ft and mbtr sc fees in IsScTxApplicableToState
      Added sc fees list to getscinfo rpc cmd output; modified py test and added to list of regression py sc_stale_ft_and_mbtr.pysc_stale_ft_and_mbtr.pysc_stale_ft_and_mbtr.pysc_stale_ft_and_mbtr.pysc_stale_ft_and_mbtr.pytests
      First set of modification after Paolo codereview; fix for py test; moved scFee list in a better section in undo structure
      Added a check than no more than a fixed number of csw input can be accepted in the mempool for a SC
      Added py test and modified a failing one; fixed mempool::check()
      Modifications after Paolo's code review
      Modifications after code review
      Modifications to py tests as per code review
      Import cleanup in a py test
      Changed segment size from 2^17 to 2^18
      backport of zen pr #379
      Added optional parameter 'constant' field element to the generation and verification of a CSW proof. mc crypto lib and cctp lib have also been changed accordingly
      Added cert support for rpc cmd gettxoutproof; also extended relevant py test
      Added comment to src code related to rpc cmd gettxoutsetinfo; also fiexd typo (duplicated code lone)
      Added some code in py tests for checking listaccounts and listaddressgroupings rpc cmd behaviour with sc related amounts
      Fix for handling of immature bwt in getreceivedbyaccount rpc cmd; Added some code in py tests for checking lockunspent, listlockunspent and getreceivedbyaccount
      Modifications after code review
      Added some code line in py test for prioritisetransaction rpc cmd in case of cert in mempool
      Added handling of sc data in export/import wallet rpc command; also extended relevant py test
      Removed fix for export/import wallet rpc cmd; a different fix should be devised reconstructing sc cert data in CWallet::ScanForWalletTransactions() from blocks rescanning
      WIP: fix for CWallet::ScanForWalletTransactions  for export/import wallet rpc cmd
      Fix for py test after merge with PR #149 which removed certificates max priority
      fix for CWallet::ScanForWalletTransactions  for export/import wallet rpc cmd
      Modified help for getmininginfo cmd; removed info on used size of tx partition
      Modified json output of getrawtransaction rpc cmd and relevant py tests
      Fix to py test
      Updated and added 2 py tests into the rpc list
      Updated and added 2 py tests into the rpc list
      Added an upper limit to the sidechain withdrawal epoch length value
      Added check for epoch length also in other rpc cmds for sc creation; added negative cases in py tests
      Fix py tests after rebase

DanieleDiBenedetto (28):
      Sync with zendoo-mc-cryptolib updatable_poseidon branch
      Updated zendoo tests
      Updated to last zendoo-mc-cryptolib + removed EmptyField class
      Updated libzendoo and mcTestCall.cpp; started updating python MCTF
      Begin updating python tests
      Fixes
      New zendoo_mc_cryptolib version + updated accordingly segment size and existing functions + sc_cert_base python test now passing
      New MC crypto version: added nullifier to CSWProofVerifierInputs + reduced proof size
      Fixed some GTests
      Fixed more GTests and python tests
      Update libzendoo.mk
      Fix error in fee conversion in mc_test.py
      Updated more tests
      Fixed tests + changed advance_epoch
      Add missing tests to rpc-tests.sh
      Updated zendoo-mc-cryptolib + fixed couple of python tests
      Changed mc-cryptolib + removed paths from init_dlog_keys + make cert_data_hash optional in py tests
      WIP: Sync with mc-cryptolib: failing proofs ids + batch verification with priority + segment size and num constraints to pass to test functions
      Sync with mc-cryptolib: added sc_id to cert prover/verifier/hash
      Sync with mc_cryptolib: updated csw and cert circuits + minor update to sc_big_block test
      Updated outdated or vulnerable dependencies
      Update libzendoo.mk
      Updated mc-cryptolib dependency, test data and test code
      Update libzendoo.mk
      Added PROOF_PLUS_VK_SIZE constant and check it for certs and csws. Updated pre-existing constants. Partially fixed deserialization bug (must be addressed mc-cryptolib side too)
      Libzendoo update completing the bug fix on proof/vk isValid()
      Updated constants in mc_test.py
      Removed call to GetCommitment() inside LogPrint (performed each time add() is called)

Jack Grigg (10):
      Add test vectors for small-order Ed25519 pubkeys
      Patch libsodium 1.0.15 pubkey validation onto 1.0.18
      Patch libsodium 1.0.15 signature validation onto 1.0.18
      test: Minor tweaks to comments in LibsodiumPubkeyValidation
      depends: Remove comments from libsodium signature validation patch
      Add test vectors for small-order Ed25519 pubkeys
      Patch libsodium 1.0.15 pubkey validation onto 1.0.18
      Patch libsodium 1.0.15 signature validation onto 1.0.18
      test: Minor tweaks to comments in LibsodiumPubkeyValidation
      depends: Remove comments from libsodium signature validation patch

Marco (3):
      Added version in gettransaction and CTP fields only if it's a sidechain transaction
      Try to add AddJoinSplit to Certificate class
      Updadted gettransaction help

MarcoOl94 (17):
      Porting of getblock improvement
      Fixed getblock help
      Porting getblock verbose improvement
      Modified help of getblock
      Porting getblock verbose improvement
      Modified help of getblock
      Added new RPC command that calculate MerkleTree and ScTxsCommitment based on a list of transactions and certificates
      Renamed getSidechainMerkleRoots RPC method into getBlockMerkleRoots
      Fix after code review
      Changes after intermediate code review
      Added new RPC command that calculate MerkleTree and ScTxsCommitment based on a list of transactions and certificates
      Renamed getSidechainMerkleRoots RPC method into getBlockMerkleRoots
      Changes after rebase
      Added python test for getblockmerkleroots RPC command
      Fixed formatting
      Changes on getblockmerkleroots.py after intermediate code review
      Added check for sidechain fork height in getblockmerkleroots

Oleksandr Iozhytsia (34):
      CTxCeasedSidechainWithdrawalInput added. CTransaction structure updated. RPC methods changed.
      ScFieldElement usage fix.
      CTransaction CSW inputs signing and checking implemented. signrawtransaction RPC updated.
      rawtransaction RPC fix. CheckLockTime fix.
      test_bitcoin tests extended with CSW input cases.
      gtest unit tests extended with CSW input cases.
      1. rawtransactions Python test updated: Tx with CSW input signing and mempool submission case added. 2. CSW inputs balance check added.
      Websocket: responses write operation was optimized to avoid redundant thread sleep in case of empty queue.
      unconfirmed_balance.py test updated.
      ForwardTransfer: mcReturnAddress field added.
      mc-cryptolib dependencies updated.
      ScProofVerifier threads priority fix.
      Python tests updated.
      CZendooLowPrioThreadGuard class restored.
      ProofVerifier low priority threads tests added.
      Python tests update.
      Issue 177 related fixes.
      RPC commands decription update.
      ProofVerifier fix on failure
      FT: sc_sendmany fix for mcReturnAddress setup.
      WalletTx: GetAmounts() changed to aggregate all sc related outputs in a single entry.
      Python tests interfaces fixes after merge.
      getrawmempool RPC command updated.
      Python tests optimization to exclude random CI failures.
      RPC commands response fields updated: getrawtransaction/cert, decoderawtransaction/cert
      gettxout RPC command improved.
      Minor fix.
      RPC minor fix.
      Crosschain outputs: pubkeyhash is no longer used. taddr ussage added for all RPC commands. JSON view modified. Tests updated.
      Websocket performance assert replaced with notification to avoid random failures for weak machines.
      Minor renaming.
      Crosschain outputs: pubkeyhash is no longer used. taddr ussage added for all RPC commands. JSON view modified. Tests updated.
      Websocket performance assert replaced with notification to avoid random failures for weak machines.
      Minor renaming.

Paolo (1):
      Apply suggestions from code review

Paolo Tagliaferri (92):
      Improved the processing of blocks when zk-SNARKs verification is not needed (e.g. when using checkpoints).
      Fixed the creation of new blk??????.dat files.
      Removed a minor unit test that could not be compiled for Windows target.
      Added the "forwardTransferScFee" and "mainchainBackwardTransferRequestScFee" data members to the certificate classes (both mutable and not mutable). Added a unit test to check that the new members affect the computation of the certificate data hash. Fixed some unit tests that stopped working after the changes.
      Refactoring of the CSidechain class: removed "lastTopQualityCertDataHash" and "pastEpochTopQualityCertDataHash", now this value is included inside the struct CScCertificateView (which additionally stores the Forward Transfer Sidechain Fee and the Mainchain Backward Transfer Request Sidechain Fee).
      Added the data member "mainchainBackwardTransferRequestDataLength" to the CSidechain class.
      Added the following members to the CTxScCreationOut class:
      Changed the scRequestData member from being a single CFieldElement to a vector (in CBwtRequestOut).
      Updated the UpdateSidechain function in order to take into account the new parameters (fees and data length). Added a unit test to check that the sidechain creation correctly sets the new parameters.
      Added a unit test to check that a new certificate updates the sidechain parameters (FT and MBTR fees).
      Added some unit tests to check that the semantic validation of sidechain certificates takes into account the new parameters (FT and MBTR fees).
      Added the following validations to the IsScTxApplicableToState function:
      Added some unit tests to check that forward transfers and mainchain backward transfer requests are accepted/rejected from mempool based on sidechain configuration (FT fee, MBTR fee and MBTR data length).
      Updated the function removeStaleTransactions().
      Removed the usage/storing of the sidechain certificate data hash in memory pool.
      Updated the following RPC commands to include FT fee, MBTR fee and MBTR data length:
      Fixed regression tests and RPC commands.
      Changes after comments on pull request #105.
      Changes after comments on pull request #105 (second round).
      Minor changes after pull request #105.
      Fixed compilation errors.
      Fixed compilation errors and Gtests.
      Fixed sc_* integration tests
      Added some early checks to avoid to submit a MBTR with empty reqData.
      Added some early checks to avoid to submit a MBTR with empty reqData.
      Moved proof verifier input structs
      Added transaction and certificate to proof verifier inputs
      Sync batch verification
      Mempool proof verification flag
      ConnectBlock() signature refactoring
      ConnectBlock() proof verification flag
      Proof verifier data load improvement
      BatchVerificationStateFlag for ProcessTxBaseMsg()
      Async proof verifier
      AcceptToMemoryPool async refactoring
      Pass CNode to CScAsyncProofVerifier
      Removed unused code
      Fixed moveMap
      Enabled the async proof verifier
      Handle async proof verification failure
      Enabled "cert" debug prints on rawtransactions.py.
      Removed duplications of class CNakedCCoinsViewCache.
      CScAsyncProofVerifier unit tests
      Implemented regression tests for the CScAsyncProofVerifier
      Added the initialization of DLog keys at startup
      Unit test for test proof verification
      Test proof generation and verification
      Fixed unit tests that were failing after the proof verifier integration.
      Added nullifier to CCswProofVerifierInput
      Python tests: fixed rawtransactions.py
      Fix after code review
      Fix after code review
      Fixed some Python tests.
      Fixed the Python test of the async proof verifier.
      Fix after code review.
      Fixed an occasional error with the test of the async proof verifier.
      Fixed the ban of nodes sending an invalid proof.
      Added a Python test for CSW management in mempool.
      Updated the usage of the batch proof verifier.
      Updated the MC Crypt Lib (improved read/write of data from file).
      Fixed typo.
      Fixed some error debug strings.
      Added the sidechain ID to the certificate proof generation.
      Unique ID for proofs.
      Removed duplicate code.
      Refactoring
      Fixed the behavior of the AsyncProofVerifier.
      New AsyncProofVerifier behavior.
      Merged the two proof verifier queues in a single one.
      Removed the ProofVerifierOutput structure.
      Minor changes
      Changes following code review
      Using a boost::variant in CProofVerifierItem
      [Python test] Check that sidechain balance is updated by mempool
      Added a Python test to check the double spending of nested transactions
      Additional check for the BitVector size
      Minor changes
      Added a test about handling huge bit vectors
      Added the sc_id when generating certificate proofs
      Fixed an issue with the management of certificates in GetBlockTemplate
      Added debug log when using GetBlockTemplate RPC call
      Added sidechain JSON examples to the doc folder
      Removed the duplicated 'scTxsCommitment' field in 'getblock' response
      Added "sc_cert_getblocktemplate.py" to regtest script
      Renaming of RPC commands for sidechain creation
      Renaming of RPC commands for sidechain forward transfers
      Renaming of RPC commands for sidechain backward transfer requests
      Renaming of RPC commands for sending sidechain certificates
      Renaming of RPC JSON fields
      Fixed the name of sc_send_certificate in a Python comment
      Python test fixes for testnet staging
      Fixed compilation error

Phoinic (6):
      Library versions and rust build system adjustments
      Broken tests commented
      Rust build updated to the actual libraries state
      Library versions adjustments
      Build system bugfix
      error.h includes removed

PowerVANO (3):
      Added setup_zend.bat and fetch-params.ps1
      Added setup_zend.bat and fetch-params.ps1
      Added setup_zend.bat and fetch-params.ps1

Taylor Hornby (2):
      Avoid names starting with __.
      Avoid names starting with __.

VolodymyrBabaiev (84):
      Added get block headers request to websocket interface.
      Added python test for Get headers websocket request.
      Added headers assertion. Added tip comment for future merge.
      Added python tests for get new block hashes and multiple block hashes requests.
      Getting header optimization.
      Added preliminary python test for certificate quality.
      Fixed bugs in quality python tests.
      Added python test for cert quality in chain of blocks.
      Updated python test for certificate quality in mempool.
      Added block reversal test for cert quality.
      Fixed mistake in comment.
      Added chain reorganization test for certificate quality.
      Added python test for voiding.
      Refactoring. Removed unused code, fixed comments.
      Refactored python tests for certificate quality.
      Updated input and output for raw sending certs in quality python tests.
      Added quality-epoch mistype python test in quality_blockchain.
      Added voiding check for certificates with lower quality.
      Removed redundant assertion.
      Added prefic sc_ to python quality tests.
      Removed debug print and unused variables from quality wallet python test.
      Added testcase with substitution of certificate which another certificate depens on.
      Added processing of CertificateHashData.
      Removed obsolete commented code.
      Added nullifiers for Ceased Sidechain Withdrawal.
      Fixed CertDataHash issues.
      Fixed nullifiers implementation due to pull request issues.
      Moved undo cert data hashes to BlockUbdo structure. Fixed bug in Nullifiers and DataHash map hash function.
      Removed obsolete code. Fixed checkcswnullifier function.
      Fixed Log output in ConnectBlock. Fixed checknullifier rpc function.
      Fixed pull request issues for nullifiers.
      Fixed pull request issues for CertDataHash.
      Moved CertDataHash undo data back to CSidechainUndoData.
      Added nullifier checking in checkcswnullifier rpc command. Moved cert data hash calculation function to proofverifier.
      Fixed updating CertDataHash in CCoinCache,
      Fixed gtests for CertDataHash handling.
      Added python test for CSW nullifiers usage.
      Added python test for CSW nullifiers usage.
      Added serialization for BlockScTxCumHash.
      Fixed prevScCumTreeHash for the first block with SC support.
      Added UT for Block Sidechain Transaction Commitment Tree Cumulative Hash.
      Fixed epoch height for Cumulative Commitment Hash.
      Added EndHeightForEpoch function. Updated unit tests.
      Removed redundunt condition.
      Changed output sequence of getscgenesisinfo command.
      Updated TODO comments.
      Updated python packages installing command with websocket support.
      Added get top quality certificates websocket request. Fixed send_certificate command.
      Fixed sending raw certificate in getTopQualityCertificates.
      Fixed websocket send certificate command. Updated unit tests.
      Fixed MBTR and FT fees. Added cumulative commitment tree hash instead of end epoch block hash.
      Fixed bug.
      Fixed issues from certificate submitter pull request.
      Fixed fee representation in topQualityCertificates websocket request.
      Fixed json field name.
      Fixed issues from pull request review.
      Fixed issues after code review. Added epoch to getTopQualityCertificates websocket request.
      Set constant as optional in test proof generation call.
      Update python calls for proof generation with optional constant.
      Added missed header inclusion.
      Fixed merge issues.
      Fixed advance_epoch in python tests.
      Fixed python tset bugs.
      Updated mc-cryptolib dependency and test_proof_generation.
      Updated proof generation function description.
      Updated mc-cryptolib dependency. Fixes after code review.
      Removed obsolete comment.
      Fixed comment.
      Added test for checking out immature balance.
      Adde listsinceblock check.
      Added listtxesbyaddress check.
      Add to sendrawtransaction certificate support.
      Removed sendrawcertificate rpc call. All sendrawcertificate rpc calls replaced with sendrawtransaction.
      Merged signrawtransaction with signrawcertificate. Removed signrawcertificate.
      Fixes after the codereview.
      Changes after the codereview.
      Changes after the codereview.
      Fixes after the codereview.
      Fixes after the codereview.
      Changes after the codereview.
      Changes after the codereview.
      Code refactoring.
      Remove redundant code.
      Changes adter the codereview.

abi87 (329):
      Added UTs for GetScInfo with scCreation in mempool
      Optimized getScIds on levelDb
      Minor improvement following code review
      Fixup mac compilation
      Fixed up getscinfo for ceased sidechains
      Added catchall scId and onlyAlive parameters on getScInfo
      Fixed sidechain gtest on windows
      fixed UTs independence cleaning up global chainActive
      Minor fixes following clang compilation
      Cleanup headers and forward declarations
      Added py test description
      First changes following final code review
      Renamed enum to allow for windows compilation
      Fixed socket fd check for portability
      Removed pass-by-value
      Fixes following code review
      Minor renaming before -reindex-fast implementation
      Added UT file for reindex
      Added UTs for LoadBlocksFromExternalFile
      Minor refactoring for readability
      reindexU UTs: added UT to store and retrieve genesis to MapBlockIndex
      Refactored generation of equihash for blockHeaders
      Reindex UTs: tested case for multiple blocks per file + minor refactoring of CreateNewBlock
      Extended asserts in reindex UTs
      reindex: refactored blocks loading from LoadBlocksFromExternalFile
      Reindex: minor fix of function signature
      Reindex: minor fixes to LoadBlocksFromExternalFile
      Draft of introduction of header-first block load
      reindex: Added Uts for out-of-order blocks
      reindex: added UTs for duplicated blocks in files
      Minimal changes for reindex-fast: removed refactoring of blocks loading
      reindex: fixed block processing following headers one
      Added UT for headers and block back-to-back processing
      reindex-fast: added global option to cli
      Refactored certs spending dependencies checks
      Moved upward check on certs dependencies
      Fixed mistakes in cert quality conflicts handling refactoring
      Fixes following code review
      Added UTs for full ancestors and descendants tracing in mempool
      Fixed checks on minimal quality acceptable to mempool
      Restructuring CheckQuality and move it IsCertApplicableToState
      Minor formatting changes
      minor formatting changes
      Inception of UTs for UpdateScInfo for multiple certificates handling
      Added UTs for UpdateScInfo for multiple certificates
      Refactored UTs for readability
      Added UTs for CheckQuality
      Refactored RemoveAnyConflictingQualityCert
      Minor code formatting
      Minor refactoring of certificates ordering check in blocks
      Added UTs for certificates ordering in blocks
      Introduced function listing certs to void upon block connection
      minor refactoring
      Restructured LowQualityCertsUponConnectionOf to keep cert block ordering
      Refactored NullifyBackwardTransfers to remove code duplication
      augmented CSidechain with replacedLastCertBwtAmount attribute
      Removed GetTopQualityCert function
      Updated UTs following introduction of CSidechain::replacedLastCertBwtAmount
      augmented CSidechain with replacedLastCertBwtAmount attribute
      Removed GetTopQualityCert function
      Updated UTs following introduction of CSidechain::replacedLastCertBwtAmount
      Minor fix following cherry-picking
      Minr improvements of UTs
      Minr improvements of UTs
      Refactored UTs for clarity
      Added UTs for sidechain undo
      Fixed minor bug on revertCertOutput
      Fixed minor bug in RevertCertOutputs
      Extracted createCoinbase out of CreateNewBlock
      Inception of ConnectBlock UTs with multiple certs
      Added UTs for ConnectBlock with multiple certs
      Minor UT refactoring
      Avoid creating undos for low quality cert outputs
      Minor fix on sync with wallet
      Minor improvements on mempool remove
      Minor change to mempool remove
      Minor changes to block formation algo + UTs
      Fixed UTs for sidechain events
      Minor fixes following code review
      Minor code cleanup following code review
      Reduced flakyness of sc_quality_blockchain
      Fixed up wallet reconstruction
      Cleaned up UpdateScinfo and RevertCertOutputs signatures
      Refactored Schedule/Cancel sidechain events for idempotency + UTs
      Added new data struct for sidechains data undo. Moved there immature amounts and ceased certs
      Moved sidechain state and low quality bwts to new undo structure
      Extracted bwt restoration from sidechain state revert
      Changes following code review
      Further changes following code review
      Minor fix to unlock mac compilation
      Fixed compilation on gcc10
      Changes following final code review
      Cleanup headers and forward declarations
      Added py test description
      First changes following final code review
      Renamed enum to allow for windows compilation
      Fixed socket fd check for portability
      Removed pass-by-value
      Unlocked compilation by manually fixing leftover conflicts post zen/master cherry-picking
      Fixed UTs by manually fixing leftover conflicts post zen/master cherry-picking
      Fixed integration tests by manually fixing leftover conflicts post zen/master cherry-picking
      Further minor fixes, following final code review
      Cleanup wallet hierarchy
      Added SyncSinchain to wallet
      Fixed wallet rescanning upon startup
      Adding low quality cert handling in wallet rebuilding
      Skipping wallet sidechain processing for low quality certs
      Optimized wallet sidechain update for top quality certs only
      Forced rescanning upon py tests checking for wallet persistence
      Fixes following code review
      Changes following code review
      Revert linux compiler to gcc
      Fixed typos
      Declared voided all bwts of certs in mempool
      Added assert in bwtAreStripped for certs in mempool
      py test fix following certs bwts voiding in mempool
      Introducing CBwtRequestOut in transactions
      Introduced mbtr checks on target scIds + UTs
      Introduced mbtr checks on scFees
      Added checks on coinbase containing mbrt outputs
      Renamed HaveScDependencies to IsScTxApplicableToState
      Introduced mock verifier for mbtr proofs.
      Extended CcIsNull to account for vmbtr_out
      Added btr handling in mempool
      Added btr checks in mempool::check
      Added removal of unconfirmed btrs upon cert connection
      Added btrs dependency on scCreation in block formation
      Fixed faulty UT
      Ensure mbtr proof is verified for co-created sidechains
      Added UTs for mbtr in blocks formation
      Added UTs for mbtr in blocks connection
      Fixes following post-merge full regression
      UT fixes following address sanitizer checks
      Minor code rearrangement to unlock asan compilation
      Fixed locking to unlock UTs execution in --enable-debug mode
      Introduced optional verification key for mbtr
      UT fixes following address sanitizer checks
      Minor code rearrangement to unlock asan compilation
      Fixed locking to unlock UTs execution in --enable-debug mode
      Signing functions cleanup
      Added UTs for csw removal from mempool
      Added comments on csw priority contribution
      Fixes following internal code review
      Minor code cleanup
      Minor rpc cmd changes for mbtr
      Fixed CheckBlockIndex for regtest checks
      Removed deprecated isCeasedAtHeight method
      Intro of ad-hoc type for hashScTxsCommitment
      Moved scTxsCommitmentBuilder to separate file
      Moved fillCrosschainOutput to SidechainTxsCommitmentBuilder; extracted loop to add
      Minor removal of useless op
      added UT for scTxCommitmentTree
      zendoo lib gtests: minor cleanup and warning fixup
      First, dummy introduction of PoseidonHash in scTxCommitmentTree
      Replaced MAGIC_SC_STRING with zero field
      Introduced PoseidonHash for each, single scId compoments in scTxCommitmentTree
      Fully replaced ScTxCommitmentTx calculations with libzendoo utilities. Refactored common code
      Further common code refactoring
      Refactored empty field class and leavesToHeight formula; removed addCrosschainOutput
      Fixed no-leaves case
      Fixed field leak
      Reverted faulty leaves to treeHeight formula
      updated flaky pyTest following Poseidon Hash introduction
      Changed data mapping to field implementation
      Cleanup headers and forward declarations
      Removed files introduced by mistake in cherry-picking
      Fixed scTx processing following bug spotted by Sasha
      Introduced alternative mapping implementations
      Fixed logging for mapping to point fields
      Cleanup custom fields types
      Inception of UTs for custom fields
      Minor code refactoring for custom fields
      minor name refactoring
      Restructuring certDataHashes interface in CCoinsViews
      Minor fixes to get green CertHashData UTs
      Minor fixes to get green sidechain py tests
      Fixes following vova's code review
      Introduced check on missing certDataHash for given epoch
      Inception of csw nullifier refactoring
      Fixes following sasha's code review
      Minor code cleanup
      Reorganization of mempool cleanup methods
      Introduced check on csw nullifier removal
      Minor mempool methods cleanup
      Removed code duplication on BatchWrite for sidechain entries
      Final fixes: coinsViewCache fixes
      Minor code cleanup
      Introduction of GetActiveCertDataHash
      Renamed scInfo to sidechain
      Minor fixes to CSidechain serialization
      Updating CertHashData upon cert reception
      Minor blockUndo sections clenaup
      Dropped pastEpochTopQualityReferencedEpoch
      Minor re-naming and re-typing
      Partial backport of: Reorganization of mempool cleanup methods 7dbafd8f0
      Fixed mbtr cleanup in mempool
      Partial backport of Minor mempool methods cleanup 7d061a8af
      Partial backport of: Inception of csw nullifier refactoring ab1c56f757
      Partial backport of: Removed code duplication on BatchWrite for sidechain entries 436feb3e0
      Minor preparatory refactoring
      Refactoring to extract logic of block to overran chainActive tip
      Added extra fields in getchaintips
      Adding py tests for verbose getchaintips
      reintroduced trace as for code review indications
      Reverted unneeded changes to zen/delay.cpp
      Backport of scState introduction in CSidechain + UTs fixing
      Removed unnecessary block height in IsScTxApplicableToState
      Fixed unconfirmed fwts/mbtrs/certs removal upon sidechain getting ceased
      Introduced height checks and removed disconnectedBlockHash
      Fixed mbtr removal from mempool upon change of ActiveCertDataHash. Missing proper mock for certDataHash
      Fixed traces on ConnectBlock via linter
      Added UTs for IsScTxApplicableToState + fix mbtr UT failure due to missing block
      blockchain cleanup in UTs for mbtr
      Minor code cleanup
      Partial backport of Fixes following sasha's code review 3e1bcf6f947fd4ef54bfa43c8d500763b080a05c
      Reactivated quality related UTs
      Removed Sidechain state
      Cleanup useless heights
      Latest changes
      Absorbing scEvents scheduling into Update/RestoreSidechain
      Started unlocking UTs following sc lifetime refactoring and fixes
      Removed Disconnecting flag from mempool
      Refactored and fixed height related sidechain methods
      Leftover from UpdateSidechain refactoring
      Completed refactoring of sc lifetime related methods
      Unlocked zen-tx compilation
      Unlocked gtests in gtest/test_sidechains.cpp
      DIRTY COMMIT: Unlocked gtests in gtest/test_sidechain_to_mempool.cpp
      Made mempool related sidechains stricter
      Made mempool related sidechains stricter + unlocked test_sidechain_events.cpp UTs
      unlocked test_sidechain_certificate_quality.cpp UTs
      unlocked test_sidechain_blocks.cpp UTs
      Minor code formatting
      Unlocked leftover UT
      Fixup mempool cleanup for mbtr
      Fixes following sasha's code review
      Fixed GetActiveCertDataHash + UTs
      Cleanup GetActiveCertDataHash UTs
      Unlocked sidechain gtests, all green
      Rework of GetActiveCertDataHash
      Moved some csw UTs to test_sidechain.cpp
      Minor fixes following rebase on sidechains_dev
      Minor fixup rpc command
      Removed purely forwarding polymorphic methods
      Just removed some tabs
      Fixed minor refactoring error
      Fixes following merge conflicts resolutions
      Coalescing scField and CPoseidonHash into CSidechainField. Unlocked UTs, py tests to go
      WIP: reverted hashScTxsCommitment to uint256 and unlocked py tests.
      Final fixes to UTs
      Removed base_blob from CSidechainField implementation
      Refactored CSidechainField serialization, in preparation for zendoo_mc_cryptolib move
      Retyped scConstant to CSidechainField
      Improved optionality of scCreation constant
      Fixed up CSidechainField::IsValid
      Fixed UTs following CSidechainField refactoring
      Cached field deserialization into CSidechainField
      Reverted minor change over readability disagreement
      Changed CSidechainField UTs to set 253 bits field expectations
      Minor UT refactoring
      WIP
      DIRTY COMMIT: moved isValid for configs to checkScTxSemanticValidity + allow multiple init with same cfg with no crash + initialized turned isValid with revalidation upon different cfg
      DIRTY COMMIT: Imported refactored CFieldElement with adaptation
      Unlocked compilation following class restructuring
      Replaced libzendoomc::ScFieldElement with CFieldElement. UTs and Integration tests to be done, build unlocked
      Fixup broken Sidechains UTs and integration tests
      Retyped back hashScTxsCommitment to uint256
      Fixed faulty processing of null field element
      Changes following code review
      WIP: Fixing libzendoo UTs warnings + expectetions
      Fixed CFieldElement Serialization
      Introduced smart_ptr wrapper for naked field_ptr
      Changes following sasha's code review
      Fixed unit256 to CFieldElement conversion + turned wrapperFieldPtr to shared_ptr
      Changed MAX_BIT_VECTOR_SIZE_BITS and validations for bitVectorSizeBits
      Fixed unit256 to CFieldElement conversion + turned wrapperFieldPtr to shared_ptr
      Changed MAX_BIT_VECTOR_SIZE_BITS and validations for bitVectorSizeBits
      Removed naked field_t pointers in favour of smart_pointer wrapped version
      Cleaned up CScProofVerifier
      Introduced CScProof class
      Introduced CScVKey class
      Introduced CScVKey class
      Fixed sidechain fork height in a few python tests
      Refactored AcceptToMempool return type
      Fixed faulty rawTransaction refactoring
      Refactored ProcessTxBaseMsg to allow dependency injection
      Minor refactoring of relay
      Inception of ProcessTxBaseMsg UTs
      Fixed faulty ProcessTxBaseMsg refactoring
      Added UTs for ProcessTxBaseMsg
      Added UTs for ProcessTxBaseMsg. Missing AND REALLY NEEDED adequated Fake
      Fixed faulty ProcessTxBaseMsg refactoring
      Improved fake mempool processing in UT
      Refactored ProcessTxBaseMsg to handle retransmitted txes separately
      Refactored ProcessTxBaseMsg to have unified handling of invalid txes
      Added UTs for retransmission cases
      Introduced CNodeInterface and FakeNode in UTs
      Refactored ProcessTxBaseMsg to have unified handling of MISSING_INPUT and INVALID cases
      Refactored ProcessTxBaseMsg to have single function handling invalid txes
      Made more uniform workflow for missing inputs txes
      Refactored ProcessTxBaseMsg into single simpler loop
      Fixed faulty assert + removed faulty/unneeded rejectInvalid on retransmissions
      Splitted ProcessTxBaseMsg into AddTxBase and Process
      Fixed memory leaks following ProcessTxBaseMsg refactoring
      Bitcoin backport to solve some sanitizer issues
      Introduced TxBaseMsgProcessor, moving mempool txes and orphan processing to it
      Improved encapsulation of recentRejects and mapOrphanTransactions
      Introduced specific locks for Orphan and rejected txes data-structures
      Moved ProcessTxBaseMsg to different thread
      Aggregated queue cleanup calls into single one
      Unlocked compilation following merge conflicts
      Minor fix of merge conflicts error
      Introduced sidechain folder under dataDir, to host aggregated verification keys.
      Introduced new flag for delayed proof verification
      Extracted proof verification from applicability to state checks
      Fixup minor regression bugs
      Introduced partial validation in accept to mempool
      Refactored TxBaseMsgProcessor to start handling partial validations
      Minor gtest update to speed it up following poseidon hash introduction
      Introduced check to avoid mempool double insertion in asynchronous mempool tx processing
      Introduced flag in ConnectBlock to skip sc proof verifications
      Introduced batch proof verification scaffolding
      WIP: Replaced naked unsigned char with enum class for CValidationState
      CValidationState refactoring
      Added mempool re-checks following asynchronous processing introduction
      Refactored GetTxBaseObj to call GetTransaction/GetCertificate
      Fixes following intermediate code review
      Further minor changes following intermediate code review
      Fixed typo in comment

albertog78 (3):
      Update README.md
      Update README.md
      Update README.md

alsala (8):
      tls py test: fixed setting of unsupported cipher; fixed tls1.3 protocol connection
      Fixed typo in a comment
      Update src/net.h
      Update src/init.cpp
      tls py test: fixed setting of unsupported cipher; fixed tls1.3 protocol connection
      Fixed typo in a comment
      Update src/net.h
      Update src/init.cpp

ca333 (2):
      update libsodium to v1.0.18
      update libsodium to v1.0.18

cronicc (102):
      Update openssl from 1.1.1d to 1.1.1g
      Update univalue to v1.1.1
      Fix MacOS build issue "conversion from 'size_t' (aka 'unsigned long') to 'const UniValue' is ambiguous"
      Replace calls to deprecated method push_back(Pair()) with pushKV()
      Update README.md
      Rename org to HorizenOfficial
      Rename org to HorizenOfficial
      Remove redundant Building section from README
      Update README.md
      Update src/net.cpp
      Update src/net.cpp
      Fix typo
      Set rpfix fork activation height
      Extend fetch-params.ps1:
      Set mainnet/testnet checkpoint blocks
      Set next deprecation block 920000
      Set version to 2.0.22, set copyright year 2020
      Rename threads from zcash to horizen
      Regenerate man pages
      Add release-notes
      Update release-notes
      Fix MacOS ZcashParams travis-ci cache dir
      Exclude failing RPC tests on MacOS Travis worker
      Fix regression B2 CLI v2.1.0 with python2
      Update openssl from 1.1.1d to 1.1.1g
      Update univalue to v1.1.1
      Fix MacOS build issue "conversion from 'size_t' (aka 'unsigned long') to 'const UniValue' is ambiguous"
      Replace calls to deprecated method push_back(Pair()) with pushKV()
      Rename org to HorizenOfficial
      Remove redundant Building section from README
      Update src/net.cpp
      Update src/net.cpp
      Fix typo
      Set rpfix fork activation height
      Extend fetch-params.ps1:
      Set mainnet/testnet checkpoint blocks
      Set next deprecation block 920000
      Set version to 2.0.22, set copyright year 2020
      Rename threads from zcash to horizen
      Regenerate man pages
      Add release-notes
      Update release-notes
      Fix MacOS ZcashParams travis-ci cache dir
      Modify block header offsets for zen
      Update documentation
      Speed up download of stage artifacts by using backblaze directly
      Fix transient failure on MacOS:
      2nd try, fix MacOS ZcashParams travis-ci cache dir
      Update OpenSSL to 1.1.1i
      Update OpenSSL to 1.1.1j
      Update domain to horizen.io
      Set mainnet/testnet checkpoint blocks
      Set next deprecation block 1027500 * next depreaction October 2021, adjusted to 30 weeks
      Set version to 2.0.23, set copyright year 2021
      Regenerate man pages
      Add release-notes
      Update OpenSSL to 1.1.1k, fix for CVE-2021-3450
      Address review comments and #176
      URL encode '+' for backblaze depends mirror
      Set correct hashes for rust nightly-2021-04-25 Windows/Darwin
      Update Rust to 1.51.0 stable
      Add websocket-client python test dependency
      Update CI builder dockerfiles:
      Replace xenial with focal builder in travis
      Use focal host os and x-large vm size in travis linux build/test stages
      Add some buildsystem information to entrypoint.sh
      Fix util-test python PATH on focal
      More Test stage parallelization
      Set version 2.2.0-beta1
      Add sidechains-testnet2 seed nodes
      Change PROTOCOL_VERSION, MIN_PEER_PROTO_VERSION to 170004
      Change testnet default p2p port to 21033
      Add preliminary testnet checkpoint block (pre-fork)
      Set preliminary fork activation heights (well into the future)
      Add --legacy-cpu build arg for CPUs without bmi2/adx
      Speed up rust installation by not installing rust-docs
      Update OpenSSL to 1.1.1i
      Update OpenSSL to 1.1.1j
      Update OpenSSL to 1.1.1k, fix for CVE-2021-3450
      Add more fallback gpg2 servers for key fetch to Dockerfiles
      Remove --emit=asm rust flag from libzendoo
      Always login to hub.docker.com with read only account, even when only pulling
      Update preliminary fork heights in test_forkmanager
      Fix SC fork detection in getblockmerkleroots RPC
      Adjust getblockmerkleroots.py to regtest sc fork activation height
      Fix copy/paste fragments
      setproofverifierlowprioityguard RPC help text formatting
      Update OpenSSL to 1.1.1l, closes #178
      Set fork activation heigts for sc-testnet2
      Revert "Change testnet default p2p port to 21033"
      Revert "Change testnet default port to 20033"
      Revert "Change PROTOCOL_VERSION, MIN_PEER_PROTO_VERSION to 170004"
      Revert "Change PROTOCOL_VERSION, MIN_PEER_PROTO_VERSION to 170003"
      Revert "Add sidechains-testnet2 seed nodes"
      Revert "Add sidechains-testnet seed nodes"
      Fix typo
      Set version 3.0.0-beta1
      Update Debian package info
      Update testnet checkpoint blocks
      Update manpages
      Set zendoo testnet fork8 height to 926225
      Update changelogs

i-Alex (18):
      wCeasedVk added to the sidechain creation output.
      CSW inputs verification proccess basic implementation.
      CSW: changes after review.
      CSW: ContextualCheckInputs code reorganization.
      CSW: Python test minor fix.
      CSW and Python tests updated.
      CSW sidechain balance verification and update.
      CSW and FT mempool check on connecttip/disconnecttip. Remove outdated CC info.
      rawtransactions.py update after merge.
      CSW unit tests updated.
      CSW: mempool unit tests updated.
      CSW: epoch field removed. CertDataHash refactored. CSW snark proof inputs list changed.
      Minor cleanup.
      CSW changes after review considering the MBTR integration.
      CSW changes after review.
      websocket send_certificate parameters order fix.
      A few prints added to sc_cert_customfileds.py
      Rust libs staging updated.

lander86 (3):
      freezing the b2 version and fixing pigz command on MacOs
      fix ipfs image url
      fix indentation and minor fixes

​Felix Ippolitov (2):
      Fix build with gcc10
      Fix build with gcc10

