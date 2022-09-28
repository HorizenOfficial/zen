// Copyright (c) 2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <univalue.h>

#include <iostream>

#include <boost/test/unit_test.hpp>

#include "consensus/validation.h"
#include "data/sighash.json.h"
#include "main.h"
#include "random.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "serialize.h"
#include "sodium.h"
#include "test/test_bitcoin.h"
#include "util.h"
#include "version.h"

extern UniValue read_json(const std::string& jsondata);

// Old script.cpp SignatureHash function
uint256 static SignatureHashOld(CScript scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType) {
    static const uint256 one(uint256S("0000000000000000000000000000000000000000000000000000000000000001"));
    if (nIn >= txTo.GetVin().size() + txTo.GetVcswCcIn().size()) {
        printf("ERROR: SignatureHash(): nIn=%d out of range\n", nIn);
        return one;
    }
    CMutableTransaction txTmp(txTo);

    // Blank out other inputs' signatures
    for (unsigned int i = 0; i < txTmp.vin.size(); i++) txTmp.vin[i].scriptSig = CScript();
    for (unsigned int i = 0; i < txTmp.vcsw_ccin.size(); i++) txTmp.vcsw_ccin[i].redeemScript = CScript();
    if (nIn < txTmp.vin.size()) {
        txTmp.vin[nIn].scriptSig = scriptCode;
    } else {
        txTmp.vcsw_ccin[nIn - txTmp.vin.size()].redeemScript = scriptCode;
    }

    // Blank out some of the outputs
    if ((nHashType & 0x1f) == SIGHASH_NONE) {
        // Wildcard payee
        txTmp.resizeOut(0);
        txTmp.vsc_ccout.resize(0);
        txTmp.vft_ccout.resize(0);
        txTmp.vmbtr_out.resize(0);

        // Let the others update at will
        for (unsigned int i = 0; i < txTmp.vin.size(); i++)
            if (i != nIn) txTmp.vin[i].nSequence = 0;
    } else if ((nHashType & 0x1f) == SIGHASH_SINGLE) {
        // Only lock-in the txout payee at same index as txin
        unsigned int nOut = nIn;
        if (nOut >= txTmp.getVout().size()) {
            printf("ERROR: SignatureHash(): nOut=%d out of range\n", nOut);
            return one;
        }
        txTmp.resizeOut(nOut + 1);
        for (unsigned int i = 0; i < nOut; i++) txTmp.getOut(i).SetNull();

        // Let the others update at will
        for (unsigned int i = 0; i < txTmp.vin.size(); i++)
            if (i != nIn) txTmp.vin[i].nSequence = 0;
    }

    // Blank out other inputs completely, not recommended for open transactions
    if (nHashType & SIGHASH_ANYONECANPAY) {
        if (nIn < txTmp.vin.size()) {
            txTmp.vin[0] = txTmp.vin[nIn];
            txTmp.vin.resize(1);
            txTmp.vcsw_ccin.resize(0);
        } else {
            txTmp.vcsw_ccin[0] = txTmp.vcsw_ccin[nIn - txTmp.vin.size()];
            txTmp.vcsw_ccin.resize(1);
            txTmp.vin.resize(0);
        }
    }

    // Blank out the joinsplit signature.
    memset(&txTmp.joinSplitSig[0], 0, txTmp.joinSplitSig.size());

    // Serialize and hash
    CHashWriter ss(SER_GETHASH, 0);
    ss << txTmp << nHashType;
    return ss.GetHash();
}

uint256 static SignatureHashCert(CScript scriptCode, const CScCertificate& certTo, unsigned int nIn, int nHashType) {
    static const uint256 one(uint256S("0000000000000000000000000000000000000000000000000000000000000001"));
    if (nIn >= certTo.GetVin().size()) {
        printf("ERROR: SignatureHash(): nIn=%d out of range\n", nIn);
        return one;
    }
    CMutableScCertificate certTmp(certTo);

    // Blank out other inputs' signatures
    for (unsigned int i = 0; i < certTmp.vin.size(); i++) certTmp.vin[i].scriptSig = CScript();
    certTmp.vin[nIn].scriptSig = scriptCode;

    // Blank out some of the outputs
    if ((nHashType & 0x1f) == SIGHASH_NONE) {
        // Wildcard payee
        certTmp.resizeOut(0);

        // Let the others update at will
        for (unsigned int i = 0; i < certTmp.vin.size(); i++)
            if (i != nIn) certTmp.vin[i].nSequence = 0;
    } else if ((nHashType & 0x1f) == SIGHASH_SINGLE) {
        // Only lock-in the txout payee at same index as txin
        unsigned int nOut = nIn;
        unsigned int outSize = certTmp.nFirstBwtPos;
        if (nOut >= outSize) {
            printf("ERROR: SignatureHash(): nOut=%d out of range\n", nOut);
            return one;
        }

        certTmp.resizeOut(nOut + 1);

        for (unsigned int i = 0; i < nOut; i++) certTmp.getOut(i).SetNull();

        // Let the others update at will
        for (unsigned int i = 0; i < certTmp.vin.size(); i++)
            if (i != nIn) certTmp.vin[i].nSequence = 0;
    }

    // Blank out other inputs completely, not recommended for open transactions
    if (nHashType & SIGHASH_ANYONECANPAY) {
        certTmp.vin[0] = certTmp.vin[nIn];
        certTmp.vin.resize(1);
    }

    // Serialize and hash
    CHashWriter ss(SER_GETHASH, 0);
    ss << certTmp << nHashType;
    return ss.GetHash();
}

static uint160 random_uint160() {
    uint160 ret;
    randombytes_buf(ret.begin(), 20);
    return ret;
}

void static RandomScript(CScript& script) {
    static const opcodetype oplist[] = {OP_FALSE, OP_1, OP_2, OP_3, OP_CHECKSIG, OP_IF, OP_VERIF, OP_RETURN};
    script = CScript();
    int ops = (insecure_rand() % 10);
    for (int i = 0; i < ops; i++) script << oplist[insecure_rand() % (sizeof(oplist) / sizeof(oplist[0]))];
}

void static RandomScriptBwt(CScript& script) {
    RandomScript(script);
    std::vector<unsigned char> pkh;
    for (unsigned int i = 0; i < sizeof(uint160); i++) {
        pkh.push_back((unsigned char)(insecure_rand() % 0xff));
    }
    script << OP_HASH160 << pkh;
}

void static RandomPubKeyHash(uint160& pubKeyHash) {
    std::string str;
    for (unsigned int i = 0; i < sizeof(uint160); i++) {
        str.push_back((unsigned char)(insecure_rand() % 0xff));
    }
    pubKeyHash.SetHex(str);
}

void static RandomSidechainField(CFieldElement& fe) {
    std::vector<unsigned char> vec;
    for (unsigned int i = 0; i < sizeof(CFieldElement); i++) {
        vec.push_back((unsigned char)(insecure_rand() % 0xff));
    }
    vec.resize(CFieldElement::ByteSize());
    fe.SetByteArray(vec);
}

void static RandomScProof(CScProof& proof) {
    std::vector<unsigned char> vec;
    for (unsigned int i = 0; i < sizeof(CScProof); i++) {
        vec.push_back((unsigned char)(insecure_rand() % 0xff));
    }
    vec.resize(insecure_rand() % CScProof::MaxByteSize() + 1);
    proof.SetByteArray(vec);
}

void static RandomScVk(CScVKey& vk) {
    std::vector<unsigned char> vec;
    for (unsigned int i = 0; i < sizeof(CScVKey); i++) {
        vec.push_back((unsigned char)(insecure_rand() % 0xff));
    }
    vec.resize(insecure_rand() % CScVKey::MaxByteSize() + 1);
    vk.SetByteArray(vec);
}

void static RandomData(std::vector<unsigned char>& data) {
    data.clear();
    for (unsigned int i = 0; i < 100; i++) {
        data.push_back((unsigned char)(insecure_rand() % 0xff));
    }
}

void static RandomTransaction(CMutableTransaction& tx, bool fSingle, bool emptyInputScript = false) {
    bool isSidechain = (insecure_rand() % 2) == 0;
    if (isSidechain) {
        tx.nVersion = SC_TX_VERSION;
    } else {
        bool isGroth = (insecure_rand() % 2) == 0;
        if (isGroth) {
            tx.nVersion = GROTH_TX_VERSION;
        } else {
            // this can generate negative versions (including GROTH_TX_VERSION)
            // test will also have to verify if negative versions are rejected except GROTH_TX_VERSION
            tx.nVersion = insecure_rand();
        }
    }

    tx.vin.clear();
    tx.resizeOut(0);
    tx.vcsw_ccin.clear();
    tx.vsc_ccout.clear();
    tx.vft_ccout.clear();
    tx.vmbtr_out.clear();

    tx.nLockTime = (insecure_rand() % 2) ? insecure_rand() : 0;
    int ins = (insecure_rand() % 4) + 1;
    int csws = isSidechain ? (insecure_rand() % 4) : 0;
    int outs = fSingle ? ins + csws : (insecure_rand() % 4) + 1;

    // we can have also empty vectors here
    int joinsplits = (insecure_rand() % 4);
    int scs = isSidechain ? (insecure_rand() % 4) : 0;
    int fts = isSidechain ? (insecure_rand() % 4) : 0;
    int mbtrs = isSidechain ? (insecure_rand() % 4) : 0;
    int mbtrScRequestDataLength = isSidechain ? (insecure_rand() % 4) : 0;
    int FieldElementCertificateFieldConfigLength = isSidechain ? (insecure_rand() % 4) : 0;
    int BitVectorCertificateFieldConfigLength = isSidechain ? (insecure_rand() % 4) : 0;

    for (int in = 0; in < ins; in++) {
        tx.vin.push_back(CTxIn());
        CTxIn& txin = tx.vin.back();
        txin.prevout.hash = GetRandHash();
        txin.prevout.n = insecure_rand() % 4;
        if (emptyInputScript) {
            txin.scriptSig = CScript();
        } else {
            RandomScript(txin.scriptSig);
        }
        txin.nSequence = (insecure_rand() % 2) ? insecure_rand() : (unsigned int)-1;
    }
    for (int out = 0; out < outs; out++) {
        tx.addOut(CTxOut());
        CTxOut& txout = tx.getOut(tx.getVout().size() - 1);
        txout.nValue = insecure_rand() % 100000000;
        RandomScript(txout.scriptPubKey);
    }
    if (tx.nVersion >= PHGR_TX_VERSION || tx.nVersion == GROTH_TX_VERSION) {
        for (int js = 0; js < joinsplits; js++) {
            JSDescription jsdesc = JSDescription::getNewInstance(tx.nVersion == GROTH_TX_VERSION);
            if (insecure_rand() % 2 == 0) {
                jsdesc.vpub_old = insecure_rand() % 100000000;
            } else {
                jsdesc.vpub_new = insecure_rand() % 100000000;
            }

            jsdesc.anchor = GetRandHash();
            jsdesc.nullifiers[0] = GetRandHash();
            jsdesc.nullifiers[1] = GetRandHash();
            jsdesc.ephemeralKey = GetRandHash();
            jsdesc.randomSeed = GetRandHash();
            randombytes_buf(jsdesc.ciphertexts[0].begin(), jsdesc.ciphertexts[0].size());
            randombytes_buf(jsdesc.ciphertexts[1].begin(), jsdesc.ciphertexts[1].size());
            if (tx.nVersion == GROTH_TX_VERSION) {
                libzcash::GrothProof zkproof;
                randombytes_buf(zkproof.begin(), zkproof.size());
                jsdesc.proof = zkproof;
            } else {
                jsdesc.proof = libzcash::PHGRProof::random_invalid();
            }
            jsdesc.macs[0] = GetRandHash();
            jsdesc.macs[1] = GetRandHash();

            tx.vjoinsplit.push_back(jsdesc);
        }

        unsigned char joinSplitPrivKey[crypto_sign_SECRETKEYBYTES];
        crypto_sign_keypair(tx.joinSplitPubKey.begin(), joinSplitPrivKey);

        // Empty output script.
        CScript scriptCode;
        CTransaction signTx(tx);
        uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);

        assert(crypto_sign_detached(&tx.joinSplitSig[0], NULL, dataToBeSigned.begin(), 32, joinSplitPrivKey) == 0);
    }

    if (tx.nVersion == SC_TX_VERSION) {
        for (int csw = 0; csw < csws; csw++) {
            tx.vcsw_ccin.push_back(CTxCeasedSidechainWithdrawalInput());
            CTxCeasedSidechainWithdrawalInput& csw_in = tx.vcsw_ccin.back();

            csw_in.nValue = insecure_rand() % 100000000;
            csw_in.scId = libzcash::random_uint256();
            RandomSidechainField(csw_in.nullifier);
            RandomPubKeyHash(csw_in.pubKeyHash);
            RandomScProof(csw_in.scProof);
            RandomSidechainField(csw_in.actCertDataHash);
            RandomSidechainField(csw_in.ceasingCumScTxCommTree);

            if (emptyInputScript) {
                csw_in.redeemScript = CScript();
            } else {
                RandomScript(csw_in.redeemScript);
            }
        }

        for (int sc = 0; sc < scs; sc++) {
            tx.vsc_ccout.push_back(CTxScCreationOut());
            CTxScCreationOut& sc_out = tx.vsc_ccout.back();

            sc_out.nValue = insecure_rand() % 100000000;
            sc_out.address = libzcash::random_uint256();
            sc_out.withdrawalEpochLength = insecure_rand() % 100;
            RandomData(sc_out.customData);
            sc_out.constant = CFieldElement{};
            RandomSidechainField(sc_out.constant.get());
            RandomScVk(sc_out.wCertVk);
            CScVKey wCeasedVk;
            RandomScVk(wCeasedVk);
            sc_out.wCeasedVk = wCeasedVk;  // boost optional
            for (int i = 0; i < FieldElementCertificateFieldConfigLength; i++) {
                FieldElementCertificateFieldConfig cfg((insecure_rand() % 4) + 1);
                sc_out.vFieldElementCertificateFieldConfig.push_back(cfg);
            }
            for (int i = 0; i < BitVectorCertificateFieldConfigLength; i++) {
                BitVectorCertificateFieldConfig cfg((insecure_rand() % 4) + 1, (insecure_rand() % 4) + 1);
                sc_out.vBitVectorCertificateFieldConfig.push_back(cfg);
            }
            sc_out.forwardTransferScFee = insecure_rand() % 1000;
            sc_out.mainchainBackwardTransferRequestScFee = insecure_rand() % 1000;
            sc_out.mainchainBackwardTransferRequestDataLength = mbtrScRequestDataLength;
        }

        for (int ft = 0; ft < fts; ft++) {
            tx.vft_ccout.push_back(CTxForwardTransferOut());
            CTxForwardTransferOut& ft_out = tx.vft_ccout.back();

            ft_out.nValue = insecure_rand() % 100000000;
            ft_out.address = libzcash::random_uint256();
            ft_out.scId = libzcash::random_uint256();
            ft_out.mcReturnAddress = libzcash::random_uint160();
        }
        for (int mbtr = 0; mbtr < mbtrs; mbtr++) {
            tx.vmbtr_out.push_back(CBwtRequestOut());
            CBwtRequestOut& mbtr_out = tx.vmbtr_out.back();

            mbtr_out.scFee = insecure_rand() % 100000000;
            mbtr_out.mcDestinationAddress = random_uint160();
            mbtr_out.scId = libzcash::random_uint256();
            for (int r = 0; r < mbtrScRequestDataLength; r++) {
                CFieldElement fe;
                RandomSidechainField(fe);
                mbtr_out.vScRequestData.push_back(fe);
            }
        }
    }
}

void static RandomCertificate(CMutableScCertificate& cert, bool fSingle, bool emptyInputScript = false) {
    static const unsigned char NUM_RAND_UCHAR = 4;
    static const int NUM_RAND_UINT = 400000;
    cert.nVersion = SC_CERT_VERSION;
    cert.vin.clear();
    cert.resizeOut(0);

    cert.scId = GetRandHash();
    RandomScProof(cert.scProof);
    cert.epochNumber = (insecure_rand() % NUM_RAND_UCHAR) + 1;
    cert.quality = (insecure_rand() % NUM_RAND_UINT) + 1;
    RandomSidechainField(cert.endEpochCumScTxCommTreeRoot);

    int FieldElementCertificateFieldLength = (insecure_rand() % NUM_RAND_UCHAR);
    for (int i = 0; i < FieldElementCertificateFieldLength; i++) {
        CFieldElement fe;
        RandomSidechainField(fe);
        cert.vFieldElementCertificateField.push_back(FieldElementCertificateField(fe.GetByteArray()));
    }
    int BitVectorCertificateFieldLength = (insecure_rand() % NUM_RAND_UCHAR);
    for (int i = 0; i < BitVectorCertificateFieldLength; i++) {
        CFieldElement fe;
        RandomSidechainField(fe);
        cert.vBitVectorCertificateField.push_back(BitVectorCertificateField(fe.GetByteArray()));
    }
    cert.forwardTransferScFee = insecure_rand() % NUM_RAND_UINT + 1;
    cert.mainchainBackwardTransferRequestScFee = insecure_rand() % NUM_RAND_UINT + 1;

    int ins = (insecure_rand() % NUM_RAND_UCHAR) + 1;
    int outs = fSingle ? ins : (insecure_rand() % NUM_RAND_UCHAR);
    int bwt_outs = (insecure_rand() % NUM_RAND_UCHAR);

    for (int in = 0; in < ins; in++) {
        cert.vin.push_back(CTxIn());
        CTxIn& txin = cert.vin.back();
        txin.prevout.hash = GetRandHash();
        txin.prevout.n = insecure_rand() % NUM_RAND_UCHAR;
        if (emptyInputScript) {
            txin.scriptSig = CScript();
        } else {
            RandomScript(txin.scriptSig);
        }
        txin.nSequence = (insecure_rand() % 2) ? insecure_rand() : (unsigned int)-1;
    }

    for (int out = 0; out < outs; out++) {
        cert.addOut(CTxOut());
        CTxOut& txout = cert.getOut(cert.getVout().size() - 1);
        txout.nValue = insecure_rand() % 100000000;
        RandomScript(txout.scriptPubKey);
    }
    for (int out = 0; out < bwt_outs; out++) {
        cert.addBwt(CTxOut());
        CTxOut& txout = cert.getOut(cert.getVout().size() - 1);
        txout.nValue = insecure_rand() % 100000000;
        RandomScriptBwt(txout.scriptPubKey);
    }
}

BOOST_FIXTURE_TEST_SUITE(sighash_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(sighash_test) {
    seed_insecure_rand(false);

#if defined(PRINT_SIGHASH_JSON)
    std::cout << "[\n";
    std::cout << "\t[\"raw_transaction, script, input_index, hashType, signature_hash (result)\"],\n";
#endif
    int nRandomTests = 50000;

#if defined(PRINT_SIGHASH_JSON)
    nRandomTests = 500;
#endif
    for (int i = 0; i < nRandomTests; i++) {
        int nHashType = insecure_rand();
        CMutableTransaction txTo;
        RandomTransaction(txTo, (nHashType & 0x1f) == SIGHASH_SINGLE);
        CScript scriptCode;
        RandomScript(scriptCode);
        int nIn = insecure_rand() % (txTo.vin.size() + txTo.vcsw_ccin.size());

        uint256 sh, sho;
        sho = SignatureHashOld(scriptCode, txTo, nIn, nHashType);
        sh = SignatureHash(scriptCode, txTo, nIn, nHashType);
#if defined(PRINT_SIGHASH_JSON)
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << txTo;

        std::cout << "\t[\"";
        std::cout << HexStr(ss.begin(), ss.end()) << "\", \"";
        std::cout << HexStr(scriptCode) << "\", ";
        std::cout << nIn << ", ";
        std::cout << nHashType << ", \"";
        std::cout << sho.GetHex() << "\"]";
        if (i + 1 != nRandomTests) {
            std::cout << ",";
        }
        std::cout << "\n";
#endif
        /* useful in case of troubleshooting/debugging
                if (sh != sho)
                {
                    // do it again
                    sho = SignatureHashOld(scriptCode, txTo, nIn, nHashType);
                    sh = SignatureHash(scriptCode, txTo, nIn, nHashType);
                }
        */
        BOOST_CHECK(sh == sho);
    }
#if defined(PRINT_SIGHASH_JSON)
    std::cout << "]\n";
#endif
}

BOOST_AUTO_TEST_CASE(sighash_cert_test) {
    seed_insecure_rand(false);

#if defined(PRINT_SIGHASH_JSON)
    std::cout << "[\n";
    std::cout << "\t[\"raw_transaction, script, input_index, hashType, signature_hash (result)\"],\n";
#endif
    int nRandomTests = 50000;

#if defined(PRINT_SIGHASH_JSON)
    nRandomTests = 500;
#endif
    for (int i = 0; i < nRandomTests; i++) {
        int nHashType = insecure_rand();
        CMutableScCertificate certTo;
        RandomCertificate(certTo, (nHashType & 0x1f) == SIGHASH_SINGLE);
        CScript scriptCode;
        RandomScript(scriptCode);
        int nIn = insecure_rand() % certTo.vin.size();

        uint256 sh, sho;
        sho = SignatureHashCert(scriptCode, certTo, nIn, nHashType);
        sh = SignatureHash(scriptCode, certTo, nIn, nHashType);
#if defined(PRINT_SIGHASH_JSON)
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << certTo;

        std::cout << "\t[\"";
        std::cout << HexStr(ss.begin(), ss.end()) << "\", \"";
        std::cout << HexStr(scriptCode) << "\", ";
        std::cout << nIn << ", ";
        std::cout << nHashType << ", \"";
        std::cout << sho.GetHex() << "\"]";
        if (i + 1 != nRandomTests) {
            std::cout << ",";
        }
        std::cout << "\n";
#endif
        if (sh != sho) {
            std::cout << "nHashType = " << nHashType << std::endl;
        }
        BOOST_CHECK(sh == sho);
    }
#if defined(PRINT_SIGHASH_JSON)
    std::cout << "]\n";
#endif
}

// Goal: check that SignatureHash generates correct hash by checking if serialization matches with the one implemented in
// CTransaction
BOOST_AUTO_TEST_CASE(sighash_from_tx) {
    int nRandomTests = 500;

    for (int i = 0; i < nRandomTests; i++) {
        CMutableTransaction txTo;
        uint256 interpreterSH, checkSH;
        CScript scriptCode;

        RandomTransaction(txTo, false, true);
        CTransaction::joinsplit_sig_t nullSig = {};
        txTo.joinSplitSig = nullSig;

        interpreterSH = SignatureHash(scriptCode, txTo, NOT_AN_INPUT, SIGHASH_ALL);

        // Serialize and hash
        CHashWriter ss(SER_GETHASH, 0);
        ss << txTo << (int)SIGHASH_ALL;
        checkSH = ss.GetHash();
        BOOST_CHECK(checkSH == interpreterSH);
    }
}

// Goal: check that SignatureHash generates correct hash
BOOST_AUTO_TEST_CASE(sighash_from_data) {
    UniValue tests = read_json(std::string(json_tests::sighash, json_tests::sighash + sizeof(json_tests::sighash)));

    for (size_t idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 1)  // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        if (test.size() == 1) continue;  // comment

        std::string raw_tx, raw_script, sigHashHex;
        int nIn, nHashType;
        uint256 sh;
        CTransaction tx;
        CScript scriptCode = CScript();

        try {
            // deserialize test data
            raw_tx = test[0].get_str();
            raw_script = test[1].get_str();
            nIn = test[2].get_int();
            nHashType = test[3].get_int();
            sigHashHex = test[4].get_str();

            uint256 sh;
            CDataStream stream(ParseHex(raw_tx), SER_NETWORK, PROTOCOL_VERSION);
            stream >> tx;

            CValidationState state;

            if (tx.nVersion < MIN_OLD_TX_VERSION && tx.nVersion != GROTH_TX_VERSION) {
                // Transaction must be invalid
                BOOST_CHECK_MESSAGE(!CheckTransactionWithoutProofVerification(tx, state), strTest);
                BOOST_CHECK(!state.IsValid());
            } else {
                BOOST_CHECK_MESSAGE(CheckTransactionWithoutProofVerification(tx, state), strTest);
                BOOST_CHECK(state.IsValid());
            }

            std::vector<unsigned char> raw = ParseHex(raw_script);
            scriptCode.insert(scriptCode.end(), raw.begin(), raw.end());
        } catch (const std::exception& e) {
            BOOST_ERROR("Bad test (exception: \"" << e.what() << "\"), couldn't deserialize data: " << strTest);
            continue;
        } catch (...) {
            BOOST_ERROR("Bad test, couldn't deserialize data: " << strTest);
            continue;
        }

        sh = SignatureHash(scriptCode, tx, nIn, nHashType);
        BOOST_CHECK_MESSAGE(sh.GetHex() == sigHashHex, strTest);
    }
}
BOOST_AUTO_TEST_SUITE_END()
