Notable changes
===============

* Introduction of Zendoo Cross-Chain Transfer Protocol (CCTP) via Hard Fork at block height 1047624.
* Support for ARM64 dropped (platforms like Rasberry Pi), it might be added back at a future date.
* Requirement of adx and bmi2 CPU flags (Introduced in Intel Broadwell and AMD Excavator architectures). Official releases published on the APT repository will require these CPU flags going forward.
* --legacy-cpu switch added to ./zcutil/build.sh. Enables building on platforms without adx and bmi2 support. Binary releases for legacy CPUs will be published at https://github.com/HorizenOfficial/zen/releases.
* --enable-address-indexing switch added to ./zcutil/build.sh to enable building with Address Indexing support for block explorers.


Changelog
=========

Alberto Sala (528):
      First implementations for side chain
      restore to previous version
      Fixes to hashing function
      Added method for getting a recap of sc info
      vccout splitted in cert lock and forward transf outputs inside tx obj
      Reverted modification for block header size
      Removed type attribute from CTxCrosschainOut objects
      First impl for refactoring of crosschain outputs arrays
      Improvement in calculation sc mkl root maps hash
      Added temporary dgb function
      Added handling of sc creation output; modified rpc methods for sc operations
      Moved sc related data to sc/sidechain files; added sending of a fee to foundation taddr when creating sc
      Added RPC methods for getting sc info
      Modified JSON related method
      First implementation of sc info serialization in level db; improved support of raw tx handling
      Added db ptr test
      minor fixes
      Added check in sc creation that fund has been added
      Fixed crosschain output json representation
      Fixes and changes for backward transfer transaction handling
      Print sc cert injson even if empty
      Fixes in handling of certificates in mempool and wallet
      Minor refactoring and optimizations. Bug fixes after unit tests
      Set up for code review
      Applied fixes from code review
      Modified structure of JSON messages; added deletion of allocated pointers
      Missing default return statement
      Modified structure of JSON messages; added deletion of allocated pointers
      add check of null ptr; added err handling for getblock
      Fix for windows build
      Added some missing debug category
      using iterators for finding pindex in map in order to prevent map corruption; added many traces; some heap allocated obj now uses stack
      Modification for getscgenesisinfo rpc cmd
      Added missing boost error code check which might cause an endless loop in reader thread; fix for possible null ptr usage; added resource cleanup upon client disconnection
      Changed polling time in writeLoop from 1 sec to 100 ms; changed tabs into spaces
      Added network type in getscgenesisinfo RPC output
      Fix for #196 (SC merkle root map does not enforce SC id correlation)
      Using shared_ptr instead of raw ptrs where possible
      Codereview, second pass
      Replaced check with assert; added modification after testing checklevel=4 at startup
      Fixed error tag due to modification of version sanity check in src
      Added missing header file in git
      removed unused methods and vars
      Rebase code with latest version of sc_development branch; ready for code review
      removed useless method
      Modifications after code review
      Removed unused type
      Fixed bug for which reverted fw transactions remained in sc view
      Modifications after code review step3
      First compiling rebased code
      Added implementation for cert consistency check
      Optimization of undo data for immature sc amounts; added cert contrib in scMerkelRootMap; added mempool check for cert
      fixed range comparison
      Added check that does not allow mempool to contain tx related to not existing sc
      Handling of sc epoch in certificates; rpc cmd changed accordingly
      Fixes for unit tests
      txmempool: imported mapSidechain handling from albene; moving common code in a private method; changed remove() method signature
      fix for windows build
      rpc cmds: implemented createrawcertificate and added two optional params to send_certificate; also allow certificates with empty vector of outputs
      Imported modification for mapSidechain from albene work
      Imported test_sidechain_to_mempool unit test from ab/issue_215
      Added state ref obj arg to some api; moved check on epoch in order to have it also in ConnectBlock; added unit tests for checking sidechain fork height in contextual controls
      Fixed default value of cert epoch in undo block data
      Clean up of sc_cert_invalidate py test; added new py test for rawcertificates
      removed raw_input call from py test
      Fixed missing iterator increment
      Fixes for having all sc py tests passed
      Added missing par in virtual method declared in bitcoin test func
      Fix for safe guard epoch border; removed useless loop when applying meture amounts; modifications to unit test and py scripts
      First modification of the algorithm for building block hashScMerkleRootsMap according to zendoo paper; fix to rpc command for creating cert with empty vout
      Commented out debug macro
      Using new class for serialization of block headers to be broadcasted on the network
      Merge code related to rpc cmds for sbh wallet
      Version handling via forkmanager for new CBlock supporting sidechain certificates
      Modified computation of SCTxsCommitment and added verification in the ConnectBlock func; modified py test
      Added fork manager api for checking block version; added obj for computing scTxsCommitment of a block; fixes from code review
      fix for pyton tests after latest src modifications
      Added fee param to rpc cmd for creating certificate; removed logic for carving fee from certificate amount; fix for websocket server; modifications to py tests
      Added new rpc cmd useful for sbh wallet send_to_sidechain; added more info to listtxesbyaddress cmd output
      Added check that no cert contribution is contained in map of sidechain in mempool befor erasing the corresponding entry
      Added rpc cmd getunconfirmedtxdata useful for sbh wallet; added option for reverting output order to listtxesbyaddress cmd
      Added a mem only bool flag in CTxOut for distinguish if an object is generated from a backward transfer in a cert vout memory representation. Using two different vectors in certificate serialization for includnig resp. standard vout and backward transfer related vout
      Added a mem only bool flag in CTxOut for distinguish if an object is generated from a backward transfer in a cert vout memory representation. Using two different vectors in certificate serialization for includnig resp. standard vout and backward transfer related vout
      Modifications due to albene code review
      Fix for getunconfirmedtxdata returning a wrong amount
      Added unconfirmedInput field in json output of getunconformedtxdata rpc command
      Fix for sendHashFromLocator function
      handling of dependancy of sc creation and forw transfers txes in mining
      Added optional flag in getunconfirmedtxdata rpc cmd for overriding spendzeroconfchange zend option
      Added handling of sc dependancies in getrawmempool rpc cmd
      Added unconfirmedInput field in json output of getunconformedtxdata rpc command
      Added optional flag in getunconfirmedtxdata rpc cmd for overriding spendzeroconfchange zend option
      Added certificate version (-5) as a different one from sc tx version and its fork manager handling
      Added a mem only bool flag in CTxOut for distinguish if an object is generated from a backward transfer in a cert vout memory representation. Using two different vectors in certificate serialization for includnig resp. standard vout and backward transfer related vout
      Added a mem only bool flag in CTxOut for distinguish if an object is generated from a backward transfer in a cert vout memory representation. Using two different vectors in certificate serialization for includnig resp. standard vout and backward transfer related vout
      Modifications due to albene code review
      Remove code related to version bit handling in block versions
      Wip: adding vin to certificates
      Modification after albene review
      Removed log file erroneously added to git
      Adding versioning in blockundo data: wip
      Added unit tests
      Using MSG_TX protocol message for handling bot tx and cert
      Modifications after albene code review
      Modifications for rawcertificate api and py test
      Minor modifications and leftovers after full regression
      removed unused parameter form unit test func
      Modifications after albene code review
      Modifications after merge and full regression test
      Fix to py test after merge; full regression ok
      Added support for certs in bloom filters; using one common function for processing tx and cert network messages
      Minor fix for passing params by ref
      Added getters for cross chain outputs in transaction base onjs; added handling of cert vin dependancy in miner
      Removed useless code and comments
      Added handling of tx dependant from cert when connecting a block; fix in CTxOut eq operator; removed assert in mempool check; cert dependancies displayed in getrawmempool rpc cmd
      Added vin handling in mempool for certificates
      Modifications after albene code review
      Spending of unconfirmed change output from a certificate is not supported
      removed log file wrongly added
      Added py scripts for testing certificate change output handling and dependancies in mempool and mining; fixes and modifications after testing
      Fixed a missing arg in an inherited method for a failing bitcoin unit test; added override specifier also to other methods
      Fixes and traces for issue in disconnecting block after node restart
      Fixes after albene code review
      MAX_BLOCKS_REQUEST limit updated in websocket_server
      Removed code relevant to obsolete certifier lock cc output
      Removed getters for cc out from txbase; splitted method for handling dependencies in miner
      removed erroneously released log file
      added try/catch block when dynamic casting
      Using two phase unserialization for tx and cert obj received from network
      Using the tx version attribute instead of parameter in ser/unser functions
      Modifications after albene review
      Certs can spend another cert unconfirmed change
      WIP: full compiling version, regression to be started
      computation of serialized size method made virtual in tx base
      Fix check of cert output maturity in checkTxInputss and added a test for that
      Modifications after i-Alex code review; wip
      Added comments in removeForBlock func
      Moved CheckOutputsCheckBlockAtHeightOpCode in contextual check function; added AcceptTxBaseToMemoryPool for calling the specific function for tx and cert; other fixes after code review
      Fix for unit test
      separate in check() method the whole processing of txs (including dependency) from certificates processing
      Fix for not accepting a certificate belonging to an old epoch
      scid can not be specified when creating a Sidechain, is generated using creating tx hash and sc creation data; WIP: unit test ok
      removed scid input from all rpc cmds and tests; full regression ok
      Added sc state info in getscinfo rpc cmd
      Removed ccout hash from scid generation func; removed useless cond check in txmempool
      Sparse fixes from code review of merge_support_branch
      Fixed merge conflict preventing build
      WIP: fix from code review in main.cpp
      WIP: fix from code review: Removed TryToAddMemPool methods
      WIP: fix from code review: moved mapNoteData to base class in wallet and removed useless code
      fix from code review: removed commented code, other minors; regression ok
      Removed unused method
      WIP: fixes after code review: first step
      Added missing comment
      WIP: fixes after code review: regression ok
      WIP: compiling version, test to be done
      WIP: rename some methods and static_assert fix
      Chenged name and signature of GetMaturityHeightForOutput
      Changed assert into a check on nullptr: added missing data members in assanment op; fixed client.cpp
      Removed wrong semicolon at the end of a check condition
      Fixed UT
      Changed logic in IsTransaction api
      WIP: Changing implementation in Signature Serializer obj for using just one with a txBase input
      WIP: Changing implementation in Signature Serializer obj; checking nIn by the caller for certificates
      Fixed a wrong position for a break statement
      Added sign test for certificates
      Second step; build succesful, test to be executed
      Removed git confilict string
      Fixed listtransaction rpc cmd
      fixed py test for listtransactions rpc
      Fixed getblocktemplate py test
      Using smart ptr for containers in mempool thread handling
      Added an assert in init.cpp if zend is run in main net
      Added an assert in init.cpp if zend is run in main net
      Changed flow for ceasing sidechain event logic
      Fix for UTs
      Added utility func to framework for getting epoch data; sone test cleanup; added new test for ceasing sc
      Added some comments and minor fixes to py tests
      Change after code review
      Added maturityHeight field to bwt entries in the Json output of the rpc cmd listtxesbyaddress
      Added scid field in the Json output of the rpc cmd listtxesbyaddress in case of certificates; temporary fix for persisting bwtMaturityDepth attribute in wallet db
      Fixed maturityHeight value in case of unconfirmed cert; added a py test
      Removed bwt contributions to unconfOutput in the json output of getunconfirmedtxdata rpc cmd
      Allow the rpc cmd for creating a certificate to use a zero fee
      Removed visitor pattern from rpc cmd; WIP- regression ok
      Support for creating certificate via websocket channel; added default value if no fee is specified
      Added help lines for websocket connection options; added examples in zen.conf
      Fixed remaining items in the backlog of code review
      Changed implementation and signature of OrderedTxWithInputsMap method for using ordered tx map data member in wallet
      Fixes after code review
      Modifications after albene codereview
      Modified some sbh cmd, namely output of create_sidechain rturns txid/scid pair; output formatting of expanded txes; added optional param for final txes handling
      Changed tag address
      Fixed param selection in getunconfirmedtxdata; using map wallet for expanding vins
      Removed debug line in py test
      Added py test for sbh rpc cmds; other fixes for getunconfirmedtxdta cmd
      sc_create rpc cmd now returns a json object with txid and created scid
      changed Zat (oshi) tag into Sat in rpc cmd
      Fix to removeImmatureExpenditures method in mempool; added a UT for it; fixed a py script after branch rebase
      Fixed a comment in test
      Removed those test parts that rely on thread execution order in getblocktemplate_proposal.py
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
      Merge branch 'speedup_SelectCoins' into merge_prs; Help msg cleanup for getblock rpc cmd
      WIP: handling of certificate quality
      Added quality attribute in scinfo and undo data structure; updateed handling of certs with different quality in mempool
      Added removal of lower quality certs from mempool; first version for handling multiple certs in blockchain
      Preliminary fix in mempool for the case of cert2 depending on cert1 with q2==q1 and fee2>fee1
      fix in mempool for the case of cert2 depending on cert1 with q2==q1 and fee2>fee1; now cert2 is refused and cert1 is kept instead
      Handled the case of cert2 depending on cert1 with q1>q2 in mempool
      Fix for removing conflicted certs based on quality comparison
      Added first implementation for multiple certs quality in blocks and undo struct; change internal data in mempool for container of certificates
      improved code for multiple certs quality in blocks and interaction with mempool; added some fix from albene first prelimnary code review
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
      Added doc/json-examples files obtained with the latest syntax for sc related rpc commands; added a py script for their automatic generation
      Fix broken py test
      Added sc_create description in payment-api.md
      Added sc_send description in payment-api.md
      Added sc_request_transfer and sc_send_certificate description in payment-api.md
      Modifications after code review
      Modification after code review
      Modified sc_send_certificate desc after merge
      Added/modified some help msgs for rpc cmds
      Fix after cronic code review
      Fix to py test using deprecated rpc api
      Fix to failing py test after PR215 merge
      Fix to py tests after PRs merge
      Fix to py tests after PRs merge for address indexing feature

DanieleDiBenedetto (41):
      WCertProofVerification and zendoo-mc-cryptolib integrations
      Small fix
      Small fix
      Updated zendoo_mc dependency to last development commit
      Removed unused dependencies
      Removed hex_utils.h dependency
      Removed hex_utils.h dependency
      Added additional test cases in a couple of Python tests
      Fix auto-merge and conflict resolution + fix in tests
      Fix in proofverifier.cpp
      Added ZENDOO_MC env variable
      Added MCTestUtil class, fixed default value for ZENDOOMC env variable
      Update README.md
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

Iozhytsia Oleksandr (4):
      Rust version and libzendoo dependencies updated for windows target platform.
      Zendoo mc test build fix for both linux and windows platforms.
      Certificate proof data logs added.
      fpic added for zendoo_mctest to make it compile for linux.

Jack Grigg (5):
      Add test vectors for small-order Ed25519 pubkeys
      Patch libsodium 1.0.15 pubkey validation onto 1.0.18
      Patch libsodium 1.0.15 signature validation onto 1.0.18
      test: Minor tweaks to comments in LibsodiumPubkeyValidation
      depends: Remove comments from libsodium signature validation patch

Luca Cermelli (1):
      Update README.md

Marco (3):
      Added version in gettransaction and CTP fields only if it's a sidechain transaction
      Try to add AddJoinSplit to Certificate class
      Updadted gettransaction help

MarcoOl94 (21):
      Refactored and reintroduced the sc_create's python tests
      Removed old test from rpc-test list
      Extended rawtransactions' test to verify SC creation and FT
      Implemented requirements requested by Albene
      Removed unused variable
      Porting of getblock improvement
      Fixed getblock help
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
      Added the new MaturityHeightIndex DB needed by the getblockexpanded RPC call (#208)

Oleksandr Iozhytsia (35):
      Wallet fix and certificate creation rpc changes.
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

Paolo (2):
      Apply suggestions from code review
      Address indexing backport (#201)

Paolo Tagliaferri (92):
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
      Fixed the issue that was making the Mac OS build fail when using the -Werror flag
      Fixed sporadic failures in mempool_tx_input_limit.py and zkey_import_export.py
      Fixed a sporadic failure of prioritisetransaction.py Python test

Phoinic (6):
      Library versions and rust build system adjustments
      Broken tests commented
      Rust build updated to the actual libraries state
      Library versions adjustments
      Build system bugfix
      error.h includes removed

PowerVANO (2):
      Added setup_zend.bat and fetch-params.ps1
      Added setup_zend.bat and fetch-params.ps1

Rodrigo Doria Medina (7):
      Pull in patch fixing OpenSSL build error on older MacOS
      Remove --enable-werror flag
      Change flag in travis CI to build Mac OS target
      Add funcsigs
      Add two extra Mac test workers
      Removed the -rpcservertimeout parameter
      Add 3 new workers for Mac OS rpc tests

Taylor Hornby (1):
      Avoid names starting with __.

VolodymyrBabaiev (85):
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
      Added fromAddress argument for sc_send_certificate. (#205)

abi87 (647):
      introduced gTests for SCManager. Added reset faciilities in ScManager code for tests independence
      minor improvement of BaseChainParams reset
      added findings about CTransaction class
      added simple test for updateScInfo
      added less simple test for updateScInfo
      added test proving not rollback for failed updateScInfo
      added simple test for forward transfer in updateScInfo
      added less simple test for forward transfer in updateScInfo
      added tests for IsTxApplicableToState
      added tests for IsTxApplicableToState and improved readability
      added tests checkTxSemanticValidity
      added futher tests for checkTxSemanticValidity
      added first tests for IsTxAllowedInMempool
      added further tests for IsTxAllowedInMempool
      refactored code to avoid db creation on tests
      added first tests on Flush
      added further tests on Flush. Fixed bug on reset
      added further tests to prove ScMgr-CoinView alignement
      minor refactor to align with alSala's coding convention
      added first test for mature balances. To be much refined and extended
      added further tests for mature balances.
      started tests refactoring for readability
      some more refactoring for readability
      some more refactoring for readability
      some more refactoring for readability
      some more refactoring for readability
      currently done with refactoring for readability
      moved fixture members definition at the botton of test file for readability
      added first tests for tx revert
      added tests for tx revert on non-existing sc
      cleaned up semantics checks
      cleaned up semantics checks
      increased CMutableTransaction locality
      fixed Flush tests
      added positive revert test
      added further tests for tx revert
      added further tests for restore
      recommit for signature
      minor refactoring
      added test for RestoreBalance
      minor refactoring for readability
      minor refactoring for readability
      verified absence of coupling with other test suites
      moved tab to 4 spaces + removed custom copy ctor
      introduces checks on non-positive fwd amount
      introduced fixes following alsala's comments
      leftover from previous commit
      added semantic checks on fwd tx amount upper bound
      removed useless prints and improved some prerequisites
      renamed sidechain test to conform other test naming convention
      leftover from previous commit
      refactored tests for adherence to naming convention
      refactored IsTxApplicableToState tests to improve errorMsgsù
      reused validTx utility from checkTransaction gtests. To complete checkValidations for Sprout Cc txs
      cleaned up Semantics validations and isApplicableToState
      refactored createValidTransaction
      refactored createValidTransaction
      refactored createValidTransaction
      fixed test on semantics checks on faulty sprout tx
      cleaned up ApplyMAtureBalances and IsAllowedInMempool
      cleaned up updateScInfo and Flush tests
      fixed tests on restore and revert
      introduced new persistence layer and some renaming
      removed initDone member, substituted with pLayer instantiation check
      moved persistence classes to cpp to speed up build
      moved db out of ScMgr into dbPersistenceLayer
      fixed segfault on reset and persistency policy setup bug
      moved map initialization into persistenceLayer
      moved write and erasedb into persistenceLayer. Missing dump info
      completed refactoring
      added erase on Flush testcase
      improved coverage by using getScBalance
      changes following alsala's comments
      added validation for cumulated fwd transfers
      Refactored uts for readability, along algar's indications
      Refactoring to remove friendness among ScManager and ScCoinsViewCache
      Refactored ScCache data structures and improved their write-locality for correctness reasoning
      Removed flush dependency on CacheScInfoMap
      Refactoring to isolate CacheScInfoMap from CacheView
      Refactoring to remove finds on CacheScInfoMap. Missing removal of looping over CacheScInfo to completelly eliminate it
      Removed any read from CacheScInfoMap. Finally amenable to be removed
      Removed any read from CacheScInfoMap. Finally amenable to be removed
      First complete draft of CacheScInfoMap removal from CacheView
      Refactoring to improve encapsulation and readability
      cleaned up sidechainExists
      cleaned up IsTxApplicableToState. Interface starts emerging
      introduced common interface for viewCache and manager
      explicit dependency of viewCache onto Manager (via viewCache ctor)
      improved manager internal map encapsulation
      moved fakePersistence layer into gtests, out of production code
      leftover from previous commit
      removed IsTxApplicableToState duplication
      Changed CCoinsViewCache ptr with ref in ConnectBlock
      Proposal for certification handling in DisconnectBlock
      Cleaned up CheckBlock
      Split tx and cert processing in ConnectBlock. Missing potential cleanup
      Minor cleanup of GetNumbOfInputs
      Cleaned up DisconnectTip
      Rolled back templatized version of RevertOutput
      Extracted AcceptCertToMemoryPool for code review. Inclusion in prod code to be discussed
      Introduced (almost all) calls to AcceptCertificateToMemoryPool. Tests ok. Review on mempool started
      Removed mempool dead checks in AcceptCertToMemoryPool
      Minor code cleanup
      Improved inheritance for CWalletTx and CWalletCert
      Laid foundations for separated AcceptToMemPool for certs and txs
      Started AcceptToMemoryPool cleanup
      removed unused method
      code cleanup
      Removal of unused methods
      minor cleanup
      code cleanup
      removed unused method from mempool
      Minor code cleanup
      Minor code cleanup and removal of useless methods
      Cleanup syncWithWallets
      Minor code cleanup
      Disentangled transactions and certificates removal in mempool
      improved instantiation for dummy objects
      cleanup UpdateScInfo
      cleanup polymorphic check(
      removed passing methods in tx and cert
      Further cleanup of unused code
      separated fwds and certs contributions in CSidechainMemPoolEntry
      verified by tests that certificates cannot cause removal of other certificates
      verified by tests that txs cannot cause removal of certificates
      cleared up txs and certs removal
      inception of python tests merging with ab/issue_215
      Refactor sc_cert_base.py to add asserts instead of prints
      sc_cert_base.py cleanup via linter
      sc_cert_base.py cleanup via linter
      Refactor sc_cert_epoch.py to add asserts instead of prints
      Minor changes for sc_cert_* readability
      Fixes to make epoch start on block including SC creation
      Fixes for zero-based numbering of epochs. Todo: fix some python tests
      Minor code reshuffling
      Minore cleanup of miners related functions
      Minor python tests cleanup
      Dirty commit: merged ab/issue_215 and wip_certificate. Compilation Ok. Tests to be checked
      Fixed merge issue and made sc_create test green
      Reabilitated asserts on mempool related to Sc
      Moved sc methods to sc/sidechain.h
      reintroduced guards in fwds and sc creation removals from mempool
      Separated certificate from fwdtx in mempool data struct
      Improved getScInfo from mempool with UTs
      Replaced loops over MapCertificates with lookups on MapSidechains in mempool
      Fixed IsCertAllowedInMempool to handle missing case
      Merging as/vsc_ccout_new into merge_support_branch
      getScInfo from mempool count certificate too now
      Renamed scInfo to CSidechain
      Renamed HaveScInfo to HaveSidechain
      Renamed GetScInfo to GetSidechain
      Removed code duplication in send_certificate
      Fixed minor bug on RestoreImmatureBalances. Introduced in-place changes rather than full overwrite of sc cache entries
      Added removeConflicts for certificates upon their confirmation in block
      Cleaned removeOutOfEpochCertificates loop
      renaming and change following code review
      added Uts for fwts and certs mix removal bug fix
      Introduced getters on txs and certs for removal of polymorphic UpdateCoins
      Introduced const getter for tx and cert vout. Made vout protected and changed callers to use getter
      Made tx.vin private and changed callers to use getter
      Made tx.vjoinsplit private and changed callers to use getter
      Fixed compilation following merge with merge_support_branch
      Imported python test framework changes from master to fix crashes in python Python 2.7.17
      Imported python test framework changes from master to fix crashes in python Python 2.7.17
      Refactored CheckCertificate and CheckTransactionWithoutProofVerification for readability
      Getters renaming following alsala's review
      Storing coins from certs in chainstate_db under different key.
      Introduced getScId for transactionBase (null for txs). Made scId private and accessible through getter only.
      Introduced custom serialization for coins-from-certs for chainstate_db.
      Moved CCoins serialization to txdb specific wrappers.
      Moved all CCoins serialization facilities to coinWrappers in CCoinsViewDB
      Added first UTs for new CCoins serialization mechanisms
      Wallet-centered code cleanup
      Refactored CheckTxInputs to accept CTransactionBase
      Removing useless methods following getters introduction
      Code cleanup to establish CCoins is from certs without inspecting version
      Coalesced txs and certs serialization facilities. Rebased tx and cert distinction on version number
      Handled CTxOut.isFromBackwardTransfer serialization in CCoins, preserving backward compatibility
      Fixes following alsala's code review
      Code cleanup on the way of solving issue233
      Cleanup Accept*ToMemoryPool so to show similarities. Removed IsCertAllowedToMempool
      Removed code duplication and refactored transaction related members
      Refactored isStandardTx to remove code duplication.
      Minor code renaming
      Refactored CheckTxInputs to accept CTransactionBase
      Removing useless methods following getters introduction
      Modified py tests to introduce checks on maturity of coins from certificates
      Introduced checks and validations of maturity of coins from certs
      Code cleanup
      Wallet-centered code clenaup and refactoring
      Introduced getter for MapWallet to check accesses
      Made CWalletObjBase cache flags and values private
      Modified py tests for checks on certificate maturity at output level
      Rewritten checks on certificate maturity at output level
      Leftover from previous commit
      Representing immature bwt amounts in relevant rpc calls
      Added checks in py tests for certificate maturity handling + bug fixing
      Fixes following alsala's review
      Fixing originScId serialization in BlockUndo
      Updated maturity for coins from certificates: py tests
      Updated maturity for coins from certificates: code changes
      Changes following alsala's code review
      Fixed failing regression following merge
      Fixed failing regression following merge
      Moved GenerateChainActive to test utils
      Refactored safeguard height calculations
      Introduction of sidechain handler with UTs
      Cleanup useless method
      Introduced handling of coins from ceased sidechains
      Introduced new BlockUndo version and restoring of ceased coins
      Fixed CTxInUndo serialization (full CCoin metadata serialization for bwts)
      Introduced muted calls to ceased sidechains handling
      Unit tested sc unregistration
      Fixed removeCertificate for complete cert cleanup
      Fixed restoreCeasedSidechains signature and enabled calls
      Forbid spending ceased coins in wallet
      Forbid fwt to ceased sidechains
      Added lastCertHash to ScInfo
      Added UTs for lastCertHash
      Introduced persistency of ceasedScId list.
      Introduced updates on ceasedScId list
      Moved UTs from sidechainHandlers to CCoinsViewCache
      Moved IsSidechainCeased to sidechain.h
      Moved hadleCeasedSidechains to CCoins + UTs
      Moved RevertCeasingScs to CCoins + UTs
      Moved unregister Sc and cert to CCoins.
      Enabled calls in code
      Completed UTs + fixed bugs
      Removed sidechain_handler
      Introduced traces to fix py tests
      Fixed handling of sc ending on epoch 1
      Fixed check on BlockUndo in DisconnectBlock
      Fixed following alsala's code review
      Fixup cert output maturity
      Fixup faulty UTs automatic merge
      Refactored cert creation utils in UTs
      Added CeasedSidechains UTs for pureBwt, noBwt and EmptyCerti Ceased coins
      Minor cleanup
      Splitted CCoins::FromTx into tx and certs
      Removed UpdateCoins with dummy CTxUndo
      Splitted UpdateCoins into tx and cert versions
      Removed getScId method from TxBase and Tx
      Renamed  queryScIds to GetScIds
      Introduced bwtMaturityHeight to sub for OriginScId
      Amended blockUndo to host bwtMaturityHeight
      Filled up bwtMaturityHeight
      Removed originScId and used nBwtMaturityHeight only
      Removed GetObjHash and moved AcceptToMemoryPool to CWalletObjectBase
      Removed Init method from MerkleAbstractBase, absorbed into ctor
      Cleared up CMerkleTx/Cert ctors calls in CWalletTx/Cert
      Moved CMerkleTX/Cert serialization to CWalletTx/Cert
      Removed CMerkleTx/Cert dependency from MerkleAbstractBase. Introduced CWalletTx/Cert dependency on MerkleAbstractBase
      Removed CMerkleTx/Cert
      Merged MerkleAbstractBase class into CWalletObjBase
      Renamed CWalletObjBase to CWalletTransactioBase
      Cleaned up CWalletTransactionBase ctors
      Removing CWalletTransactionBase dependency from CTransactionBase
      Fixed up copy ctor && assignement op in CWalletTx/Cert. Regression green
      First chunck of fixes following code review
      Fix following code review
      Completed fixes, green regression
      Fixed minor compilation error
      Reintroduced change to CWalletTransactionBase::IsOutputMature
      Changes following code review
      Made ContextualChecks polymorphic only
      Moved CheckBlockAtHeight to ContextualCheck
      Txs/Certs checks renaming and aggregation
      Moved CBackwardTransferOut to certificate.h
      Fixed checkInputsInteraction
      Moved output maturity checks to CCoins class
      Refactored isOutputMature and added UTs
      Substituted IsOutputMature with GetMaturityHeightForOutput
      Added asserts on GetOutputMaturityHeight in mainchain
      Fixed ApplyTxInUndo + Added UT for cert coin reconstruction from blockUndo
      Moving maturity heights to CWalletTx/certs
      Added signal for ceased bwt in Wallet
      Removed outMaturityHeight from Wallet
      Extended cert signal to notice bwtMaturityDepth to wallets
      Stored bwt related attributes for CWalletCert
      Cleanup ctors of transaction related classes (Base/Tx-Cert/WalletTx-Cert/Mutables)
      Fixed following Alsala's code review
      Fixed UpdateCoins for certs
      Minor refactoring of CCoins serialization
      Moved ceased bwt undo operations into CVoidedCertUndo
      Minor fixes following code-review
      Renamed attributes in CBlockUndo/ScUndoData
      Moved LastCertEpoch/Hash to CTxUndo, out of ScUndoData
      Introduced nFirstBwtPos in CCoins and BlockUndo. Store full coin only for fully spent coins
      Remove duplicated nFirstBwtPos in CVoidedCertUndo
      Minor fix for faulty conflict resolution
      Introduced calls to nFirstBwtPos in CCoins
      Handled nFirstBwtPos for coins from txs
      Minor fixes following code-review
      Renamed cache members to host sidechains events
      Added scheduling of maturing scCreation amounts
      Refactored ScheduleSidechainEvent for certs
      Refactored schedule methods, handled cross-deletion of fwds and ceasing events
      Handled cancel schedule event for maturing creation amount
      Introduced scheduling of fwd amount maturing
      Added handling of maturing amounts
      Added handling of revering maturing amounts
      Moved UTs from ApplyMatureBalances/RestoreImmatureBalances HandleSidechainEvents/RevertSidechainEvents.
      Fixup typos and add traces
      Fixed double fwd handling
      Renamed gtest
      Added UTs for double fwds handling
      Improved traces
      Minor fixes following code-review
      Introduced IsBackwardTransfer on CTx/CScCert
      Drastically reduced the reads from IsFromBackwardTransfer flag
      Fixes following code-review
      Refactored cert deserialization + removed default value for IsFromBackwardTransfer flag
      Minor fix to addBwt member
      Introduced getters for vout mutCTx and mutCCert
      Defined stricter interface for output operations on MutTxBase
      Specialized ops on vout for MutCert to track nFirstBwtPos
      Removed isFromBackwardTrasfer flag
      Minor renaming + comments
      Moving voiding cert signal to connectTip
      Renaming and cleanup
      Introduced dummyVoidedCert on dummy calls
      Variables cleanup
      Consuming sidechain events upon handling and recreating them on revert
      Optimized blockUndo for ceased Certs
      Forbid sidechain event recreation if no scIds are present
      Added check on certificate height
      Removed misleading comment
      Fixed and UTed resizeOut and resizeBwt on CTx and CCert
      Removed BWT_POS_UNSET from Certs [used for Coins only]
      Turned CWalletTx/Cert into wrappers for Tx/Cert [rather than inheriting from them]
      Adding UTs for walletDb
      Fixed up wallet UTs
      Introduced ModifySidechain for in-place changes to sidechain cache
      Introduced AccessSidechain/ModifySidechainEvents for in-place changes to sidechainEvents cache
      UTs fixes following code review
      Fixes following Alsala's review. Bi-Mapped null CTxOut to null CBackwardTransferOut
      Minor fixup in UTs
      Introducing UTs for tx mature credit calculations
      Fixed up CWalletTx/Cert association to block in UTs
      Added UT for GetCredit on CoinBase
      Fixed credit maturity calculations for coinbase
      Added UTs for GetCredit on Certs + fix maturity
      Update txCreationUtils::createCertificate and removed its default paramenters
      Complete UTs for maturity of non-voided certs
      Completed UTs for maturity of voided certs
      Fixed up credit calculations and UTed GetImmatureCredit
      Added UTs for new sync signals + fixed SyncVoidedCert
      Improved const correctness of CWalletTx/Cert
      Minor UT change following code review
      Improved const correctness of CWalletTx/Cert
      Improved const correctness of CWalletTx/Cert
      Minor optimization following final code review
      Added UTs for GetScInfo with scCreation in mempool
      Optimized getScIds on levelDb
      Minor improvement following code review
      Fixup mac compilation
      Fixed up getscinfo for ceased sidechains
      Added catchall scId and onlyAlive parameters on getScInfo
      Fixed sidechain gtest on windows
      fixed UTs independence cleaning up global chainActive
      Minor fixes following clang compilation
      Refactored certs spending dependencies checks
      Moved upward check on certs dependencies
      Fixed mistakes in cert quality conflicts handling refactoring
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

alsala (20):
      tls py test: fixed setting of unsupported cipher; fixed tls1.3 protocol connection
      Fixed typo in a comment
      Update src/net.h
      Update src/init.cpp
      As/issue 167 (#200)
      Update qa/rpc-tests/sc_rpc_cmds_json_output.py
      Update qa/rpc-tests/sc_rpc_cmds_json_output.py
      Update qa/rpc-tests/sc_rpc_cmds_json_output.py
      Update qa/rpc-tests/sc_rpc_cmds_json_output.py
      Update qa/rpc-tests/sc_rpc_cmds_json_output.py
      Update qa/rpc-tests/sc_rpc_cmds_json_output.py
      Update qa/rpc-tests/sc_rpc_cmds_json_output.py
      Update qa/rpc-tests/sc_rpc_cmds_json_output.py
      Update qa/rpc-tests/sc_rpc_cmds_json_output.py
      Update payment-api.md
      Modification for ws get_top_quality_certificate for not relying on tx… (#214)
      As/issue 164 (#215)
      As/issue 211 (#213)
      Automatic fee computation for SC related rpc commands (#207)
      As/issue 194 (#197)

ca333 (1):
      update libsodium to v1.0.18

cronicc (113):
      Update README.md
      Update README.md
      Set version 2.1.0-beta1
      Add sidechains-testnet seed nodes
      Change PROTOCOL_VERSION, MIN_PEER_PROTO_VERSION to 170003
      Change testnet default port to 20033
      Set checkpoint block 655826 splitting from public testnet3
      Set sidechain-beta_v1 hardfork activation block 657000
      Disable OSX CI builders for now
      Set checkpoint block 657000, activation block of sidechain hard fork
      Update man pages
      Add 2.1.0-beta1 changelog
      Set version to v2.1.0-beta2
      Don't consider hashreserved if blockversion != 0x3
      Set version to v2.1.0-beta3
      Add src/zendoo/mcTest to .gitignore
      Bump version to 2.1.0-beta4
      Regenerate man pages
      Add 2.1.0-beta4 release notes
      Update README.md
      Rename org to HorizenOfficial
      Update README.md
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
      Install b2 CLI using pip3
      Add --legacy-cpu support to MacOS build script
      Add --enable-address-indexing option to build scripts
      Build with --legacy-cpu on MacOS (CPU flags not available in travis-ci)
      Add --legacy-cpu support to MacOS build script #2
      Update python interpreter workaround for MacOS tests
      Fix unbound var on linux
      Install python3 version of b2 in linux Dockerfiles
      Allow passing MAKEFLAGS variable through travis env
      Lint ci shell scripts
      Fix MacOS test stage and intermittent build stage error
      Enable build of AddressIndexing binaries in CI
      Split MacOS RPC tests over 10 workers
      websocket server: Allow listening on non-localhost addresses again
      Prepare for move back to HorizenOfficial/zen.git
      Revert "Added an assert in init.cpp if zend is run in main net"
      Update testnet and mainnet checkpoint blocks
      Set fork8 activation at block height 1047624 (60m after 2.0.24 deprecation)
      Set version 3.0.0
      Zencash -> Horizen
      Update debian info
      Update manpages
      Set deprecation height 1119000/2022-04
      Update README.md
      Update zendoo-mc-cryptolib to v0.2.0
      Sync zendoo-mc-cryptolib dependencies:
      Modify debian package script to rename legacy cpu packages

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

pier (1):
      use original rustzcash source

pierstab (10):
      sidechain ws connector
      add socket close
      websocket sidechain interface implementation
      change from fromHash-fromHeight to afterHash-afterHeight, change error type in the request, add msgId to the request keep reference in the response messages, add method getBlockHashes, change method name getblock->getBlock, getblocks->getBlocks
      change indent
      change indent, clean up unused code
      minor change indent
      websocket add msgId to error messages
      websocket add getSyncInfo (type = 5) command
      websocket add comment

xgarreau (1):
      Update README.md

​Felix Ippolitov (1):
      Fix build with gcc10

