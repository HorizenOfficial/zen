#include "fork11_shieldedpooldeprecationfork.h"

namespace zen {

ShieldedPoolDeprecationFork::ShieldedPoolDeprecationFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,1426200},
                  {CBaseChainParams::Network::REGTEST,990},
                  {CBaseChainParams::Network::TESTNET,1313400}});


    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zsq5TpKdHRTXTaeKeToTiPTE4Re4279nUj3"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrBsetyTneFLjJzgnS3YTs6od689MNRyyJ7"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrA11hUpuPNofRm3nhSrwBYZ3886B22zgX5"
                                 }}}, CommunityFundType::FOUNDATION);

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zstp5e9WBs5wUQcrNHx2S1UmkaN4koPVBBf"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrACbdqaYnprPbPkuf5P2ZDTfes3dQoJCvz"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrKHh4dNiRCqUe4F9iDUiQcyp9soH86Sx2L"
                                 }}}, CommunityFundType::SECURENODE);

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zsvR2ihXmtjGrmAyFWytLdj76VvdFxVUJpP"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrPTHLGBvs4j4Fd85aXVhqUGrPsNsWGqkab"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrDdMQS7nbn5d3o3Ufk1cQnjZPAxJEMBJ36"
                                 }}}, CommunityFundType::SUPERNODE);
}
}