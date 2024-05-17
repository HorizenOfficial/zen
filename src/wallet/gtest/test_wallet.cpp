// Copyright (c) 2018-2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sodium.h>

#include "base58.h"
#include "chainparams.h"
#include "main.h"
#include "primitives/block.h"
#include "random.h"
#include "utiltest.h"
#include "wallet/wallet.h"
#include "zcash/JoinSplit.hpp"
#include "zcash/Note.hpp"
#include "zcash/NoteEncryption.hpp"

#include <boost/filesystem.hpp>

using ::testing::Return;
using ::testing::Eq;
using ::testing::ByRef;

extern ZCJoinSplit* params;

ACTION(ThrowLogicError) {
    throw std::logic_error("Boom");
}

class MockWalletDB {
public:
    MOCK_METHOD0(TxnBegin, bool());
    MOCK_METHOD0(TxnCommit, bool());
    MOCK_METHOD0(TxnAbort, bool());

#if 0
    MOCK_METHOD2(WriteWalletTxBase, bool(uint256 hash, const CWalletTx& wtx));
#else
    MOCK_METHOD2(WriteWalletTxBase, bool(uint256 hash, const CWalletTransactionBase& wtx));
#endif
    MOCK_METHOD1(WriteWitnessCacheSize, bool(int64_t nWitnessCacheSize));
    MOCK_METHOD1(WriteBestBlock, bool(const CBlockLocator& loc));
};

template void CWallet::SetBestChainINTERNAL<MockWalletDB>(
        MockWalletDB& walletdb, const CBlockLocator& loc);

class TestWallet : public CWallet {
public:
    TestWallet() : CWallet(),
                   csWalletLock(cs_wallet, "cs_wallet", __FILE__, __LINE__) {}

    bool EncryptKeys(CKeyingMaterial& vMasterKeyIn) {
        return CCryptoKeyStore::EncryptKeys(vMasterKeyIn);
    }

    bool Unlock(const CKeyingMaterial& vMasterKeyIn) {
        return CCryptoKeyStore::Unlock(vMasterKeyIn);
    }

    void IncrementNoteWitnesses(const CBlockIndex* pindex,
                                const CBlock* pblock,
                                ZCIncrementalMerkleTree& tree) {
        CWallet::IncrementNoteWitnesses(pindex, pblock, tree);
    }
    void DecrementNoteWitnesses(const CBlockIndex* pindex) {
        CWallet::DecrementNoteWitnesses(pindex);
    }
    void SetBestChain(MockWalletDB& walletdb, const CBlockLocator& loc) {
        CWallet::SetBestChainINTERNAL(walletdb, loc);
    }
    bool UpdatedNoteData(const CWalletTx& wtxIn, CWalletTx& wtx) {
        return CWallet::UpdatedNoteData(wtxIn, wtx);
    }
    void MarkAffectedTransactionsDirty(const CTransaction& tx) {
        CWallet::MarkAffectedTransactionsDirty(tx);
    }

private:
    CCriticalBlock csWalletLock;
};

CWalletTx GetValidReceive(const libzcash::SpendingKey& sk, CAmount value, bool randomInputs) {
    return GetValidReceive(*params, sk, value, randomInputs);
}

CWalletTx GetInvalidCommitmentReceive(const libzcash::SpendingKey& sk, CAmount value, bool randomInputs, int32_t version = 2) {
    return GetInvalidCommitmentReceive(*params, sk, value, randomInputs, version);
}

libzcash::Note GetNote(const libzcash::SpendingKey& sk,
                       const CTransaction& tx, size_t js, size_t n) {
    return GetNote(*params, sk, tx, js, n);
}

CWalletTx GetValidSpend(const libzcash::SpendingKey& sk,
                        const libzcash::Note& note, CAmount value) {
    return GetValidSpend(*params, sk, note, value);
}

JSOutPoint CreateValidBlock(TestWallet& wallet,
                            const libzcash::SpendingKey& sk,
                            const CBlockIndex& index,
                            CBlock& block,
                            ZCIncrementalMerkleTree& tree) {
    auto wtx = GetValidReceive(sk, 50, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;
    wtx.SetNoteData(noteData);
    wallet.AddToWallet(wtx, true, NULL);

    block.vtx.push_back(wtx.getWrappedTx());
    wallet.IncrementNoteWitnesses(&index, &block, tree);

    return jsoutpt;
}

class WalletTest: public ::testing::Test {

protected:

    WalletTest() : csMainLock(cs_main, "cs_main", __FILE__, __LINE__) {}

    void SetUp() override {
        mempool.reset(new CTxMemPool(::minRelayTxFee, DEFAULT_MAX_MEMPOOL_SIZE_MB * 1000000));
    }

    CCriticalBlock csMainLock;
};

TEST_F(WalletTest, setup_datadir_location_run_as_first_test) {
    // Get temporary and unique path for file.
    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();
}

TEST_F(WalletTest, note_data_serialisation) {
    auto sk = libzcash::SpendingKey::random();
    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    ZCIncrementalMerkleTree tree;
    nd.witnesses.push_front(tree.witness());
    noteData[jsoutpt] = nd;

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << noteData;

    mapNoteData_t noteData2;
    ss >> noteData2;

    EXPECT_EQ(noteData, noteData2);
    EXPECT_EQ(noteData[jsoutpt].witnesses, noteData2[jsoutpt].witnesses);
}


TEST_F(WalletTest, find_unspent_notes) {

    SelectParams(CBaseChainParams::TESTNET);
    CWallet wallet;
    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;

    wtx.SetNoteData(noteData);
    wallet.AddToWallet(wtx, true, NULL);
    EXPECT_FALSE(wallet.IsSpent(nullifier));

    // We currently have an unspent and unconfirmed note in the wallet (depth of -1)
    std::vector<CNotePlaintextEntry> entries;
    wallet.GetFilteredNotes(entries, "", 0);
    EXPECT_EQ(0, entries.size());
    entries.clear();
    wallet.GetFilteredNotes(entries, "", -1);
    EXPECT_EQ(1, entries.size());
    entries.clear();

    // Fake-mine the transaction
    EXPECT_EQ(-1, chainActive.Height());
    CBlock block;
    block.vtx.push_back(wtx.getWrappedTx());
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
    chainActive.SetTip(&fakeIndex);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex));
    EXPECT_EQ(0, chainActive.Height());

    wtx.SetMerkleBranch(block);
    wallet.AddToWallet(wtx, true, NULL);
    EXPECT_FALSE(wallet.IsSpent(nullifier));


    // We now have an unspent and confirmed note in the wallet (depth of 1)
    wallet.GetFilteredNotes(entries, "", 0);
    EXPECT_EQ(1, entries.size());
    entries.clear();
    wallet.GetFilteredNotes(entries, "", 1);
    EXPECT_EQ(1, entries.size());
    entries.clear();
    wallet.GetFilteredNotes(entries, "", 2);
    EXPECT_EQ(0, entries.size());
    entries.clear();


    // Let's spend the note.
    auto wtx2 = GetValidSpend(sk, note, 5);
    wallet.AddToWallet(wtx2, true, NULL);
    EXPECT_FALSE(wallet.IsSpent(nullifier));

    // Fake-mine a spend transaction
    EXPECT_EQ(0, chainActive.Height());
    CBlock block2;
    block2.vtx.push_back(wtx2.getWrappedTx());
    block2.hashMerkleRoot = block2.BuildMerkleTree();
    block2.hashPrevBlock = blockHash;
    auto blockHash2 = block2.GetHash();
    CBlockIndex fakeIndex2 {block2};
    mapBlockIndex.insert(std::make_pair(blockHash2, &fakeIndex2));
    fakeIndex2.nHeight = 1;
    chainActive.SetTip(&fakeIndex2);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex2));
    EXPECT_EQ(1, chainActive.Height());

    wtx2.SetMerkleBranch(block2);
    wallet.AddToWallet(wtx2, true, NULL);
    EXPECT_TRUE(wallet.IsSpent(nullifier));

    // The note has been spent.  By default, GetFilteredNotes() ignores spent notes.
    wallet.GetFilteredNotes(entries, "", 0);
    EXPECT_EQ(0, entries.size());
    entries.clear();
    // Let's include spent notes to retrieve it.
    wallet.GetFilteredNotes(entries, "", 0, false);
    EXPECT_EQ(1, entries.size());
    entries.clear();
    // The spent note has two confirmations.
    wallet.GetFilteredNotes(entries, "", 2, false);
    EXPECT_EQ(1, entries.size());
    entries.clear();
    // It does not have 3 confirmations.
    wallet.GetFilteredNotes(entries, "", 3, false);
    EXPECT_EQ(0, entries.size());
    entries.clear();


    // Let's receive a new note
    std::unique_ptr<CWalletTx> wtx3;
    {
        auto wtx = GetValidReceive(sk, 20, true);
        auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
        auto nullifier = note.nullifier(sk);

        mapNoteData_t noteData;
        JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
        CNoteData nd {sk.address(), nullifier};
        noteData[jsoutpt] = nd;

        wtx.SetNoteData(noteData);
        wallet.AddToWallet(wtx, true, NULL);
        EXPECT_FALSE(wallet.IsSpent(nullifier));

        wtx3 = std::unique_ptr<CWalletTx>(new CWalletTx(wtx));
    }

    // Fake-mine the new transaction
    EXPECT_EQ(1, chainActive.Height());
    CBlock block3;
    block3.vtx.push_back(wtx3->getWrappedTx());
    block3.hashMerkleRoot = block3.BuildMerkleTree();
    block3.hashPrevBlock = blockHash2;
    auto blockHash3 = block3.GetHash();
    CBlockIndex fakeIndex3 {block3};
    mapBlockIndex.insert(std::make_pair(blockHash3, &fakeIndex3));
    fakeIndex3.nHeight = 2;
    chainActive.SetTip(&fakeIndex3);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex3));
    EXPECT_EQ(2, chainActive.Height());

    wtx3->SetMerkleBranch(block3);
    wallet.AddToWallet(*wtx3, true, NULL);

    // We now have an unspent note which has one confirmation, in addition to our spent note.
    wallet.GetFilteredNotes(entries, "", 1);
    EXPECT_EQ(1, entries.size());
    entries.clear();
    // Let's return the spent note too.
    wallet.GetFilteredNotes(entries, "", 1, false);
    EXPECT_EQ(2, entries.size());
    entries.clear();
    // Increasing number of confirmations will exclude our new unspent note.
    wallet.GetFilteredNotes(entries, "", 2, false);
    EXPECT_EQ(1, entries.size());
    entries.clear();    
    // If we also ignore spent notes at thie depth, we won't find any notes.
    wallet.GetFilteredNotes(entries, "", 2, true);
    EXPECT_EQ(0, entries.size());
    entries.clear(); 

    // Tear down
    chainActive.SetTip(NULL);
    mapBlockIndex.erase(blockHash);
    mapBlockIndex.erase(blockHash2);
    mapBlockIndex.erase(blockHash3);
}


TEST_F(WalletTest, set_note_addrs_in_cwallettx) {
    auto sk = libzcash::SpendingKey::random();
    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);
    EXPECT_EQ(0, wtx.mapNoteData.size());

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;

    wtx.SetNoteData(noteData);
    EXPECT_EQ(noteData, wtx.mapNoteData);
}

TEST_F(WalletTest, set_invalid_note_addrs_in_cwallettx) {
    CWalletTx wtx;
    EXPECT_EQ(0, wtx.mapNoteData.size());

    mapNoteData_t noteData;
    auto sk = libzcash::SpendingKey::random();
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), uint256()};
    noteData[jsoutpt] = nd;

    EXPECT_THROW(wtx.SetNoteData(noteData), std::logic_error);
}

TEST_F(WalletTest, CheckNoteCommitmentAgainstNotePlaintext) {
    CWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    auto address = sk.address();
    auto dec = ZCNoteDecryption(sk.receiving_key());

    auto wtx = GetInvalidCommitmentReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    auto hSig = wtx.getWrappedTx().GetVjoinsplit()[0].h_sig(
        *params, wtx.getWrappedTx().joinSplitPubKey);

    ASSERT_THROW(wallet.GetNoteNullifier(
        wtx.getWrappedTx().GetVjoinsplit()[0],
        address,
        dec,
        hSig, 1), libzcash::note_decryption_failed);
}

TEST_F(WalletTest, GetNoteNullifier) {
    CWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    auto address = sk.address();
    auto dec = ZCNoteDecryption(sk.receiving_key());

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    auto hSig = wtx.getWrappedTx().GetVjoinsplit()[0].h_sig(
        *params, wtx.getWrappedTx().joinSplitPubKey);

    auto ret = wallet.GetNoteNullifier(
        wtx.getWrappedTx().GetVjoinsplit()[0],
        address,
        dec,
        hSig, 1);
    EXPECT_NE(nullifier, ret);

    wallet.AddSpendingKey(sk);

    ret = wallet.GetNoteNullifier(
        wtx.getWrappedTx().GetVjoinsplit()[0],
        address,
        dec,
        hSig, 1);
    EXPECT_EQ(nullifier, ret);
}

TEST_F(WalletTest, FindMyNotes) {
    CWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    auto sk2 = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk2);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    auto noteMap = wallet.FindMyNotes(wtx.getWrappedTx());
    EXPECT_EQ(0, noteMap.size());

    wallet.AddSpendingKey(sk);

    noteMap = wallet.FindMyNotes(wtx.getWrappedTx());
    EXPECT_EQ(2, noteMap.size());

    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    EXPECT_EQ(1, noteMap.count(jsoutpt));
    EXPECT_EQ(nd, noteMap[jsoutpt]);
}

TEST_F(WalletTest, FindMyNotesInEncryptedWallet) {
    TestWallet wallet;
    uint256 r {GetRandHash()};
    CKeyingMaterial vMasterKey (r.begin(), r.end());

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    ASSERT_TRUE(wallet.EncryptKeys(vMasterKey));

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    auto noteMap = wallet.FindMyNotes(wtx.getWrappedTx());
    EXPECT_EQ(2, noteMap.size());

    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    EXPECT_EQ(1, noteMap.count(jsoutpt));
    EXPECT_NE(nd, noteMap[jsoutpt]);

    ASSERT_TRUE(wallet.Unlock(vMasterKey));

    noteMap = wallet.FindMyNotes(wtx.getWrappedTx());
    EXPECT_EQ(2, noteMap.size());
    EXPECT_EQ(1, noteMap.count(jsoutpt));
    EXPECT_EQ(nd, noteMap[jsoutpt]);
}

TEST_F(WalletTest, get_conflicted_notes) {
    TestWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    auto wtx2 = GetValidSpend(sk, note, 5);
    auto wtx3 = GetValidSpend(sk, note, 10);
    auto hash2 = wtx2.getWrappedTx().GetHash();
    auto hash3 = wtx3.getWrappedTx().GetHash();

    // No conflicts for no spends
    EXPECT_EQ(0, wallet.GetConflicts(hash2).size());
    wallet.AddToWallet(wtx, true, NULL);
    EXPECT_EQ(0, wallet.GetConflicts(hash2).size());

    // No conflicts for one spend
    wallet.AddToWallet(wtx2, true, NULL);
    EXPECT_EQ(0, wallet.GetConflicts(hash2).size());

    // Conflicts for two spends
    wallet.AddToWallet(wtx3, true, NULL);
    auto c3 = wallet.GetConflicts(hash2);
    EXPECT_EQ(2, c3.size());
    EXPECT_EQ(std::set<uint256>({hash2, hash3}), c3);
}

TEST_F(WalletTest, nullifier_is_spent) {
    CWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    EXPECT_FALSE(wallet.IsSpent(nullifier));

    wallet.AddToWallet(wtx, true, NULL);
    EXPECT_FALSE(wallet.IsSpent(nullifier));

    auto wtx2 = GetValidSpend(sk, note, 5);
    wallet.AddToWallet(wtx2, true, NULL);
    EXPECT_FALSE(wallet.IsSpent(nullifier));

    // Fake-mine the transaction
    EXPECT_EQ(-1, chainActive.Height());
    CBlock block;
    block.vtx.push_back(wtx2.getWrappedTx());
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
    chainActive.SetTip(&fakeIndex);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex));
    EXPECT_EQ(0, chainActive.Height());

    wtx2.SetMerkleBranch(block);
    wallet.AddToWallet(wtx2, true, NULL);
    EXPECT_TRUE(wallet.IsSpent(nullifier));

    // Tear down
    chainActive.SetTip(NULL);
    mapBlockIndex.erase(blockHash);
}

TEST_F(WalletTest, navigate_from_nullifier_to_note) {
    CWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;

    wtx.SetNoteData(noteData);

    EXPECT_EQ(0, wallet.mapNullifiersToNotes.count(nullifier));

    wallet.AddToWallet(wtx, true, NULL);
    EXPECT_EQ(1, wallet.mapNullifiersToNotes.count(nullifier));
    EXPECT_EQ(wtx.getWrappedTx().GetHash(), wallet.mapNullifiersToNotes[nullifier].hash);
    EXPECT_EQ(0, wallet.mapNullifiersToNotes[nullifier].js);
    EXPECT_EQ(1, wallet.mapNullifiersToNotes[nullifier].n);
}

TEST_F(WalletTest, spent_note_is_from_me) {
    CWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);
    auto wtx2 = GetValidSpend(sk, note, 5);

    EXPECT_FALSE(wallet.IsFromMe(wtx.getWrappedTx()));
    EXPECT_FALSE(wallet.IsFromMe(wtx2.getWrappedTx()));

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;

    wtx.SetNoteData(noteData);
    EXPECT_FALSE(wallet.IsFromMe(wtx.getWrappedTx()));
    EXPECT_FALSE(wallet.IsFromMe(wtx2.getWrappedTx()));

    wallet.AddToWallet(wtx, true, NULL);
    EXPECT_FALSE(wallet.IsFromMe(wtx.getWrappedTx()));
    EXPECT_TRUE(wallet.IsFromMe(wtx2.getWrappedTx()));
}

TEST_F(WalletTest, cached_witnesses_empty_chain) {
    TestWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 0);
    auto note2 = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);
    auto nullifier2 = note2.nullifier(sk);

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 0};
    JSOutPoint jsoutpt2 {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    CNoteData nd2 {sk.address(), nullifier2};
    noteData[jsoutpt] = nd;
    noteData[jsoutpt2] = nd2;
    wtx.SetNoteData(noteData);

    std::vector<JSOutPoint> notes {jsoutpt, jsoutpt2};
    std::vector<std::optional<ZCIncrementalWitness>> witnesses;
    uint256 anchor;

    wallet.GetNoteWitnesses(notes, witnesses, anchor);
    EXPECT_FALSE((bool) witnesses[0]);
    EXPECT_FALSE((bool) witnesses[1]);

    wallet.AddToWallet(wtx, true, NULL);
    witnesses.clear();
    wallet.GetNoteWitnesses(notes, witnesses, anchor);
    EXPECT_FALSE((bool) witnesses[0]);
    EXPECT_FALSE((bool) witnesses[1]);

    CBlock block;
    block.vtx.push_back(wtx.getWrappedTx());
    CBlockIndex index(block);
    ZCIncrementalMerkleTree tree;
    wallet.IncrementNoteWitnesses(&index, &block, tree);
    witnesses.clear();
    wallet.GetNoteWitnesses(notes, witnesses, anchor);
    EXPECT_TRUE((bool) witnesses[0]);
    EXPECT_TRUE((bool) witnesses[1]);

    // Until #1302 is implemented, this should triggger an assertion
    /*EXPECT_DEATH(wallet.DecrementNoteWitnesses(&index),
                 "Assertion( `| failed: \()nWitnessCacheSize > 0");*/
}

TEST_F(WalletTest, cached_witnesses_chain_tip) {
    TestWallet wallet;
    uint256 anchor1;
    CBlock block1;
    ZCIncrementalMerkleTree tree;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    {
        // First block (case tested in _empty_chain)
        CBlockIndex index1(block1);
        index1.nHeight = 1;
        auto jsoutpt = CreateValidBlock(wallet, sk, index1, block1, tree);

        // Called to fetch anchor
        std::vector<JSOutPoint> notes {jsoutpt};
        std::vector<std::optional<ZCIncrementalWitness>> witnesses;
        wallet.GetNoteWitnesses(notes, witnesses, anchor1);
    }

    {
        // Second transaction
        auto wtx = GetValidReceive(sk, 50, true);
        auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
        auto nullifier = note.nullifier(sk);

        mapNoteData_t noteData;
        JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
        CNoteData nd {sk.address(), nullifier};
        noteData[jsoutpt] = nd;
        wtx.SetNoteData(noteData);
        wallet.AddToWallet(wtx, true, NULL);

        std::vector<JSOutPoint> notes {jsoutpt};
        std::vector<std::optional<ZCIncrementalWitness>> witnesses;
        uint256 anchor2;

        wallet.GetNoteWitnesses(notes, witnesses, anchor2);
        EXPECT_FALSE((bool) witnesses[0]);

        // Second block
        CBlock block2;
        block2.hashPrevBlock = block1.GetHash();
        block2.vtx.push_back(wtx.getWrappedTx());
        CBlockIndex index2(block2);
        index2.nHeight = 2;
        ZCIncrementalMerkleTree tree2 {tree};
        wallet.IncrementNoteWitnesses(&index2, &block2, tree2);
        witnesses.clear();
        wallet.GetNoteWitnesses(notes, witnesses, anchor2);
        EXPECT_TRUE((bool) witnesses[0]);
        EXPECT_NE(anchor1, anchor2);

        // Decrementing should give us the previous anchor
        uint256 anchor3;
        wallet.DecrementNoteWitnesses(&index2);
        witnesses.clear();
        wallet.GetNoteWitnesses(notes, witnesses, anchor3);
        EXPECT_FALSE((bool) witnesses[0]);
        // Should not equal first anchor because none of these notes had witnesses
        EXPECT_NE(anchor1, anchor3);

        // Re-incrementing with the same block should give the same result
        uint256 anchor4;
        wallet.IncrementNoteWitnesses(&index2, &block2, tree);
        witnesses.clear();
        wallet.GetNoteWitnesses(notes, witnesses, anchor4);
        EXPECT_TRUE((bool) witnesses[0]);
        EXPECT_EQ(anchor2, anchor4);

        // Incrementing with the same block again should not change the cache
        uint256 anchor5;
        wallet.IncrementNoteWitnesses(&index2, &block2, tree);
        std::vector<std::optional<ZCIncrementalWitness>> witnesses5;
        wallet.GetNoteWitnesses(notes, witnesses5, anchor5);
        EXPECT_EQ(witnesses, witnesses5);
        EXPECT_EQ(anchor4, anchor5);
    }
}

TEST_F(WalletTest, CachedWitnessesDecrementFirst) {
    TestWallet wallet;
    uint256 anchor2;
    CBlock block2;
    CBlockIndex index2(block2);
    ZCIncrementalMerkleTree tree;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    {
        // First block (case tested in _empty_chain)
        CBlock block1;
        CBlockIndex index1(block1);
        index1.nHeight = 1;
        CreateValidBlock(wallet, sk, index1, block1, tree);
    }

    {
        // Second block (case tested in _chain_tip)
        index2.nHeight = 2;
        auto jsoutpt = CreateValidBlock(wallet, sk, index2, block2, tree);

        // Called to fetch anchor
        std::vector<JSOutPoint> notes {jsoutpt};
        std::vector<std::optional<ZCIncrementalWitness>> witnesses;
        wallet.GetNoteWitnesses(notes, witnesses, anchor2);
    }

    {
        // Third transaction - never mined
        auto wtx = GetValidReceive(sk, 20, true);
        auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
        auto nullifier = note.nullifier(sk);

        mapNoteData_t noteData;
        JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
        CNoteData nd {sk.address(), nullifier};
        noteData[jsoutpt] = nd;
        wtx.SetNoteData(noteData);
        wallet.AddToWallet(wtx, true, NULL);

        std::vector<JSOutPoint> notes {jsoutpt};
        std::vector<std::optional<ZCIncrementalWitness>> witnesses;
        uint256 anchor3;

        wallet.GetNoteWitnesses(notes, witnesses, anchor3);
        EXPECT_FALSE((bool) witnesses[0]);

        // Decrementing (before the transaction has ever seen an increment)
        // should give us the previous anchor
        uint256 anchor4;
        wallet.DecrementNoteWitnesses(&index2);
        witnesses.clear();
        wallet.GetNoteWitnesses(notes, witnesses, anchor4);
        EXPECT_FALSE((bool) witnesses[0]);
        // Should not equal second anchor because none of these notes had witnesses
        EXPECT_NE(anchor2, anchor4);

        // Re-incrementing with the same block should give the same result
        uint256 anchor5;
        wallet.IncrementNoteWitnesses(&index2, &block2, tree);
        witnesses.clear();
        wallet.GetNoteWitnesses(notes, witnesses, anchor5);
        EXPECT_FALSE((bool) witnesses[0]);
        EXPECT_EQ(anchor3, anchor5);
    }
}

TEST_F(WalletTest, CachedWitnessesCleanIndex) {
    TestWallet wallet;
    std::vector<CBlock> blocks;
    std::vector<CBlockIndex> indices;
    std::vector<JSOutPoint> notes;
    std::vector<uint256> anchors;
    ZCIncrementalMerkleTree tree;
    ZCIncrementalMerkleTree riTree = tree;
    std::vector<std::optional<ZCIncrementalWitness>> witnesses;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    // Generate a chain
    size_t numBlocks = WITNESS_CACHE_SIZE + 10;
    blocks.resize(numBlocks);
    indices.resize(numBlocks);
    for (size_t i = 0; i < numBlocks; i++) {
        indices[i].nHeight = i;
        auto old = tree.root();
        auto jsoutpt = CreateValidBlock(wallet, sk, indices[i], blocks[i], tree);
        EXPECT_NE(old, tree.root());
        notes.push_back(jsoutpt);

        witnesses.clear();
        uint256 anchor;
        wallet.GetNoteWitnesses(notes, witnesses, anchor);
        for (size_t j = 0; j <= i; j++) {
            EXPECT_TRUE((bool) witnesses[j]);
        }
        anchors.push_back(anchor);
    }

    // Now pretend we are reindexing: the chain is cleared, and each block is
    // used to increment witnesses again.
    for (size_t i = 0; i < numBlocks; i++) {
        ZCIncrementalMerkleTree riPrevTree {riTree};
        wallet.IncrementNoteWitnesses(&(indices[i]), &(blocks[i]), riTree);
        witnesses.clear();
        uint256 anchor;
        wallet.GetNoteWitnesses(notes, witnesses, anchor);
        for (size_t j = 0; j < numBlocks; j++) {
            EXPECT_TRUE((bool) witnesses[j]);
        }
        // Should equal final anchor because witness cache unaffected
        EXPECT_EQ(anchors.back(), anchor);

        if ((i == 5) || (i == 50)) {
            // Pretend a reorg happened that was recorded in the block files
            {
                wallet.DecrementNoteWitnesses(&(indices[i]));
                witnesses.clear();
                uint256 anchor;
                wallet.GetNoteWitnesses(notes, witnesses, anchor);
                for (size_t j = 0; j < numBlocks; j++) {
                    EXPECT_TRUE((bool) witnesses[j]);
                }
                // Should equal final anchor because witness cache unaffected
                EXPECT_EQ(anchors.back(), anchor);
            }

            {
                wallet.IncrementNoteWitnesses(&(indices[i]), &(blocks[i]), riPrevTree);
                witnesses.clear();
                uint256 anchor;
                wallet.GetNoteWitnesses(notes, witnesses, anchor);
                for (size_t j = 0; j < numBlocks; j++) {
                    EXPECT_TRUE((bool) witnesses[j]);
                }
                // Should equal final anchor because witness cache unaffected
                EXPECT_EQ(anchors.back(), anchor);
            }
        }
    }
}

TEST_F(WalletTest, ClearNoteWitnessCache) {
    TestWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto hash = wtx.getWrappedTx().GetHash();
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 0);
    auto nullifier = note.nullifier(sk);

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 0};
    JSOutPoint jsoutpt2 {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;
    wtx.SetNoteData(noteData);

    // Pretend we mined the tx by adding a fake witness
    ZCIncrementalMerkleTree tree;
    wtx.mapNoteData[jsoutpt].witnesses.push_front(tree.witness());
    wtx.mapNoteData[jsoutpt].witnessHeight = 1;
    wallet.nWitnessCacheSize = 1;

    wallet.AddToWallet(wtx, true, NULL);

    std::vector<JSOutPoint> notes {jsoutpt, jsoutpt2};
    std::vector<std::optional<ZCIncrementalWitness>> witnesses;
    uint256 anchor2;

    // Before clearing, we should have a witness for one note
    wallet.GetNoteWitnesses(notes, witnesses, anchor2);
    EXPECT_TRUE((bool) witnesses[0]);
    EXPECT_FALSE((bool) witnesses[1]);
    EXPECT_EQ(1, wallet.getMapWallet().at(hash)->mapNoteData[jsoutpt].witnessHeight);
    EXPECT_EQ(1, wallet.nWitnessCacheSize);

    // After clearing, we should not have a witness for either note
    wallet.ClearNoteWitnessCache();
    witnesses.clear();
    wallet.GetNoteWitnesses(notes, witnesses, anchor2);
    EXPECT_FALSE((bool) witnesses[0]);
    EXPECT_FALSE((bool) witnesses[1]);
    EXPECT_EQ(-1, wallet.getMapWallet().at(hash)->mapNoteData[jsoutpt].witnessHeight);
    EXPECT_EQ(0, wallet.nWitnessCacheSize);
}

TEST_F(WalletTest, WriteWitnessCache) {
    TestWallet wallet;
    MockWalletDB walletdb;
    CBlockLocator loc;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;
    wtx.SetNoteData(noteData);

    CWalletTransactionBase& refWtx(wtx);
    wallet.AddToWallet(refWtx, true, NULL);

    // TxnBegin fails
    EXPECT_CALL(walletdb, TxnBegin())
        .WillOnce(Return(false));
    wallet.SetBestChain(walletdb, loc);
    EXPECT_CALL(walletdb, TxnBegin())
        .WillRepeatedly(Return(true));

    // WriteWalletTxBase fails
    EXPECT_CALL(walletdb, WriteWalletTxBase(wtx.getWrappedTx().GetHash(), Eq(ByRef(refWtx))))
        .WillOnce(Return(false));
    EXPECT_CALL(walletdb, TxnAbort())
        .Times(1);
    wallet.SetBestChain(walletdb, loc);             
                                                    
    // WriteWalletTxBase throws
    EXPECT_CALL(walletdb, WriteWalletTxBase(wtx.getWrappedTx().GetHash(), Eq(ByRef(refWtx))))
        .WillOnce(ThrowLogicError());
    EXPECT_CALL(walletdb, TxnAbort())
        .Times(1);
    wallet.SetBestChain(walletdb, loc);
    EXPECT_CALL(walletdb, WriteWalletTxBase(wtx.getWrappedTx().GetHash(), Eq(ByRef(refWtx))))
        .WillRepeatedly(Return(true));

    // WriteWitnessCacheSize fails
    EXPECT_CALL(walletdb, WriteWitnessCacheSize(0))
        .WillOnce(Return(false));
    EXPECT_CALL(walletdb, TxnAbort())
        .Times(1);
    wallet.SetBestChain(walletdb, loc);

    // WriteWitnessCacheSize throws
    EXPECT_CALL(walletdb, WriteWitnessCacheSize(0))
        .WillOnce(ThrowLogicError());
    EXPECT_CALL(walletdb, TxnAbort())
        .Times(1);
    wallet.SetBestChain(walletdb, loc);
    EXPECT_CALL(walletdb, WriteWitnessCacheSize(0))
        .WillRepeatedly(Return(true));

    // WriteBestBlock fails
    EXPECT_CALL(walletdb, WriteBestBlock(loc))
        .WillOnce(Return(false));
    EXPECT_CALL(walletdb, TxnAbort())
        .Times(1);
    wallet.SetBestChain(walletdb, loc);

    // WriteBestBlock throws
    EXPECT_CALL(walletdb, WriteBestBlock(loc))
        .WillOnce(ThrowLogicError());
    EXPECT_CALL(walletdb, TxnAbort())
        .Times(1);
    wallet.SetBestChain(walletdb, loc);
    EXPECT_CALL(walletdb, WriteBestBlock(loc))
        .WillRepeatedly(Return(true));

    // TxCommit fails
    EXPECT_CALL(walletdb, TxnCommit())
        .WillOnce(Return(false));
    wallet.SetBestChain(walletdb, loc);
    EXPECT_CALL(walletdb, TxnCommit())
        .WillRepeatedly(Return(true));

    // Everything succeeds
    wallet.SetBestChain(walletdb, loc);
}

TEST_F(WalletTest, UpdateNullifierNoteMap) {
    TestWallet wallet;
    uint256 r {GetRandHash()};
    CKeyingMaterial vMasterKey (r.begin(), r.end());

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    ASSERT_TRUE(wallet.EncryptKeys(vMasterKey));

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);

    // Pretend that we called FindMyNotes while the wallet was locked
    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd {sk.address()};
    noteData[jsoutpt] = nd;
    wtx.SetNoteData(noteData);

    wallet.AddToWallet(wtx, true, NULL);
    EXPECT_EQ(0, wallet.mapNullifiersToNotes.count(nullifier));

    EXPECT_FALSE(wallet.UpdateNullifierNoteMap());

    ASSERT_TRUE(wallet.Unlock(vMasterKey));

    EXPECT_TRUE(wallet.UpdateNullifierNoteMap());
    EXPECT_EQ(1, wallet.mapNullifiersToNotes.count(nullifier));
    EXPECT_EQ(wtx.getWrappedTx().GetHash(), wallet.mapNullifiersToNotes[nullifier].hash);
    EXPECT_EQ(0, wallet.mapNullifiersToNotes[nullifier].js);
    EXPECT_EQ(1, wallet.mapNullifiersToNotes[nullifier].n);
}

TEST_F(WalletTest, UpdatedNoteData) {
    TestWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 0);
    auto note2 = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);
    auto nullifier2 = note2.nullifier(sk);
    auto wtx2 = wtx;

    // First pretend we added the tx to the wallet and
    // we don't have the key for the second note
    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 0};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;
    wtx.SetNoteData(noteData);

    // Pretend we mined the tx by adding a fake witness
    ZCIncrementalMerkleTree tree;
    wtx.mapNoteData[jsoutpt].witnesses.push_front(tree.witness());
    wtx.mapNoteData[jsoutpt].witnessHeight = 100;

    // Now pretend we added the key for the second note, and
    // the tx was "added" to the wallet again to update it.
    // This happens via the 'z_importkey' RPC method.
    JSOutPoint jsoutpt2 {wtx2.getWrappedTx().GetHash(), 0, 1};
    CNoteData nd2 {sk.address(), nullifier2};
    noteData[jsoutpt2] = nd2;
    wtx2.SetNoteData(noteData);

    // The txs should initially be different
    EXPECT_NE(wtx.mapNoteData, wtx2.mapNoteData);
    EXPECT_EQ(1, wtx.mapNoteData[jsoutpt].witnesses.size());
    EXPECT_EQ(100, wtx.mapNoteData[jsoutpt].witnessHeight);

    // After updating, they should be the same
    EXPECT_TRUE(wallet.UpdatedNoteData(wtx2, wtx));
    EXPECT_EQ(wtx.mapNoteData, wtx2.mapNoteData);
    EXPECT_EQ(1, wtx.mapNoteData[jsoutpt].witnesses.size());
    EXPECT_EQ(100, wtx.mapNoteData[jsoutpt].witnessHeight);
    // TODO: The new note should get witnessed (but maybe not here) (#1350)
}

TEST_F(WalletTest, MarkAffectedTransactionsDirty) {
    TestWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto hash = wtx.getWrappedTx().GetHash();
    auto note = GetNote(sk, wtx.getWrappedTx(), 0, 1);
    auto nullifier = note.nullifier(sk);
    auto wtx2 = GetValidSpend(sk, note, 5);

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {hash, 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;

    wtx.SetNoteData(noteData);
    wallet.AddToWallet(wtx, true, NULL);
    wallet.MarkAffectedTransactionsDirty(wtx.getWrappedTx());

    // After getting a cached value, the first tx should be clean
    wallet.getMapWallet().at(hash)->GetDebit(ISMINE_ALL);
    EXPECT_TRUE(wallet.getMapWallet().at(hash)->GetfDebitCached());

    // After adding the note spend, the first tx should be dirty
    wallet.AddToWallet(wtx2, true, NULL);
    wallet.MarkAffectedTransactionsDirty(wtx2.getWrappedTx());
    EXPECT_FALSE(wallet.getMapWallet().at(hash)->GetfDebitCached());
}

TEST_F(WalletTest, SetBestChainIgnoresTxsWithoutShieldedData) {
    SelectParams(CBaseChainParams::REGTEST);

    TestWallet wallet;
    MockWalletDB walletdb;
    CBlockLocator loc;

    // Set up transparent address
    CKey tsk;
    tsk.MakeNewKey(true);
    wallet.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    // Set up a Sprout address
    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    // Generate a transparent transaction that is ours
    CMutableTransaction t;
    t.resizeOut(1);
    t.getOut(0).nValue = 90*CENT;
    t.getOut(0).scriptPubKey = scriptPubKey;
    CWalletTx wtxTransparent {nullptr, t};
    wallet.AddToWallet(wtxTransparent, true, nullptr);

    // Generate a Sprout transaction that is ours
    auto wtxSprout = GetValidReceive(sk, 10, true);
    auto noteMap = wallet.FindMyNotes(wtxSprout.getWrappedTx());
    wtxSprout.SetNoteData(noteMap);
    wallet.AddToWallet(wtxSprout, true, nullptr);

    // Generate a Sprout transaction that only involves our transparent address
    auto sk2 = libzcash::SpendingKey::random();
    auto wtxInput = GetValidReceive(sk2, 10, true);
    auto note = GetNote(sk2, wtxInput.getWrappedTx(), 0, 0);
    auto wtxTmp = GetValidSpend(sk2, note, 5);
    CMutableTransaction mtx {wtxTmp.getWrappedTx()};
    mtx.getOut(0).scriptPubKey = scriptPubKey;
    CWalletTx wtxSproutTransparent {nullptr, mtx};
    wallet.AddToWallet(wtxSproutTransparent, true, nullptr);

    EXPECT_CALL(walletdb, TxnBegin())
        .WillOnce(Return(true));
    EXPECT_CALL(walletdb, WriteWalletTxBase(wtxTransparent.getWrappedTx().GetHash(), Eq(ByRef(wtxTransparent))))
        .Times(0);
    EXPECT_CALL(walletdb, WriteWalletTxBase(wtxSprout.getWrappedTx().GetHash(), Eq(ByRef(wtxSprout))))
        .Times(1).WillOnce(Return(true));
    EXPECT_CALL(walletdb, WriteWalletTxBase(wtxSproutTransparent.getWrappedTx().GetHash(), Eq(ByRef(wtxSproutTransparent))))
        .Times(0);
    EXPECT_CALL(walletdb, WriteWitnessCacheSize(0))
        .WillOnce(Return(true));
    EXPECT_CALL(walletdb, WriteBestBlock(loc))
        .WillOnce(Return(true));
    EXPECT_CALL(walletdb, TxnCommit())
        .WillOnce(Return(true));
    wallet.SetBestChain(walletdb, loc);
}

TEST_F(WalletTest, NoteLocking) {
    TestWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto wtx2 = GetValidReceive(sk, 10, true);

    JSOutPoint jsoutpt {wtx.getWrappedTx().GetHash(), 0, 0};
    JSOutPoint jsoutpt2 {wtx2.getWrappedTx().GetHash(),0, 0};

    // Test selective locking
    wallet.LockNote(jsoutpt);
    EXPECT_TRUE(wallet.IsLockedNote(jsoutpt.hash, jsoutpt.js, jsoutpt.n));
    EXPECT_FALSE(wallet.IsLockedNote(jsoutpt2.hash, jsoutpt2.js, jsoutpt2.n));

    // Test selective unlocking
    wallet.UnlockNote(jsoutpt);
    EXPECT_FALSE(wallet.IsLockedNote(jsoutpt.hash, jsoutpt.js, jsoutpt.n));

    // Test multiple locking
    wallet.LockNote(jsoutpt);
    wallet.LockNote(jsoutpt2);
    EXPECT_TRUE(wallet.IsLockedNote(jsoutpt.hash, jsoutpt.js, jsoutpt.n));
    EXPECT_TRUE(wallet.IsLockedNote(jsoutpt2.hash, jsoutpt2.js, jsoutpt2.n));

    // Test unlock all
    wallet.UnlockAllNotes();
    EXPECT_FALSE(wallet.IsLockedNote(jsoutpt.hash, jsoutpt.js, jsoutpt.n));
    EXPECT_FALSE(wallet.IsLockedNote(jsoutpt2.hash, jsoutpt2.js, jsoutpt2.n));
}

TEST_F(WalletTest, HaveWatchOnly) {
    TestWallet wallet;

    EXPECT_FALSE(wallet.HaveWatchOnly());

    // Add watch only scripts for multiple types

    // P2SH
    CScriptID script_id;
    GetRandBytes((unsigned char*)&script_id, sizeof(script_id));
    CScript p2sh_scriptPubKey_wo_replay;
    p2sh_scriptPubKey_wo_replay << ToByteVector(script_id) << OP_EQUAL;
    EXPECT_TRUE(wallet.AddWatchOnly(p2sh_scriptPubKey_wo_replay));

    // P2PK
    std::vector<unsigned char> rnd_bytes(PUBLIC_KEY_SIZE);
    GetRandBytes(rnd_bytes.data(), PUBLIC_KEY_SIZE);
    CPubKey pubkey(rnd_bytes.begin(), rnd_bytes.end());
    CScript p2pk_scriptPubKey_wo_replay;
    p2pk_scriptPubKey_wo_replay << OP_HASH160 << ToByteVector(pubkey) << OP_CHECKSIG;
    EXPECT_TRUE(wallet.AddWatchOnly(p2pk_scriptPubKey_wo_replay));

    // P2PKH
    CScript p2pkh_scriptPubKey_wo_replay;
    p2pkh_scriptPubKey_wo_replay << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    EXPECT_TRUE(wallet.AddWatchOnly(p2pkh_scriptPubKey_wo_replay));

    EXPECT_TRUE(wallet.HaveWatchOnly());

    // Add replay protection to each of the previous scripts
    uint256 random_block_hash = GetRandHash();

    CScript p2sh_scriptPubKey_w_replay = p2sh_scriptPubKey_wo_replay;
    p2sh_scriptPubKey_w_replay << ToByteVector(random_block_hash) << 21 << OP_CHECKBLOCKATHEIGHT;

    CScript p2pk_scriptPubKey_w_replay = p2pk_scriptPubKey_wo_replay;
    p2pk_scriptPubKey_w_replay << ToByteVector(random_block_hash) << 21 << OP_CHECKBLOCKATHEIGHT;

    CScript p2pkh_scriptPubKey_w_replay = p2pkh_scriptPubKey_wo_replay;
    p2pkh_scriptPubKey_w_replay << ToByteVector(random_block_hash) << 21 << OP_CHECKBLOCKATHEIGHT;

    // Check that scripts with replay protection are found
    EXPECT_TRUE(wallet.HaveWatchOnly(p2sh_scriptPubKey_w_replay));
    EXPECT_TRUE(wallet.HaveWatchOnly(p2pk_scriptPubKey_w_replay));
    EXPECT_TRUE(wallet.HaveWatchOnly(p2pkh_scriptPubKey_w_replay));

    // Check that scripts without replay protection are found
    EXPECT_TRUE(wallet.HaveWatchOnly(p2sh_scriptPubKey_wo_replay));
    EXPECT_TRUE(wallet.HaveWatchOnly(p2pk_scriptPubKey_wo_replay));
    EXPECT_TRUE(wallet.HaveWatchOnly(p2pkh_scriptPubKey_wo_replay));

    // Check that another script is not found
    CScriptID another_script_id;
    GetRandBytes((unsigned char*)&another_script_id, sizeof(another_script_id));
    CScript another_p2sh_scriptPubKey_w_replay;
    another_p2sh_scriptPubKey_w_replay << OP_HASH160 << ToByteVector(another_script_id) << OP_EQUAL << ToByteVector(random_block_hash) << 21 << OP_CHECKBLOCKATHEIGHT;
    EXPECT_FALSE(wallet.HaveWatchOnly(another_p2sh_scriptPubKey_w_replay));
}
