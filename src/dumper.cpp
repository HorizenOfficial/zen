// Copyright (c) 2025 The Horizen Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <getopt.h>
#include <unordered_map>

#include "txdb.h"
#include "chainparams.h"
#include "script/script.h"
#include "main.h"
#include "base58.h"

constexpr size_t nCoinDBCache = (1 << 31);
constexpr int maxOpenFiles = 1000;
constexpr int MAX_RECONSIDER = 100;
constexpr size_t MPOOL_SIZE = MAX_RECONSIDER * 4000000; // do not bother, we empty the mempool anyway

const char* usage = R"([-t] [-H height]

  -h, --help:    print this help message
  -t, --testnet: use testnet (default: mainnet)
  -H, --height:  use given height (default: best known tip)
)";

void print_usage(char const* self) {
    std::cout << "Usage: " << self << " " << usage << std::endl;
    exit(-1);
}

int main(int argc, char *argv[]) {
    bool testnet = false;
    int height = INT_MAX;

    char opt;
    int option_index = 0;
    static struct option long_options[] = {
        {"help",    no_argument,       0,  'h' },
        {"testnet", no_argument,       0,  't' },
        {"height",  required_argument, 0,  'H' },
        {0,         0,                 0,  0 }
    };

    while ((opt = getopt_long(argc, argv, "thH:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 't':
                testnet = true;
                break;
            case 'h':
                print_usage(argv[0]);
                break;
            case 'H':
                height = atoi(optarg);
                if (height < 1) print_usage(argv[0]);
                break;
            case '?':
                print_usage(argv[0]);
                break;
      }
    }

    if (!testnet) {
        std::cerr << "Initializing MAINNET parameters" << std::endl;
        SelectParams(CBaseChainParams::MAIN);
    }
    else {
        std::cerr << "Initializing TESTNET parameters" << std::endl;
        SelectParams(CBaseChainParams::TESTNET);
    }

    const CChainParams& chainparams = Params();
    std::cerr << "Loading block index..." << std::flush;
    pblocktree = new CBlockTreeDB(nCoinDBCache, maxOpenFiles, false, false);
    CCoinsViewDB* pcoinsdbview = new CCoinsViewDB(nCoinDBCache, maxOpenFiles, false, false);
    pcoinsTip = new CCoinsViewCache(pcoinsdbview);
    LoadBlockIndex();
    std::cerr << " done!" << std::endl;

    int best_height = pcoinsTip->GetHeight();
    if (height != INT_MAX && (height <= best_height - MAX_RECONSIDER || height > best_height)) {
        std::cerr << "Invalid height requested; current height: " << best_height << std::endl;
        return -1;
    }

    uint256 inv_hash;
    CBlockIndex* pblockindex = nullptr;
    std::unique_ptr<ECCVerifyHandle> globalVerifyHandle(new ECCVerifyHandle());
    ECC_Start();
    mempool.reset(new CTxMemPool(CFeeRate(0), MPOOL_SIZE));
    if (height != INT_MAX && height < best_height) {
        std::cerr << "Setting desired height (" << height << ") ..." << std::flush;
        pblockindex = chainActive[height + 1];
        CValidationState state;
        InvalidateBlock(state, pblockindex);
        if (state.IsValid()) {
            ActivateBestChain(state);
        }
        mempool->clear();
        std::cerr << " done!" << std::endl;
    }
    FlushStateToDisk();

    size_t tot_known = 0;
    size_t tot_utxos = 0;
    size_t tot_amount = 0;
    size_t tot_p2pk, tot_p2pkh, tot_p2sh, tot_others;
    tot_p2pk = tot_p2pkh = tot_p2sh = tot_others = 0;

    std::cerr << "Dumping utxos..." << std::flush;
    std::unordered_map<std::string, AddressInfo> agg = pcoinsdbview->dumpUtxoSet();
    for (auto const& [addr, info]: agg) {
        switch (info.type) {
            case CScript::ScriptType::P2SH:
                tot_p2sh++;
                break;
            case CScript::ScriptType::P2PKH:
                tot_p2pkh++;
                break;
            case CScript::ScriptType::P2PK:
                tot_p2pk++;
                break;
            default:
                std::cerr << "Unknown address type found!" << std::endl;
                tot_others++;
        }

        std::cout << addr << "," << info.amount << "," << (int)info.type << std::endl;
        tot_utxos += info.count;
        tot_amount += info.amount;
    }
    std::cerr << " done!" << std::endl;
    std::cerr << "Tot addresses: " << agg.size() << std::endl;
    std::cerr << "Tot utxos: " << tot_utxos << std::endl;
    std::cerr << "Tot P2PK: " << tot_p2pk << std::endl;
    std::cerr << "Tot P2PKH: " << tot_p2pkh << std::endl;
    std::cerr << "Tot P2SH: " << tot_p2sh << std::endl;
    std::cerr << "Tot Others: " << tot_others << std::endl;

    std::cerr << "Checking correctness..." << std::flush;
    CCoinsStats stats;
    if (pcoinsdbview->GetStats(stats)) {
        assert(stats.nTransactionOutputs == tot_utxos);
        assert(stats.nTotalAmount == tot_amount);
    }
    std::cerr << " ok" << std::endl;

    if (pblockindex) {
        connman.reset(new CConnman()); // needed to prevent ActivateBestChain from crashing
        std::cerr << "Restoring previous height..." << std::flush;
        CValidationState state;
        ReconsiderBlock(state, pblockindex);
        if (state.IsValid()) {
            ActivateBestChain(state);
        }
        std::cerr << " ok" << std::endl;
    }
    FlushStateToDisk();
}
