// Copyright (c) 2016 The Zcash developers
// Copyright (c) 2019 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/wallet.h"
#include "zcash/JoinSplit.hpp"
#include "zcash/Note.hpp"
#include "zcash/NoteEncryption.hpp"

CWalletTx GetValidReceive(ZCJoinSplit& params,
                                const libzcash::SpendingKey& sk,
                                CAmount value,
                                bool randomInputs,
                                int32_t version = 2);
CWalletTx GetInvalidCommitmentReceive(ZCJoinSplit& params,
                                const libzcash::SpendingKey& sk,
                                CAmount value,
                                bool randomInputs,
                                int32_t version = 2);
libzcash::Note GetNote(ZCJoinSplit& params,
                                   const libzcash::SpendingKey& sk,
                                   const CTransaction& tx, size_t js, size_t n);
CWalletTx GetValidSpend(ZCJoinSplit& params,
                              const libzcash::SpendingKey& sk,
                              const libzcash::Note& note,
                              CAmount value);
