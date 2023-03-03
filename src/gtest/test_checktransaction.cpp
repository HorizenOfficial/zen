#include <gtest/gtest.h>
#include <sodium.h>

#include "main.h"
#include "primitives/transaction.h"
#include "consensus/validation.h"
#include <streams.h>
#include <clientversion.h>

#include <gtest/libzendoo_test_files.h>

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

        CTxCeasedSidechainWithdrawalInput csw_ccin;
        csw_ccin.nValue = 2.0 * COIN;
        csw_ccin.scId = uint256S("efefef");
        std::vector<unsigned char> nullifierStr(CFieldElement::ByteSize(), 0x0);
        GetRandBytes((unsigned char*)&nullifierStr[0], CFieldElement::ByteSize()-2);
        csw_ccin.nullifier.SetByteArray(nullifierStr);
        GetRandBytes((unsigned char*)&csw_ccin.pubKeyHash, csw_ccin.pubKeyHash.size());
        std::vector<unsigned char> proofStr(CScProof::MaxByteSize(), 0x0);
        GetRandBytes((unsigned char*)&proofStr[0], CScProof::MaxByteSize());
        csw_ccin.scProof.SetByteArray(proofStr);
        csw_ccin.redeemScript = CScript();
        mtx.vcsw_ccin.push_back(csw_ccin);

        CTxScCreationOut cr_ccout;
        cr_ccout.version = 0;
        cr_ccout.nValue = 1.0 * COIN;
        cr_ccout.withdrawalEpochLength = 111;
        cr_ccout.wCertVk   = CScVKey{SAMPLE_CERT_DARLIN_VK};
        cr_ccout.wCeasedVk = CScVKey{SAMPLE_CSW_DARLIN_VK};
        mtx.vsc_ccout.push_back(cr_ccout);

        CTxForwardTransferOut ft_ccout;
        ft_ccout.nValue = 10.0 * COIN;
        ft_ccout.scId = uint256S("effeef");
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
    }

    return mtx;
}

CMutableScCertificate GetValidCertificate() {
    CMutableScCertificate mcert;
    mcert.nVersion = SC_CERT_VERSION;

    mcert.addOut(CTxOut(0.5 * COIN,CScript())); //CAmount is measured in zatoshi
    mcert.addOut(CTxOut(1 * COIN,CScript()));   //CAmount is measured in zatoshi

    mcert.scId = GetRandHash();
    mcert.epochNumber = 3;
    mcert.endEpochCumScTxCommTreeRoot = CFieldElement{SAMPLE_FIELD};

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
    CValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
}

TEST(checktransaction_tests, invalid_transparent_transaction_with_certificate_version) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.nVersion = SC_CERT_VERSION;
    CTransaction tx(mtx);
    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
}

TEST(checktransaction_tests, valid_sprout_transaction) {
    CMutableTransaction mtx = GetValidTransaction();
    CTransaction tx(mtx);
    CValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
}

TEST(checktransaction_tests, BadVersionTooLow) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.nVersion = 0;

    CTransaction tx(mtx);
    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-version-too-low"));
    EXPECT_FALSE(state.CorruptionPossible());

}

TEST(checktransaction_tests, bad_txns_vin_empty) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.vin.resize(0);

    CTransaction tx(mtx);
    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 10);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-vin-empty"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_vout_empty) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.resizeOut(0);

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 10);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-vout-empty"));
    EXPECT_FALSE(state.CorruptionPossible());
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
    
        CValidationState state;
        EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
        EXPECT_TRUE(state.GetDoS() == 100);
        EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
        EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-oversize"));
        EXPECT_FALSE(state.CorruptionPossible());
    }
}

TEST(checktransaction_tests, bad_txns_vout_negative) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.getOut(0).nValue = -1;

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-vout-negative"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_vout_toolarge) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.getOut(0).nValue = MAX_MONEY + 1;

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-vout-toolarge"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_txouttotal_toolarge_outputs) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.getOut(0).nValue = MAX_MONEY;
    mtx.getOut(1).nValue = 1;

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-txouttotal-toolarge"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_txouttotal_toolarge_joinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.getOut(0).nValue = 1;
    mtx.vjoinsplit[0].vpub_old = MAX_MONEY;

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-txouttotal-toolarge"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_txintotal_toolarge_joinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_new = MAX_MONEY - 1;
    mtx.vjoinsplit[1].vpub_new = MAX_MONEY - 1;

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-txintotal-toolarge"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_vpub_old_negative) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_old = -1;

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-vpub_old-negative"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_vpub_new_negative) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_new = -1;

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-vpub_new-negative"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_vpub_old_toolarge) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_old = MAX_MONEY + 1;

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-vpub_old-toolarge"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_vpub_new_toolarge) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_new = MAX_MONEY + 1;

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-vpub_new-toolarge"));
}

TEST(checktransaction_tests, bad_txns_vpubs_both_nonzero) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_old = 1;
    mtx.vjoinsplit[0].vpub_new = 1;

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-vpubs-both-nonzero"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_inputs_duplicate) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vin[1].prevout.hash = mtx.vin[0].prevout.hash;
    mtx.vin[1].prevout.n = mtx.vin[0].prevout.n;

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-inputs-duplicate"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_joinsplits_nullifiers_duplicate_same_joinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");
    mtx.vjoinsplit[0].nullifiers.at(1) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-joinsplits-nullifiers-duplicate"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_joinsplits_nullifiers_duplicate_different_joinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");
    mtx.vjoinsplit[1].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");

    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-joinsplits-nullifiers-duplicate"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_cb_has_joinsplits) {
    CMutableTransaction mtx = GetValidTransaction();
    // Make it a coinbase.
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();

    mtx.vjoinsplit.resize(1);

    CTransaction tx(mtx);
    EXPECT_TRUE(tx.IsCoinBase());

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-cb-has-joinsplits"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_cb_empty_scriptsig) {
    CMutableTransaction mtx = GetValidTransaction();
    // Make it a coinbase.
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();

    mtx.vjoinsplit.resize(0);

    CTransaction tx(mtx);
    EXPECT_TRUE(tx.IsCoinBase());

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-cb-length"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_prevout_null) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vin[1].prevout.SetNull();

    CTransaction tx(mtx);
    EXPECT_FALSE(tx.IsCoinBase());

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 10);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-prevout-null"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_invalid_joinsplit_signature) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.joinSplitSig[0] += 1;
    CTransaction tx(mtx);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-invalid-joinsplit-signature"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, non_canonical_ed25519_signature) {
    CMutableTransaction mtx = GetValidTransaction();

    // Check that the signature is valid before we add L
    {
        CTransaction tx(mtx);
        CValidationState state;
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

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-invalid-joinsplit-signature"));
    EXPECT_FALSE(state.CorruptionPossible());
}

// Test that a Sprout tx with a negative version number is detected
// given the new Overwinter logic
TEST(checktransaction_tests, SproutTxVersionTooLow) {
    SelectParams(CBaseChainParams::REGTEST);
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.nVersion = -1;

    CTransaction tx(mtx);
    CValidationState state;

    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-version-too-low"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, TransparentTxVersionWithJoinsplit) {
    SelectParams(CBaseChainParams::REGTEST);
    CMutableTransaction mtx = GetValidTransaction(TRANSPARENT_TX_VERSION);
    CTransaction tx(mtx);
    CValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(tx.ContextualCheck(state, 1, 100));

    EXPECT_FALSE(tx.ContextualCheck(state, 200, 100));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-transparent-jsnotempty"));
}

TEST(checktransaction_tests, GrothTxVersion) {
    SelectParams(CBaseChainParams::REGTEST);
    CMutableTransaction mtx = GetValidTransaction(GROTH_TX_VERSION);
    CTransaction tx(mtx);
    CValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));

    EXPECT_FALSE(tx.ContextualCheck(state, 1, 100));
    EXPECT_TRUE(state.GetDoS() == 0);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-tx-version-unexpected"));
    EXPECT_FALSE(state.CorruptionPossible());

    EXPECT_TRUE(tx.ContextualCheck(state, 200, 100));
}

TEST(checktransaction_tests, PhgrTxVersion) {
    SelectParams(CBaseChainParams::REGTEST);
    CMutableTransaction mtx = GetValidTransaction(PHGR_TX_VERSION);
    CTransaction tx(mtx);
    CValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(tx.ContextualCheck(state, 1, 100));

    EXPECT_FALSE(tx.ContextualCheck(state, 200, 100));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-tx-version-unexpected"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, ScTxVersion) {
    SelectParams(CBaseChainParams::REGTEST);
    CMutableTransaction mtx = GetValidTransaction(SC_TX_VERSION);
    mtx.vjoinsplit.clear();

    CTransaction tx(mtx);
    CValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(tx.ContextualCheck(state, 420, 100));
    EXPECT_FALSE(tx.ContextualCheck(state, 419, 100));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-tx-version-unexpected"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, ScTxVersionWithCrosschainDataOnly) {
    SelectParams(CBaseChainParams::REGTEST);
    CMutableTransaction mtx = GetValidTransaction(SC_TX_VERSION);
    mtx.vin.resize(0);
    mtx.resizeOut(0);

    CTransaction tx(mtx);
    CValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
}

TEST(checktransaction_tests, bad_txns_txcswin_toosmall) {
    CMutableTransaction mtx = GetValidTransaction(SC_TX_VERSION);
    mtx.vcsw_ccin[0].nValue = -1;

    CTransaction tx(mtx);

    CValidationState state;

    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-txcswin-invalid"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_txcswin_toolarge) {
    CMutableTransaction mtx = GetValidTransaction(SC_TX_VERSION);
    mtx.vcsw_ccin[0].nValue = MAX_MONEY + 1;

    CTransaction tx(mtx);

    CValidationState state;

    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-txcswin-invalid"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_txintotal_toolarge) {
    CMutableTransaction mtx = GetValidTransaction(SC_TX_VERSION);
    mtx.vcsw_ccin[0].nValue = MAX_MONEY;
    CTxCeasedSidechainWithdrawalInput csw_in;
    csw_in.nValue = 1;
    mtx.vcsw_ccin.push_back(csw_in);

    CTransaction tx(mtx);

    CValidationState state;

    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-txintotal-toolarge"));
    EXPECT_FALSE(state.CorruptionPossible());
}

TEST(checktransaction_tests, bad_txns_csw_inputs_duplicate) {
    CMutableTransaction mtx = GetValidTransaction(SC_TX_VERSION);
    CTxCeasedSidechainWithdrawalInput csw_in;
    csw_in.nullifier = mtx.vcsw_ccin[0].nullifier;
    csw_in.nValue = 1.0;
    mtx.vcsw_ccin.push_back(csw_in);

    CTransaction tx(mtx);

    CValidationState state;

    EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-csw-inputs-duplicate"));
    EXPECT_FALSE(state.CorruptionPossible());
}


TEST(checktransaction_tests, ScCertVersion) {
    SelectParams(CBaseChainParams::REGTEST);
    CMutableScCertificate mcert = GetValidCertificate();

    CScCertificate cert(mcert);
    CValidationState state;
    EXPECT_TRUE(cert.ContextualCheck(state, 420, 100));
    EXPECT_FALSE(cert.ContextualCheck(state, 419, 100));
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

TEST(SidechainsCertificateManipulation, EmptyCertTransformationToMutableIsNotReversible) {
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
    CScCertificate revertedTxFromAssignement(mutByAssignOp);

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

TEST(SidechainsCertificateManipulation, NonEmptyCertTransformationToMutableIsReversible)
{
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

TEST(SidechainsCertificateManipulation, ExtendingCertificateOutsAndBwts) {
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

TEST(SidechainsCertificateManipulation, ResizingCertificateChangeOutputs) {
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

extern const CBlockIndex* makeMain(int trunk_size);
extern void CleanUpAll();

TEST(checktransaction_tests, isStandardTransaction) {

//    fDebug = true;
//    fPrintToConsole = true;
//    mapMultiArgs["-debug"].push_back("cbh");
//    mapArgs["-debug"] = "cbh";

    SelectParams(CBaseChainParams::REGTEST);
    CMutableTransaction mtx = GetValidTransaction(TRANSPARENT_TX_VERSION);
    mtx.resizeOut(0);
    mtx.resizeBwt(0);
    CScript scriptPubKey;

    // a -1 value for height, minimally encoded
    scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG
        << ToByteVector(uint256()) << -1 << OP_CHECKBLOCKATHEIGHT;
    mtx.insertAtPos(0, CTxOut(CAmount(1),scriptPubKey));

    // height and hash are swapped
    scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG
        << 2 << ToByteVector(uint256()) << OP_CHECKBLOCKATHEIGHT;
    mtx.insertAtPos(1, CTxOut(CAmount(1),scriptPubKey));

    // an invalid op (0xFF) where height is expected
    std::vector<unsigned char> data1(ParseHex("76a914f85d211e4175cd4b0f53284af6ddab6bbb3c5f0288ac20bf309c2d04f3fdd3cb6f4ccddb3985211d360e08e4f790c3d780d5c3f912e704ffb4"));
    CScript bad_script1(data1.begin(), data1.end());
    mtx.insertAtPos(2, CTxOut(CAmount(1),bad_script1));

    // an unknown op (0xBA) where height is expected
    std::vector<unsigned char> data2(ParseHex("76a914f85d211e4175cd4b0f53284af6ddab6bbb3c5f0288ac20bf309c2d04f3fdd3cb6f4ccddb3985211d360e08e4f790c3d780d5c3f912e704bab4"));
    CScript bad_script2(data2.begin(), data2.end());
    mtx.insertAtPos(3, CTxOut(CAmount(1),bad_script2));

    // a non minimal height, caught by CScriptNum
    std::vector<unsigned char> hnm1(ParseHex("01000000"));
    scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG
        << ToByteVector(uint256()) << hnm1 << OP_CHECKBLOCKATHEIGHT;
    mtx.insertAtPos(4, CTxOut(CAmount(1), scriptPubKey));

    // another non minimal height
    std::vector<unsigned char> hnm2(ParseHex("00"));
    scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG
        << ToByteVector(uint256()) << hnm2 << OP_CHECKBLOCKATHEIGHT;
    mtx.insertAtPos(5, CTxOut(CAmount(1), scriptPubKey));

    // another non minimal height, not caught by CScriptNum but checking minimal pushing
    std::vector<unsigned char> hnm3(ParseHex("10"));
    scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG
        << ToByteVector(uint256()) << hnm3 << OP_CHECKBLOCKATHEIGHT;
    mtx.insertAtPos(6, CTxOut(CAmount(1), scriptPubKey));

    // minimal height, ok in both forks
    std::vector<unsigned char> hnm4(ParseHex("11"));
    scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG
        << ToByteVector(uint256()) << hnm4 << OP_CHECKBLOCKATHEIGHT;
    mtx.insertAtPos(7, CTxOut(CAmount(1), scriptPubKey));

    // an OP_0 op (0x00) where height is expected
    std::vector<unsigned char> good_data(ParseHex("76a914f85d211e4175cd4b0f53284af6ddab6bbb3c5f0288ac20bf309c2d04f3fdd3cb6f4ccddb3985211d360e08e4f790c3d780d5c3f912e70400b4"));
    CScript good_script(good_data.begin(), good_data.end());
    mtx.insertAtPos(8, CTxOut(CAmount(1), good_script));

    CTransaction tx(mtx);

    // these are expected to fail in both forks
    CMutableTransaction mtx_bad_param = GetValidTransaction(TRANSPARENT_TX_VERSION);
    mtx_bad_param.resizeOut(0);
    mtx_bad_param.resizeBwt(0);

    // a hash representation shorter than 32 bytes
    std::vector<unsigned char> data31NullBytes;
    data31NullBytes.resize(31);
    scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG
        << data31NullBytes << 19 << OP_CHECKBLOCKATHEIGHT;
    mtx_bad_param.insertAtPos(0, CTxOut(CAmount(1), scriptPubKey));

    // a hash representation longer than 32 bytes
    std::vector<unsigned char> data33NullBytes;
    data33NullBytes.resize(33);
    scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG
        << data33NullBytes << 19 << OP_CHECKBLOCKATHEIGHT;
    mtx_bad_param.insertAtPos(1, CTxOut(CAmount(1), scriptPubKey));

    // a -1 height not minimally encoded, caught in different places before an after the fork
    std::vector<unsigned char> hnm5(ParseHex("81"));
    scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG
        << ToByteVector(uint256()) << hnm5 << OP_CHECKBLOCKATHEIGHT;
    mtx_bad_param.insertAtPos(2, CTxOut(CAmount(1), scriptPubKey));

    // a height larger than 4 bytes
    std::vector<unsigned char> hnm6(ParseHex("aabbccddee"));
    scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG
        << ToByteVector(uint256()) << hnm6 << OP_CHECKBLOCKATHEIGHT;
    mtx_bad_param.insertAtPos(3, CTxOut(CAmount(1), scriptPubKey));

    CTransaction tx_bad_param(mtx_bad_param);

    ReplayProtectionAttributes rpAttributes;
    txnouttype whichType;
    std::string reason;


    // ------------------ before rp fix
    static const int H_PRE_FORK = 220;
    CleanUpAll();
    makeMain(H_PRE_FORK);

    // This is useful only for the tests of pre-rp-fix fork.
    // This is for avoiding checking blockheight against blockhash in scripts, because hashes are fake
    // in this simple test environment, and it would always make IsStandard() return false even when scripts parse ok.
    mapArgs["-cbhsafedepth"] = "10";

    EXPECT_TRUE(IsStandardTx(tx, reason, H_PRE_FORK));

    EXPECT_TRUE(IsStandard(tx.GetVout()[0].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_PUBKEYHASH_REPLAY);
    EXPECT_TRUE(IsStandard(tx.GetVout()[1].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_PUBKEYHASH_REPLAY);
    EXPECT_TRUE(IsStandard(tx.GetVout()[2].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_PUBKEYHASH_REPLAY);
    EXPECT_TRUE(IsStandard(tx.GetVout()[3].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_PUBKEYHASH_REPLAY);
    EXPECT_TRUE(IsStandard(tx.GetVout()[4].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_PUBKEYHASH_REPLAY);
    EXPECT_TRUE(IsStandard(tx.GetVout()[5].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_PUBKEYHASH_REPLAY);
    EXPECT_TRUE(IsStandard(tx.GetVout()[6].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_PUBKEYHASH_REPLAY);
    EXPECT_TRUE(IsStandard(tx.GetVout()[7].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_PUBKEYHASH_REPLAY);
    EXPECT_TRUE(IsStandard(tx.GetVout()[8].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_PUBKEYHASH_REPLAY);

    // expecting to fail before and after the fork
    EXPECT_FALSE(IsStandardTx(tx_bad_param, reason, H_PRE_FORK));
    EXPECT_TRUE(reason == "scriptpubkey");

    EXPECT_FALSE(IsStandard(tx_bad_param.GetVout()[0].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);
    EXPECT_FALSE(IsStandard(tx_bad_param.GetVout()[1].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);
    EXPECT_FALSE(IsStandard(tx_bad_param.GetVout()[2].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);
    EXPECT_FALSE(IsStandard(tx_bad_param.GetVout()[3].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);



    // ------------------ after rp fix
    static const int H_POST_FORK = 500;
    CleanUpAll();
    makeMain(H_POST_FORK);

    EXPECT_FALSE(IsStandardTx(tx, reason, H_POST_FORK));
    EXPECT_TRUE(reason == "scriptpubkey");

    EXPECT_FALSE(IsStandard(tx.GetVout()[0].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);
    EXPECT_FALSE(IsStandard(tx.GetVout()[1].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);
    EXPECT_FALSE(IsStandard(tx.GetVout()[2].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);
    EXPECT_FALSE(IsStandard(tx.GetVout()[3].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);

    // non minimal height encodings are not legal anymore
    EXPECT_FALSE(IsStandard(tx.GetVout().at(4).scriptPubKey, whichType, rpAttributes));
    EXPECT_FALSE(IsStandard(tx.GetVout().at(5).scriptPubKey, whichType, rpAttributes));
    EXPECT_FALSE(IsStandard(tx.GetVout().at(6).scriptPubKey, whichType, rpAttributes));

    // legal height encodings
    EXPECT_TRUE(IsStandard(tx.GetVout().at(7).scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_PUBKEYHASH_REPLAY);

    EXPECT_TRUE(IsStandard(tx.GetVout().at(8).scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_PUBKEYHASH_REPLAY);

    // expecting to fail before and after the fork
    EXPECT_FALSE(IsStandardTx(tx_bad_param, reason, H_PRE_FORK));
    EXPECT_TRUE(reason == "scriptpubkey");

    EXPECT_FALSE(IsStandard(tx_bad_param.GetVout()[0].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);
    EXPECT_FALSE(IsStandard(tx_bad_param.GetVout()[1].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);
    EXPECT_FALSE(IsStandard(tx_bad_param.GetVout()[2].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);
    EXPECT_FALSE(IsStandard(tx_bad_param.GetVout()[3].scriptPubKey, whichType, rpAttributes));
    EXPECT_TRUE(whichType == TX_NONSTANDARD);
}

TEST(SidechainsCertificateCustomFields, FieldElementCertificateFieldConfig_Validation)
{
    FieldElementCertificateFieldConfig zeroFieldConfig{0};
    EXPECT_FALSE(zeroFieldConfig.IsValid());

    FieldElementCertificateFieldConfig positiveFieldConfig{10};
    EXPECT_TRUE(positiveFieldConfig.IsValid());
    // FieldElementCertificateFieldConfig::nBits is an uint8_t, testing larger values or negative ones is not possible 
}

TEST(SidechainsCertificateCustomFields, BitVectorCertificateFieldConfig_Validation)
{
    BitVectorCertificateFieldConfig negativeSizeBitVector_BitVectorConfig{-1, 12};
    EXPECT_FALSE(negativeSizeBitVector_BitVectorConfig.IsValid());

    BitVectorCertificateFieldConfig negativeSizeCompressed_BitVectorConfig{1, -1};
    EXPECT_FALSE(negativeSizeCompressed_BitVectorConfig.IsValid());

    BitVectorCertificateFieldConfig zeroSizeBitVector_BitVectorConfig{0, 12};
    EXPECT_FALSE(zeroSizeBitVector_BitVectorConfig.IsValid());

    BitVectorCertificateFieldConfig zeroSizeCompressed_BitVectorConfig{1, 0};
    EXPECT_FALSE(zeroSizeCompressed_BitVectorConfig.IsValid());

    BitVectorCertificateFieldConfig offSizeBitVectorConfig_1{253*8, 12};
    EXPECT_FALSE(offSizeBitVectorConfig_1.IsValid());

    BitVectorCertificateFieldConfig offSizeBitVectorConfig_2{254*7, 12};
    EXPECT_FALSE(offSizeBitVectorConfig_2.IsValid());

    BitVectorCertificateFieldConfig positiveBitVectorConfig{254*8, 12};
    EXPECT_TRUE(positiveBitVectorConfig.IsValid());

    BitVectorCertificateFieldConfig tooBigBitVector_BitVectorConfig{BitVectorCertificateFieldConfig::MAX_BIT_VECTOR_SIZE_BITS+1, 12};
    EXPECT_FALSE(tooBigBitVector_BitVectorConfig.IsValid());

    BitVectorCertificateFieldConfig tooBigCompressed_BitVectorConfig{1, BitVectorCertificateFieldConfig::MAX_COMPRESSED_SIZE_BYTES+1};
    EXPECT_FALSE(tooBigCompressed_BitVectorConfig.IsValid());
}
