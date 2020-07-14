Changelog
=========

Alberto Sala (197):
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
      Support for creating certificate via websocket channel; added default value if no fee is specified
      Added help lines for websocket connection options; added examples in zen.conf
      Changed implementation and signature of OrderedTxWithInputsMap method for using ordered tx map data member in wallet
      Fixes after code review
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

DanieleDiBenedetto (13):
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

Iozhytsia Oleksandr (4):
      Rust version and libzendoo dependencies updated for windows target platform.
      Zendoo mc test build fix for both linux and windows platforms.
      Certificate proof data logs added.
      fpic added for zendoo_mctest to make it compile for linux.

MarcoOl94 (5):
      Refactored and reintroduced the sc_create's python tests
      Removed old test from rpc-test list
      Extended rawtransactions' test to verify SC creation and FT
      Implemented requirements requested by Albene
      Removed unused variable

Oleksandr Iozhytsia (1):
      Wallet fix and certificate creation rpc changes.

abi87 (352):
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

cronicc (11):
      Update README.md
      Update README.md
      Set version 2.1.0-beta1
      Set version to v2.1.0-beta2
      Don't consider hashreserved if blockversion != 0x3
      Set version to v2.1.0-beta3
      Disable aria2 installation on MacOS and use wget as alternative downloader
      CI fixes:
      Add src/zendoo/mcTest to .gitignore
      Bump version to 2.1.0-beta4
      Regenerate man pages

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

