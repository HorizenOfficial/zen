DIST_SUBDIRS = secp256k1 univalue

AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS) $(SAN_LDFLAGS) $(HARDENED_LDFLAGS)
AM_CXXFLAGS = $(SAN_CXXFLAGS) $(HARDENED_CXXFLAGS) $(ERROR_CXXFLAGS) -ffunction-sections -fdata-sections
AM_CPPFLAGS = $(HARDENED_CPPFLAGS)
EXTRA_LIBRARIES =

if EMBEDDED_LEVELDB
LEVELDB_CPPFLAGS += -I$(srcdir)/leveldb/include
LEVELDB_CPPFLAGS += -I$(srcdir)/leveldb/helpers/memenv
LIBLEVELDB += $(builddir)/leveldb/libleveldb.a
LIBMEMENV  += $(builddir)/leveldb/libmemenv.a

# NOTE: This dependency is not strictly necessary, but without it make may try to build both in parallel, which breaks the LevelDB build system in a race
$(LIBLEVELDB): $(LIBMEMENV)

$(LIBLEVELDB) $(LIBMEMENV):
	@echo "Building LevelDB ..." && $(MAKE) -C $(@D) $(@F) CXX="$(CXX)" \
	  CC="$(CC)" PLATFORM=$(TARGET_OS) AR="$(AR)" $(LEVELDB_TARGET_FLAGS) \
          OPT="$(AM_CXXFLAGS) $(PIE_FLAGS) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(COVERAGE_FLAGS) -D__STDC_LIMIT_MACROS"
endif

BITCOIN_CONFIG_INCLUDES=-I$(builddir)/config
BITCOIN_INCLUDES=-I$(builddir) -I$(builddir)/obj $(BDB_CPPFLAGS) $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS)

BITCOIN_INCLUDES += -I$(srcdir)/secp256k1/include
BITCOIN_INCLUDES += -I$(srcdir)/snark
BITCOIN_INCLUDES += -I$(srcdir)/snark/libsnark
BITCOIN_INCLUDES += -I$(srcdir)/univalue/include

LIBBITCOIN_SERVER=libbitcoin_server.a
LIBBITCOIN_WALLET=libbitcoin_wallet.a
LIBBITCOIN_COMMON=libbitcoin_common.a
LIBBITCOIN_CLI=libbitcoin_cli.a
LIBBITCOIN_UTIL=libbitcoin_util.a
LIBBITCOIN_CRYPTO=crypto/libbitcoin_crypto.a
LIBSECP256K1=secp256k1/libsecp256k1.la
LIBSNARK=snark/libsnark.a
LIBUNIVALUE=univalue/libunivalue.la
LIBZCASH=libzcash.a
LIBZENCASH=libzencash.a

$(LIBSECP256K1): $(wildcard secp256k1/src/*) $(wildcard secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)


LIBSNARK_CXXFLAGS = $(CXXFLAGS) $(AM_CXXFLAGS) $(PIC_FLAGS) -DBINARY_OUTPUT -DNO_PT_COMPRESSION=1 -fstack-protector-all
LIBSNARK_CONFIG_FLAGS = CURVE=ALT_BN128 NO_PROCPS=1 NO_DOCS=1 STATIC=1 NO_SUPERCOP=1 FEATUREFLAGS=-DMONTGOMERY_OUTPUT NO_COPY_DEPINST=1 NO_COMPILE_LIBGTEST=1
if HAVE_OPENMP
LIBSNARK_CONFIG_FLAGS += MULTICORE=1
endif
LIBSNARK_OPTFLAGS =-O2
if !IS_ARM64
LIBSNARK_OPTFLAGS +=-march=x86-64
endif

$(LIBSNARK): $(wildcard snark/src/*)
	$(AM_V_at) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" CXXFLAGS="$(LIBSNARK_CXXFLAGS)" $(MAKE) $(AM_MAKEFLAGS) -C snark/ DEPINST="$(LIBSNARK_DEPINST)" $(LIBSNARK_CONFIG_FLAGS) OPTFLAGS="$(LIBSNARK_OPTFLAGS)"

libsnark-tests: $(wildcard snark/src/*)
	$(AM_V_at) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" CXXFLAGS="$(LIBSNARK_CXXFLAGS)" $(MAKE) $(AM_MAKEFLAGS) -C snark/ check DEPINST="$(LIBSNARK_DEPINST)" $(LIBSNARK_CONFIG_FLAGS) OPTFLAGS="$(LIBSNARK_OPTFLAGS)"

$(LIBUNIVALUE): $(wildcard univalue/lib/*) $(wildcard univalue/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)


# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES = \
  crypto/libbitcoin_crypto.a \
  libbitcoin_util.a \
  libbitcoin_common.a \
  libbitcoin_server.a \
  libbitcoin_cli.a \
  libzcash.a \
  libzencash.a 

if ENABLE_WALLET
BITCOIN_INCLUDES += $(BDB_CPPFLAGS)
EXTRA_LIBRARIES += libbitcoin_wallet.a
endif
if ENABLE_ZMQ
EXTRA_LIBRARIES += libbitcoin_zmq.a
endif
if ENABLE_PROTON
EXTRA_LIBRARIES += libbitcoin_proton.a
endif

if BUILD_BITCOIN_LIBS
lib_LTLIBRARIES = libzcashconsensus.la
LIBZCASH_CONSENSUS=libzcashconsensus.la
else
LIBZCASH_CONSENSUS=
endif

bin_PROGRAMS =
noinst_PROGRAMS =
TESTS =

if BUILD_BITCOIND
  bin_PROGRAMS += zend
endif

if BUILD_BITCOIN_UTILS
  bin_PROGRAMS += zen-cli zen-tx
endif

LIBZCASH_H = \
  zcash/IncrementalMerkleTree.hpp \
  zcash/NoteEncryption.hpp \
  zcash/Address.hpp \
  zcash/JoinSplit.hpp \
  zcash/Note.hpp \
  zcash/prf.h \
  zcash/Proof.hpp \
  zcash/util.h \
  zcash/Zcash.h

.PHONY: FORCE  cargo-build collate-libsnark check-symbols check-security
# bitcoin core #
BITCOIN_CORE_H = \
  addressindex.h \
  addrman.h \
  amount.h \
  amqp/amqpabstractnotifier.h \
  amqp/amqpconfig.h \
  amqp/amqpnotificationinterface.h \
  amqp/amqppublishnotifier.h \
  amqp/amqpsender.h \
  arith_uint256.h \
  asyncrpcoperation.h \
  asyncrpcqueue.h \
  base58.h \
  bloom.h \
  chain.h \
  chainparams.h \
  chainparamsbase.h \
  chainparamsseeds.h \
  checkpoints.h \
  checkqueue.h \
  clientversion.h \
  coincontrol.h \
  coins.h \
  compat.h \
  compat/byteswap.h \
  compat/endian.h \
  compat/sanity.h \
  compressor.h \
  consensus/consensus.h \
  consensus/params.h \
  consensus/validation.h \
  consolecolors.h \
  core_io.h \
  core_memusage.h \
  deprecation.h \
  hash.h \
  httprpc.h \
  httpserver.h \
  init.h \
  key.h \
  keystore.h \
  leveldbwrapper.h \
  limitedmap.h \
  main.h \
  memusage.h \
  merkleblock.h \
  metrics.h \
  miner.h \
  mruset.h \
  net.h \
  netbase.h \
  noui.h \
  paymentdisclosure.h \
  paymentdisclosuredb.h \
  policy/fees.h \
  pow.h \
  primitives/block.h \
  primitives/transaction.h \
  protocol.h \
  pubkey.h \
  random.h \
  reverselock.h \
  rpc/client.h \
  rpc/protocol.h \
  rpc/server.h \
  rpc/utils.h \
  scheduler.h \
  script/interpreter.h \
  script/script.h \
  script/script_error.h \
  script/sigcache.h \
  script/sign.h \
  script/standard.h \
  serialize.h \
  spentindex.h \
  streams.h \
  support/allocators/secure.h \
  support/allocators/zeroafterfree.h \
  support/cleanse.h \
  support/events.h \
  support/pagelocker.h \
  sync.h \
  threadinterrupt.h \
  threadsafety.h \
  timedata.h \
  timestampindex.h \
  tinyformat.h \
  torcontrol.h \
  txdb.h \
  txmempool.h \
  ui_interface.h \
  uint256.h \
  uint252.h \
  undo.h \
  util.h \
  utilmoneystr.h \
  utilstrencodings.h \
  utiltime.h \
  validationinterface.h \
  version.h \
  wallet/asyncrpcoperation_mergetoaddress.h \
  wallet/asyncrpcoperation_sendmany.h \
  wallet/asyncrpcoperation_shieldcoinbase.h \
  wallet/crypter.h \
  wallet/db.h \
  wallet/wallet.h \
  wallet/wallet_ismine.h \
  wallet/walletdb.h \
  zmq/zmqabstractnotifier.h \
  zmq/zmqconfig.h\
  zmq/zmqnotificationinterface.h \
  zmq/zmqpublishnotifier.h

LIBZENCASH_H = \
    zen/utiltls.h\
    zen/forkmanager.h\
    zen/replayprotectionlevel.h 	

obj/build.h: FORCE
	@$(MKDIR_P) $(builddir)/obj
	@$(top_srcdir)/share/genbuild.sh $(abs_top_builddir)/src/obj/build.h \
	  $(abs_top_srcdir)
libbitcoin_util_a-clientversion.$(OBJEXT): obj/build.h

# server: zcashd
libbitcoin_server_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libbitcoin_server_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
libbitcoin_server_a_SOURCES = \
  addrman.cpp \
  asyncrpcoperation.cpp \
  asyncrpcqueue.cpp \
  bloom.cpp \
  chain.cpp \
  checkpoints.cpp \
  deprecation.cpp \
  httprpc.cpp \
  httpserver.cpp \
  init.cpp \
  leveldbwrapper.cpp \
  main.cpp \
  merkleblock.cpp \
  metrics.cpp \
  miner.cpp \
  net.cpp \
  noui.cpp \
  paymentdisclosure.cpp \
  paymentdisclosuredb.cpp \
  policy/fees.cpp \
  pow.cpp \
  rest.cpp \
  rpc/blockchain.cpp \
  rpc/mining.cpp \
  rpc/misc.cpp \
  rpc/net.cpp \
  rpc/rawtransaction.cpp \
  rpc/server.cpp \
  rpc/utils.cpp \
  sc/asyncproofverifier.cpp \
  sc/sidechainTxsCommitmentBuilder.cpp \
  sc/sidechainTxsCommitmentGuard.cpp \
  sc/sidechaintypes.cpp \
  sc/proofverifier.cpp \
  sc/sidechain.cpp \
  sc/sidechainrpc.cpp \
  timedata.cpp \
  torcontrol.cpp \
  txdb.cpp \
  txmempool.cpp \
  validationinterface.cpp \
  $(BITCOIN_CORE_H) \
  $(LIBZCASH_H) \
  $(LIBZENCASH_H)

if ENABLE_ZMQ
LIBBITCOIN_ZMQ=libbitcoin_zmq.a

libbitcoin_zmq_a_CPPFLAGS = $(BITCOIN_INCLUDES) $(ZMQ_CFLAGS)
libbitcoin_zmq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
libbitcoin_zmq_a_SOURCES = \
  zmq/zmqabstractnotifier.cpp \
  zmq/zmqnotificationinterface.cpp \
  zmq/zmqpublishnotifier.cpp
endif

if ENABLE_PROTON
LIBBITCOIN_PROTON=libbitcoin_proton.a

libbitcoin_proton_a_CPPFLAGS = $(BITCOIN_INCLUDES)
libbitcoin_proton_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
libbitcoin_proton_a_SOURCES = \
  amqp/amqpabstractnotifier.cpp \
  amqp/amqpnotificationinterface.cpp \
  amqp/amqppublishnotifier.cpp
endif

# wallet: zcashd, but only linked when wallet enabled
libbitcoin_wallet_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_wallet_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
libbitcoin_wallet_a_SOURCES = \
  utiltest.cpp \
  utiltest.h \
  zcbenchmarks.cpp \
  zcbenchmarks.h \
  wallet/asyncrpcoperation_mergetoaddress.cpp \
  wallet/asyncrpcoperation_sendmany.cpp \
  wallet/asyncrpcoperation_shieldcoinbase.cpp \
  wallet/crypter.cpp \
  wallet/db.cpp \
  paymentdisclosure.cpp \
  paymentdisclosuredb.cpp \
  wallet/rpcdisclosure.cpp \
  wallet/rpcdump.cpp \
  wallet/rpcwallet.cpp \
  wallet/wallet.cpp \
  wallet/wallet_ismine.cpp \
  wallet/walletdb.cpp \
  $(BITCOIN_CORE_H) \
  $(LIBZCASH_H)

# crypto primitives library
crypto_libbitcoin_crypto_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_CONFIG_INCLUDES)
crypto_libbitcoin_crypto_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
crypto_libbitcoin_crypto_a_SOURCES = \
  crypto/common.h \
  crypto/equihash.cpp \
  crypto/equihash.h \
  crypto/equihash.tcc \
  crypto/hmac_sha256.cpp \
  crypto/hmac_sha256.h \
  crypto/hmac_sha512.cpp \
  crypto/hmac_sha512.h \
  crypto/ripemd160.cpp \
  crypto/ripemd160.h \
  crypto/sha1.cpp \
  crypto/sha1.h \
  crypto/sha256.cpp \
  crypto/sha256.h \
  crypto/sha512.cpp \
  crypto/sha512.h

if ENABLE_MINING
EQUIHASH_TROMP_SOURCES = \
  pow/tromp/equi_miner.h \
  pow/tromp/equi.h \
  pow/tromp/osx_barrier.h

crypto_libbitcoin_crypto_a_CPPFLAGS += \
  -DEQUIHASH_TROMP_ATOMIC
crypto_libbitcoin_crypto_a_SOURCES += \
  ${EQUIHASH_TROMP_SOURCES}
endif

# common: shared between zcashd and non-server tools
libbitcoin_common_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_common_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
libbitcoin_common_a_SOURCES = \
  amount.cpp \
  arith_uint256.cpp \
  base58.cpp \
  base58_codec.cpp \
  chainparams.cpp \
  sc/proofverifier.cpp \
  coins.cpp \
  compressor.cpp \
  core_read.cpp \
  core_write.cpp \
  hash.cpp \
  key.cpp \
  keystore.cpp \
  netbase.cpp \
  primitives/block.cpp \
  primitives/transaction.cpp \
  primitives/certificate.cpp \
  protocol.cpp \
  pubkey.cpp \
  scheduler.cpp \
  script/interpreter.cpp \
  script/script.cpp \
  script/sigcache.cpp \
  script/script_error.cpp \
  script/sign.cpp \
  script/standard.cpp \
  $(BITCOIN_CORE_H) \
  $(LIBZCASH_H)

# util: shared between all executables.
# This library *must* be included to make sure that the glibc
# backward-compatibility objects and their sanity checks are linked.
libbitcoin_util_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_util_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
libbitcoin_util_a_SOURCES = \
  support/pagelocker.cpp \
  chainparamsbase.cpp \
  clientversion.cpp \
  compat/glibc_sanity.cpp \
  compat/glibcxx_sanity.cpp \
  compat/strnlen.cpp \
  random.cpp \
  rpc/protocol.cpp \
  support/cleanse.cpp \
  sync.cpp \
  threadinterrupt.cpp \
  uint256.cpp \
  util.cpp \
  utilmoneystr.cpp \
  utilstrencodings.cpp \
  utiltime.cpp \
  $(BITCOIN_CORE_H) \
  $(LIBZCASH_H)


if GLIBC_BACK_COMPAT
libbitcoin_util_a_SOURCES += compat/glibc_compat.cpp
endif

# cli: zcash-cli
libbitcoin_cli_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_cli_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
libbitcoin_cli_a_SOURCES = \
  rpc/client.cpp \
  $(BITCOIN_CORE_H) \
  $(LIBZCASH_H)

nodist_libbitcoin_util_a_SOURCES = $(srcdir)/obj/build.h
#

dumper_SOURCES = dumper.cpp
dumper_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
dumper_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
dumper_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(COVERAGE_FLAGS)

dumper_LDADD = \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_COMMON) \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBZCASH) \
  $(LIBZENCASH) \
  $(LIBSNARK) \
  $(LIBLEVELDB) \
  $(LIBMEMENV) \
  $(LIBSECP256K1) \
  libbitcoin_wallet.a \
  $(BOOST_LIBS) \
  $(BDB_LIBS) \
  $(SSL_LIBS) \
  $(CRYPTO_LIBS) \
  $(EVENT_PTHREADS_LIBS) \
  $(EVENT_LIBS) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBZCASH_LIBS)

if ENABLE_ZMQ
dumper_LDADD += $(LIBBITCOIN_ZMQ) $(ZMQ_LIBS)
endif

noinst_PROGRAMS += \
  dumper

# bitcoind binary #
zend_SOURCES = bitcoind.cpp
zend_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
zend_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
zend_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(COVERAGE_FLAGS)

if TARGET_DARWIN
zend_LDFLAGS += -framework CoreFoundation
endif

if TARGET_WINDOWS
zend_SOURCES += bitcoind-res.rc
endif

zend_LDADD = \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_COMMON) \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBZCASH) \
  $(LIBZENCASH) \
  $(LIBSNARK) \
  $(LIBLEVELDB) \
  $(LIBMEMENV) \
  $(LIBSECP256K1) 

if ENABLE_ZMQ
zend_LDADD += $(LIBBITCOIN_ZMQ) $(ZMQ_LIBS)
endif

if ENABLE_WALLET
zend_LDADD += libbitcoin_wallet.a
endif

zend_LDADD += \
  $(BOOST_LIBS) \
  $(BDB_LIBS) \
  $(SSL_LIBS) \
  $(CRYPTO_LIBS) \
  $(EVENT_PTHREADS_LIBS) \
  $(EVENT_LIBS) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBZCASH_LIBS)

if ENABLE_PROTON
zend_LDADD += $(LIBBITCOIN_PROTON) $(PROTON_LIBS)
endif

# bitcoin-cli binary #
zen_cli_SOURCES = bitcoin-cli.cpp
zen_cli_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS)
zen_cli_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
zen_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(COVERAGE_FLAGS)

if TARGET_WINDOWS
zen_cli_SOURCES += bitcoin-cli-res.rc
endif

zen_cli_LDADD = \
  $(LIBBITCOIN_CLI) \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_UTIL) \
  $(BOOST_LIBS) \
  $(SSL_LIBS) \
  $(CRYPTO_LIBS) \
  $(EVENT_LIBS) \
  $(LIBZCASH) \
  $(LIBZENCASH) \
  $(LIBSNARK) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBZCASH_LIBS) 


# zen-tx binary #
zen_tx_SOURCES = \
    bitcoin-tx.cpp \
    primitives/transaction.cpp \
    primitives/certificate.cpp \
    sc/sidechainTxsCommitmentBuilder.cpp \
    sc/sidechainTxsCommitmentGuard.cpp \
    sc/sidechaintypes.cpp \
    sc/proofverifier.cpp \
    sc/sidechain.cpp \
    coins.cpp \
    script/interpreter.cpp \
    script/standard.cpp
zen_tx_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS) -DBITCOIN_TX
zen_tx_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) -DBITCOIN_TX $(COVERAGE_FLAGS)
zen_tx_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(COVERAGE_FLAGS)

if TARGET_WINDOWS
zen_tx_SOURCES += bitcoin-tx-res.rc
endif

# FIXME: Is libzcash needed for zen_tx?
zen_tx_LDADD = \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_UTIL) \
  $(LIBSECP256K1) \
  $(LIBZCASH) \
  $(LIBZENCASH) \
  $(LIBSNARK) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBZCASH_LIBS) 

zen_tx_LDADD += $(BOOST_LIBS) $(CRYPTO_LIBS)


# zcash protocol primitives #
libzcash_a_SOURCES = \
  zcash/IncrementalMerkleTree.cpp \
  zcash/NoteEncryption.cpp \
  zcash/Address.cpp \
  zcash/JoinSplit.cpp \
  zcash/Proof.cpp \
  zcash/Note.cpp \
  zcash/prf.cpp \
  zcash/util.cpp \
  zcash/circuit/commitment.tcc \
  zcash/circuit/gadget.tcc \
  zcash/circuit/merkle.tcc \
  zcash/circuit/note.tcc \
  zcash/circuit/prfs.tcc \
  zcash/circuit/utils.tcc

libzcash_a_CPPFLAGS = $(AM_CPPFLAGS) $(PIC_FLAGS) -DBINARY_OUTPUT -DCURVE_ALT_BN128 -DBOOST_SPIRIT_THREADSAFE -DSTATIC $(BITCOIN_INCLUDES)
libzcash_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libzcash_a_LDFLAGS = $(AM_LDFLAGS)

libzcash_a_CPPFLAGS += -DMONTGOMERY_OUTPUT

# zcashconsensus library #
if BUILD_BITCOIN_LIBS
include_HEADERS = script/zcashconsensus.h
libzcashconsensus_la_SOURCES = \
  crypto/equihash.cpp \
  crypto/hmac_sha512.cpp \
  crypto/ripemd160.cpp \
  crypto/sha1.cpp \
  crypto/sha256.cpp \
  crypto/sha512.cpp \
  hash.cpp \
  primitives/transaction.cpp \
  pubkey.cpp \
  script/zcashconsensus.cpp \
  script/interpreter.cpp \
  script/script.cpp \
  uint256.cpp \
  utilstrencodings.cpp

if GLIBC_BACK_COMPAT
  libzcashconsensus_la_SOURCES += compat/glibc_compat.cpp
endif

libzcashconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
libzcashconsensus_la_LIBADD = $(LIBSECP256K1)
libzcashconsensus_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj -I$(srcdir)/secp256k1/include -DBUILD_BITCOIN_INTERNAL
libzcashconsensus_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)

endif
#

# zencash-specific sources #
libzencash_a_SOURCES = \
    zen/utiltls.cpp\
    zen/forkmanager.cpp\
    zen/forks/fork.cpp\
    zen/forks/fork0_originalfork.cpp\
    zen/forks/fork1_chainsplitfork.cpp\
    zen/forks/fork2_replayprotectionfork.cpp\
    zen/forks/fork3_communityfundandrpfixfork.cpp\
    zen/forks/fork4_nulltransactionfork.cpp\
    zen/forks/fork5_shieldfork.cpp\
    zen/forks/fork6_timeblockfork.cpp\
    zen/forks/fork7_replayprotectionfixfork.cpp\
    zen/forks/fork8_sidechainfork.cpp\
    zen/forks/fork9_sidechainversionfork.cpp\
    zen/forks/fork10_nonceasingsidechainfork.cpp\
    zen/forks/fork11_shieldedpooldeprecationfork.cpp\
    zen/forks/fork12_shieldedpoolremovalfork.cpp\
    zen/forks/fork13_stoptransactionsfork.cpp\
    zen/tlsmanager.cpp\
    zen/delay.cpp \
    zen/websocket_server.cpp

libzencash_a_CPPFLAGS := $(libzencash_a_CPPFLAGS) -fPIC -DBINARY_OUTPUT -DMONTGOMERY_OUTPUT -DCURVE_ALT_BN128 -DBOOST_SPIRIT_THREADSAFE -DHAVE_BUILD_INFO -D__STDC_FORMAT_MACROS $(HARDENED_CPPFLAGS) -pipe -O1 -g  -Wstack-protector -fstack-protector-all -fPIE -DSTATIC $(BITCOIN_INCLUDES)
if HAVE_OPENMP
libzencash_a_CPPFLAGS += -DMULTICORE -fopenmp
endif

libzencash_a_CXXFLAGS = $(HARDENED_CXXFLAGS) -fwrapv -fno-strict-aliasing $(COVERAGE_FLAGS)

libzencash_a_LDFLAGS = $(HARDENED_LDFLAGS)


noinst_PROGRAMS += \
  zendoo/mcTest
  
# tool for testing sidechain
zendoo_mcTest_SOURCES = \
    zendoo/mcTestCallMain.cpp \
    base58_codec.cpp \
    crypto/sha256.cpp \
    uint256.cpp

zendoo_mcTest_CPPFLAGS = $(AM_CPPFLAGS) $(EVENT_CFLAGS) $(PIC_FLAGS)
zendoo_mcTest_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(COVERAGE_FLAGS)
zendoo_mcTest_LDADD = $(ZENDOO_MCTEST_LIBS)
zendoo_mcTest_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(COVERAGE_FLAGS)

if TARGET_DARWIN
zendoo_mcTest_LDFLAGS += -framework CoreFoundation
endif


CLEANFILES = leveldb/libleveldb.a leveldb/libmemenv.a *.gcda *.gcno */*.gcno wallet/*/*.gcno zen/*/*.gcno

DISTCLEANFILES = obj/build.h

EXTRA_DIST = leveldb snark

clean-local:
	-$(MAKE) -C leveldb clean
	-$(MAKE) -C secp256k1 clean
	-$(MAKE) -C snark clean
	-$(MAKE) -C univalue clean
	rm -f leveldb/*/*.gcno leveldb/helpers/memenv/*.gcno
	-rm -f config.h

.rc.o:
	@test -f $(WINDRES)
	$(AM_V_GEN) $(WINDRES) -DWINDRES_PREPROC -i $< -o $@

.mm.o:
	$(AM_V_CXX) $(OBJCXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(AM_CXXFLAGS) $(PIE_FLAGS) $(CXXFLAGS) -c -o $@ $<

check-symbols: $(bin_PROGRAMS)
if GLIBC_BACK_COMPAT
	@echo "Checking glibc back compat of [$(bin_PROGRAMS)]..."
	$(AM_V_at) READELF=$(READELF) CPPFILT=$(CPPFILT) $(top_srcdir)/contrib/devtools/symbol-check.py $(bin_PROGRAMS)
endif

check-security: $(bin_PROGRAMS)
if HARDEN
	@echo "Checking binary security of [$(bin_PROGRAMS)]..."
	$(AM_V_at) READELF=$(READELF) OBJDUMP=$(OBJDUMP) $(top_srcdir)/contrib/devtools/security-check.py $(bin_PROGRAMS)
endif

%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(abspath $(<D) $<)

if ENABLE_TESTS
include Makefile.test.include
include Makefile.gtest.include
endif

include Makefile.zcash.include
