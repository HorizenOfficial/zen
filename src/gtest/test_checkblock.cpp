// Copyright (c) 2018-2022 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "consensus/validation.h"
#include "main.h"
#include "zcash/Proof.hpp"
#include <pow.h>
#include "base58.h"
#include "zen/forkmanager.h"
#include "zen/forks/fork1_chainsplitfork.h"
#include "zen/forks/fork3_communityfundandrpfixfork.h"
#include "zen/forks/fork4_nulltransactionfork.h"
#include "zen/forks/fork5_shieldfork.h"
#include "zen/forks/fork8_sidechainfork.h"

#include "tx_creation_utils.h"

using namespace zen;

TEST(CheckBlock, VersionTooLow) {
    auto verifier = libzcash::ProofVerifier::Strict();

    CBlock block;
    block.nVersion = 1;

    CValidationState state;
    EXPECT_FALSE(CheckBlock(block, state, verifier, flagCheckPow::OFF, flagCheckMerkleRoot::OFF));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("version-invalid"));
    EXPECT_FALSE(state.CorruptionPossible());
}


// Test that a tx with negative version is still rejected
// by CheckBlock under consensus rules.
TEST(CheckBlock, BlockRejectsBadVersion) {
    SelectParams(CBaseChainParams::MAIN);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 1 << OP_0;
    mtx.resizeOut(1);
    mtx.getOut(0).scriptPubKey = CScript() << OP_TRUE;
    mtx.getOut(0).nValue = 0;

    mtx.nVersion = -1;

    CTransaction tx {mtx};
    CBlock block;
    // explicitly set to minimum, otherwise a preliminary check will fail
    block.nVersion = MIN_BLOCK_VERSION;
    block.vtx.push_back(tx);

    CValidationState state;

    auto verifier = libzcash::ProofVerifier::Strict();

    EXPECT_FALSE(CheckBlock(block, state, verifier, flagCheckPow::OFF, flagCheckMerkleRoot::OFF));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("bad-txns-version-too-low"));
    EXPECT_FALSE(state.CorruptionPossible());
}


extern CBlockIndex* AddToBlockIndex(const CBlockHeader& block);
extern void CleanUpAll();
std::vector<CBlockIndex*> vecBlocks;
const CBlockIndex* helpMakeMain(int size)
{
    // Create genesis block
    CBlock b = Params().GenesisBlock();
    CBlockIndex* genesis = AddToBlockIndex(b);

    // add to blocks vector for ease of main chain creation, it will be erased at the end
    vecBlocks.push_back(genesis);

    for (int i = 0; i < size; i++)
    {
        CBlock b;
        b.nVersion = MIN_BLOCK_VERSION;
        b.nNonce = uint256(GetRandHash());
        b.nBits = arith_uint256(uint256(GetRandHash()).ToString() ).GetCompact();

        b.hashPrevBlock = vecBlocks.back()->GetBlockHash();

        CBlockIndex *bi = AddToBlockIndex(b);

        chainActive.SetTip(bi);
        vecBlocks.push_back(bi);
    }

    //std::cout << " main chain built: length(" << size << ")" << std::endl;

    // remove genesis
    vecBlocks.erase(vecBlocks.begin());

    return vecBlocks.back();
}

// Test that a tx without CHECKBLOCKATHEIGHT in script is rejected 
// by CheckBlock under consensus rules.
TEST(CheckBlock, BlockRejectsNoCbh) {
    CleanUpAll();
    SelectParams(CBaseChainParams::REGTEST);

    const CBlockIndex* fm = helpMakeMain(100); // this is ReplayProtectionFork height

    // the block to be checked
    CBlock block;
    block.nVersion = MIN_BLOCK_VERSION;

    // build a dummy coin base (any block needs it) 
    CMutableTransaction mtx_cb;
    mtx_cb.vin.resize(1);
    mtx_cb.vin[0].prevout.SetNull();
    mtx_cb.vin[0].scriptSig = CScript() << 1 << OP_0;
    mtx_cb.resizeOut(1);
    mtx_cb.getOut(0).scriptPubKey = CScript() << OP_TRUE;
    mtx_cb.getOut(0).nValue = 0;

    block.vtx.push_back(mtx_cb);

    // build the tx with the bad script
    CMutableTransaction mtx;

    mtx.vin.resize(1);
    mtx.vin[0].prevout.n = 0;
    mtx.vin[0].scriptSig = CScript() << 2 << OP_0;

    // get a valid address for having a legal scriptPubKey 
    CBitcoinAddress address(Params().GetCommunityFundAddressAtHeight(110001, Fork::CommunityFundType::FOUNDATION).c_str());
    CScriptID scriptID = boost::get<CScriptID>(address.Get());

    // build a script without the CHECKBLOCKATHEIGHT part
    CScript scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(scriptID) << OP_EQUALVERIFY << OP_CHECKSIG;

    //std::cout << "Script: " << scriptPubKey.ToString() << std::endl;

    mtx.addOut(CTxOut(0.5 * COIN, scriptPubKey)); //CAmount is measured in zatoshi

    block.vtx.push_back(mtx);

    CValidationState state;

    EXPECT_FALSE(ContextualCheckBlock(block, state, fm->pprev));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::CHECKBLOCKATHEIGHT_NOT_FOUND);
    EXPECT_TRUE(state.GetRejectReason() == std::string("op-checkblockatheight-needed"));
    EXPECT_FALSE(state.CorruptionPossible());

    CleanUpAll();
}


class ContextualCheckBlockTest : public ::testing::Test {
protected:
    virtual void SetUp() {
        SelectParams(CBaseChainParams::MAIN);
    }

    // Returns a valid but empty mutable transaction at given block height.
    CMutableTransaction GetBlockTxWithHeight(const int height) {
        CMutableTransaction mtx;

        // No inputs.
        mtx.vin.resize(1);
        mtx.vin[0].prevout.SetNull();

        // Set height
        mtx.vin[0].scriptSig = CScript() << height << OP_0;

        mtx.resizeOut(1);
        mtx.getOut(0).scriptPubKey = CScript() << OP_TRUE;
        mtx.getOut(0).nValue = 0;

        CAmount reward = GetBlockSubsidy(height, Params().GetConsensus());

        for (Fork::CommunityFundType cfType=Fork::CommunityFundType::FOUNDATION; cfType < Fork::CommunityFundType::ENDTYPE; cfType = Fork::CommunityFundType(cfType + 1)) {
            CAmount vCommunityFund = ForkManager::getInstance().getCommunityFundReward(height, reward, cfType);
            if (vCommunityFund > 0) {
                // Take some reward away from miners
                mtx.getOut(0).nValue -= vCommunityFund;
                // And give it to the community
                mtx.addOut(CTxOut(vCommunityFund, Params().GetCommunityFundScriptAtHeight(height, cfType)));
            }
        }
        return mtx;
    }

    // Expects a height-n block containing a given transaction to pass
    // ContextualCheckBlock. You should not call it without
    // calling a SCOPED_TRACE macro first to usefully label any failures.
    void ExpectValidBlockFromTx(const CTransaction& tx, const int height) {
        // Create a block and add the transaction to it.
        CBlock block;
        block.vtx.push_back(tx);

        // Set the previous block index with the passed heigth
        CBlock prev;
        CBlockIndex indexPrev {prev};
        indexPrev.nHeight = height - 1;

        // We now expect this to be a valid block.
        CValidationState state;
        EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));
    }

    // Expects a height-1 block containing a given transaction to fail
    // ContextualCheckBlock. You should not call it without
    // calling a SCOPED_TRACE macro first to usefully label any failures.
    void ExpectInvalidBlockFromTx(
        const CTransaction& tx, const int height, int level, std::string reason)
    {
        // Create a block and add the transaction to it.
        CBlock block;
        block.vtx.push_back(tx);

        // Set the previous block index with the passed heigth
        CBlock prev;
        CBlockIndex indexPrev {prev};
        indexPrev.nHeight = height - 1;

        // We now expect this to be an invalid block, for the given reason.
        CValidationState state;
        EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));
        EXPECT_TRUE(state.GetDoS() == level);
        EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
        EXPECT_TRUE(state.GetRejectReason() == reason);
        EXPECT_FALSE(state.CorruptionPossible());
    }
};



TEST_F(ContextualCheckBlockTest, BadCoinbaseHeight) {
    // Put a transaction in a block with no height in scriptSig
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << OP_0; // missing height
    mtx.resizeOut(1);
    mtx.getOut(0).scriptPubKey = CScript() << OP_TRUE;
    mtx.getOut(0).nValue = 0;

    CBlock block;
    block.vtx.push_back(mtx);

    // Treating block as genesis (no prev blocks) should pass
    CValidationState state_1;
    EXPECT_TRUE(ContextualCheckBlock(block, state_1, NULL));

    // Treating block as non-genesis (a prev block with height=0) should fail
    CTransaction tx2 {mtx};
    block.vtx[0] = tx2;
    CBlock prev;
    CBlockIndex indexPrev {prev};
    indexPrev.nHeight = 0;

    CValidationState state_2;
    EXPECT_FALSE(ContextualCheckBlock(block, state_2, &indexPrev));
    EXPECT_TRUE(state_2.GetDoS() == 100);
    EXPECT_TRUE(state_2.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state_2.GetRejectReason() == std::string("bad-cb-height"));
    EXPECT_FALSE(state_2.CorruptionPossible());

    // Setting to an incorrect height should fail
    mtx.vin[0].scriptSig = CScript() << 2 << OP_0;
    CTransaction tx3 {mtx};
    block.vtx[0] = tx3;

    CValidationState state_3;
    EXPECT_FALSE(ContextualCheckBlock(block, state_3, &indexPrev));
    EXPECT_TRUE(state_3.GetDoS() == 100);
    EXPECT_TRUE(state_3.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state_3.GetRejectReason() == std::string("bad-cb-height"));
    EXPECT_FALSE(state_3.CorruptionPossible());

    // After correcting the scriptSig, should pass
    mtx.vin[0].scriptSig = CScript() << 1 << OP_0;
    CTransaction tx4 {mtx};
    block.vtx[0] = tx4;

    CValidationState state_4;
    EXPECT_TRUE(ContextualCheckBlock(block, state_4, &indexPrev));
}

// TODO check this: is it meaningful? Why is it called Sprout?
TEST_F(ContextualCheckBlockTest, BlockSproutRulesAcceptSproutTx) {
    CMutableTransaction mtx = GetBlockTxWithHeight(1);

    // Make it a Sprout transaction w/o JoinSplits
    mtx.nVersion = 1;

    SCOPED_TRACE("BlockShieldRulesAcceptSproutTx");
    ExpectValidBlockFromTx(CTransaction(mtx), 0);
}


class ContextualTxsCheckBlockTest : public ContextualCheckBlockTest {
protected:
    void TestTxsAcceptanceRules(CBaseChainParams::Network network, int expectedGrothTxSupportHeight) {
            SelectParams(network);

            CMutableTransaction mtx = GetBlockTxWithHeight(expectedGrothTxSupportHeight - 1);

            // Make it a Groth transaction: since nHeight is below 200, it will fail
            mtx.nVersion = GROTH_TX_VERSION;
            {
                SCOPED_TRACE("BlockShieldRulesRejectGrothTx");
//                ExpectInvalidBlockFromTx(CTransaction(mtx), expectedGrothTxSupportHeight - 1, 0, "bad-tx-shielded-version-too-low");
                ExpectInvalidBlockFromTx(CTransaction(mtx), expectedGrothTxSupportHeight - 1, 0, "bad-tx-version-unexpected");
            }

            // Make it a PHGR transaction: since nHeight is below 200, it will succeed
            mtx.nVersion = PHGR_TX_VERSION;
            {
                SCOPED_TRACE("BlockShieldRulesAcceptPhgrTx");
                ExpectValidBlockFromTx(CTransaction(mtx), expectedGrothTxSupportHeight - 1);
            }

            mtx = GetBlockTxWithHeight(expectedGrothTxSupportHeight);
            mtx.nVersion = GROTH_TX_VERSION;
            {
                SCOPED_TRACE("BlockShieldRulesAcceptGrothTx");
                ExpectValidBlockFromTx(CTransaction(mtx), expectedGrothTxSupportHeight);
            }

            mtx.nVersion = PHGR_TX_VERSION;
            {
                SCOPED_TRACE("BlockShieldRulesRejectPhgrTx");
                //ExpectInvalidBlockFromTx(CTransaction(mtx), expectedGrothTxSupportHeight, 100, "bad-tx-shielded-version-too-low");
                ExpectInvalidBlockFromTx(CTransaction(mtx), expectedGrothTxSupportHeight, 100, "bad-tx-version-unexpected");
            }

        }
};


TEST_F(ContextualTxsCheckBlockTest, BlockShieldRulesRejectOtherTx) {

    TestTxsAcceptanceRules(CBaseChainParams::REGTEST, 200);
    TestTxsAcceptanceRules(CBaseChainParams::TESTNET, 369900);
    TestTxsAcceptanceRules(CBaseChainParams::MAIN, 455555);

}

TEST(ContextualCheckBlock, CoinbaseCommunityReward) {
    SelectParams(CBaseChainParams::MAIN);
    ChainsplitFork chainsplitFork;

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 109999 << OP_0;
    mtx.resizeOut(1);
    mtx.getOut(0).scriptPubKey = CScript() << OP_TRUE;
    mtx.getOut(0).nValue = 0;
    CTransaction tx{mtx};
    CBlock block;
    block.vtx.push_back(tx);
    block.nTime = chainsplitFork.getMinimumTime(CBaseChainParams::Network::MAIN);

    CValidationState state;
    CBlock prev;
    CBlockIndex indexPrev{prev};

    // Blocks before chain split at 110001 don't need to contain community reward output
    indexPrev.nHeight = 109998;
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));

    // Blocks after chain split at 110001 should redirect a part of block subsidy to community fund
    mtx.vin[0].scriptSig = CScript() << 110001 << OP_0;
    block.vtx[0] = CTransaction(mtx);
    indexPrev.nHeight = 110000;

    EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));
    EXPECT_TRUE(state.GetDoS() == 100);
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state.GetRejectReason() == std::string("cb-no-community-fund"));
    EXPECT_FALSE(state.CorruptionPossible());

    // Add community reward output for post chain split block
    CBitcoinAddress address(Params().GetCommunityFundAddressAtHeight(110001, Fork::CommunityFundType::FOUNDATION).c_str());
    CScriptID scriptID = boost::get<CScriptID>(address.Get());
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
    mtx.getOut(0).nValue = 1.0625 * COIN;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));

    CommunityFundAndRPFixFork communityFundAndRPFixFork;

    int hardForkHeight = communityFundAndRPFixFork.getHeight(CBaseChainParams::MAIN);
    // Test community reward output for post hard fork block
    CScriptID scriptID2 = boost::get<CScriptID>(CBitcoinAddress("zsyF68hcYYNLPj5i4PfQJ1kUY6nsFnZkc82").Get());
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID2) << OP_EQUAL;
    mtx.getOut(0).nValue = 1.5 * COIN;
    mtx.vin[0].scriptSig = CScript() << hardForkHeight << OP_0;
    indexPrev.nHeight = hardForkHeight - 1;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));


    NullTransactionFork nullTransactionFork;

    hardForkHeight = nullTransactionFork.getHeight(CBaseChainParams::MAIN);
    // Test community reward output for post hard fork block
    CBitcoinAddress address_foundation(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::FOUNDATION).c_str());
    CBitcoinAddress address_sec_node(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SECURENODE).c_str());
    CBitcoinAddress address_sup_node(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SUPERNODE).c_str());

    CScriptID scriptID_found    = boost::get<CScriptID>(address_foundation.Get());
    CScriptID scriptID_sec_node = boost::get<CScriptID>(address_sec_node.Get());
    CScriptID scriptID_sup_node = boost::get<CScriptID>(address_sup_node.Get());

    mtx.vin[0].scriptSig = CScript() << hardForkHeight << OP_0;

    mtx.resizeOut(3);
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_found) << OP_EQUAL;
    mtx.getOut(0).nValue = 1.25 * COIN;

    mtx.getOut(1).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sec_node) << OP_EQUAL;
    mtx.getOut(1).nValue = 1.25 * COIN;

    mtx.getOut(2).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sup_node) << OP_EQUAL;
    mtx.getOut(2).nValue = 1.25 * COIN;

    indexPrev.nHeight = hardForkHeight - 1;
    block.vtx[0] = CTransaction(mtx);;
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));

    ShieldFork shieldFork;

    hardForkHeight = shieldFork.getHeight(CBaseChainParams::MAIN);

    address_foundation.SetString(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::FOUNDATION).c_str());
    address_sec_node.SetString(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SECURENODE).c_str());
    address_sup_node.SetString(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SUPERNODE).c_str());

    scriptID_found    = boost::get<CScriptID>(address_foundation.Get());
    scriptID_sec_node = boost::get<CScriptID>(address_sec_node.Get());
    scriptID_sup_node = boost::get<CScriptID>(address_sup_node.Get());

    mtx.vin[0].scriptSig = CScript() << hardForkHeight << OP_0;

    mtx.resizeOut(3);
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_found) << OP_EQUAL;
    mtx.getOut(0).nValue = 2.5 * COIN;

    mtx.getOut(1).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sec_node) << OP_EQUAL;
    mtx.getOut(1).nValue = 1.25 * COIN;

    mtx.getOut(2).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sup_node) << OP_EQUAL;
    mtx.getOut(2).nValue = 1.25 * COIN;

    indexPrev.nHeight = hardForkHeight -1;
    block.vtx[0] = CTransaction(mtx);;
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));

    //Exceed the LastCommunityRewardBlockHeight
    // this is also the first block after the halving height
    int exceedHeight=Params().GetConsensus()._deprecatedGetLastCommunityRewardBlockHeight()+1;

    address_foundation.SetString(Params().GetCommunityFundAddressAtHeight(exceedHeight, Fork::CommunityFundType::FOUNDATION).c_str());
    address_sec_node.SetString(Params().GetCommunityFundAddressAtHeight(exceedHeight, Fork::CommunityFundType::SECURENODE).c_str());
    address_sup_node.SetString(Params().GetCommunityFundAddressAtHeight(exceedHeight, Fork::CommunityFundType::SUPERNODE).c_str());

    scriptID_found    = boost::get<CScriptID>(address_foundation.Get());
    scriptID_sec_node = boost::get<CScriptID>(address_sec_node.Get());
    scriptID_sup_node = boost::get<CScriptID>(address_sup_node.Get());

    mtx.vin[0].scriptSig = CScript() << exceedHeight << OP_0;

    mtx.resizeOut(3);
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_found) << OP_EQUAL;
    mtx.getOut(0).nValue = 1.25 * COIN;

    mtx.getOut(1).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sec_node) << OP_EQUAL;
    mtx.getOut(1).nValue = 0.625 * COIN;

    mtx.getOut(2).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sup_node) << OP_EQUAL;
    mtx.getOut(2).nValue = 0.625 * COIN;

    indexPrev.nHeight = exceedHeight -1;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));

    // this is 10 block after the first halving height
    exceedHeight = Params().GetConsensus().nSubsidyHalvingInterval + 10;

    // consider that only in fork1 there were many adresses to rotate, from fork4 up to now there is just one and one
    // only address for each cType and network
    address_foundation.SetString(Params().GetCommunityFundAddressAtHeight(exceedHeight, Fork::CommunityFundType::FOUNDATION).c_str());
    address_sec_node.SetString(Params().GetCommunityFundAddressAtHeight(exceedHeight, Fork::CommunityFundType::SECURENODE).c_str());
    address_sup_node.SetString(Params().GetCommunityFundAddressAtHeight(exceedHeight, Fork::CommunityFundType::SUPERNODE).c_str());

    scriptID_found    = boost::get<CScriptID>(address_foundation.Get());
    scriptID_sec_node = boost::get<CScriptID>(address_sec_node.Get());
    scriptID_sup_node = boost::get<CScriptID>(address_sup_node.Get());

    mtx.vin[0].scriptSig = CScript() << exceedHeight << OP_0;

    mtx.resizeOut(3);
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_found) << OP_EQUAL;
    mtx.getOut(0).nValue = 2.5 * COIN;

    mtx.getOut(1).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sec_node) << OP_EQUAL;
    mtx.getOut(1).nValue = 1.25 * COIN;

    mtx.getOut(2).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sup_node) << OP_EQUAL;
    mtx.getOut(2).nValue = 1.25 * COIN;

    indexPrev.nHeight = exceedHeight -1;
    block.vtx[0] = CTransaction(mtx);

    // check that pre-halving amounts are rejected
    EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));
    EXPECT_TRUE(state.GetRejectCode() == CValidationState::Code::INVALID);

    // this is 10 block after the second halving height
    exceedHeight = Params().GetConsensus().nSubsidyHalvingInterval*2  + 10;

    address_foundation.SetString(Params().GetCommunityFundAddressAtHeight(exceedHeight, Fork::CommunityFundType::FOUNDATION).c_str());
    address_sec_node.SetString(Params().GetCommunityFundAddressAtHeight(exceedHeight, Fork::CommunityFundType::SECURENODE).c_str());
    address_sup_node.SetString(Params().GetCommunityFundAddressAtHeight(exceedHeight, Fork::CommunityFundType::SUPERNODE).c_str());

    scriptID_found    = boost::get<CScriptID>(address_foundation.Get());
    scriptID_sec_node = boost::get<CScriptID>(address_sec_node.Get());
    scriptID_sup_node = boost::get<CScriptID>(address_sup_node.Get());

    mtx.vin[0].scriptSig = CScript() << exceedHeight << OP_0;

    mtx.resizeOut(4);
    // add also miner subsidy quote, even if it is not checked by ContextualCheckBlock()
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ParseHex("28daa861e86d49694937c3ee6e637d50e8343e4b") << OP_EQUAL;
    mtx.getOut(0).nValue = 1.8755 * COIN;

    mtx.getOut(1).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_found) << OP_EQUAL;
    mtx.getOut(1).nValue = 0.625 * COIN;

    mtx.getOut(2).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sec_node) << OP_EQUAL;
    mtx.getOut(2).nValue = 0.3125 * COIN;

    mtx.getOut(3).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sup_node) << OP_EQUAL;
    mtx.getOut(3).nValue = 0.3125 * COIN;

    indexPrev.nHeight = exceedHeight -1;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));
}

TEST(ContextualCheckBlockHeader, CheckBlockVersion) {
    SelectParams(CBaseChainParams::MAIN);

    const CChainParams& chainParams = Params();
    const Consensus::Params& consensusParams = chainParams.GetConsensus();

    CBlock prev;
    CBlockIndex indexPrev{prev};

    SidechainFork scFork;

    CBlock block;
    block.nBits = GetNextWorkRequired(&indexPrev, &block, consensusParams);
    block.nTime = scFork.getMinimumTime(CBaseChainParams::Network::MAIN);

    // check that after sidechain fork, the only legal block version is BLOCK_VERSION_SC_SUPPORT 
    int hardForkHeight = scFork.getHeight(CBaseChainParams::MAIN);
    indexPrev.nHeight = hardForkHeight -1;

    int newBlockVersion = ForkManager::getInstance().getNewBlockVersion(hardForkHeight);
    EXPECT_TRUE(newBlockVersion == BLOCK_VERSION_SC_SUPPORT);

    block.nVersion = BLOCK_VERSION_ORIGINAL;

    CValidationState state_1;
    EXPECT_FALSE(ContextualCheckBlockHeader(block, state_1, &indexPrev));
    EXPECT_TRUE(state_1.GetDoS() == 0);
    EXPECT_TRUE(state_1.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state_1.GetRejectReason() == std::string("bad-version"));
    EXPECT_FALSE(state_1.CorruptionPossible());

    block.nVersion = BLOCK_VERSION_BEFORE_SC;

    CValidationState state_2;
    EXPECT_FALSE(ContextualCheckBlockHeader(block, state_2, &indexPrev));
    EXPECT_TRUE(state_2.GetDoS() == 0);
    EXPECT_TRUE(state_2.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state_2.GetRejectReason() == std::string("bad-version"));
    EXPECT_FALSE(state_2.CorruptionPossible());

    block.nVersion = BLOCK_VERSION_SC_SUPPORT;

    CValidationState state_3;
    EXPECT_TRUE(ContextualCheckBlockHeader(block, state_3, &indexPrev));

    // check that before sidechain fork, block version BLOCK_VERSION_SC_SUPPORT is not valid 
    // since is considered obsolete (<4)

    EXPECT_TRUE(BLOCK_VERSION_SC_SUPPORT < BLOCK_VERSION_ORIGINAL);
    hardForkHeight -= 1;
    indexPrev.nHeight = hardForkHeight -1;
    // set a suited prev block time not to have errors since sc fork is after timeblock fork 
    indexPrev.nTime = block.nTime - MAX_FUTURE_BLOCK_TIME_LOCAL/2;
    block.nVersion = BLOCK_VERSION_SC_SUPPORT;

    CValidationState state_4;
    EXPECT_FALSE(ContextualCheckBlockHeader(block, state_4, &indexPrev));
    EXPECT_TRUE(state_4.GetDoS() == 0);
    EXPECT_TRUE(state_4.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state_4.GetRejectReason() == std::string("bad-version"));
    EXPECT_FALSE(state_4.CorruptionPossible());

    // and before sidechain fork the new block version is the legacy BLOCK_VERSION_0x2000000
    newBlockVersion = ForkManager::getInstance().getNewBlockVersion(hardForkHeight);
    EXPECT_TRUE(newBlockVersion == BLOCK_VERSION_BEFORE_SC);
}

TEST(ContextualCheckBlock, CoinbaseCommunityRewardAmount) {
    SelectParams(CBaseChainParams::MAIN);

    CBlock prev;
    CBlockIndex indexPrev {prev};
    ChainsplitFork chainplitFork;
    CommunityFundAndRPFixFork communityFundAndRPFixFork;

    int blockIndex = chainplitFork.getHeight(CBaseChainParams::MAIN)+1;
    CBitcoinAddress address(Params().GetCommunityFundAddressAtHeight(blockIndex, Fork::CommunityFundType::FOUNDATION).c_str());
    CScriptID scriptID = boost::get<CScriptID>(address.Get());

    // Test bad amount for community reward output
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 110001 << OP_0;
    mtx.resizeOut(1);
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
    mtx.getOut(0).nValue = 1.0624 * COIN;
    indexPrev.nHeight = 110000;
    CBlock block;
    block.vtx.push_back(CTransaction(mtx));
    block.nTime = chainplitFork.getMinimumTime(CBaseChainParams::Network::MAIN);

    CValidationState state_1;
    EXPECT_FALSE(ContextualCheckBlock(block, state_1, &indexPrev));
    EXPECT_TRUE(state_1.GetDoS() == 100);
    EXPECT_TRUE(state_1.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state_1.GetRejectReason() == std::string("cb-no-community-fund"));
    EXPECT_FALSE(state_1.CorruptionPossible());

    // Test bad amount for community reward output after hard fork
    int hardForkHeight = communityFundAndRPFixFork.getHeight(CBaseChainParams::MAIN);
    mtx.vin[0].scriptSig = CScript() << hardForkHeight << OP_0;
    CBitcoinAddress address1(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::FOUNDATION).c_str());
    CScriptID scriptID1 = boost::get<CScriptID>(address1.Get());
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID1) << OP_EQUAL;
    mtx.getOut(0).nValue = 1.0625 * COIN;
    indexPrev.nHeight = hardForkHeight - 1;
    block.vtx[0] = CTransaction(mtx);

    CValidationState state_2;
    EXPECT_FALSE(ContextualCheckBlock(block, state_2, &indexPrev));
    EXPECT_TRUE(state_2.GetDoS() == 100);
    EXPECT_TRUE(state_2.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state_2.GetRejectReason() == std::string("cb-no-community-fund"));
    EXPECT_FALSE(state_2.CorruptionPossible());

    mtx.resizeOut(3);

    NullTransactionFork nullTransactionFork;

    hardForkHeight = nullTransactionFork.getHeight(CBaseChainParams::MAIN);
    mtx.vin[0].scriptSig = CScript() << hardForkHeight << OP_0;

    address1.SetString((Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::FOUNDATION).c_str()));
    CBitcoinAddress address_sec_node(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SECURENODE).c_str());
    CBitcoinAddress address_sup_node(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SUPERNODE).c_str());

    CScriptID scriptID_sec_node = boost::get<CScriptID>(address_sec_node.Get());
    CScriptID scriptID_sup_node = boost::get<CScriptID>(address_sup_node.Get());
    scriptID1 = boost::get<CScriptID>(address1.Get());


    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID1) << OP_EQUAL;
    // this is the wrong amount for the FOUNDATION
    mtx.getOut(0).nValue = 1.2 * COIN;

    mtx.getOut(1).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sec_node) << OP_EQUAL;
    mtx.getOut(1).nValue = 1.25 * COIN;

    mtx.getOut(2).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sup_node) << OP_EQUAL;
    mtx.getOut(2).nValue = 1.25 * COIN;

    indexPrev.nHeight = hardForkHeight - 1;
    block.vtx[0] = CTransaction(mtx);

    CValidationState state_3;
    EXPECT_FALSE(ContextualCheckBlock(block, state_3, &indexPrev));
    EXPECT_TRUE(state_3.GetDoS() == 100);
    EXPECT_TRUE(state_3.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state_3.GetRejectReason() == std::string("cb-no-community-fund"));
    EXPECT_FALSE(state_3.CorruptionPossible());

    // this is the correct amount for the FOUNDATION
    mtx.getOut(0).nValue = 1.25 * COIN;

    indexPrev.nHeight = hardForkHeight - 1;
    block.vtx[0] = CTransaction(mtx);

    CValidationState state_4;
    EXPECT_TRUE(ContextualCheckBlock(block, state_4, &indexPrev));


    ShieldFork shieldFork;

    hardForkHeight = shieldFork.getHeight(CBaseChainParams::MAIN);
    mtx.vin[0].scriptSig = CScript() << hardForkHeight << OP_0;
    address1.SetString((Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::FOUNDATION).c_str()));
    address_sec_node.SetString(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SECURENODE).c_str());
    address_sup_node.SetString(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SUPERNODE).c_str());

    scriptID_sec_node = boost::get<CScriptID>(address_sec_node.Get());
    scriptID_sup_node = boost::get<CScriptID>(address_sup_node.Get());
    scriptID1 = boost::get<CScriptID>(address1.Get());


    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID1) << OP_EQUAL;
    // this is the wrong amount for the FOUNDATION
    mtx.getOut(0).nValue = 1.25 * COIN;

    mtx.getOut(1).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sec_node) << OP_EQUAL;
    mtx.getOut(1).nValue = 1.25 * COIN;

    mtx.getOut(2).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sup_node) << OP_EQUAL;
    mtx.getOut(2).nValue = 1.25 * COIN;

    indexPrev.nHeight = hardForkHeight - 1;
    block.vtx[0] = CTransaction(mtx);

    CValidationState state_5;
    EXPECT_FALSE(ContextualCheckBlock(block, state_5, &indexPrev));
    EXPECT_TRUE(state_5.GetDoS() == 100);
    EXPECT_TRUE(state_5.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state_5.GetRejectReason() == std::string("cb-no-community-fund"));
    EXPECT_FALSE(state_5.CorruptionPossible());

    // this is the correct amount for the FOUNDATION
    mtx.getOut(0).nValue = 2.5 * COIN;
    indexPrev.nHeight = hardForkHeight - 1;
    block.vtx[0] = CTransaction(mtx);

    CValidationState state_6;
    EXPECT_TRUE(ContextualCheckBlock(block, state_6, &indexPrev));
}

TEST(ContextualCheckBlock, CoinbaseCommunityRewardAddress) {
    SelectParams(CBaseChainParams::MAIN);

    CBlock prev;
    CBlockIndex indexPrev {prev};
    ChainsplitFork chainsplitFork;

    // Test bad addr for community reward output
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 139199 << OP_0;
    mtx.resizeOut(1);
    CScriptID scriptID = boost::get<CScriptID>(CBitcoinAddress("zsyF68hcYYNLPj5i4PfQJ1kUY6nsFnZkc82").Get());
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
    mtx.getOut(0).nValue = 1.0625 * COIN;
    indexPrev.nHeight = 139198;
    CBlock block;
    block.vtx.push_back(CTransaction(mtx));
    block.nTime = chainsplitFork.getMinimumTime(CBaseChainParams::Network::MAIN);

    CValidationState state_1;
    EXPECT_FALSE(ContextualCheckBlock(block, state_1, &indexPrev));
    EXPECT_TRUE(state_1.GetDoS() == 100);
    EXPECT_TRUE(state_1.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state_1.GetRejectReason() == std::string("cb-no-community-fund"));
    EXPECT_FALSE(state_1.CorruptionPossible());

    // Test bad addr for community reward output after hardfork
    CScriptID scriptID1 = boost::get<CScriptID>(CBitcoinAddress("zsfa9VVJCEdjfPbku4XrFcRR8kTDm2T64rz").Get());
    mtx.vin[0].scriptSig = CScript() << 139200 << OP_0;
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID1) << OP_EQUAL;
    mtx.getOut(0).nValue = 1.5 * COIN;
    indexPrev.nHeight = 139199;
    block.vtx[0] = CTransaction(mtx);;

    CValidationState state_2;
    EXPECT_FALSE(ContextualCheckBlock(block, state_2, &indexPrev));
    EXPECT_TRUE(state_2.GetDoS() == 100);
    EXPECT_TRUE(state_2.GetRejectCode() == CValidationState::Code::INVALID);
    EXPECT_TRUE(state_2.GetRejectReason() == std::string("cb-no-community-fund"));
    EXPECT_FALSE(state_2.CorruptionPossible());

    // Test community reward address rotation. Addresses should change every 50000 blocks in a round-robin fashion.
    CScriptID scriptID3 = boost::get<CScriptID>(CBitcoinAddress("zsfULrmbX7xbhqhAFRffVqCw9RyGv2hqNNG").Get());
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID3) << OP_EQUAL;
    mtx.vin[0].scriptSig = CScript() << 189200 << OP_0;
    indexPrev.nHeight = 189199;
    block.vtx[0] = CTransaction(mtx);

    CValidationState state_3;
    EXPECT_TRUE(ContextualCheckBlock(block, state_3, &indexPrev));

    // Test community reward address rotation. Addresses should change every 50000 blocks in a round-robin fashion.
    CScriptID scriptID4 = boost::get<CScriptID>(CBitcoinAddress("zsoemTfqjicem2QVU8cgBHquKb1o9JR5p4Z").Get());
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID4) << OP_EQUAL;
    mtx.vin[0].scriptSig = CScript() << 239200 << OP_0;
    indexPrev.nHeight = 239199;
    block.vtx[0] = CTransaction(mtx);

    CValidationState state_4;
    EXPECT_TRUE(ContextualCheckBlock(block, state_4, &indexPrev));

    // Test community reward address rotation. Addresses should change every 50000 blocks in a round-robin fashion.
    CScriptID scriptID5 = boost::get<CScriptID>(CBitcoinAddress("zt339oiGL6tTgc9Q71f5g1sFTZf6QiXrRUr").Get());
    mtx.getOut(0).scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID5) << OP_EQUAL;
    mtx.vin[0].scriptSig = CScript() << 289200 << OP_0;
    indexPrev.nHeight = 289199;
    block.vtx[0] = CTransaction(mtx);

    CValidationState state_5;
    EXPECT_TRUE(ContextualCheckBlock(block, state_5, &indexPrev));
}

/**
 * @brief Tests if a list of transactions are valid or not at a specified height.
 * 
 * The check is performed in relation to the logic introduced by the SidechainVersionFork only.
 * Please, note that the block forged to perform the test already includes a coinbase transaction.
 * 
 * @param blockHeight The height of the block in which the transactions are included
 * @param transactions The transactions to be included in the block
 * @param shouldSucceed True if the test must succeed, false otherwise
 */
void TestSidechainCreationVersion(int blockHeight, std::vector<CTransaction> transactions, bool shouldSucceed)
{
    CBlock prev;
    CBlockIndex indexPrev {prev};
    indexPrev.nHeight = blockHeight - 1;

    CBlock block = blockchain_test_utils::BlockchainTestManager::GenerateValidBlock(blockHeight);

    for (auto& tx : transactions)
    {
        block.vtx.push_back(tx);
    }

    CValidationState state;

    if (shouldSucceed)
    {
        EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));
    }
    else
    {
        EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));
        EXPECT_EQ(state.GetDoS(), 100);
        EXPECT_EQ(state.GetRejectCode(), CValidationState::Code::INVALID);
        EXPECT_EQ(state.GetRejectReason(), std::string("bad-tx-sc-creation-wrong-version"));
        EXPECT_FALSE(state.CorruptionPossible());
    }
}

TEST(ContextualCheckBlock, SidechainCreationVersion)
{
    SelectParams(CBaseChainParams::MAIN);

    int sidechainVersionForkHeight = 1127000;

    // Create a Sidechain Creation transaction with version 0
    CMutableTransaction mtx_v0 = txCreationUtils::createNewSidechainTxWith(CAmount(10));

    for (CTxScCreationOut& out : mtx_v0.vsc_ccout)
    {
        out.version = 0;
    }

    // Create a Sidechain Creation transaction with version 1
    CMutableTransaction mtx_v1 = txCreationUtils::createNewSidechainTxWith(CAmount(10));

    for (CTxScCreationOut& out : mtx_v1.vsc_ccout)
    {
        out.version = 1;
    }

    // Test a block immediately before the sidechain version fork point; SC version 0 must be accepted
    TestSidechainCreationVersion(sidechainVersionForkHeight - 1, {mtx_v0, mtx_v0}, true);

    // Create a block immediately before the sidechain version fork point; SC version 1 must be rejected
    TestSidechainCreationVersion(sidechainVersionForkHeight - 1, {mtx_v1, mtx_v1}, false);

    // Create a block immediately before the sidechain version fork point; SC version 1 must be rejected (even though the first transaction is OK)
    TestSidechainCreationVersion(sidechainVersionForkHeight - 1, {mtx_v0, mtx_v1}, false);

    // Create a block immediately before the sidechain version fork point; SC version 1 must be rejected (even though the second transaction is OK)
    TestSidechainCreationVersion(sidechainVersionForkHeight - 1, {mtx_v1, mtx_v0}, false);


    // Test a block exactly at the sidechain version fork point; SC version 0 must be accepted
    TestSidechainCreationVersion(sidechainVersionForkHeight, {mtx_v0, mtx_v0}, true);

    // Create a block exactly at the sidechain version fork point; SC version 1 must be accepted
    TestSidechainCreationVersion(sidechainVersionForkHeight, {mtx_v1, mtx_v1}, true);

    // Create a block exactly at the sidechain version fork point; SC version 0 and 1 must be accepted
    TestSidechainCreationVersion(sidechainVersionForkHeight, {mtx_v0, mtx_v1}, true);

    // Create a block exactly at the sidechain version fork point; SC version 0 and 1 must be accepted
    TestSidechainCreationVersion(sidechainVersionForkHeight, {mtx_v1, mtx_v0}, true);


    // Test a block after the sidechain version fork point; SC version 0 must be accepted
    TestSidechainCreationVersion(sidechainVersionForkHeight + 1, {mtx_v0, mtx_v0}, true);

    // Create a block after the sidechain version fork point; SC version 1 must be accepted
    TestSidechainCreationVersion(sidechainVersionForkHeight + 1, {mtx_v1, mtx_v1}, true);

    // Create a block after the sidechain version fork point; SC version 0 and 1 must be accepted
    TestSidechainCreationVersion(sidechainVersionForkHeight + 1, {mtx_v0, mtx_v1}, true);

    // Create a block after the sidechain version fork point; SC version 0 and 1 must be accepted
    TestSidechainCreationVersion(sidechainVersionForkHeight + 1, {mtx_v1, mtx_v0}, true);
}
