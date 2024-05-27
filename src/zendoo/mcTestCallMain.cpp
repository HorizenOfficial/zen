// Copyright (c) 2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "zendoo/mcTestCall.h"
#include "base58.h" // DecodeBase58

static const int SEGMENT_SIZE = Sidechain::SEGMENT_SIZE;

int main(int argc, char** argv) {
    run(argc, argv, SEGMENT_SIZE, [](const char* in, std::vector<unsigned char>& out) {
        DecodeBase58(in, out);
        out.erase(out.begin(), out.begin() + 2);
        return true;
        });
}

