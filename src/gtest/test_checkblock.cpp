#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "consensus/validation.h"
#include "main.h"
#include "zcash/Proof.hpp"
#include "base58.h"
#include "zen/forkmanager.h"
#include "zen/forks/fork1_chainsplitfork.h"
#include "zen/forks/fork3_communityfundandrpfixfork.h"
#include "zen/forks/fork4_nulltransactionfork.h"
#include "zen/forks/fork5_shieldfork.h"
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


// Test that a tx with negative version is still rejected
// by CheckBlock under consensus rules.
TEST(CheckBlock, BlockRejectsBadVersion) {
    SelectParams(CBaseChainParams::MAIN);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 1 << OP_0;
    mtx.vout.resize(1);
    mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx.vout[0].nValue = 0;

    mtx.nVersion = -1;

    CTransaction tx {mtx};
    CBlock block;
    // explicitly set to minimum, otherwise a preliminary check will fail
    block.nVersion = MIN_BLOCK_VERSION;
    block.vtx.push_back(tx);

    MockCValidationState state;

    auto verifier = libzcash::ProofVerifier::Strict();

    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-version-too-low", false)).Times(1);
    EXPECT_FALSE(CheckBlock(block, state, verifier, false, false));
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
    SelectParams(CBaseChainParams::MAIN);

    const CBlockIndex* fm = helpMakeMain(117576);

    // the block to be checked
    CBlock block;
    block.nVersion = MIN_BLOCK_VERSION;

    // build a dummy coin base (any block needs it) 
    CMutableTransaction mtx_cb;
    mtx_cb.vin.resize(1);
    mtx_cb.vin[0].prevout.SetNull();
    mtx_cb.vin[0].scriptSig = CScript() << 1 << OP_0;
    mtx_cb.vout.resize(1);
    mtx_cb.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx_cb.vout[0].nValue = 0;

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

    mtx.vout.push_back( CTxOut(0.5, scriptPubKey));

    block.vtx.push_back(mtx);

    MockCValidationState state;

    auto verifier = libzcash::ProofVerifier::Strict();

    EXPECT_CALL(state, DoS(0, false, REJECT_CHECKBLOCKATHEIGHT_NOT_FOUND, "op-checkblockatheight-needed", false)).Times(1);
    EXPECT_FALSE(CheckBlock(block, state, verifier, false, false));

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

        mtx.vout.resize(1);
        mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;
        mtx.vout[0].nValue = 0;

        CAmount reward = GetBlockSubsidy(height, Params().GetConsensus());

		for (Fork::CommunityFundType cfType=Fork::CommunityFundType::FOUNDATION; cfType < Fork::CommunityFundType::ENDTYPE; cfType = Fork::CommunityFundType(cfType + 1)) {
			CAmount vCommunityFund = ForkManager::getInstance().getCommunityFundReward(height, reward, cfType);
			if (vCommunityFund > 0) {
				// Take some reward away from miners
				mtx.vout[0].nValue -= vCommunityFund;
				// And give it to the community
				mtx.vout.push_back(CTxOut(vCommunityFund, Params().GetCommunityFundScriptAtHeight(height, cfType)));
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
        MockCValidationState state;
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
        MockCValidationState state;
        EXPECT_CALL(state, DoS(level, false, REJECT_INVALID, reason, false)).Times(1);
        EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));
    }



};



TEST_F(ContextualCheckBlockTest, BadCoinbaseHeight) {
    // Put a transaction in a block with no height in scriptSig
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << OP_0; // missing height
    mtx.vout.resize(1);
    mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx.vout[0].nValue = 0;

    CBlock block;
    block.vtx.push_back(mtx);

    // Treating block as genesis (no prev blocks) should pass
    MockCValidationState state;
    EXPECT_TRUE(ContextualCheckBlock(block, state, NULL));

    // Treating block as non-genesis (a prev block with height=0) should fail
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
//	            ExpectInvalidBlockFromTx(CTransaction(mtx), expectedGrothTxSupportHeight - 1, 0, "bad-tx-shielded-version-too-low");
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

    CommunityFundAndRPFixFork communityFundAndRPFixFork;

    int hardForkHeight = communityFundAndRPFixFork.getHeight(CBaseChainParams::MAIN);
    // Test community reward output for post hard fork block
    CScriptID scriptID2 = boost::get<CScriptID>(CBitcoinAddress("zsyF68hcYYNLPj5i4PfQJ1kUY6nsFnZkc82").Get());
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID2) << OP_EQUAL;
    mtx.vout[0].nValue = 1.5 * COIN;
    mtx.vin[0].scriptSig = CScript() << hardForkHeight << OP_0;
    indexPrev.nHeight = hardForkHeight - 1;
    block.vtx[0] = CTransaction(mtx);;
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

    mtx.vout.resize(3);
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_found) << OP_EQUAL;
    mtx.vout[0].nValue = 1.25 * COIN;

    mtx.vout[1].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sec_node) << OP_EQUAL;
    mtx.vout[1].nValue = 1.25 * COIN;

    mtx.vout[2].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sup_node) << OP_EQUAL;
    mtx.vout[2].nValue = 1.25 * COIN;

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

    mtx.vout.resize(3);
    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_found) << OP_EQUAL;
    mtx.vout[0].nValue = 2.5 * COIN;

    mtx.vout[1].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sec_node) << OP_EQUAL;
    mtx.vout[1].nValue = 1.25 * COIN;

    mtx.vout[2].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sup_node) << OP_EQUAL;
    mtx.vout[2].nValue = 1.25 * COIN;

    indexPrev.nHeight = hardForkHeight -1;
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

    mtx.vout.resize(3);

    NullTransactionFork nullTransactionFork;

    hardForkHeight = nullTransactionFork.getHeight(CBaseChainParams::MAIN);
    mtx.vin[0].scriptSig = CScript() << hardForkHeight << OP_0;

    address1.SetString((Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::FOUNDATION).c_str()));
    CBitcoinAddress address_sec_node(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SECURENODE).c_str());
	CBitcoinAddress address_sup_node(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SUPERNODE).c_str());

	CScriptID scriptID_sec_node = boost::get<CScriptID>(address_sec_node.Get());
	CScriptID scriptID_sup_node = boost::get<CScriptID>(address_sup_node.Get());
    scriptID1 = boost::get<CScriptID>(address1.Get());


    mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID1) << OP_EQUAL;
    // this is the wrong amount for the FOUNDATION
    mtx.vout[0].nValue = 1.2 * COIN;

    mtx.vout[1].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sec_node) << OP_EQUAL;
	mtx.vout[1].nValue = 1.25 * COIN;

	mtx.vout[2].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sup_node) << OP_EQUAL;
	mtx.vout[2].nValue = 1.25 * COIN;

    indexPrev.nHeight = hardForkHeight - 1;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "cb-no-community-fund", false)).Times(1);
    EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));

    // this is the correct amount for the FOUNDATION
    mtx.vout[0].nValue = 1.25 * COIN;

    indexPrev.nHeight = hardForkHeight - 1;
    block.vtx[0] = CTransaction(mtx);
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));


    ShieldFork shieldFork;

    hardForkHeight = shieldFork.getHeight(CBaseChainParams::MAIN);
    mtx.vin[0].scriptSig = CScript() << hardForkHeight << OP_0;
    address1.SetString((Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::FOUNDATION).c_str()));
    address_sec_node.SetString(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SECURENODE).c_str());
    address_sup_node.SetString(Params().GetCommunityFundAddressAtHeight(hardForkHeight, Fork::CommunityFundType::SUPERNODE).c_str());

    scriptID_sec_node = boost::get<CScriptID>(address_sec_node.Get());
	scriptID_sup_node = boost::get<CScriptID>(address_sup_node.Get());
	scriptID1 = boost::get<CScriptID>(address1.Get());


	mtx.vout[0].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID1) << OP_EQUAL;
	// this is the wrong amount for the FOUNDATION
	mtx.vout[0].nValue = 1.25 * COIN;

	mtx.vout[1].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sec_node) << OP_EQUAL;
	mtx.vout[1].nValue = 1.25 * COIN;

	mtx.vout[2].scriptPubKey = CScript() << OP_HASH160 << ToByteVector(scriptID_sup_node) << OP_EQUAL;
	mtx.vout[2].nValue = 1.25 * COIN;

	indexPrev.nHeight = hardForkHeight - 1;
	block.vtx[0] = CTransaction(mtx);
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "cb-no-community-fund", false)).Times(1);
    EXPECT_FALSE(ContextualCheckBlock(block, state, &indexPrev));

    // this is the correct amount for the FOUNDATION
    mtx.vout[0].nValue = 2.5 * COIN;
	indexPrev.nHeight = hardForkHeight - 1;
	block.vtx[0] = CTransaction(mtx);
    EXPECT_TRUE(ContextualCheckBlock(block, state, &indexPrev));

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


}

