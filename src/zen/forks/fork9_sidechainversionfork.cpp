// Copyright (c) 2022 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fork9_sidechainversionfork.h"

namespace zen {

SidechainVersionFork::SidechainVersionFork()
{
    setHeightMap({{CBaseChainParams::Network::MAIN,1127000},
                  {CBaseChainParams::Network::REGTEST,450},
                  {CBaseChainParams::Network::TESTNET,1028900}});

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zshX5BAgUvNgM1VoBVKZyFVVozTDjjJvRxJ"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrLaR63UYCHVvo5BJHoMUTuZFPmcUu866wB"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrFwQjR613EuvLSufoNvUzZrfKvjSQx5a23"
                                 }}}, CommunityFundType::FOUNDATION);

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zsx68qSKMNoc1ZPQpGwNFZXVzgf27KN6a9u"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrPaU1KWpNrg5fcLsSk17z7cc71FvnVnXxi"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrQM7AZ1qpm9TPzLc2YinGhWePt7vaHz4Rg"
                                 }}}, CommunityFundType::SECURENODE);

    setCommunityFundAddressMap({{CBaseChainParams::Network::MAIN,{
                                     "zszMgcogAqz49sLHGV22YCDFSvwzwkfog4k"
                                     }},
                                {CBaseChainParams::Network::REGTEST,{
                                     "zrMna8FbuTyrvFikAsmQMyAfufF3WoGksFu"
                                 }},
                                {CBaseChainParams::Network::TESTNET,{
                                     "zrSRNSqeBNEtXqn8NkAgJ9gwhLTJmXjKqoX"
                                 }}}, CommunityFundType::SUPERNODE);

}

}

/*
 * All TESTNET and MAINNET P2SH addresses are 4-of-7
 *
 * Transaction IDs of TESTNET test spends:
 *   - FOUNDATION sig 1-4 https://explorer-testnet.horizen.io/tx/f2fa104ef0860d9acde910fc12dc2811605d1004bdc5782ea485ab3a631fa109
 *   - FOUNDATION sig 4-7 https://explorer-testnet.horizen.io/tx/0a5ea2b738205948fe8f405879849b395e7e1ba9ba9cf0e1853205d375d1ae99
 *   - SECURENODE sig 1-4 https://explorer-testnet.horizen.io/tx/dcb499d09124f7af2878b29b8561f5ddbe3ca87162ec90bb72b0602f6c0832e4
 *   - SECURENODE sig 4-7 https://explorer-testnet.horizen.io/tx/aa95f1de2d2f262fdc79f3c76110936fb2725a8f4247c21477efefb07d52cfc4
 *   - SUPERNODE  sig 1-4 https://explorer-testnet.horizen.io/tx/a62910954d0da695595f2aea64e11bc4401aa0278f180778d7640316ebe27b83
 *   - SUPERNODE  sig 4-7 https://explorer-testnet.horizen.io/tx/6f869ead9863fc48b35d88839b08c807dfa806492645ca5bb9cdd355d9b9bc7b
 *
 * Transaction IDs of MAINNET test spends:
 *   - FOUNDATION sig 1-4 https://explorer.horizen.io/tx/14f816686bd5bf742e1d43223afbef5f8c3457260e17225d6e1522610038733e
 *   - FOUNDATION sig 4-7 https://explorer.horizen.io/tx/0472999feb5412acfa6435c24b40457c10622bf3565c442c40e40b217d1bbf6b
 *   - SECURENODE sig 1-4 https://explorer.horizen.io/tx/c43c6cf943d86ff9f934f450b9596b92a20152f28748867e784e8a296ca6d827
 *   - SECURENODE sig 4-7 https://explorer.horizen.io/tx/1661852b4c7911207d047230b22f4c593a9565abdb732267526c1c64e12981b8
 *   - SUPERNODE  sig 1-4 https://explorer.horizen.io/tx/a6594c6e85d93e9bc3ff8f50523239a566e4fa05d849708c3cebeca3301dd25c
 *   - SUPERNODE  sig 4-7 https://explorer.horizen.io/tx/b55ad707e34054e9463304d6e44d31ed03e74e137db734a90d9c6deb2be62099
 *
 * These are the private keys used in REGTEST for getting the community fund P2SH addresses:
 *
 * === FOUNDATION ===
 * "privkey": "cUMHPZfWjg6Gdh39afY7WPpeRppZMyUVWV7C42aQEZFk8WVbrBL7"
 *
 * === SECURENODE ===
 * "privkey": "cSth3ZwnkFyS755DfGKzsPK1bJt84ch3zhL6LvcBrnr1r4PiYA8K"
 *
 * === SUPERNODE ===
 * "privkey": "cPdgSYx5wuXkx3FGqNk8ByUFiEXRC5EDhF5iQB4KwnkGHgXDhnZz"
 * After having imported the relevant priv key:
 *     src/zen-cli --regtest importprivkey <privkey>
 * The multi sig (m=1) redeemscript can be added to the wallet via:
 *     src/zen-cli --regtest addmultisigaddress 1 "[\"<zen_addr>\"]
 */
