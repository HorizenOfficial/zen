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

// To run tests:
// ./zcash-gtest --gtest_filter="founders_reward_test.*"

//
// Enable this test to generate and print 48 testnet 2-of-3 multisig addresses.
// The output can be copied into chainparams.cpp.
// The temporary wallet file can be renamed as wallet.dat and used for testing with zcashd.
//
#if 0
TEST(founders_reward_test, create_testnet_2of3multisig) {
    ECC_Start();
    SelectParams(CBaseChainParams::TESTNET);
    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();
    bool fFirstRun;
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

    ECC_Stop();
}
#endif


#if 0 // Disabling all these tests for now until all the latest horizen commits have been integrated then will re-enable and fix

// Utility method to check the number of unique addresses from height 1 to maxHeight
void checkNumberOfUniqueAddresses(int nUnique) {

    int maxHeight = Params().GetConsensus().GetLastFoundersRewardBlockHeight();
    printf("maxHeight = %d\n",maxHeight);
    std::set<std::string> addresses;
    for (int i = 1; i <= maxHeight; i++) {
        addresses.insert(Params().GetFoundersRewardAddressAtHeight(i));
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
    
    // Fourth testnet reward:
    // address = zrBAG3pXCTDq14nivNK9mW8SfwMNcdmMQpb
    // script.ToString() = OP_HASH160 9990975a435209031e247dccf9bc3e3ed3c81339 OP_EQUAL
    // HexStr(script) = a9149990975a435209031e247dccf9bc3e3ed3c8133987
    
    EXPECT_EQ(params.GetFoundersRewardScriptAtHeight(1), ParseHex("a9149990975a435209031e247dccf9bc3e3ed3c8133987"));
    EXPECT_EQ(params.GetFoundersRewardAddressAtHeight(1), "zrH8KT8KUcpKKNBu3fjH4hA84jZBCawErqn");
    EXPECT_EQ(params.GetFoundersRewardScriptAtHeight(53126), ParseHex("a914581dd4277287b64d523f5cd70ccd69f9db384d5387"));
    EXPECT_EQ(params.GetFoundersRewardAddressAtHeight(53126), "zrBAG3pXCTDq14nivNK9mW8SfwMNcdmMQpb");
    EXPECT_EQ(params.GetFoundersRewardScriptAtHeight(53127), ParseHex("a914581dd4277287b64d523f5cd70ccd69f9db384d5387"));
    EXPECT_EQ(params.GetFoundersRewardAddressAtHeight(53127), "zrBAG3pXCTDq14nivNK9mW8SfwMNcdmMQpb");


    int maxHeight = params.GetConsensus().GetLastFoundersRewardBlockHeight();
    
    // If the block height parameter is out of bounds, there is an assert.
    EXPECT_DEATH(params.GetFoundersRewardScriptAtHeight(0), "nHeight");
    EXPECT_DEATH(params.GetFoundersRewardScriptAtHeight(maxHeight+1), "nHeight");
    EXPECT_DEATH(params.GetFoundersRewardAddressAtHeight(0), "nHeight");
    EXPECT_DEATH(params.GetFoundersRewardAddressAtHeight(maxHeight+1), "nHeight"); 
}


#define NUM_MAINNET_FOUNDER_ADDRESSES 48

TEST(founders_reward_test, mainnet) {
    SelectParams(CBaseChainParams::MAIN);
    checkNumberOfUniqueAddresses(NUM_MAINNET_FOUNDER_ADDRESSES);
}


#define NUM_TESTNET_FOUNDER_ADDRESSES 48

TEST(founders_reward_test, testnet) {
    SelectParams(CBaseChainParams::TESTNET);
    checkNumberOfUniqueAddresses(NUM_TESTNET_FOUNDER_ADDRESSES);
}


#define NUM_REGTEST_FOUNDER_ADDRESSES 1

TEST(founders_reward_test, regtest) {
    SelectParams(CBaseChainParams::REGTEST);
    checkNumberOfUniqueAddresses(NUM_REGTEST_FOUNDER_ADDRESSES);
}



// Test that 10% founders reward is fully rewarded after the first halving and slow start shift.
// On Mainnet, this would be 2,100,000 ZEC after 850,000 blocks (840,000 + 10,000).
TEST(founders_reward_test, slow_start_subsidy) {
    SelectParams(CBaseChainParams::MAIN);
    CChainParams params = Params();

    int maxHeight = params.GetConsensus().GetLastFoundersRewardBlockHeight();    
    CAmount totalSubsidy = 0;
    for (int nHeight = 1; nHeight <= maxHeight; nHeight++) {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, params.GetConsensus()) / 5;
        totalSubsidy += nSubsidy;
    }
    
    ASSERT_TRUE(totalSubsidy == MAX_MONEY/10.0);
}


// For use with mainnet and testnet which each have 48 addresses.
// Verify the number of rewards each individual address receives.
void verifyNumberOfRewards() {
    CChainParams params = Params();
    int maxHeight = params.GetConsensus().GetLastFoundersRewardBlockHeight();
    std::multiset<std::string> ms;
    for (int nHeight = 1; nHeight <= maxHeight; nHeight++) {
        ms.insert(params.GetFoundersRewardAddressAtHeight(nHeight));
    }

    EXPECT_EQ(ms.count(params.GetFoundersRewardAddressAtIndex(0)),17500);
    for (int i = 1; i <= params.GetNumFoundersRewardAddresses()-2; i++) {
        EXPECT_EQ(ms.count(params.GetFoundersRewardAddressAtIndex(i)), 17501);
    }
    EXPECT_EQ(ms.count(params.GetFoundersRewardAddressAtIndex(params.GetNumFoundersRewardAddresses()-1)), 17454);

    EXPECT_EQ(ms.count(params.GetFoundersRewardAddress2AtIndex(0)), 17501);
    for (int i = 1; i <= params.GetNumFoundersRewardAddresses2()-2; i++) {
        EXPECT_EQ(ms.count(params.GetFoundersRewardAddress2AtIndex(i)), 17501);
    }
    EXPECT_EQ(ms.count(params.GetFoundersRewardAddress2AtIndex(params.GetNumFoundersRewardAddresses2()-1)), 17501);
}

// Verify the number of rewards going to each mainnet address
TEST(founders_reward_test, per_address_reward_mainnet) {
    SelectParams(CBaseChainParams::MAIN);
    verifyNumberOfRewards();
}

// Verify the number of rewards going to each testnet address
TEST(founders_reward_test, per_address_reward_testnet) {
    SelectParams(CBaseChainParams::TESTNET);
    verifyNumberOfRewards();
}
#endif
