// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_MEMUSAGE_H
#define BITCOIN_CORE_MEMUSAGE_H

#include "primitives/transaction.h"
#include "primitives/block.h"
#include "memusage.h"

static inline size_t RecursiveDynamicUsage(const CScript& script) {
    return memusage::DynamicUsage(*static_cast<const std::vector<unsigned char>*>(&script));
}

static inline size_t RecursiveDynamicUsage(const COutPoint& out) {
    return 0;
}

static inline size_t RecursiveDynamicUsage(const CTxIn& in) {
    return RecursiveDynamicUsage(in.scriptSig) + RecursiveDynamicUsage(in.prevout);
}

static inline size_t RecursiveDynamicUsage(const CTxOut& out) {
    return RecursiveDynamicUsage(out.scriptPubKey);
}

// no dynamic fields
static inline size_t RecursiveDynamicUsage(const CTxScCreationOut& ccout) { return 0; }
static inline size_t RecursiveDynamicUsage(const CTxCertifierLockOut& ccout) { return 0; }
static inline size_t RecursiveDynamicUsage(const CTxForwardTransferOut& ccout) { return 0; }

static inline size_t RecursiveDynamicUsage(const CTransaction& tx) {
    size_t mem = 0;
    mem += memusage::DynamicUsage(tx.GetVin());
    mem += memusage::DynamicUsage(tx.GetVout());
    for (std::vector<CTxIn>::const_iterator it = tx.GetVin().begin(); it != tx.GetVin().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    for (std::vector<CTxOut>::const_iterator it = tx.GetVout().begin(); it != tx.GetVout().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
// what about shielded components???
    mem += memusage::DynamicUsage(tx.GetVscCcOut());
    mem += memusage::DynamicUsage(tx.GetVclCcOut());
    mem += memusage::DynamicUsage(tx.GetVftCcOut());
    for (std::vector<CTxScCreationOut>::const_iterator it = tx.GetVscCcOut().begin(); it != tx.GetVscCcOut().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    for (std::vector<CTxCertifierLockOut>::const_iterator it = tx.GetVclCcOut().begin(); it != tx.GetVclCcOut().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    for (std::vector<CTxForwardTransferOut>::const_iterator it = tx.GetVftCcOut().begin(); it != tx.GetVftCcOut().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CScCertificate& cert) {
    size_t mem = 0;
    mem += memusage::DynamicUsage(cert.GetVout());
    for (std::vector<CTxOut>::const_iterator it = cert.GetVout().begin(); it != cert.GetVout().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    return mem;
}

// these symbols seem not to be referenced by anyone
//--------------------------------------------------
static inline size_t RecursiveDynamicUsage(const CMutableTransaction& tx) {
    size_t mem = memusage::DynamicUsage(tx.vin) + memusage::DynamicUsage(tx.vout);
    for (std::vector<CTxIn>::const_iterator it = tx.vin.begin(); it != tx.vin.end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    for (std::vector<CTxOut>::const_iterator it = tx.vout.begin(); it != tx.vout.end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
// what about shielded components???
    mem += memusage::DynamicUsage(tx.vsc_ccout);
    mem += memusage::DynamicUsage(tx.vcl_ccout);
    mem += memusage::DynamicUsage(tx.vft_ccout);
    for (std::vector<CTxScCreationOut>::const_iterator it = tx.vsc_ccout.begin(); it != tx.vsc_ccout.end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    for (std::vector<CTxCertifierLockOut>::const_iterator it = tx.vcl_ccout.begin(); it != tx.vcl_ccout.end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    for (std::vector<CTxForwardTransferOut>::const_iterator it = tx.vft_ccout.begin(); it != tx.vft_ccout.end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CBlock& block) {
    size_t mem =
        memusage::DynamicUsage(block.vtx) +
        memusage::DynamicUsage(block.vcert) +
        memusage::DynamicUsage(block.vMerkleTree);
    for (std::vector<CTransaction>::const_iterator it = block.vtx.begin(); it != block.vtx.end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    for (std::vector<CScCertificate>::const_iterator it = block.vcert.begin(); it != block.vcert.end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CBlockLocator& locator) {
    return memusage::DynamicUsage(locator.vHave);
}

#endif // BITCOIN_CORE_MEMUSAGE_H
