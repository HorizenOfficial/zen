#include "fork4_nulltransactionfork.h"

namespace zen {

NullTransactionFork::NullTransactionFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,335000},
                  {CBaseChainParams::Network::REGTEST,105},
                  {CBaseChainParams::Network::TESTNET,245000}});
    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zsrS7gXdR16PAxGF7gZedTXQroNzkq2Dup9",
                                     "zskcernKDXreLyUGkN4YB1d7uTDY3CR1oRj",
                                     "zsutZxk6NwWSCjZe7kiG987dLjCfVYUmYJs",
                                     "zszpcLB6C5B8QvfDbF2dYWXsrpac5DL9WRk"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrKmSdqZKZjnARd5e8FfRg4v1m74X7twxGa"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1"
                                 }}}, CommunityFundType::FOUNDATION);

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zszKB4u12v31QdWbzL9QUVeXmZ3rVyzfav5",
                                     "zstmj8Gsr3iiKh8PzA1qRaGFLhMoPkK7J6y",
                                     "zsvHuvuE536tb31B7nF5fhNQwDAnQs71vSo",
                                     "zsxWnyDbU8pk2Vp98Uvkx5Nh33RFzqnCpWN"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrKmSdqZKZjnARd5e8FfRg4v1m74X7twxGa"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1"
                                 }}}, CommunityFundType::SECURENODE);

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zsqekTQ59LxwZVDU6z3ZZCpVKxGzhY2jtYV",
                                     "zsve4xKR4A4AcKWgzZXeDXJ4BADtrVgrHdx",
                                     "zsj5LDk3TyN37Q7u2itfAXVvHyeEU4cSruL",
                                     "zsnL6pKdzvZ1BPVzALUoqw2KsY966XFs5CE"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrKmSdqZKZjnARd5e8FfRg4v1m74X7twxGa"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrRBQ5heytPMN5nY3ssPf3cG4jocXeD8fm1"
                                 }}}, CommunityFundType::SUPERNODE);

    addressChangeIntervals = {
        {CBaseChainParams::Network::MAIN,50000},
        {CBaseChainParams::Network::REGTEST,100},
        {CBaseChainParams::Network::TESTNET,10000}
    };
}



/*
* @brief getCommunityFundReward returns the community fund reward based on the height and passed-in reward
* @param reward the main reward
* @return the community reward
*/
CAmount NullTransactionFork::getCommunityFundReward(const CAmount& amount, CommunityFundType cfType) const {
    if (cfType == CommunityFundType::FOUNDATION) {
        return (CAmount)amount*100/1000;
    }
    if (cfType == CommunityFundType::SECURENODE) {
        return (CAmount)amount*100/1000;
    }
    if (cfType == CommunityFundType::SUPERNODE) {
        return (CAmount)amount*100/1000;
    }
    return (CAmount)0L;
}

/**
 * @brief getCommunityFundAddress returns the community fund address based on the passed in height and maxHeight
 * @param height the height
 * @param maxHeight the maximum height sometimes used in the computation of the proper address
 * @return the community fund address for this height
 */
const std::string& NullTransactionFork::getCommunityFundAddress(CBaseChainParams::Network network, int height, int maxHeight, CommunityFundType cfType) const {
    const std::vector<std::string>& communityFundAddresses = this->getCommunityFundAddresses(network, cfType);
    int addressChangeInterval = addressChangeIntervals.at(network);
    // change CF addresses every addressChangeInterval in a round-robin fashion
    size_t i = ((height - getHeight(network)) / addressChangeInterval) % communityFundAddresses.size();
    return communityFundAddresses[i];
}

/**
 * @brief isTransactionTypeAllowed returns true if this transaction type is allowed in this fork, false otherwise
 * @param transactionType transaction type
 * @return true if allowed, false otherwise
 */
bool NullTransactionFork::isTransactionTypeAllowed(txnouttype transactionType) const {
    if (transactionType == TX_NULL_DATA_REPLAY)
        return true;
    return CommunityFundAndRPFixFork::isTransactionTypeAllowed(transactionType);
}

}
