// Copyright (c) 2016 The Zcash developers
// Copyright (c) 2018-2020 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utiltest.h"

#include <array>

CMutableTransaction GetValidReceiveTransaction(ZCJoinSplit& params,
                                const libzcash::SpendingKey& sk,
                                CAmount value,
                                bool randomInputs,
                                int32_t version /* = 2 */) {
    CMutableTransaction mtx;
    mtx.nVersion = 2; // Enable JoinSplits
    mtx.vin.resize(2);
    if (randomInputs) {
        mtx.vin[0].prevout.hash = GetRandHash();
        mtx.vin[1].prevout.hash = GetRandHash();
    } else {
        mtx.vin[0].prevout.hash = uint256S("0000000000000000000000000000000000000000000000000000000000000001");
        mtx.vin[1].prevout.hash = uint256S("0000000000000000000000000000000000000000000000000000000000000002");
    }
    mtx.vin[0].prevout.n = 0;
    mtx.vin[1].prevout.n = 0;

    // Generate an ephemeral keypair.
    uint256 joinSplitPubKey;
    unsigned char joinSplitPrivKey[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(joinSplitPubKey.begin(), joinSplitPrivKey);
    mtx.joinSplitPubKey = joinSplitPubKey;

    std::array<libzcash::JSInput, 2> inputs = {
        libzcash::JSInput(), // dummy input
        libzcash::JSInput() // dummy input
    };

    std::array<libzcash::JSOutput, 2> outputs = {
        libzcash::JSOutput(sk.address(), value),
        libzcash::JSOutput(sk.address(), value)
    };

    // Prepare JoinSplits
    uint256 rt;
    JSDescription jsdesc {false, params, mtx.joinSplitPubKey, rt,
                          inputs, outputs, 2*value, 0, false};
    mtx.vjoinsplit.push_back(jsdesc);

    // Empty output script.
    CScript scriptCode;
    CTransaction signTx(mtx);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);

    // Add the signature
    assert(crypto_sign_detached(&mtx.joinSplitSig[0], NULL,
                                dataToBeSigned.begin(), 32,
                                joinSplitPrivKey
                               ) == 0);

    return mtx;
}

CWalletTx GetValidReceive(ZCJoinSplit& params,
                                const libzcash::SpendingKey& sk,
                                CAmount value,
                                bool randomInputs,
                                int32_t version /* = 2 */)
{
    CMutableTransaction mtx = GetValidReceiveTransaction(
        params, sk, value, randomInputs, version
    );
    CTransaction tx {mtx};
    CWalletTx wtx {NULL, tx};
    return wtx;
}

CWalletTx GetInvalidCommitmentReceive(ZCJoinSplit& params,
                                const libzcash::SpendingKey& sk,
                                CAmount value,
                                bool randomInputs,
                                int32_t version /* = 2 */)
{
    CMutableTransaction mtx = GetValidReceiveTransaction(
        params, sk, value, randomInputs, version
    );
    mtx.vjoinsplit[0].commitments[0] = uint256();
    mtx.vjoinsplit[0].commitments[1] = uint256();
    CTransaction tx {mtx};
    CWalletTx wtx {NULL, tx};
    return wtx;
}

libzcash::Note GetNote(ZCJoinSplit& params,
                       const libzcash::SpendingKey& sk,
                       const CTransaction& tx, size_t js, size_t n) {
    ZCNoteDecryption decryptor {sk.receiving_key()};
    auto hSig = tx.GetVjoinsplit()[js].h_sig(params, tx.joinSplitPubKey);
    auto note_pt = libzcash::NotePlaintext::decrypt(
        decryptor,
        tx.GetVjoinsplit()[js].ciphertexts[n],
        tx.GetVjoinsplit()[js].ephemeralKey,
        hSig,
        (unsigned char) n);
    return note_pt.note(sk.address());
}

CWalletTx GetValidSpend(ZCJoinSplit& params,
                        const libzcash::SpendingKey& sk,
                        const libzcash::Note& note, CAmount value) {
    CMutableTransaction mtx;
    mtx.addOut(CTxOut(value,CScript()));
    mtx.addOut(CTxOut(0,CScript()));

    // Generate an ephemeral keypair.
    uint256 joinSplitPubKey;
    unsigned char joinSplitPrivKey[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(joinSplitPubKey.begin(), joinSplitPrivKey);
    mtx.joinSplitPubKey = joinSplitPubKey;

    // Fake tree for the unused witness
    ZCIncrementalMerkleTree tree;

    libzcash::JSOutput dummyout;
    libzcash::JSInput dummyin;

    {
        if (note.value() > value) {
            libzcash::SpendingKey dummykey = libzcash::SpendingKey::random();
            libzcash::PaymentAddress dummyaddr = dummykey.address();
            dummyout = libzcash::JSOutput(dummyaddr, note.value() - value);
        } else if (note.value() < value) {
            libzcash::SpendingKey dummykey = libzcash::SpendingKey::random();
            libzcash::PaymentAddress dummyaddr = dummykey.address();
            libzcash::Note dummynote(dummyaddr.a_pk, (value - note.value()), uint256(), uint256());
            tree.append(dummynote.cm());
            dummyin = libzcash::JSInput(tree.witness(), dummynote, dummykey);
        }
    }

    tree.append(note.cm());

    std::array<libzcash::JSInput, 2> inputs = {
        libzcash::JSInput(tree.witness(), note, sk),
        dummyin
    };

    std::array<libzcash::JSOutput, 2> outputs = {
        dummyout, // dummy output
        libzcash::JSOutput() // dummy output
    };

    // Prepare JoinSplits
    uint256 rt = tree.root();
    JSDescription jsdesc {false, params, mtx.joinSplitPubKey, rt,
                          inputs, outputs, 0, value, false};
    mtx.vjoinsplit.push_back(jsdesc);

    // Empty output script.
    CScript scriptCode;
    CTransaction signTx(mtx);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);

    // Add the signature
    assert(crypto_sign_detached(&mtx.joinSplitSig[0], NULL,
                                dataToBeSigned.begin(), 32,
                                joinSplitPrivKey
                               ) == 0);
    CTransaction tx {mtx};
    CWalletTx wtx {NULL, tx};
    return wtx;
}
