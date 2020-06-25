#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sodium.h>

#include "main.h"
#include "primitives/transaction.h"
#include "consensus/validation.h"

TEST(checktransaction_tests, check_vpub_not_both_nonzero) {
    CMutableTransaction tx;
    tx.nVersion = 2;

    {
        // Ensure that values within the joinsplit are well-formed.
        CMutableTransaction newTx(tx);
        CValidationState state;

        newTx.vjoinsplit.push_back(JSDescription());

        JSDescription *jsdesc = &newTx.vjoinsplit[0];
        jsdesc->vpub_old = 1;
        jsdesc->vpub_new = 1;

        EXPECT_FALSE(CheckTransactionWithoutProofVerification(newTx, state));
        EXPECT_EQ(state.GetRejectReason(), "bad-txns-vpubs-both-nonzero");
    }
}

class MockCValidationState : public CValidationState {
public:
    MOCK_METHOD5(DoS, bool(int level, bool ret,
             unsigned char chRejectCodeIn, std::string strRejectReasonIn,
             bool corruptionIn));
    MOCK_METHOD3(Invalid, bool(bool ret,
                 unsigned char _chRejectCode, std::string _strRejectReason));
    MOCK_METHOD1(Error, bool(std::string strRejectReasonIn));
    MOCK_CONST_METHOD0(IsValid, bool());
    MOCK_CONST_METHOD0(IsInvalid, bool());
    MOCK_CONST_METHOD0(IsError, bool());
    MOCK_CONST_METHOD1(IsInvalid, bool(int &nDoSOut));
    MOCK_CONST_METHOD0(CorruptionPossible, bool());
    MOCK_CONST_METHOD0(GetRejectCode, unsigned char());
    MOCK_CONST_METHOD0(GetRejectReason, std::string());
};


CMutableTransaction GetValidTransaction(int txVersion) {
    CMutableTransaction mtx;
	mtx.nVersion = txVersion;
    mtx.vin.resize(2);
    mtx.vin[0].prevout.hash = uint256S("0000000000000000000000000000000000000000000000000000000000000001");
    mtx.vin[0].prevout.n = 0;
    mtx.vin[1].prevout.hash = uint256S("0000000000000000000000000000000000000000000000000000000000000002");
    mtx.vin[1].prevout.n = 0;
    mtx.addOut(CTxOut(0,CScript()));
    mtx.addOut(CTxOut(0,CScript()));

    if (txVersion == SC_TX_VERSION)
    {     
	    mtx.vjoinsplit.clear();

        CTxScCreationOut cr_ccout;
        cr_ccout.nValue = 1.0;
        cr_ccout.withdrawalEpochLength = 111;
        mtx.vsc_ccout.push_back(cr_ccout);

        CTxForwardTransferOut ft_ccout;
        ft_ccout.nValue = 10.0;
        ft_ccout.scId = GetRandHash();
        mtx.vft_ccout.push_back(ft_ccout);
    }
    else
    {
		mtx.vjoinsplit.clear();
		mtx.vjoinsplit.push_back(JSDescription::getNewInstance(txVersion == GROTH_TX_VERSION));
		mtx.vjoinsplit.push_back(JSDescription::getNewInstance(txVersion == GROTH_TX_VERSION));
    
        mtx.vjoinsplit[0].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");
        mtx.vjoinsplit[0].nullifiers.at(1) = uint256S("0000000000000000000000000000000000000000000000000000000000000001");
        mtx.vjoinsplit[1].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000002");
        mtx.vjoinsplit[1].nullifiers.at(1) = uint256S("0000000000000000000000000000000000000000000000000000000000000003");
    }
    
    // Generate an ephemeral keypair.
    uint256 joinSplitPubKey;
    unsigned char joinSplitPrivKey[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(joinSplitPubKey.begin(), joinSplitPrivKey);
    mtx.joinSplitPubKey = joinSplitPubKey;

    // Compute the correct hSig.
    // TODO: #966.
    static const uint256 one(uint256S("0000000000000000000000000000000000000000000000000000000000000001"));
    // Empty output script.
    CScript scriptCode;
    CTransaction signTx(mtx);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);
    if (dataToBeSigned == one) {
        throw std::runtime_error("SignatureHash failed");
    }

    // Add the signature
    assert(crypto_sign_detached(&mtx.joinSplitSig[0], NULL,
                         dataToBeSigned.begin(), 32,
                         joinSplitPrivKey
                        ) == 0);
    return mtx;
}

CMutableScCertificate GetValidCertificate() {
    CMutableScCertificate mcert;
	mcert.nVersion = SC_CERT_VERSION;

    mcert.addOut(CTxOut(0.5,CScript()));
    mcert.addOut(CTxOut(1,CScript()));

    mcert.scId = GetRandHash();
    mcert.epochNumber = 3;
    mcert.endEpochBlockHash = GetRandHash();

    return mcert;
}

CMutableTransaction GetValidTransaction() {
	return GetValidTransaction(PHGR_TX_VERSION);
}

TEST(checktransaction_tests, valid_transparent_transaction) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.nVersion = 1;
    CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
}

TEST(checktransaction_tests, valid_sprout_transaction) {
    CMutableTransaction mtx = GetValidTransaction();
    CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
}

TEST(checktransaction_tests, BadVersionTooLow) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.nVersion = 0;

    CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-version-too-low", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_vin_empty) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.vin.resize(0);

    CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_vout_empty) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.resizeOut(0);

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_oversize) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.nVersion = 1;
    mtx.vjoinsplit.resize(0);
    mtx.vin[0].scriptSig = CScript();
    std::vector<unsigned char> vchData(520);
    for (unsigned int i = 0; i < 190; ++i)
        mtx.vin[0].scriptSig << vchData << OP_DROP;
    mtx.vin[0].scriptSig << OP_1;

    {
        // Transaction is just under the limit...
        CTransaction tx(mtx);
        CValidationState state;
        ASSERT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
    }

    // Not anymore!
    mtx.vin[1].scriptSig << vchData << OP_DROP;
    mtx.vin[1].scriptSig << OP_1;

    {
        CTransaction tx(mtx);
        ASSERT_EQ(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION), 100202);
    
        MockCValidationState state;
        EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-oversize", false)).Times(1);
        CheckTransactionWithoutProofVerification(tx, state);
    }
}

TEST(checktransaction_tests, bad_txns_vout_negative) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.getOut(0).nValue = -1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_vout_toolarge) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.getOut(0).nValue = MAX_MONEY + 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_txouttotal_toolarge_outputs) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.getOut(0).nValue = MAX_MONEY;
    mtx.getOut(1).nValue = 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_txouttotal_toolarge_joinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.getOut(0).nValue = 1;
    mtx.vjoinsplit[0].vpub_old = MAX_MONEY;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_txintotal_toolarge_joinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_new = MAX_MONEY - 1;
    mtx.vjoinsplit[1].vpub_new = MAX_MONEY - 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-txintotal-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_vpub_old_negative) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_old = -1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vpub_old-negative", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_vpub_new_negative) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_new = -1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vpub_new-negative", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_vpub_old_toolarge) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_old = MAX_MONEY + 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vpub_old-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_vpub_new_toolarge) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_new = MAX_MONEY + 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vpub_new-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_vpubs_both_nonzero) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_old = 1;
    mtx.vjoinsplit[0].vpub_new = 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vpubs-both-nonzero", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_inputs_duplicate) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vin[1].prevout.hash = mtx.vin[0].prevout.hash;
    mtx.vin[1].prevout.n = mtx.vin[0].prevout.n;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_joinsplits_nullifiers_duplicate_same_joinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");
    mtx.vjoinsplit[0].nullifiers.at(1) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-joinsplits-nullifiers-duplicate", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_joinsplits_nullifiers_duplicate_different_joinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");
    mtx.vjoinsplit[1].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-joinsplits-nullifiers-duplicate", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_cb_has_joinsplits) {
    CMutableTransaction mtx = GetValidTransaction();
    // Make it a coinbase.
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();

    mtx.vjoinsplit.resize(1);

    CTransaction tx(mtx);
    EXPECT_TRUE(tx.IsCoinBase());

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-cb-has-joinsplits", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_cb_empty_scriptsig) {
    CMutableTransaction mtx = GetValidTransaction();
    // Make it a coinbase.
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();

    mtx.vjoinsplit.resize(0);

    CTransaction tx(mtx);
    EXPECT_TRUE(tx.IsCoinBase());

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-cb-length", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_prevout_null) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vin[1].prevout.SetNull();

    CTransaction tx(mtx);
    EXPECT_FALSE(tx.IsCoinBase());

    MockCValidationState state;
    EXPECT_CALL(state, DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_invalid_joinsplit_signature) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.joinSplitSig[0] += 1;
    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-invalid-joinsplit-signature", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, non_canonical_ed25519_signature) {
    CMutableTransaction mtx = GetValidTransaction();

    // Check that the signature is valid before we add L
    {
        CTransaction tx(mtx);
        MockCValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
    }

    // Copied from libsodium/crypto_sign/ed25519/ref10/open.c
    static const unsigned char L[32] =
      { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 };

    // Add L to S, which starts at mtx.joinSplitSig[32].
    unsigned int s = 0;
    for (size_t i = 0; i < 32; i++) {
        s = mtx.joinSplitSig[32 + i] + L[i] + (s >> 8);
        mtx.joinSplitSig[32 + i] = s & 0xff;
    }

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-invalid-joinsplit-signature", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

// Test that a Sprout tx with a negative version number is detected
// given the new Overwinter logic
TEST(checktransaction_tests, SproutTxVersionTooLow) {
	SelectParams(CBaseChainParams::REGTEST);
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.nVersion = -1;

    CTransaction tx(mtx);
    MockCValidationState state;

	EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-version-too-low", false)).Times(1);
	CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, TransparentTxVersionWithJoinsplit) {
	SelectParams(CBaseChainParams::REGTEST);
	CMutableTransaction mtx = GetValidTransaction(TRANSPARENT_TX_VERSION);
	CTransaction tx(mtx);
	MockCValidationState state;
	EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
	EXPECT_TRUE(tx.ContextualCheck(state, 1, 100));
	EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-transparent-jsnotempty", false)).Times(1);
	EXPECT_FALSE(tx.ContextualCheck(state, 200, 100));
}

TEST(checktransaction_tests, GrothTxVersion) {
	SelectParams(CBaseChainParams::REGTEST);
	CMutableTransaction mtx = GetValidTransaction(GROTH_TX_VERSION);
	CTransaction tx(mtx);
	MockCValidationState state;
	EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
	EXPECT_CALL(state, DoS(0, false, REJECT_INVALID, "bad-tx-version-unexpected", false)).Times(1);
	EXPECT_FALSE(tx.ContextualCheck(state, 1, 100));
	EXPECT_TRUE(tx.ContextualCheck(state, 200, 100));
}

TEST(checktransaction_tests, PhgrTxVersion) {
	SelectParams(CBaseChainParams::REGTEST);
	CMutableTransaction mtx = GetValidTransaction(PHGR_TX_VERSION);
	CTransaction tx(mtx);
	MockCValidationState state;
	EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
	EXPECT_TRUE(tx.ContextualCheck(state, 1, 100));
	EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-tx-version-unexpected", false)).Times(1);
	EXPECT_FALSE(tx.ContextualCheck(state, 200, 100));
}

TEST(checktransaction_tests, ScTxVersion) {
	SelectParams(CBaseChainParams::REGTEST);
	CMutableTransaction mtx = GetValidTransaction(SC_TX_VERSION);
	mtx.vjoinsplit.clear();

	CTransaction tx(mtx);
	MockCValidationState state;
	EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
	EXPECT_TRUE(tx.ContextualCheck(state, 220, 100));
	EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-tx-version-unexpected", false)).Times(1);
	EXPECT_FALSE(tx.ContextualCheck(state, 219, 100));
}

TEST(checktransaction_tests, ScCertVersion) {
	SelectParams(CBaseChainParams::REGTEST);
	CMutableScCertificate mcert = GetValidCertificate();

	CScCertificate cert(mcert);
	CValidationState state;
	EXPECT_TRUE(cert.ContextualCheck(state, 220, 100));
	EXPECT_FALSE(cert.ContextualCheck(state, 219, 100));
}

TEST(TransactionManipulation, EmptyTxTransformationToMutableIsNotReversible) {
    // CopyCtor -> CopyCtor
    CTransaction        EmptyOriginalTx;
    CMutableTransaction mutByCopyCtor(EmptyOriginalTx);
    CTransaction        revertedTxByCopyCtor(mutByCopyCtor);

    EXPECT_FALSE(EmptyOriginalTx == revertedTxByCopyCtor);
    EXPECT_TRUE(EmptyOriginalTx.GetHash().IsNull());
    EXPECT_FALSE(revertedTxByCopyCtor.GetHash().IsNull());

    // AssignOp -> CopyCtor
    CMutableTransaction mutByAssignOp;
    mutByAssignOp = EmptyOriginalTx;
    CTransaction revertedTxFromAssignement(mutByAssignOp);

    EXPECT_FALSE(EmptyOriginalTx == revertedTxFromAssignement);
    EXPECT_TRUE(EmptyOriginalTx.GetHash().IsNull());
    EXPECT_FALSE(revertedTxFromAssignement.GetHash().IsNull());

    // CopyCtor -> AssignOp
    CTransaction revertedTxByAssignOp;
    revertedTxByAssignOp = mutByCopyCtor;

    EXPECT_FALSE(EmptyOriginalTx == revertedTxByAssignOp);
    EXPECT_TRUE(EmptyOriginalTx.GetHash().IsNull());
    EXPECT_FALSE(revertedTxByAssignOp.GetHash().IsNull());
}

TEST(TransactionManipulation, NonEmptyTxTransformationToMutableIsReversible) {
    //create non-empty transaction
    CMutableTransaction helperMutTx;
    unsigned int OutNum = 5;
    for(unsigned int idx = 0; idx < OutNum; ++idx)
        helperMutTx.addOut(CTxOut(CAmount(idx),CScript()));

    CTransaction nonEmptyOriginalTx(helperMutTx);

    // CopyCtor -> CopyCtor
    CMutableTransaction mutByCopyCtor(nonEmptyOriginalTx);
    CTransaction        revertedTxByCopyCtor(mutByCopyCtor);

    EXPECT_TRUE(nonEmptyOriginalTx == revertedTxByCopyCtor)
        <<" nonEmptyOriginalTx.GetHash() "<<nonEmptyOriginalTx.GetHash().ToString()
        <<" revertedTxByCopyCtor.GetHash() "<<revertedTxByCopyCtor.GetHash().ToString();

    // AssignOp -> CopyCtor
    CMutableTransaction mutByAssignOp;
    mutByAssignOp = nonEmptyOriginalTx;
    CTransaction revertedTxFromAssignement(mutByAssignOp);

    EXPECT_TRUE(nonEmptyOriginalTx == revertedTxByCopyCtor)
        <<" nonEmptyOriginalTx.GetHash() "<<nonEmptyOriginalTx.GetHash().ToString()
        <<" revertedTxFromAssignement.GetHash() "<<revertedTxFromAssignement.GetHash().ToString();

    // CopyCtor -> AssignOp
    CTransaction revertedTxByAssignOp;
    revertedTxByAssignOp = mutByCopyCtor;

    EXPECT_TRUE(nonEmptyOriginalTx == revertedTxByCopyCtor)
        <<" nonEmptyOriginalTx.GetHash() "<<nonEmptyOriginalTx.GetHash().ToString()
        <<" revertedTxByAssignOp.GetHash() "<<revertedTxByAssignOp.GetHash().ToString();
}

TEST(TransactionManipulation, ExtendingTransactionOuts) {
    CMutableTransaction mutTx;
    EXPECT_TRUE(mutTx.getVout().size() == 0);

    unsigned int OutNum = 10;
    for(unsigned int idx = 0; idx < OutNum; ++idx)
        mutTx.addOut(CTxOut(CAmount(idx),CScript()));

    CTransaction txOutOnly = mutTx;
    EXPECT_TRUE(txOutOnly.GetVout().size() == OutNum);
    for(unsigned int idx = 0; idx < OutNum; ++idx)
        EXPECT_FALSE(txOutOnly.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<"wrongly marked as bwt";

    unsigned int BwtNum = 7;
    for(unsigned int idx = 0; idx < BwtNum; ++idx)
        mutTx.addBwt(CTxOut(CAmount(idx + OutNum),CScript()));

    CTransaction txBwtAttempt = mutTx;
    EXPECT_TRUE(txBwtAttempt.GetVout().size() == OutNum);
    for(unsigned int idx = 0; idx < OutNum; ++idx)
        EXPECT_FALSE(txBwtAttempt.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<"wrongly marked as bwt";
}

TEST(CertificateManipulation, EmptyCertTransformationToMutableIsNotReversible) {
    // CopyCtor -> CopyCtor
    CScCertificate        EmptyOriginalCert;
    CMutableScCertificate mutByCopyCtor(EmptyOriginalCert);
    CScCertificate        revertedCertByCopyCtor(mutByCopyCtor);

    EXPECT_FALSE(EmptyOriginalCert == revertedCertByCopyCtor);
    EXPECT_TRUE(EmptyOriginalCert.GetHash().IsNull());
    EXPECT_FALSE(revertedCertByCopyCtor.GetHash().IsNull());
    EXPECT_TRUE(EmptyOriginalCert.nFirstBwtPos == 0);
    EXPECT_TRUE(revertedCertByCopyCtor.nFirstBwtPos == 0);

    // AssignOp -> CopyCtor
    CMutableScCertificate mutByAssignOp;
    mutByAssignOp = EmptyOriginalCert;
    CScCertificate revertedTxFromAssignement(mutByCopyCtor);

    EXPECT_FALSE(EmptyOriginalCert == revertedTxFromAssignement);
    EXPECT_TRUE(EmptyOriginalCert.GetHash().IsNull());
    EXPECT_FALSE(revertedTxFromAssignement.GetHash().IsNull());
    EXPECT_TRUE(revertedTxFromAssignement.nFirstBwtPos == 0);

    // CopyCtor -> AssignOp
    CScCertificate revertedTxByAssignOp;
    revertedTxByAssignOp = mutByCopyCtor;

    EXPECT_FALSE(EmptyOriginalCert == revertedTxByAssignOp);
    EXPECT_TRUE(EmptyOriginalCert.GetHash().IsNull());
    EXPECT_FALSE(revertedTxByAssignOp.GetHash().IsNull());
    EXPECT_TRUE(revertedTxByAssignOp.nFirstBwtPos == 0);
}

TEST(CertificateManipulation, NonEmptyCertTransformationToMutableIsReversible) {
    //create non-empty transaction
    CMutableScCertificate helperMutCert;
    unsigned int OutNum = 10;
    for(unsigned int idx = 0; idx < OutNum; ++idx)
        helperMutCert.addOut(CTxOut(CAmount(idx),CScript()));

    unsigned int bwtOut = 3;
    CScript bwtScript = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG;
    for(unsigned int idx = 0; idx < bwtOut; ++idx)
        helperMutCert.addBwt(CTxOut(CAmount(idx),bwtScript));

    CScCertificate nonEmptyOriginalCert(helperMutCert);

    // CopyCtor -> CopyCtor
    CMutableScCertificate mutByCopyCtor(nonEmptyOriginalCert);
    CScCertificate        revertedCertByCopyCtor(mutByCopyCtor);

    EXPECT_TRUE(nonEmptyOriginalCert == revertedCertByCopyCtor)
        <<" nonEmptyOriginalTx.GetHash() "<<nonEmptyOriginalCert.GetHash().ToString()
        <<" revertedTxByCopyCtor.GetHash() "<<revertedCertByCopyCtor.GetHash().ToString();

    // AssignOp -> CopyCtor
    CMutableScCertificate mutByAssignOp;
    mutByAssignOp = nonEmptyOriginalCert;
    CScCertificate revertedCertFromAssignement(mutByAssignOp);

    EXPECT_TRUE(nonEmptyOriginalCert == revertedCertFromAssignement)
        <<" nonEmptyOriginalTx.GetHash() "<<nonEmptyOriginalCert.GetHash().ToString()
        <<" revertedTxFromAssignement.GetHash() "<<revertedCertFromAssignement.GetHash().ToString();

    // CopyCtor -> AssignOp
    CScCertificate revertedCertByAssignOp;
    revertedCertByAssignOp = mutByCopyCtor;

    EXPECT_TRUE(nonEmptyOriginalCert == revertedCertByAssignOp)
        <<" nonEmptyOriginalTx.GetHash() "<<nonEmptyOriginalCert.GetHash().ToString()
        <<" revertedTxByAssignOp.GetHash() "<<revertedCertByAssignOp.GetHash().ToString();
}

TEST(CertificateManipulation, ExtendingCertificateOutsAndBwts) {
    CMutableScCertificate mutCert;
    EXPECT_TRUE(mutCert.getVout().size() == 0);

    // add OutNum change outputs
    unsigned int OutNum = 1;
    for(unsigned int idx = 0; idx < OutNum; ++idx)
        mutCert.addOut(CTxOut(CAmount(idx),CScript()));

    CScCertificate OutputOnlyCert = mutCert;
    EXPECT_TRUE(OutputOnlyCert.GetVout().size() == OutNum);
    EXPECT_TRUE(OutputOnlyCert.nFirstBwtPos == OutNum);
    for(unsigned int idx = 0; idx < OutNum; ++idx)
        EXPECT_FALSE(OutputOnlyCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as bwt";

    // add BwtNum bwts
    unsigned int BwtNum = 2;
    CScript bwtScript = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG;
    for(unsigned int idx = 0; idx < BwtNum; ++idx)
        mutCert.addBwt(CTxOut(CAmount(idx + OutNum),bwtScript));

    CScCertificate OutsAndBwtsCert = mutCert;
    EXPECT_TRUE(OutsAndBwtsCert.GetVout().size() == OutNum + BwtNum);
    EXPECT_TRUE(OutputOnlyCert.nFirstBwtPos == OutNum);
    for(unsigned int idx = 0; idx < OutNum ; ++idx)
        EXPECT_FALSE(OutsAndBwtsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as bwt";
    for(unsigned int idx = OutNum; idx < OutNum + BwtNum; ++idx)
        EXPECT_TRUE(OutsAndBwtsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as output";

    // add some extra outputs
    unsigned int ExtraOuts = 3;
    for(unsigned int idx = 0; idx < ExtraOuts; ++idx)
        mutCert.addOut(CTxOut(CAmount(idx),CScript()));

    CScCertificate ExtraOutAndBwtsCert = mutCert;
    EXPECT_TRUE(ExtraOutAndBwtsCert.GetVout().size() == OutNum + ExtraOuts + BwtNum)
        <<"certOutAndBwt.GetVout().size() "<<OutsAndBwtsCert.GetVout().size();
    EXPECT_TRUE(ExtraOutAndBwtsCert.nFirstBwtPos == OutNum + ExtraOuts);
    for(unsigned int idx = 0; idx < OutNum + ExtraOuts; ++idx)
        EXPECT_FALSE(ExtraOutAndBwtsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as bwt";
    for(unsigned int idx = OutNum + ExtraOuts; idx < OutNum + ExtraOuts + BwtNum; ++idx)
        EXPECT_TRUE(ExtraOutAndBwtsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as output";
}

TEST(CertificateManipulation, ResizingCertificateChangeOutputs) {
    CMutableScCertificate mutCert;
    EXPECT_TRUE(mutCert.getVout().size() == 0);

    // Create initial certificate
    unsigned int OutNum = 10;
    for(unsigned int idx = 0; idx < OutNum; ++idx)
        mutCert.addOut(CTxOut(CAmount(idx),CScript()));

    unsigned int BwtNum = 3;
    CScript bwtScript = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG;
    for(unsigned int idx = 0; idx < BwtNum; ++idx)
        mutCert.addBwt(CTxOut(CAmount(idx + OutNum),bwtScript));

    CScCertificate initialCert = mutCert;
    ASSERT_TRUE(initialCert.GetVout().size() == OutNum + BwtNum)
        <<"initialCert.GetVout().size() "<<initialCert.GetVout().size();
    for(unsigned int idx = 0; idx < OutNum; ++idx)
        ASSERT_FALSE(initialCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as bwt";
    for(unsigned int idx = OutNum; idx < OutNum + BwtNum; ++idx)
        ASSERT_TRUE(initialCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as output";

    //Reduce change outputs
    unsigned int reducedOutNum = 5;
    mutCert.resizeOut(reducedOutNum);
    CScCertificate reducedOutsCert = mutCert;
    EXPECT_TRUE(reducedOutsCert.GetVout().size() == reducedOutNum + BwtNum)
        <<"reducedOutsCert.GetVout().size() "<<reducedOutsCert.GetVout().size();
    for(unsigned int idx = 0; idx < reducedOutNum; ++idx)
        EXPECT_FALSE(reducedOutsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as bwt";
    for(unsigned int idx = reducedOutNum; idx < reducedOutNum + BwtNum; ++idx)
        EXPECT_TRUE(reducedOutsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as output";

    //Increase change outputs
    unsigned increasedOutNum = 15;
    mutCert.resizeOut(increasedOutNum);
    CScCertificate increasedOutsCert = mutCert;
    EXPECT_TRUE(increasedOutsCert.GetVout().size() == increasedOutNum + BwtNum)
        <<"increasedOutsCert.GetVout().size() "<<increasedOutsCert.GetVout().size();
    for(unsigned int idx = 0; idx < increasedOutNum; ++idx)
        EXPECT_FALSE(increasedOutsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as bwt";
    for(unsigned int idx = increasedOutNum; idx < increasedOutNum + BwtNum; ++idx)
        EXPECT_TRUE(increasedOutsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as output";

    //Reduce bwt
    unsigned reducedBwtNum = 1;
    mutCert.resizeBwt(reducedBwtNum);
    CScCertificate reducedBwtsCert = mutCert;
    EXPECT_TRUE(reducedBwtsCert.GetVout().size() == increasedOutNum + reducedBwtNum)
        <<"reducedBwtsCert.GetVout().size() "<<reducedBwtsCert.GetVout().size();
    for(unsigned int idx = 0; idx < increasedOutNum; ++idx)
        EXPECT_FALSE(reducedBwtsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as bwt";
    for(unsigned int idx = increasedOutNum; idx < increasedOutNum + reducedBwtNum; ++idx)
        EXPECT_TRUE(reducedBwtsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as output";

    //increase bwt
    unsigned increaseBwtNum = 10;
    mutCert.resizeBwt(increaseBwtNum);

    CScCertificate increasedBwtsCert = mutCert;
    EXPECT_TRUE(increasedBwtsCert.GetVout().size() == increasedOutNum + increaseBwtNum)
        <<"increasedBwtsCert.GetVout().size() "<<increasedBwtsCert.GetVout().size();
    for(unsigned int idx = 0; idx < increasedOutNum; ++idx)
        EXPECT_FALSE(increasedBwtsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as bwt";
    for(unsigned int idx = increasedOutNum; idx < increasedOutNum + increaseBwtNum; ++idx)
        EXPECT_TRUE(increasedBwtsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as output";

    //remove all change outputs
    unsigned int noOutNum = 0;
    mutCert.resizeOut(noOutNum);
    CScCertificate noOutsCert = mutCert;
    EXPECT_TRUE(noOutsCert.GetVout().size() == noOutNum + increaseBwtNum)
        <<"noOutsCert.GetVout().size() "<<noOutsCert.GetVout().size();
    for(unsigned int idx = 0; idx < noOutNum; ++idx)
        EXPECT_FALSE(noOutsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as bwt";
    for(unsigned int idx = noOutNum; idx < noOutNum + increaseBwtNum; ++idx)
        EXPECT_TRUE(noOutsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as output";

    //remove all bwts
    unsigned int noBwtNum = 0;
    mutCert.resizeBwt(noBwtNum);
    CScCertificate noBwtsCert = mutCert;
    EXPECT_TRUE(noBwtsCert.GetVout().size() == noOutNum + noBwtNum)
        <<"noBwtsCert.GetVout().size() "<<noBwtsCert.GetVout().size();
    for(unsigned int idx = 0; idx < noOutNum; ++idx)
        EXPECT_FALSE(noBwtsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as bwt";
    for(unsigned int idx = noOutNum; idx < noOutNum + noBwtNum; ++idx)
        EXPECT_TRUE(noBwtsCert.IsBackwardTransfer(idx))<<"Output at pos "<<idx<<" wrongly marked as output";
}
