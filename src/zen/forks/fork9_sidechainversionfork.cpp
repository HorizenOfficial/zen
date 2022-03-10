#include "fork9_sidechainversionfork.h"

namespace zen {

SidechainVersionFork::SidechainVersionFork()
{
    // TODO: set proper fork height values.
    setHeightMap({{CBaseChainParams::Network::MAIN,2000000},
                  {CBaseChainParams::Network::REGTEST,450},
                  {CBaseChainParams::Network::TESTNET,2000000}});

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zstvVYkJdEVinD9hxqyyevKcaX7dkGddTS8"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrGrtLkMLiGPeRtzDmHeRiAKjVcRYmGWddh"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrRARDJGHe6VxZRPHd5bUtkkWn8qb5nGwKb"
                                 }}}, CommunityFundType::FOUNDATION);

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zszxbgE4fPZMeMyVhfhiejG9VRE4LPMVSVf"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zr8SmmBHw7ios52wx7UktF3NhejxmBTUAwM"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zr8mk2LzX243vfTJGuUavbhr9Zv5hihrXQg"
                                 }}}, CommunityFundType::SECURENODE);

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zsmtifdePb5b1fgwkdz1msgSEJLJQ8NyXpG"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zr7hHLWwfQeFeDjWAy1NsimsKm5pra5snLG"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrCqcKMTwc5ukZ19LP7aUvsNtGscHFNMrw5"
                                 }}}, CommunityFundType::SUPERNODE);

}

}