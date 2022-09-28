// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_MEMUSAGE_H
#define BITCOIN_CORE_MEMUSAGE_H

#include "memusage.h"
#include "primitives/block.h"
#include "primitives/transaction.h"

static inline size_t RecursiveDynamicUsage(const CScript& script) {
    return memusage::DynamicUsage(*static_cast<const std::vector<unsigned char>*>(&script));
}

static inline size_t RecursiveDynamicUsage(const COutPoint& out) { return 0; }

static inline size_t RecursiveDynamicUsage(const CTxIn& in) {
    return RecursiveDynamicUsage(in.scriptSig) + RecursiveDynamicUsage(in.prevout);
}

static inline size_t RecursiveDynamicUsage(const CTxOut& out) { return RecursiveDynamicUsage(out.scriptPubKey); }

static inline size_t RecursiveDynamicUsage(const CTxCeasedSidechainWithdrawalInput& cswIn) {
    return memusage::DynamicUsage(cswIn.redeemScript);
}

static inline size_t RecursiveDynamicUsage(const CTxScCreationOut& ccout) {
    return memusage::DynamicUsage(ccout.customData) + memusage::DynamicUsage(ccout.vFieldElementCertificateFieldConfig) +
           memusage::DynamicUsage(ccout.vBitVectorCertificateFieldConfig);
}

// no dynamic fields
static inline size_t RecursiveDynamicUsage(const CTxForwardTransferOut& ccout) { return 0; }
static inline size_t RecursiveDynamicUsage(const CBwtRequestOut& ccout) { return 0; }

static inline size_t RecursiveDynamicUsage(const CTransaction& tx) {
    size_t mem = 0;
    mem += memusage::DynamicUsage(tx.GetVin());
    for (std::vector<CTxIn>::const_iterator it = tx.GetVin().begin(); it != tx.GetVin().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    mem += memusage::DynamicUsage(tx.GetVout());
    for (std::vector<CTxOut>::const_iterator it = tx.GetVout().begin(); it != tx.GetVout().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    // what about shielded components???
    mem += memusage::DynamicUsage(tx.GetVcswCcIn());
    for (std::vector<CTxCeasedSidechainWithdrawalInput>::const_iterator it = tx.GetVcswCcIn().begin();
         it != tx.GetVcswCcIn().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }

    mem += memusage::DynamicUsage(tx.GetVscCcOut());
    for (std::vector<CTxScCreationOut>::const_iterator it = tx.GetVscCcOut().begin(); it != tx.GetVscCcOut().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }

    // no dynamic fields for ft and bt
    mem += memusage::DynamicUsage(tx.GetVftCcOut());
    mem += memusage::DynamicUsage(tx.GetVBwtRequestOut());
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CScCertificate& cert) {
    size_t mem = 0;
    mem += memusage::DynamicUsage(cert.GetVin());
    for (std::vector<CTxIn>::const_iterator it = cert.GetVin().begin(); it != cert.GetVin().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    mem += memusage::DynamicUsage(cert.GetVout());
    for (std::vector<CTxOut>::const_iterator it = cert.GetVout().begin(); it != cert.GetVout().end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CBlock& block) {
    size_t mem =
        memusage::DynamicUsage(block.vtx) + memusage::DynamicUsage(block.vcert) + memusage::DynamicUsage(block.vMerkleTree);
    for (std::vector<CTransaction>::const_iterator it = block.vtx.begin(); it != block.vtx.end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    for (std::vector<CScCertificate>::const_iterator it = block.vcert.begin(); it != block.vcert.end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CBlockLocator& locator) { return memusage::DynamicUsage(locator.vHave); }

#endif  // BITCOIN_CORE_MEMUSAGE_H
