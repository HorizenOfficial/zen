#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "consensus/validation.h"
#include "main.h"
#include "zcash/Proof.hpp"
#include "base58.h"
#include "zen/forkmanager.h"
#include "zen/forks/fork1_chainsplitfork.h"
#include "zen/forks/fork3_communityfundandrpfixfork.h"
using namespace zen;

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

TEST(CheckBlock, VersionTooLow) {
    auto verifier = libzcash::ProofVerifier::Strict();

    CBlock block;
    block.nVersion = 1;

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "version-too-low", false)).Times(1);
    EXPECT_FALSE(CheckBlock(block, state, verifier, false, false));
}

TEST(ContextualCheckBlock, BadCoinbaseHeight) {
    SelectParams(CBaseChainParams::MAIN);

    // Create a block with no height in scriptSig
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << OP_0;
    mtx.vout.resize(1);
    mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx.vout[0].nValue = 0;
    CTransaction tx {mtx};
    CBlock block;
    block.vtx.push_back(tx);

    // Treating block as genesis should pass
    MockCValidationState state;
    EXPECT_TRUE(ContextualCheckBlock(block, state, NULL));

    // Treating block as non-genesis should fail
    //mtx.vout.push_back(CTxOut(GetBlockSubsidy(1, Params().GetConsensus())/5, Params().GetCommunityFundScriptAtHeight(1))); // disabled for Zen
    CTransaction tx2 {mtx};
    block.vtx[0] = tx2;
    CBlock prev;
    CBlockIndex indexPrev {prev};
    indexPrev.nHeight = 0;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-cb-height", false)).Times(1);
    EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));

    // Setting to an incorrect height should fail
    mtx.vin[0].scriptSig = CScript() << 2 << OP_0;
    CTransaction tx3 {mtx};
    block.vtx[0] = tx3;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-cb-height", false)).Times(1);
    EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));

    // After correcting the scriptSig, should pass
    mtx.vin[0].scriptSig = CScript() << 1 << OP_0;
    CTransaction tx4 {mtx};
    block.vtx[0] = tx4;
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));
}

TEST(ContextualCheckBlock, CoinbaseCommunityReward) {
    SelectParams(CBaseChainParams::MAIN);
    ChainsplitFork chainsplitFork;

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 109999 << OP_0;
    mtx.vout.resize(1);
    mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx.vout[0].nValue = 0;
    CTransaction tx{mtx};
    CBlock block;
    block.vtx.push_back(tx);
    block.nTime = chainsplitFork.getMinimumTime(CBaseChainParams::Network::MAIN);

    MockCValidationState state;
    CBlock prev;
    CBlockIndex indexPrev{prev};

    // Blocks before chain split at 110001 don't need to contain community reward output
    indexPrev.nHeight = 109998;
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));

    // Blocks after chain split at 110001 should redirect a part of block subsidy to community fund
    mtx.vin[0].scriptSig = CScript() << 110001 << OP_0;
    block.vtx[0] = CTransaction(mtx);
    indexPrev.nHeight = 110000;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "cb-no-community-fund", false)).Times(1);
    EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));

    // Add community reward output for post chain split block
    CBitcoinAddress address(Params().GetCommunityFundAddressAtHeight(110001, Fork::CommunityFundType::FOUNDATION).c_str());
    CScriptID scriptID = boost::get<CScriptID>(address.Get());
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
    mtx.vout[0].nValue = 1.0625 * COIN;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));

    // Test community reward output for post hard fork block
    CScriptID scriptID2 = boost::get<CScriptID>(CBitcoinAddress("zsyF68hcYYNLPj5i4PfQJ1kUY6nsFnZkc82").Get());
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID2) << OP_EQUAL;
    mtx.vout[0].nValue = 1.5 * COIN;
    mtx.vin[0].scriptSig = CScript() << 139200 << OP_0;
    indexPrev.nHeight = 139199;
    block.vtx[0] = CTransaction(mtx);;
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));
}

TEST(ContextualCheckBlock, CoinbaseCommunityRewardAmount) {
    SelectParams(CBaseChainParams::MAIN);

    MockCValidationState state;
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
    mtx.vout.resize(1);
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
    mtx.vout[0].nValue = 1.0624 * COIN;
    indexPrev.nHeight = 110000;
    CBlock block;
    block.vtx.push_back(CTransaction(mtx));
    block.nTime = chainplitFork.getMinimumTime(CBaseChainParams::Network::MAIN);
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "cb-no-community-fund", false)).Times(1);
    EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));

    // Test bad amount for community reward output after hard fork
    int hardForkHeight = communityFundAndRPFixFork.getHeight(CBaseChainParams::MAIN);
    mtx.vin[0].scriptSig = CScript() << hardForkHeight << OP_0;
    CBitcoinAddress address1(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::FOUNDATION).c_str());
    CScriptID scriptID1 = boost::get<CScriptID>(address1.Get());
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID1) << OP_EQUAL;
    mtx.vout[0].nValue = 1.0625 * COIN;
    indexPrev.nHeight = hardForkHeight - 1;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "cb-no-community-fund", false)).Times(1);
    EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));

    //TODO rewrite with 3 community outs
    // Test community reward halving
    /*
    int halvingBlock = Params().GetConsensus().nSubsidyHalvingInterval + Params().GetConsensus().SubsidySlowStartShift();
    CBitcoinAddress address2(Params().GetCommunityFundAddressAtHeight(halvingBlock, Fork::CommunityFundType::FOUNDATION).c_str());
    CScriptID scriptID2 = boost::get<CScriptID>(address2.Get());
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID2) << OP_EQUAL;
    mtx.vout[0].nValue = 0.625 * COIN;
    mtx.vin[0].scriptSig = CScript() << halvingBlock << OP_0;
    block.vtx[0] = CTransaction(mtx);;
    indexPrev.nHeight = halvingBlock - 1;
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));
    */
}

TEST(ContextualCheckBlock, CoinbaseCommunityRewardAddress) {
    SelectParams(CBaseChainParams::MAIN);

    MockCValidationState state;
    CBlock prev;
    CBlockIndex indexPrev {prev};
    ChainsplitFork chainsplitFork;

    // Test bad addr for community reward output
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 139199 << OP_0;
    mtx.vout.resize(1);
    CScriptID scriptID = boost::get<CScriptID>(CBitcoinAddress("zsyF68hcYYNLPj5i4PfQJ1kUY6nsFnZkc82").Get());
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
    mtx.vout[0].nValue = 1.0625 * COIN;
    indexPrev.nHeight = 139198;
    CBlock block;
    block.vtx.push_back(CTransaction(mtx));
    block.nTime = chainsplitFork.getMinimumTime(CBaseChainParams::Network::MAIN);
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "cb-no-community-fund", false)).Times(1);
    EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));

    // Test bad addr for community reward output after hardfork
    CScriptID scriptID1 = boost::get<CScriptID>(CBitcoinAddress("zsfa9VVJCEdjfPbku4XrFcRR8kTDm2T64rz").Get());
    mtx.vin[0].scriptSig = CScript() << 139200 << OP_0;
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID1) << OP_EQUAL;
    mtx.vout[0].nValue = 1.5 * COIN;
    indexPrev.nHeight = 139199;
    block.vtx[0] = CTransaction(mtx);;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "cb-no-community-fund", false)).Times(1);
    EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));

    // Test community reward address rotation. Addresses should change every 50000 blocks in a round-robin fashion.
    CScriptID scriptID3 = boost::get<CScriptID>(CBitcoinAddress("zsfULrmbX7xbhqhAFRffVqCw9RyGv2hqNNG").Get());
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID3) << OP_EQUAL;
    mtx.vin[0].scriptSig = CScript() << 189200 << OP_0;
    indexPrev.nHeight = 189199;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));

    // Test community reward address rotation. Addresses should change every 50000 blocks in a round-robin fashion.
    CScriptID scriptID4 = boost::get<CScriptID>(CBitcoinAddress("zsoemTfqjicem2QVU8cgBHquKb1o9JR5p4Z").Get());
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID4) << OP_EQUAL;
    mtx.vin[0].scriptSig = CScript() << 239200 << OP_0;
    indexPrev.nHeight = 239199;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));

    // Test community reward address rotation. Addresses should change every 50000 blocks in a round-robin fashion.
    CScriptID scriptID5 = boost::get<CScriptID>(CBitcoinAddress("zt339oiGL6tTgc9Q71f5g1sFTZf6QiXrRUr").Get());
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID5) << OP_EQUAL;
    mtx.vin[0].scriptSig = CScript() << 289200 << OP_0;
    indexPrev.nHeight = 289199;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));

    //TODO rewrite with 3 community outs
    // Test community reward address rotation. Addresses should change every 50000 blocks in a round-robin fashion.
    /*
    CScriptID scriptID6 = boost::get<CScriptID>(CBitcoinAddress("zsrS7gXdR16PAxGF7gZedTXQroNzkq2Dup9").Get());
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID6) << OP_EQUAL;
    mtx.vin[0].scriptSig = CScript() << 339200 << OP_0;
    mtx.vout[0].nValue = 1.25 * COIN;   // 10% for the foundation address
    indexPrev.nHeight = 339199;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));
    */
}
