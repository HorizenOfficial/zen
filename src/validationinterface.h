// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2020-2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATIONINTERFACE_H
#define BITCOIN_VALIDATIONINTERFACE_H

#include <boost/signals2/signal.hpp>

#include "zcash/IncrementalMerkleTree.hpp"

class CBlock;
class CBlockIndex;
struct CBlockLocator;
class CTransaction;
class CScCertificate;
class CValidationInterface;
class CValidationState;
class uint256;
struct CMinimalSidechain;
struct CScCertificateStatusUpdateInfo;

// These functions dispatch to one or all registered wallets

/** Register a wallet to receive updates from core */
void RegisterValidationInterface(CValidationInterface* pwalletIn);
/** Unregister a wallet from core */
void UnregisterValidationInterface(CValidationInterface* pwalletIn);
/** Unregister all wallets from core */
void UnregisterAllValidationInterfaces();
/** Push an updated transaction to all registered wallets */
void SyncWithWallets(const CTransaction& tx, const CBlock* pblock = NULL);
/** Push an updated certificate to all registered wallets */
void SyncWithWallets(const CScCertificate& cert, const CBlock* pblock = NULL, int bwtMaturityDepth = -1);
/** Push to wallets updates about bwt state and related sidechain information */
void SyncCertStatusUpdate(const CScCertificateStatusUpdateInfo& certStatusInfo);

class CValidationInterface {
protected:
    virtual ~CValidationInterface() {}
    virtual void UpdatedBlockTip(const CBlockIndex *pindex) {}
    virtual void SyncTransaction(const CTransaction &tx, const CBlock *pblock) {}
    virtual void SyncCertificate(const CScCertificate &tx, const CBlock *pblock, int bwtMaturityDepth) {}
    virtual void SyncCertStatusInfo(const CScCertificateStatusUpdateInfo& certStatusInfo) {}
    virtual void EraseFromWallet(const uint256 &hash) {}
    virtual void ChainTip(const CBlockIndex *pindex, const CBlock *pblock, ZCIncrementalMerkleTree tree, bool added) {}
    virtual void SetBestChain(const CBlockLocator &locator) {}
    virtual void UpdatedTransaction(const uint256 &hash) {}
    virtual void ResendWalletTransactions(int64_t nBestBlockTime) {}
    virtual void BlockChecked(const CBlock&, const CValidationState&) {}
    friend void ::RegisterValidationInterface(CValidationInterface*);
    friend void ::UnregisterValidationInterface(CValidationInterface*);
    friend void ::UnregisterAllValidationInterfaces();
};

struct CMainSignals {
    /** Notifies listeners of updated block chain tip */
    boost::signals2::signal<void (const CBlockIndex *)> UpdatedBlockTip;
    /** Notifies listeners of updated transaction data (transaction, and optionally the block it is found in. */
    boost::signals2::signal<void (const CTransaction &, const CBlock *)> SyncTransaction;
    /** Notifies listeners of an erased transaction (currently disabled, requires transaction replacement). */
    boost::signals2::signal<void (const uint256 &)> EraseTransaction;
    /** Notifies listeners of an updated transaction without new data (for now: a coinbase potentially becoming visible). */
    boost::signals2::signal<void (const uint256 &)> UpdatedTransaction;
    /** Notifies listeners of a change to the tip of the active block chain. */
    boost::signals2::signal<void (const CBlockIndex *, const CBlock *, ZCIncrementalMerkleTree, bool)> ChainTip;
    /** Notifies listeners of a new active block chain. */
    boost::signals2::signal<void (const CBlockLocator &)> SetBestChain;
    /** Tells listeners to broadcast their data. */
    boost::signals2::signal<void (int64_t nBestBlockTime)> Broadcast;
    /** Notifies listeners of a block validation result */
    boost::signals2::signal<void (const CBlock&, const CValidationState&)> BlockChecked;
    /** Notifies listeners of updated certificate data (certificate, and optionally the block it is found in. */
    boost::signals2::signal<void (const CScCertificate &, const CBlock *, int bwtMaturityDepth)> SyncCertificate;
    /** Notifies listeners of updated bwts for given certificate.*/
    boost::signals2::signal<void (const CScCertificateStatusUpdateInfo& certStatusInfo)> SyncCertStatus;
};

CMainSignals& GetMainSignals();

#endif // BITCOIN_VALIDATIONINTERFACE_H
