#include <gtest/gtest.h>

#include "main.h"
#include "utilmoneystr.h"
#include "chainparams.h"
#include "utilstrencodings.h"
#include "zcash/Address.hpp"
#include "wallet/wallet.h"
#include "amount.h"
#include <memory>
#include <string>
#include <set>
#include <vector>
#include <boost/filesystem.hpp>
#include "util.h"
#include <boost/assign/list_of.hpp>


// To run tests:
// ./zcash-gtest --gtest_filter="founders_reward_test.*"

//
// Enable this test to generate and print 48 testnet 2-of-3 multisig addresses.
// The output can be copied into chainparams.cpp.
// The temporary wallet file can be renamed as wallet.dat and used for testing with zcashd.
//

TEST(founders_reward_test, create_testnet_2of3multisig) {
    SelectParams(CBaseChainParams::TESTNET);
    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();
    bool fFirstRun=true;
    auto pWallet = std::make_shared<CWallet>("wallet.dat");
    ASSERT_EQ(DB_LOAD_OK, pWallet->LoadWallet(fFirstRun));
    pWallet->TopUpKeyPool();
    std::cout << "Test wallet and logs saved in folder: " << pathTemp.native() << std::endl;
    
    int numKeys = 48;
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(3);
    CPubKey newKey;
    std::vector<std::string> addresses;
    for (int i = 0; i < numKeys; i++) {
        ASSERT_TRUE(pWallet->GetKeyFromPool(newKey));
        pubkeys[0] = newKey;
        pWallet->SetAddressBook(newKey.GetID(), "", "receive");

        ASSERT_TRUE(pWallet->GetKeyFromPool(newKey));
        pubkeys[1] = newKey;
        pWallet->SetAddressBook(newKey.GetID(), "", "receive");

        ASSERT_TRUE(pWallet->GetKeyFromPool(newKey));
        pubkeys[2] = newKey;
        pWallet->SetAddressBook(newKey.GetID(), "", "receive");

        CScript result = GetScriptForMultisig(2, pubkeys);
        ASSERT_FALSE(result.size() > MAX_SCRIPT_ELEMENT_SIZE);
        CScriptID innerID(result);
        pWallet->AddCScript(result);
        pWallet->SetAddressBook(innerID, "", "receive");

        std::string address = CBitcoinAddress(innerID).ToString();
        addresses.push_back(address);
    }
    
    // Print out the addresses, 4 on each line.
    std::string s = "vFoundersRewardAddress = {\n";
    int i=0;
    int colsPerRow = 4;
    ASSERT_TRUE(numKeys % colsPerRow == 0);
    int numRows = numKeys/colsPerRow;
    for (int row=0; row<numRows; row++) {
        s += "    ";
        for (int col=0; col<colsPerRow; col++) {
            s += "\"" + addresses[i++] + "\", ";
        }
        s += "\n";
    }
    s += "    };";
    std::cout << s << std::endl;

    pWallet->Flush(true);
}


// Utility method to check the number of unique addresses from height 1 to maxHeight
void checkNumberOfUniqueAddresses(int nUnique) {

    int maxHeight = Params().GetConsensus().GetLastCommunityRewardBlockHeight();
    printf("maxHeight = %d\n",maxHeight);
    std::set<std::string> addresses;
    for (int i = 1; i <= maxHeight; i++) {
        if(Params().GetCommunityFundAddressAtHeight(i, Fork::CommunityFundType::FOUNDATION).size()>0) {
            auto result=addresses.insert(Params().GetCommunityFundAddressAtHeight(i, Fork::CommunityFundType::FOUNDATION));

        }
    }
    std::set<std::string>::iterator it;
    for (it = addresses.begin(); it != addresses.end(); it++) {
        printf("Found address %s\n",(*it).c_str());
    }
    EXPECT_EQ(addresses.size(), nUnique);
}

TEST(founders_reward_test, general) {
    SelectParams(CBaseChainParams::TESTNET);
    CChainParams params = Params();
    
    //You can retrieve Hex from a BitCoin address as follows:
    //CBitcoinAddress add ("zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1");
    //CScriptID scriptID = boost::get<CScriptID>(add.Get()); // Get() returns a boost variant
    //CScript script = CScript() << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
    //std::cout<<HexStr(script)<<std::endl;
   	
    EXPECT_EQ(params.GetCommunityFundScriptAtHeight(70001,Fork::CommunityFundType::FOUNDATION), ParseHex("a914581dd4277287b64d523f5cd70ccd69f9db384d5387"));
    EXPECT_EQ(params.GetCommunityFundAddressAtHeight(70001,Fork::CommunityFundType::FOUNDATION), "zrBAG3pXCTDq14nivNK9mW8SfwMNcdmMQpb");
    EXPECT_EQ(params.GetCommunityFundScriptAtHeight(70004,Fork::CommunityFundType::FOUNDATION), ParseHex("a914f3b4f2d391592337d6b4d67a5d67a7207596fd3487"));
    EXPECT_EQ(params.GetCommunityFundAddressAtHeight(70004,Fork::CommunityFundType::FOUNDATION), "zrRLwpYRYky4wsvwLVrDp8fs89EBTRhNMB1");
    EXPECT_EQ(params.GetCommunityFundScriptAtHeight(85500,Fork::CommunityFundType::FOUNDATION), ParseHex("a914f1e6b5f767701e3277330b4d7acd45c2af80580687"));
    EXPECT_EQ(params.GetCommunityFundAddressAtHeight(85500,Fork::CommunityFundType::FOUNDATION), "zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1");
    EXPECT_EQ(params.GetCommunityFundScriptAtHeight(260500,Fork::CommunityFundType::FOUNDATION), ParseHex("a9148d3468b6686ac59caf9ad94e547a737b09fa102787"));
    EXPECT_EQ(params.GetCommunityFundAddressAtHeight(260500,Fork::CommunityFundType::FOUNDATION), "zrFzxutppvxEdjyu4QNjogBMjtC1py9Hp1S");

    int maxHeight = params.GetConsensus().GetLastCommunityRewardBlockHeight();

    // If the block height parameter is out of bounds, there is an assert.
    ASSERT_DEATH(params.GetCommunityFundScriptAtHeight(0,Fork::CommunityFundType::FOUNDATION), "nHeight > 0");
    ASSERT_DEATH(params.GetCommunityFundScriptAtHeight(maxHeight+1,Fork::CommunityFundType::FOUNDATION), "nHeight<=consensus.GetLastCommunityRewardBlockHeight()");
    ASSERT_DEATH(params.GetCommunityFundAddressAtHeight(0,Fork::CommunityFundType::FOUNDATION), "nHeight > 0");
    ASSERT_DEATH(params.GetCommunityFundAddressAtHeight(maxHeight+1,Fork::CommunityFundType::FOUNDATION), "nHeight<=consensus.GetLastCommunityRewardBlockHeight()");
}

TEST(founders_reward_test, mainnet) {
    int NUM_MAINNET_FOUNDER_ADDRESSES = 7;
    SelectParams(CBaseChainParams::MAIN);
    checkNumberOfUniqueAddresses(NUM_MAINNET_FOUNDER_ADDRESSES);
}

TEST(founders_reward_test, testnet) {
    int NUM_TESTNET_FOUNDER_ADDRESSES = 4;
    SelectParams(CBaseChainParams::TESTNET);
    checkNumberOfUniqueAddresses(NUM_TESTNET_FOUNDER_ADDRESSES);
}

TEST(founders_reward_test, regtest) {
    int NUM_REGTEST_FOUNDER_ADDRESSES = 1;
    SelectParams(CBaseChainParams::REGTEST);
    checkNumberOfUniqueAddresses(NUM_REGTEST_FOUNDER_ADDRESSES);
}

// Test that 10% founders reward is fully rewarded after the first halving and slow start shift.
// On Mainnet, this would be 2,100,000 ZEC after 850,000 blocks (840,000 + 10,000).
TEST(founders_reward_test, slow_start_subsidy) {
    SelectParams(CBaseChainParams::MAIN);
    CChainParams params = Params();

    int maxHeight = params.GetConsensus().GetLastCommunityRewardBlockHeight();
    CAmount totalSubsidy = 0;
    for (int nHeight = 1; nHeight <= maxHeight; nHeight++) {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, params.GetConsensus()) / 5;
        totalSubsidy += nSubsidy;
    }    
    ASSERT_TRUE(totalSubsidy == MAX_MONEY/10.0);
}

