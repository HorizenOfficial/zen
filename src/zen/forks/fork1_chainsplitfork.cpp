#include "fork1_chainsplitfork.h"

namespace zen {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PUBLIC MEMBERS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief ChainsplitFork constructor
 */
ChainsplitFork::ChainsplitFork() {
    setHeightMap({{CBaseChainParams::Network::MAIN,110001},
                  {CBaseChainParams::Network::REGTEST,1},
                  {CBaseChainParams::Network::TESTNET,70001}});
    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zssEdGnZCQ9G86LZFtbynMn1hYTVhn6eYCL","zsrCsXXmUf8k59NLasEKfxA7us3iNvaPATz","zsnLPsWMXW2s4w9EmFSwtSLRxL2LhPcfdby","zshdovbcPfUAfkPeEE2qLbKnoue9RsbVokU",
                                     "zsqmq97JAKCCBFRGvxgv6FiJgQLCZBDp62S","zskyFVFA7VRYX8EGdXmYN75WaBB25FmiL3g","zsmncLmwEUdVmAGPUrUnNKmPGXyej7mbmdM","zsfa9VVJCEdjfPbku4XrFcRR8kTDm2T64rz",
                                     "zsjdMnfWuFi46VeN2HSXVQWEGsnGHgVxayY","zseb8wRQ8rZ722oLX5B8rx7qwZiBRb9mdig","zsjxkovhqiMVggoW7jvSRi3NTSD3a6b6qfd","zsokCCSU3wvZrS2G6mEDpJ5cH49E7sDyNr1",
                                     "zt12EsFgkABHLMRXA7JNnpMqLrxsgCLnVEV","zt39mvuG9gDTHX8A8Qk45rbk3dSdQoJ8ZAv","zssTQZs5YxDGijKC86dvcDxzWogWcK7n5AK","zsywuMoQK7Bved2nrXs56AEtWBhpb88rMzS",
                                     "zsxVS2w7h1fHFX2nQtGm4372pd4DSHzq9ee","zsupGi7ro3uC8CEVwm9r7vrdVUZaXQnHF6T","zshVZvW47dA5AB3Sqk1h7ytwWJeUJUJxxaE","zsubBCjvDx252MKFsL4Dcf5rJU9Z9Upqr1N",
                                     "zsweaST3NcU4hfgkVULfCsfEq41pjgMDgcW","zswz6Rxb1S33fUpftETZwtGiVSeYxNKq2xc","zswnpHtiBbrvYDzbhPQshkgvLSfYhDMRJ4S","zsjSYAWaEYj35Ht7aXrRJUGY6Dc8qCmgYqu",
                                     "zsvMv8fGroWR8epbSiGDCJHmfe6ec2uFQrt","zsujxCT56BExQDAwKwktBjtnopYnw8BiKbg","zsxeXc2FTAzmUmeZmqVsKVdwTMSvzyns4rT","zsuLqgABNudD8bVPbVGeUjGqapuoXp68i7F",
                                     "zsoc39J1dCFK1U8kckZznvQkv8As7sajYLz","zt21NFdu1KRPJ7VRKtrWugM2Jqe5ePNmU4T","zsp15qbVcbx9ifcjKe6XZEJTvzsFUZ2BHLT","zso2KvqH6yxLQEYggHdmfL3Tcd5V6E9tqhp",
                                     "zsnFG2W5ZHRYh3QucNze4mp31tBkemtfxdj","zsex2CGJtxHyHbpLXm7kESBmp3vWRqUkJMy","zsvtFv96nrgrXKUbtNe2BpCt8aQEp5oJ7F8","zsk5KitThmhK9KBa1KDybPgEmGSFTHzhMVA",
                                     "zsuy4n48c4NsJyaCZEzwdAKULM1FqbB6Y4z","zsgtQVMpX2zNMLvHHG2NDwfqKoaebvVectJ","zszQqXRSPGdqsWw4iaMTNN6aJz4JjEzSdCF","zst6dBLrTtaMQBX7BLMNjKLTGcP11PBmgTV",
                                     "zshD9r6Eb6dZGdzYW2HCb9CzkMokCT1NGJR","zswUaj1TboEGmvSfF7fdoxWyH3RMx7MBHHo","zsv8s4Poi5GxCsbBrRJ97Vsvazp84nrz5AN","zsmmxrKU6dqWFwUKow1iyovg3gxrgXpEivr",
                                     "zskh1221aRC9WEfb5a59WxffeW34McmZZsw","zssAhuj57NnVm4yNFT6o8muRctABkUaBu3L","zsi5Yr4Z8HwBvdBqQE8gk7ahExDu95J4oqZ","zsy6ryEaxfk8emJ8bGVB7tmwRwBL8cfSqBW",
                                     //"zsvM6GdLJWXAvKs9ruUDgEdKiJzN7qrtKcP","zsre8uXg4TJTuqSaiKLQYjMd5ST3UwYorTj","zsem4VjWQuzhhhWPLwQN39SewXV1xaCVrR4","zt17Ett8K57LnhMt5RjUeYrjXDocjqt2oja",
                                     //"zt2PZSoyKuigEgM6ss6id5wqem69mwSKSnP","zszxnNPj2zg81McDarbQi76y3NYeqj8PkwU","zsi3PoGMUzkj8kPAaq9YGYUS8Wp2pDRjR8X",
                                     }},
                                {CBaseChainParams::Network::REGTEST,{ "zrKmSdqZKZjnARd5e8FfRg4v1m74X7twxGa" }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrH8KT8KUcpKKNBu3fjH4hA84jZBCawErqn", "zrGsMC4ou1r5Vxy7Dnxg4PfKpansx83BM8g", "zr6sB2Az36D8CqzeZNavB11WbovsGtJSAZG", "zrBAG3pXCTDq14nivNK9mW8SfwMNcdmMQpb",
                                     "zrRLwpYRYky4wsvwLVrDp8fs89EBTRhNMB1", "zrLozMfptTmE3zLP5SrTLyB8TXqH84Agjrr", "zrMckkaLtVTEUvxj4ouU7BPDGa8xmdTZSVE", "zrFc897wJXmF7BcEdbvi2mS1bLrcuWYK6hm",
                                     "zrHEnni486u9SNcLWacroSgcdyMA33tfM92", "zrJ3ymPV3R8Xk4N3BdNb898xvZvARm5K7mq", "zrDj3P6trx73291krxU51U9QnfkbGxkyJ6E", "zrJs3vMGFJi9pQCireeSLckJonamBnwTSrY",
                                     "zrKFdXQoAkkycy52EFoWARyzZWx6Kat2Som", "zrEXbSe79FXA9KRMnJTZUWZdZhNnjcnAdrq", "zr7iAwfNgJsMpSCmijK3TuVDuNvSmLr1rUz", "zrDEK7K6cftqSjeyVUH1WqJtBUkXN7GidxH",
                                     "zrRennuq75hyBVU4JymtZk8UcQ1vRPKpmpj", "zr9HRTL79pKmn5R8fvkC9kucZ4u1bQruLTD", "zrML8KXpJsa1NVnbJuawX86ZvAn543tWdTT", "zrLBAkQoxpEtnztSUEcdxnEvuwtgxyAMGX7",
                                     "zr6kPnVzFBYmcBDsWoTrPHRuBxLq21o4zvT", "zrMY3vdvqs9KSvx9TawvcyuVurt1Jj6GPVo", "zr9WB1qBpM4nwi1mudUFfjtMNmqzaBQDsXn", "zrAHbtHDPAqmzWJMQqSYzGyFnDWN3oELZRs",
                                     "zrH1f5K3z7EQ6RWWZ7StCDWHTZwFChBVA2W", "zrNTacAid9LS4kAqzM4sw1YcF7gLFrzVM7U", "zrFyZpMVKMeDqbn6A2uUiL9mZmgxuR1pUBg", "zrD1cqGFGzBcPogFHJvnN4XegvvmbTjA43t",
                                     "zr5A1D7czWkB4pAWfGC5Pux5Ek7anYybdPK", "zr8yTAxCy6jAdsc6qPvmVEQHbYo25AJKhy9", "zrFW2YjQw4cABim5kEDwapbSqTz3wW7cWkk", "zr9nJvNbsrvUTZD41fhqAQeUcgMfqZmAweN",
                                     "zrCx4dXZd5b2tD483Ds4diHpo1QxBMJ76Jr", "zr6eVeRwU6Puob3K1RfWtva1R458oj8pzkL", "zr7B92iHtQcobZjGCXo3DAqMQjsn7ka31wE", "zr8bcemLWAjYuphXSVqtqZWEnJipCB9F5oC",
                                     "zrFzsuPXb7HsFd3srBqtVqnC9GQ94DQubV2", "zr4yiBobiHjHnCYi75NmYtyoqCV4A3kpHDL", "zrGVdR4K4F8MfmWxhUiTypK7PTsvHi8uTAh", "zr7WiCDqCMvUdH1xmMu8YrBMFb2x2E6BX3z",
                                     "zrEFrGWLX4hPHuHRUD3TPbMAJyeSpMSctUc", "zr5c3f8PTnW8qBFX1GvK2LhyLBBCb1WDdGG", "zrGkAZkZLqC9QKJR3XomgxNizCpNuAupTeg", "zrM7muDowiun9tCHhu5K9vcDGfUptuYorfZ",
                                     "zrCsWfwKotWnQmFviqAHAPAJ2jXqZYW966P", "zrLLB3JB3jozUoMGFEGhjqyVXTpngVQ8c4T", "zrAEa8YjJ2f3m2VsM1Xa9EwibZxEnRoSLUx", "zrAdJgp7Cx35xTvB7ABWP8YLTNDArMjP1s3"
                                 }}}, CommunityFundType::FOUNDATION);
    setMinimumTimeMap({
                               {CBaseChainParams::Network::MAIN,1496187000},
                               {CBaseChainParams::Network::REGTEST,0},
                               {CBaseChainParams::Network::TESTNET,1494616813}
                           });
}

/**
 * @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
 * @param reward the main reward
 * @return the community reward
 */
CAmount ChainsplitFork::getCommunityFundReward(const CAmount& amount, CommunityFundType cfType) const {
    if (cfType != CommunityFundType::FOUNDATION) { return 0; }
    return (CAmount)amount*85/1000;
}

/**
 * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
 * @param height the height
 * @param maxHeight the maximum height sometimes used in the computation of the proper address
 * @return the community fund address for this height
 */
const std::string& ChainsplitFork::getCommunityFundAddress(CBaseChainParams::Network network, int height, int maxHeight, CommunityFundType cfType) const {
    if (cfType != CommunityFundType::FOUNDATION) {
        static std::string emptyAddress = "";
        return emptyAddress;
    }
    assert(height > 0 && height <= maxHeight);
    const std::vector<std::string>& communityFundAddresses = this->getCommunityFundAddresses(network, cfType);
    size_t addressChangeInterval = (maxHeight + communityFundAddresses.size()) / communityFundAddresses.size();
    size_t i = height / addressChangeInterval;
    return communityFundAddresses[i];
}


/**
 * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
 * @param transactionType transaction type
 * @return true if allowed, false otherwise
 */
bool ChainsplitFork::isTransactionTypeAllowed(txnouttype transactionType) const {
    switch (transactionType) {
    case TX_NONSTANDARD:
    case TX_PUBKEY_REPLAY:
    case TX_PUBKEYHASH_REPLAY:
    case TX_MULTISIG_REPLAY:
    case TX_PUBKEYHASH: // bug in replay protection
        return true;
    default:
        return false;
    }
}

}
