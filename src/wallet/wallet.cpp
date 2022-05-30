// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/wallet.h"

#include "base58.h"
#include "checkpoints.h"
#include "coincontrol.h"
#include "consensus/validation.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "random.h"
#include "script/script.h"
#include "script/sign.h"
#include "timedata.h"
#include "utilmoneystr.h"
#include "zcash/Note.hpp"
#include "crypter.h"
#include "chainparams.h"
#include "zen/forkmanager.h"
using namespace zen;

#include <algorithm>
#include <assert.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include "sc/sidechain.h"
#include <univalue.h>
#include "rpc/protocol.h"

using namespace std;
using namespace libzcash;
using namespace Sidechain;

/**
 * Settings
 */
CFeeRate payTxFee(DEFAULT_TRANSACTION_FEE);
CAmount maxTxFee = DEFAULT_TRANSACTION_MAXFEE;
unsigned int nTxConfirmTarget = DEFAULT_TX_CONFIRM_TARGET;
bool bSpendZeroConfChange = true;
bool fSendFreeTransactions = false;
bool fPayAtLeastCustomFee = true;

/**
 * Fees smaller than this (in satoshi) are considered zero fee (for transaction creation)
 * Override with -mintxfee
 */
CFeeRate CWallet::minTxFee = CFeeRate(1000);

/** @defgroup mapWallet
 *
 * @{
 */

struct CompareValueOnly
{
    bool operator()(const pair<CAmount, pair<const CWalletTransactionBase*, unsigned int> >& t1,
                    const pair<CAmount, pair<const CWalletTransactionBase*, unsigned int> >& t2) const
    {
        return t1.first < t2.first;
    }
};

std::string JSOutPoint::ToString() const
{
    return strprintf("JSOutPoint(%s, %d, %d)", hash.ToString().substr(0,10), js, n);
}

std::string COutput::ToString() const
{
    return strprintf("COutput(%s, %d, %d) [%s]",
        tx->getTxBase()->GetHash().ToString(), pos, nDepth, FormatMoney(tx->getTxBase()->GetVout()[pos].nValue));
}

const CWalletTransactionBase* CWallet::GetWalletTx(const uint256& hash) const
{
    LOCK(cs_wallet);
    const MAP_WALLET_CONST_IT it = mapWallet.find(hash);
    if (it == mapWallet.end())
        return nullptr;
    return it->second.get();
}

// Generate a new spending key and return its public payment address
CZCPaymentAddress CWallet::GenerateNewZKey()
{
    AssertLockHeld(cs_wallet); // mapZKeyMetadata
    auto k = SpendingKey::random();
    auto addr = k.address();

    // Check for collision, even though it is unlikely to ever occur
    if (CCryptoKeyStore::HaveSpendingKey(addr))
        throw std::runtime_error("CWallet::GenerateNewZKey(): Collision detected");

    // Create new metadata
    int64_t nCreationTime = GetTime();
    mapZKeyMetadata[addr] = CKeyMetadata(nCreationTime);

    CZCPaymentAddress pubaddr(addr);
    if (!AddZKey(k))
        throw std::runtime_error("CWallet::GenerateNewZKey(): AddZKey failed");
    return pubaddr;
}

// Add spending key to keystore and persist to disk
bool CWallet::AddZKey(const libzcash::SpendingKey &key)
{
    AssertLockHeld(cs_wallet); // mapZKeyMetadata
    auto addr = key.address();

    if (!CCryptoKeyStore::AddSpendingKey(key))
        return false;

    // check if we need to remove from viewing keys
    if (HaveViewingKey(addr))
        RemoveViewingKey(key.viewing_key());

    if (!fFileBacked)
        return true;

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteZKey(addr,
                                                  key,
                                                  mapZKeyMetadata[addr]);
    }
    return true;
}

CPubKey CWallet::GenerateNewKey()
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

    CKey secret;
    secret.MakeNewKey(fCompressed);

    // Compressed public keys were introduced in version 0.6.0
    if (fCompressed)
        SetMinVersion(FEATURE_COMPRPUBKEY);

    CPubKey pubkey = secret.GetPubKey();
    assert(secret.VerifyPubKey(pubkey));

    // Create new metadata
    int64_t nCreationTime = GetTime();
    mapKeyMetadata[pubkey.GetID()] = CKeyMetadata(nCreationTime);
    if (!nTimeFirstKey || nCreationTime < nTimeFirstKey)
        nTimeFirstKey = nCreationTime;

    if (!AddKeyPubKey(secret, pubkey))
        throw std::runtime_error("CWallet::GenerateNewKey(): AddKey failed");
    return pubkey;
}

bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey &pubkey)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey))
        return false;

    // check if we need to remove from watch-only
    CScript script;
    script = GetScriptForDestination(pubkey.GetID(), false);
    if (HaveWatchOnly(script))
        RemoveWatchOnly(script);

    if (!fFileBacked)
        return true;
    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteKey(pubkey,
                                                 secret.GetPrivKey(),
                                                 mapKeyMetadata[pubkey.GetID()]);
    }
    return true;
}

bool CWallet::AddCryptedKey(const CPubKey &vchPubKey,
                            const vector<unsigned char> &vchCryptedSecret)
{
    if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret))
        return false;
    if (!fFileBacked)
        return true;
    {
        LOCK(cs_wallet);
        if (pwalletdbEncryption)
            return pwalletdbEncryption->WriteCryptedKey(vchPubKey,
                                                        vchCryptedSecret,
                                                        mapKeyMetadata[vchPubKey.GetID()]);
        else
            return CWalletDB(strWalletFile).WriteCryptedKey(vchPubKey,
                                                            vchCryptedSecret,
                                                            mapKeyMetadata[vchPubKey.GetID()]);
    }
    return false;
}


bool CWallet::AddCryptedSpendingKey(const libzcash::PaymentAddress &address,
                                    const libzcash::ReceivingKey &rk,
                                    const std::vector<unsigned char> &vchCryptedSecret)
{
    if (!CCryptoKeyStore::AddCryptedSpendingKey(address, rk, vchCryptedSecret))
        return false;
    if (!fFileBacked)
        return true;
    {
        LOCK(cs_wallet);
        if (pwalletdbEncryption) {
            return pwalletdbEncryption->WriteCryptedZKey(address,
                                                         rk,
                                                         vchCryptedSecret,
                                                         mapZKeyMetadata[address]);
        } else {
            return CWalletDB(strWalletFile).WriteCryptedZKey(address,
                                                             rk,
                                                             vchCryptedSecret,
                                                             mapZKeyMetadata[address]);
        }
    }
    return false;
}

bool CWallet::LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    mapKeyMetadata[pubkey.GetID()] = meta;
    return true;
}

bool CWallet::LoadZKeyMetadata(const PaymentAddress &addr, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapZKeyMetadata
    mapZKeyMetadata[addr] = meta;
    return true;
}

bool CWallet::LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

bool CWallet::LoadCryptedZKey(const libzcash::PaymentAddress &addr, const libzcash::ReceivingKey &rk, const std::vector<unsigned char> &vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedSpendingKey(addr, rk, vchCryptedSecret);
}

bool CWallet::LoadZKey(const libzcash::SpendingKey &key)
{
    return CCryptoKeyStore::AddSpendingKey(key);
}

bool CWallet::AddViewingKey(const libzcash::ViewingKey &vk)
{
    if (!CCryptoKeyStore::AddViewingKey(vk)) {
        return false;
    }
    nTimeFirstKey = 1; // No birthday information for viewing keys.
    if (!fFileBacked) {
        return true;
    }
    return CWalletDB(strWalletFile).WriteViewingKey(vk);
}

bool CWallet::RemoveViewingKey(const libzcash::ViewingKey &vk)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveViewingKey(vk)) {
        return false;
    }
    if (fFileBacked) {
        if (!CWalletDB(strWalletFile).EraseViewingKey(vk)) {
            return false;
        }
    }

    return true;
}

bool CWallet::LoadViewingKey(const libzcash::ViewingKey &vk)
{
    return CCryptoKeyStore::AddViewingKey(vk);
}

bool CWallet::AddCScript(const CScript& redeemScript)
{
    if (!CCryptoKeyStore::AddCScript(redeemScript))
        return false;
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
}

bool CWallet::LoadCScript(const CScript& redeemScript)
{
    /* A sanity check was added in pull #3843 to avoid adding redeemScripts
     * that never can be redeemed. However, old wallets may still contain
     * these. Do not add them to the wallet and warn. */
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
    {
        std::string strAddr = CBitcoinAddress(CScriptID(redeemScript)).ToString();
        LogPrintf("%s: Warning: This wallet contains a redeemScript of size %i which exceeds maximum size %i thus can never be redeemed. Do not use address %s.\n",
            __func__, redeemScript.size(), MAX_SCRIPT_ELEMENT_SIZE, strAddr);
        return true;
    }

    return CCryptoKeyStore::AddCScript(redeemScript);
}

bool CWallet::AddWatchOnly(const CScript &dest)
{
    if (!CCryptoKeyStore::AddWatchOnly(dest))
        return false;
    nTimeFirstKey = 1; // No birthday information for watch-only keys.
    NotifyWatchonlyChanged(true);
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript &dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveWatchOnly(dest))
        return false;
    if (!HaveWatchOnly())
        NotifyWatchonlyChanged(false);
    if (fFileBacked)
        if (!CWalletDB(strWalletFile).EraseWatchOnly(dest))
            return false;

    return true;
}

bool CWallet::LoadWatchOnly(const CScript &dest)
{
    return CCryptoKeyStore::AddWatchOnly(dest);
}

bool CWallet::Unlock(const SecureString& strWalletPassphrase)
{
    CCrypter crypter;
    CKeyingMaterial vMasterKey;

    {
        LOCK(cs_wallet);
        BOOST_FOREACH(const MasterKeyMap::value_type& pMasterKey, mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                continue; // try another master key
            if (CCryptoKeyStore::Unlock(vMasterKey))
                return true;
        }
    }
    return false;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase)
{
    bool fWasLocked = IsLocked();

    {
        LOCK(cs_wallet);
        Lock();

        CCrypter crypter;
        CKeyingMaterial vMasterKey;
        BOOST_FOREACH(MasterKeyMap::value_type& pMasterKey, mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strOldWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                return false;
            if (CCryptoKeyStore::Unlock(vMasterKey))
            {
                int64_t nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime)));

                nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = (pMasterKey.second.nDeriveIterations + pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

                if (pMasterKey.second.nDeriveIterations < 25000)
                    pMasterKey.second.nDeriveIterations = 25000;

                LogPrintf("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

                if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                    return false;
                if (!crypter.Encrypt(vMasterKey, pMasterKey.second.vchCryptedKey))
                    return false;
                CWalletDB(strWalletFile).WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();
                return true;
            }
        }
    }

    return false;
}

void CWallet::ChainTip(const CBlockIndex *pindex, const CBlock *pblock,
                       ZCIncrementalMerkleTree tree, bool added)
{
    if (added) {
        IncrementNoteWitnesses(pindex, pblock, tree);
    } else {
        DecrementNoteWitnesses(pindex);
    }
}

void CWallet::SetBestChain(const CBlockLocator& loc)
{
    LOCK(cs_wallet);
    CWalletDB walletdb(strWalletFile);
    SetBestChainINTERNAL(walletdb, loc);
}

bool CWallet::SetMinVersion(enum WalletFeature nVersion, CWalletDB* pwalletdbIn, bool fExplicit)
{
    LOCK(cs_wallet); // nWalletVersion
    if (nWalletVersion >= nVersion)
        return true;

    // when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
    if (fExplicit && nVersion > nWalletMaxVersion)
            nVersion = FEATURE_LATEST;

    nWalletVersion = nVersion;

    if (nVersion > nWalletMaxVersion)
        nWalletMaxVersion = nVersion;

    if (fFileBacked)
    {
        CWalletDB* pwalletdb = pwalletdbIn ? pwalletdbIn : new CWalletDB(strWalletFile);
        if (nWalletVersion > 40000)
            pwalletdb->WriteMinVersion(nWalletVersion);
        if (!pwalletdbIn)
            delete pwalletdb;
    }

    return true;
}

bool CWallet::SetMaxVersion(int nVersion)
{
    LOCK(cs_wallet); // nWalletVersion, nWalletMaxVersion
    // cannot downgrade below current version
    if (nWalletVersion > nVersion)
        return false;

    nWalletMaxVersion = nVersion;

    return true;
}

set<uint256> CWallet::GetConflicts(const uint256& txid) const
{
    set<uint256> result;
    AssertLockHeld(cs_wallet);

    const MAP_WALLET_CONST_IT it = mapWallet.find(txid);
    if (it == mapWallet.end())
        return result;
    const CWalletTransactionBase& wtx = *(it->second);

    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;

    for(const CTxIn& txin: wtx.getTxBase()->GetVin()) {
        if (mapTxSpends.count(txin.prevout) <= 1)
            continue;  // No conflict if zero or one spends
        range = mapTxSpends.equal_range(txin.prevout);
        for (TxSpends::const_iterator it = range.first; it != range.second; ++it)
            result.insert(it->second);
    }

    std::pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range_n;

    for (const JSDescription& jsdesc : wtx.getTxBase()->GetVjoinsplit()) {
        for (const uint256& nullifier : jsdesc.nullifiers) {
            if (mapTxNullifiers.count(nullifier) <= 1) {
                continue;  // No conflict if zero or one spends
            }
            range_n = mapTxNullifiers.equal_range(nullifier);
            for (TxNullifiers::const_iterator it = range_n.first; it != range_n.second; ++it) {
                result.insert(it->second);
            }
        }
    }
    return result;
}

void CWallet::Flush(bool shutdown)
{
    bitdb.Flush(shutdown);
}

bool CWallet::Verify(const string& walletFile, string& warningString, string& errorString)
{
    if (!bitdb.Open(GetDataDir()))
    {
        // try moving the database env out of the way
        boost::filesystem::path pathDatabase = GetDataDir() / "database";
        boost::filesystem::path pathDatabaseBak = GetDataDir() / strprintf("database.%d.bak", GetTime());
        try {
            boost::filesystem::rename(pathDatabase, pathDatabaseBak);
            LogPrintf("Moved old %s to %s. Retrying.\n", pathDatabase.string(), pathDatabaseBak.string());
        } catch (const boost::filesystem::filesystem_error&) {
            // failure is ok (well, not really, but it's not worse than what we started with)
        }
        // try again
        if (!bitdb.Open(GetDataDir())) {
            // if it still fails, it probably means we can't even create the database env
            string msg = strprintf(_("Error initializing wallet database environment %s!"), GetDataDir());
            errorString += msg;
            return true;
        }
    }
    if (GetBoolArg("-salvagewallet", false))
    {
        // Recover readable keypairs:
        if (!CWalletDB::Recover(bitdb, walletFile, true))
            return false;
    }
    if (boost::filesystem::exists(GetDataDir() / walletFile))
    {
        CDBEnv::VerifyResult r = bitdb.Verify(walletFile, CWalletDB::Recover);
        if (r == CDBEnv::RECOVER_OK)
        {
            warningString += strprintf(_("Warning: wallet.dat corrupt, data salvaged!"
                                     " Original wallet.dat saved as wallet.{timestamp}.bak in %s; if"
                                     " your balance or transactions are incorrect you should"
                                     " restore from a backup."), GetDataDir());
        }
        if (r == CDBEnv::RECOVER_FAIL)
            errorString += _("wallet.dat corrupt, salvage failed");
    }
    return true;
}

template <class T>
void CWallet::SyncMetaData(pair<typename TxSpendMap<T>::iterator, typename TxSpendMap<T>::iterator> range)
{
    // We want all the wallet transactions in range to have the same metadata as
    // the oldest (smallest nOrderPos).
    // So: find smallest nOrderPos:

    int nMinOrderPos = std::numeric_limits<int>::max();
    const CWalletTransactionBase* copyFrom = NULL;
    for (typename TxSpendMap<T>::iterator it = range.first; it != range.second; ++it)
    {
        const uint256& hash = it->second;
        int n = mapWallet[hash]->nOrderPos;
        if (n < nMinOrderPos)
        {
            nMinOrderPos = n;
            copyFrom = mapWallet[hash].get();
        }
    }
    // Now copy data from copyFrom to rest:
    for (typename TxSpendMap<T>::iterator it = range.first; it != range.second; ++it)
    {
        const uint256& hash = it->second;
        CWalletTransactionBase* copyTo = mapWallet[hash].get();
        if (copyFrom == copyTo) continue;
        copyTo->mapValue = copyFrom->mapValue;
        // mapNoteData not copied on purpose
        // (it is always set correctly for each CWalletTx)
        copyTo->vOrderForm = copyFrom->vOrderForm;
        // fTimeReceivedIsTxTime not copied on purpose
        // nTimeReceived not copied on purpose
        copyTo->nTimeSmart = copyFrom->nTimeSmart;
        copyTo->fFromMe = copyFrom->fFromMe;
        copyTo->strFromAccount = copyFrom->strFromAccount;
        // nOrderPos not copied on purpose
        // cached members not copied on purpose
    }
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool CWallet::IsSpent(const uint256& hash, unsigned int n) const
{
    const COutPoint outpoint(hash, n);
    pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
    range = mapTxSpends.equal_range(outpoint);

    for (TxSpends::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        const MAP_WALLET_CONST_IT mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end()) {
            if (mit->second->GetDepthInMainChain() >= 0) {
                return true; // Spent
            } else {
                LogPrint("cert", "%s():%d - obj[%s] has depth %d\n", __func__, __LINE__,
                    wtxid.ToString(), mit->second->GetDepthInMainChain());
            }
        }
    }
    return false;
}

/**
 * Note is spent if any non-conflicted transaction
 * spends it:
 */
bool CWallet::IsSpent(const uint256& nullifier) const
{
    pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range;
    range = mapTxNullifiers.equal_range(nullifier);

    for (TxNullifiers::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        const MAP_WALLET_CONST_IT mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end() && mit->second->GetDepthInMainChain() >= 0) {
            return true; // Spent
        }
    }
    return false;
}

void CWallet::AddToSpends(const COutPoint& outpoint, const uint256& wtxid)
{
    mapTxSpends.insert(make_pair(outpoint, wtxid));

    pair<TxSpends::iterator, TxSpends::iterator> range;
    range = mapTxSpends.equal_range(outpoint);
    SyncMetaData<COutPoint>(range);
}

void CWallet::AddToSpends(const uint256& nullifier, const uint256& wtxid)
{
    mapTxNullifiers.insert(make_pair(nullifier, wtxid));

    pair<TxNullifiers::iterator, TxNullifiers::iterator> range;
    range = mapTxNullifiers.equal_range(nullifier);
    SyncMetaData<uint256>(range);
}

void CWallet::AddToSpends(const uint256& wtxid)
{
    assert(mapWallet.count(wtxid));
    CWalletTransactionBase& thisTx = *(mapWallet[wtxid]);

    if (thisTx.getTxBase()->IsCoinBase()) // Coinbases don't spend anything!
        return;

    for (const CTxIn& txin : thisTx.getTxBase()->GetVin()) {
        LogPrint("cert", "%s():%d - obj[%s] spends out %d of [%s]\n", __func__, __LINE__,
                thisTx.getTxBase()->GetHash().ToString(), txin.prevout.n, txin.prevout.hash.ToString());
        AddToSpends(txin.prevout, wtxid);
    }

    for (const JSDescription& jsdesc : thisTx.getTxBase()->GetVjoinsplit()) {
        for (const uint256& nullifier : jsdesc.nullifiers) {
            AddToSpends(nullifier, wtxid);
        }
    }
}

void CWallet::ClearNoteWitnessCache()
{
    LOCK(cs_wallet);
    for (std::pair<const uint256, std::shared_ptr<CWalletTransactionBase> >& wtxItem : mapWallet) {
        for (mapNoteData_t::value_type& item : wtxItem.second->mapNoteData) {
            item.second.witnesses.clear();
            item.second.witnessHeight = -1;
        }
    }
    nWitnessCacheSize = 0;
}

void CWallet::IncrementNoteWitnesses(const CBlockIndex* pindex,
                                     const CBlock* pblockIn,
                                     ZCIncrementalMerkleTree& tree)
{
    {
        LOCK(cs_wallet);
        for (auto& wtxItem : mapWallet)
        {
            for (mapNoteData_t::value_type& item : wtxItem.second->mapNoteData) {
                CNoteData* nd = &(item.second);
                // Only increment witnesses that are behind the current height
                if (nd->witnessHeight < pindex->nHeight) {
                    // Check the validity of the cache
                    // The only time a note witnessed above the current height
                    // would be invalid here is during a reindex when blocks
                    // have been decremented, and we are incrementing the blocks
                    // immediately after.
                    assert(nWitnessCacheSize >= nd->witnesses.size());
                    // Witnesses being incremented should always be either -1
                    // (never incremented or decremented) or one below pindex
                    assert((nd->witnessHeight == -1) ||
                           (nd->witnessHeight == pindex->nHeight - 1));
                    // Copy the witness for the previous block if we have one
                    if (nd->witnesses.size() > 0) {
                        nd->witnesses.push_front(nd->witnesses.front());
                    }
                    if (nd->witnesses.size() > WITNESS_CACHE_SIZE) {
                        nd->witnesses.pop_back();
                    }
                }
            }
        }
        if (nWitnessCacheSize < WITNESS_CACHE_SIZE) {
            nWitnessCacheSize += 1;
        }

        const CBlock* pblock {pblockIn};
        CBlock block;
        if (!pblock) {
            ReadBlockFromDisk(block, pindex);
            pblock = &block;
        }

        for (const CTransaction& tx : pblock->vtx) {
            auto hash = tx.GetHash();
            bool txIsOurs = mapWallet.count(hash);
            for (size_t i = 0; i < tx.GetVjoinsplit().size(); i++) {
                const JSDescription& jsdesc = tx.GetVjoinsplit()[i];
                for (uint8_t j = 0; j < jsdesc.commitments.size(); j++) {
                    const uint256& note_commitment = jsdesc.commitments[j];
                    tree.append(note_commitment);

                    // Increment existing witnesses
                    for (auto& wtxItem : mapWallet)
                    {
                        for (mapNoteData_t::value_type& item : wtxItem.second->mapNoteData) {
                            CNoteData* nd = &(item.second);
                            if (nd->witnessHeight < pindex->nHeight &&
                                    nd->witnesses.size() > 0) {
                                // Check the validity of the cache
                                // See earlier comment about validity.
                                assert(nWitnessCacheSize >= nd->witnesses.size());
                                nd->witnesses.front().append(note_commitment);
                            }
                        }
                    }

                    // If this is our note, witness it
                    if (txIsOurs) {
                        JSOutPoint jsoutpt {hash, i, j};
                        if (mapWallet[hash]->mapNoteData.count(jsoutpt) &&
                                mapWallet[hash]->mapNoteData[jsoutpt].witnessHeight < pindex->nHeight) {
                            CNoteData* nd = &(mapWallet[hash]->mapNoteData[jsoutpt]);
                            if (nd->witnesses.size() > 0) {
                                // We think this can happen because we write out the
                                // witness cache state after every block increment or
                                // decrement, but the block index itself is written in
                                // batches. So if the node crashes in between these two
                                // operations, it is possible for IncrementNoteWitnesses
                                // to be called again on previously-cached blocks. This
                                // doesn't affect existing cached notes because of the
                                // CNoteData::witnessHeight checks. See #1378 for details.
                                LogPrintf("Inconsistent witness cache state found for %s\n- Cache size: %d\n- Top (height %d): %s\n- New (height %d): %s\n",
                                          jsoutpt.ToString(), nd->witnesses.size(),
                                          nd->witnessHeight,
                                          nd->witnesses.front().root().GetHex(),
                                          pindex->nHeight,
                                          tree.witness().root().GetHex());
                                nd->witnesses.clear();
                            }
                            nd->witnesses.push_front(tree.witness());
                            // Set height to one less than pindex so it gets incremented
                            nd->witnessHeight = pindex->nHeight - 1;
                            // Check the validity of the cache
                            assert(nWitnessCacheSize >= nd->witnesses.size());
                        }
                    }
                }
            }
        }

        // Update witness heights
        for (auto& wtxItem : mapWallet)
        {
            for (mapNoteData_t::value_type& item : wtxItem.second->mapNoteData) {
                CNoteData* nd = &(item.second);
                if (nd->witnessHeight < pindex->nHeight) {
                    nd->witnessHeight = pindex->nHeight;
                    // Check the validity of the cache
                    // See earlier comment about validity.
                    assert(nWitnessCacheSize >= nd->witnesses.size());
                }
            }
        }

        // For performance reasons, we write out the witness cache in
        // CWallet::SetBestChain() (which also ensures that overall consistency
        // of the wallet.dat is maintained).
    }
}

void CWallet::DecrementNoteWitnesses(const CBlockIndex* pindex)
{
    {
        LOCK(cs_wallet);
        for (auto& wtxItem : mapWallet)
        {
            for (mapNoteData_t::value_type& item : wtxItem.second->mapNoteData) {
                CNoteData* nd = &(item.second);
                // Only increment witnesses that are not above the current height
                if (nd->witnessHeight <= pindex->nHeight) {
                    // Check the validity of the cache
                    // See comment below (this would be invalid if there was a
                    // prior decrement).
                    assert(nWitnessCacheSize >= nd->witnesses.size());
                    // Witnesses being decremented should always be either -1
                    // (never incremented or decremented) or equal to pindex
                    assert((nd->witnessHeight == -1) ||
                           (nd->witnessHeight == pindex->nHeight));
                    if (nd->witnesses.size() > 0) {
                        nd->witnesses.pop_front();
                    }
                    // pindex is the block being removed, so the new witness cache
                    // height is one below it.
                    nd->witnessHeight = pindex->nHeight - 1;
                }
            }
        }
        nWitnessCacheSize -= 1;
        for (auto& wtxItem : mapWallet)
        {
            for (mapNoteData_t::value_type& item : wtxItem.second->mapNoteData) {
                CNoteData* nd = &(item.second);
                // Check the validity of the cache
                // Technically if there are notes witnessed above the current
                // height, their cache will now be invalid (relative to the new
                // value of nWitnessCacheSize). However, this would only occur
                // during a reindex, and by the time the reindex reaches the tip
                // of the chain again, the existing witness caches will be valid
                // again.
                // We don't set nWitnessCacheSize to zero at the start of the
                // reindex because the on-disk blocks had already resulted in a
                // chain that didn't trigger the assertion below.
                if (nd->witnessHeight < pindex->nHeight) {
                    assert(nWitnessCacheSize >= nd->witnesses.size());
                }
            }
        }
        // TODO: If nWitnessCache is zero, we need to regenerate the caches (#1302)
        assert(nWitnessCacheSize > 0);

        // For performance reasons, we write out the witness cache in
        // CWallet::SetBestChain() (which also ensures that overall consistency
        // of the wallet.dat is maintained).
    }
}

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase)
{
    if (IsCrypted())
        return false;

    CKeyingMaterial vMasterKey;

    vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
    GetRandBytes(&vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

    CMasterKey kMasterKey;

    kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
    GetRandBytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

    CCrypter crypter;
    int64_t nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25000, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = 2500000 / ((double)(GetTimeMillis() - nStartTime));

    nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = (kMasterKey.nDeriveIterations + kMasterKey.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

    if (kMasterKey.nDeriveIterations < 25000)
        kMasterKey.nDeriveIterations = 25000;

    LogPrintf("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

    if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod))
        return false;
    if (!crypter.Encrypt(vMasterKey, kMasterKey.vchCryptedKey))
        return false;

    {
        LOCK(cs_wallet);
        mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
        if (fFileBacked)
        {
            assert(!pwalletdbEncryption);
            pwalletdbEncryption = new CWalletDB(strWalletFile);
            if (!pwalletdbEncryption->TxnBegin()) {
                delete pwalletdbEncryption;
                pwalletdbEncryption = NULL;
                return false;
            }
            pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey);
        }

        if (!EncryptKeys(vMasterKey))
        {
            if (fFileBacked) {
                pwalletdbEncryption->TxnAbort();
                delete pwalletdbEncryption;
            }
            // We now probably have half of our keys encrypted in memory, and half not...
            // die and let the user reload the unencrypted wallet.
            assert(false);
        }

        // Encryption was introduced in version 0.4.0
        SetMinVersion(FEATURE_WALLETCRYPT, pwalletdbEncryption, true);

        if (fFileBacked)
        {
            if (!pwalletdbEncryption->TxnCommit()) {
                delete pwalletdbEncryption;
                // We now have keys encrypted in memory, but not on disk...
                // die to avoid confusion and let the user reload the unencrypted wallet.
                assert(false);
            }

            delete pwalletdbEncryption;
            pwalletdbEncryption = NULL;
        }

        Lock();
        Unlock(strWalletPassphrase);
        NewKeyPool();
        Lock();

        // Need to completely rewrite the wallet file; if we don't, bdb might keep
        // bits of the unencrypted private key in slack space in the database file.
        CDB::Rewrite(strWalletFile);

    }
    NotifyStatusChanged(this);

    return true;
}

int64_t CWallet::IncOrderPosNext(CWalletDB *pwalletdb)
{
    AssertLockHeld(cs_wallet); // nOrderPosNext
    int64_t nRet = nOrderPosNext++;
    if (pwalletdb) {
        pwalletdb->WriteOrderPosNext(nOrderPosNext);
    } else {
        CWalletDB(strWalletFile).WriteOrderPosNext(nOrderPosNext);
    }
    return nRet;
}

vTxWithInputs CWallet::OrderedTxWithInputs(const std::string& address) const
{
    AssertLockHeld(cs_wallet);
    CWalletDB walletdb(strWalletFile);

    vTxWithInputs vOrderedTxes;

    CBitcoinAddress taddr = CBitcoinAddress(address);

    if (!taddr.IsValid())
    {
        // taddr should be checked by the caller
        return vOrderedTxes;
    }

    const CScript& scriptPubKey = GetScriptForDestination(taddr.Get(), false);

    // tx are ordered in this map, from the oldest to the newest. As a consequence, the returned vector is ordered 
    // as well
    for (auto it : wtxOrdered)
    {
        CWalletTransactionBase* wtx = it.second.first;
        int64_t orderPos = it.first;

        LogPrintf("%s():%d - processing ordered tx: nOrderPos[%d]: tx[%s]\n", __func__, __LINE__,
            orderPos, wtx->getTxBase()->GetHash().ToString() );

        if (wtx->GetDepthInMainChain() < 0) {
            LogPrintf("%s():%d - skipping tx[%s]: conflicted\n", __func__, __LINE__, wtx->getTxBase()->GetHash().ToString() );
            continue;
        }

        bool outputFound = wtx->HasOutputFor(scriptPubKey);

        bool inputFound = false;
        if (!outputFound && !wtx->getTxBase()->IsCoinBase())
        {
            inputFound = wtx->HasInputFrom(scriptPubKey);
        }

        if (outputFound || inputFound) {
            vOrderedTxes.push_back(wtx);
        }
    }

    return vOrderedTxes;
}

void CWallet::MarkDirty()
{
    {
        LOCK(cs_wallet);
        for (auto& item: mapWallet)
            item.second->MarkDirty();
    }
}

/**
 * Ensure that every note in the wallet (for which we possess a spending key)
 * has a cached nullifier.
 */
bool CWallet::UpdateNullifierNoteMap()
{
    {
        LOCK(cs_wallet);

        if (IsLocked())
            return false;

        ZCNoteDecryption dec;
        for (auto& wtxItem : mapWallet)
        {
            for (mapNoteData_t::value_type& item : wtxItem.second->mapNoteData)
            {
                if (!item.second.nullifier) {
                    if (GetNoteDecryptor(item.second.address, dec)) {
                        auto i = item.first.js;
                        auto hSig = wtxItem.second->getTxBase()->GetVjoinsplit()[i].h_sig(
                            *pzcashParams, wtxItem.second->getTxBase()->GetJoinSplitPubKey());
                        item.second.nullifier = GetNoteNullifier(
                            wtxItem.second->getTxBase()->GetVjoinsplit()[i],
                            item.second.address,
                            dec,
                            hSig,
                            item.first.n);
                    }
                }
            }
            UpdateNullifierNoteMapWithTx(*(wtxItem.second));
        }
    }
    return true;
}

/**
 * Update mapNullifiersToNotes with the cached nullifiers in this tx.
 */
void CWallet::UpdateNullifierNoteMapWithTx(const CWalletTransactionBase& obj)
{
    {
        LOCK(cs_wallet);
        for (const mapNoteData_t::value_type& item : obj.mapNoteData) {
            if (item.second.nullifier) {
                mapNullifiersToNotes[*item.second.nullifier] = item.first;
            }
        }
    }
}

std::shared_ptr<CWalletTransactionBase> CWalletTx::MakeWalletMapObject() const
{
    return std::shared_ptr<CWalletTransactionBase>( new CWalletTx(*this));
}

bool CWallet::AddToWallet(const CWalletTransactionBase& wtxIn, bool fFromLoadWallet, CWalletDB* pwalletdb)
{
    uint256 hash = wtxIn.getTxBase()->GetHash();

    if (fFromLoadWallet)
    {
        mapWallet[hash] = wtxIn.MakeWalletMapObject();
        CWalletTransactionBase& wtx = *mapWallet[hash];
        wtx.BindWallet(this);
        wtxOrdered.insert(make_pair(wtx.nOrderPos, TxPair(&wtx, (CAccountingEntry*)0)));
        UpdateNullifierNoteMapWithTx(*(mapWallet[hash]));
        AddToSpends(hash);
    }
    else
    {
        LOCK(cs_wallet);
        // Inserts only if not already there, returns tx inserted or tx found
        auto obj = wtxIn.MakeWalletMapObject();
        auto ret = mapWallet.insert(make_pair(hash, obj) );
        CWalletTransactionBase& wtx = *((*ret.first).second);
        wtx.BindWallet(this);
        UpdateNullifierNoteMapWithTx(wtx);
        bool fInsertedNew = ret.second;
        if (fInsertedNew)
        {
            wtx.nTimeReceived = GetTime();
            wtx.nOrderPos = IncOrderPosNext(pwalletdb);
            wtxOrdered.insert(make_pair(wtx.nOrderPos, TxPair(&wtx, (CAccountingEntry*)0)));

            wtx.nTimeSmart = wtx.nTimeReceived;
            if (!wtxIn.hashBlock.IsNull())
            {
                if (mapBlockIndex.count(wtxIn.hashBlock))
                {
                    int64_t latestNow = wtx.nTimeReceived;
                    int64_t latestEntry = 0;
                    {
                        // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
                        int64_t latestTolerated = latestNow + 300;
                        const TxItems & txOrdered = wtxOrdered;
                        for (TxItems::const_reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
                        {
                            CWalletTransactionBase *const pwtx = (*it).second.first;
                            if (pwtx == &wtx)
                                continue;
                            CAccountingEntry *const pacentry = (*it).second.second;
                            int64_t nSmartTime;
                            if (pwtx)
                            {
                                nSmartTime = pwtx->nTimeSmart;
                                if (!nSmartTime)
                                    nSmartTime = pwtx->nTimeReceived;
                            }
                            else
                                nSmartTime = pacentry->nTime;
                            if (nSmartTime <= latestTolerated)
                            {
                                latestEntry = nSmartTime;
                                if (nSmartTime > latestNow)
                                    latestNow = nSmartTime;
                                break;
                            }
                        }
                    }

                    int64_t blocktime = mapBlockIndex[wtxIn.hashBlock]->GetBlockTime();
                    wtx.nTimeSmart = std::max(latestEntry, std::min(blocktime, latestNow));
                }
                else
                    LogPrintf("AddToWallet(): found %s in block %s not in index\n",
                             wtxIn.getTxBase()->GetHash().ToString(),
                             wtxIn.hashBlock.ToString());
            }
            AddToSpends(hash);
        }

        bool fUpdated = false;
        if (!fInsertedNew)
        {
            // Merge
            if (!wtxIn.hashBlock.IsNull() && wtxIn.hashBlock != wtx.hashBlock)
            {
                wtx.hashBlock = wtxIn.hashBlock;
                fUpdated = true;
            }
            if (wtxIn.nIndex != -1 && (wtxIn.vMerkleBranch != wtx.vMerkleBranch || wtxIn.nIndex != wtx.nIndex))
            {
                wtx.vMerkleBranch = wtxIn.vMerkleBranch;
                wtx.nIndex = wtxIn.nIndex;
                fUpdated = true;
            }
            if (UpdatedNoteData(wtxIn, wtx)) {
                fUpdated = true;
            }
            if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe)
            {
                wtx.fFromMe = wtxIn.fFromMe;
                fUpdated = true;
            }
            if (wtxIn.bwtAreStripped != wtx.bwtAreStripped)
            {
                wtx.bwtAreStripped = wtxIn.bwtAreStripped;
                fUpdated = true;
            }

            wtx.bwtMaturityDepth = wtxIn.bwtMaturityDepth;
        }

        //// debug print
        LogPrintf("AddToWallet %s  %s%s\n", wtxIn.getTxBase()->GetHash().ToString(), (fInsertedNew ? "new" : ""), (fUpdated ? "update" : ""));

        // Write to disk
        if (fInsertedNew || fUpdated)
            if (!wtx.WriteToDisk(pwalletdb))
                return false;

        // Break debit/credit balance caches:
        wtx.MarkDirty();

        // Notify UI of new or updated transaction
        NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

        // notify an external script when a wallet transaction comes in or is updated
        std::string strCmd = GetArg("-walletnotify", "");

        if ( !strCmd.empty())
        {
            boost::replace_all(strCmd, "%s", wtxIn.getTxBase()->GetHash().GetHex());
            boost::thread t(runCommand, strCmd); // thread runs free
        }

    }
    return true;
}

bool CWallet::UpdatedNoteData(const CWalletTransactionBase& wtxIn, CWalletTransactionBase& wtx)
{
    if (wtxIn.mapNoteData.empty() || wtxIn.mapNoteData == wtx.mapNoteData) {
        return false;
    }
    auto tmp = wtxIn.mapNoteData;
    // Ensure we keep any cached witnesses we may already have
    for (const std::pair<JSOutPoint, CNoteData> nd : wtx.mapNoteData) {
        if (tmp.count(nd.first) && nd.second.witnesses.size() > 0) {
            tmp.at(nd.first).witnesses.assign(
                nd.second.witnesses.cbegin(), nd.second.witnesses.cend());
        }
        tmp.at(nd.first).witnessHeight = nd.second.witnessHeight;
    }
    // Now copy over the updated note data
    wtx.mapNoteData = tmp;
    return true;
}

/**
 * Add a transaction to the wallet, or update it.
 * pblock is optional, but should be provided if the transaction is known to be in a block.
 * If fUpdate is true, existing transactions will be updated.
 *
 * If pblock is null, this transaction has either recently entered the mempool from the
 * network, is re-entering the mempool after a block was disconnected, or is exiting the
 * mempool because it conflicts with another transaction. In all these cases, if there is
 * an existing wallet transaction, the wallet transaction's Merkle branch data is _not_
 * updated; instead, the transaction being in the mempool or conflicted is determined on
 * the fly in CMerkleTx::GetDepthInMainChain().
 */
bool CWallet::AddToWalletIfInvolvingMe(const CTransactionBase& obj, const CBlock* pblock, int bwtMaturityDepth, bool fUpdate)
{
    {
        AssertLockHeld(cs_wallet);
        bool fExisted = mapWallet.count(obj.GetHash()) != 0;
        if (fExisted && !fUpdate) return false;
        auto noteData = FindMyNotes(obj);
        try
        {
            if (fExisted || IsMine(obj) || IsFromMe(obj) || noteData.size() > 0)
            {
                std::shared_ptr<CWalletTransactionBase> sobj = CWalletTransactionBase::MakeWalletObjectBase(obj, this);
                sobj->bwtMaturityDepth = bwtMaturityDepth;

                // Off chain certificates have their bwt ceased by choice
                sobj->bwtAreStripped = (pblock == nullptr)? true: false;

                if (noteData.size() > 0) {
                    sobj->SetNoteData(noteData);
                }
 
                // Get merkle branch if transaction was found in a block
                if (pblock)
                    sobj->SetMerkleBranch(*pblock);
 
                // Do not flush the wallet here for performance reasons
                // this is safe, as in case of a crash, we rescan the necessary blocks on startup through our SetBestChain-mechanism
                CWalletDB walletdb(strWalletFile, "r+", false);

                return AddToWallet(*sobj, false, &walletdb);
            }
        }
        catch (const std::exception &exc)
        {
            LogPrintf("%s():%d - %s\n", __func__, __LINE__, exc.what());
        }
        catch(...)
        {
            LogPrintf("%s():%d - Unexpected exception caught\n", __func__, __LINE__);
        }
    }
    return false;
}

void CWallet::SyncTransaction(const CTransaction& tx, const CBlock* pblock)
{
    LOCK(cs_wallet);
    if (!AddToWalletIfInvolvingMe(tx, pblock, -1, true))
        return; // Not one of ours

    MarkAffectedTransactionsDirty(tx);
}

void CWallet::SyncCertificate(const CScCertificate& cert, const CBlock* pblock, int bwtMaturityHeight)
{
    LOCK(cs_wallet);
    if (!AddToWalletIfInvolvingMe(cert, pblock, bwtMaturityHeight, true))
        return; // Not one of ours

    MarkAffectedTransactionsDirty(cert);
}

void CWallet::SyncCertStatusInfo(const CScCertificateStatusUpdateInfo& certStatusInfo)
{
    LOCK(cs_wallet);

    LogPrint("cert", "%s():%d - Called for cert[%s], bwtAreStripped[%d]\n",
        __func__, __LINE__, certStatusInfo.certHash.ToString(), certStatusInfo.bwtState == CScCertificateStatusUpdateInfo::BwtState::BWT_OFF);

    CWalletDB walletdb(strWalletFile, "r+", false);

    // Update sidechain state with top quality certs only, i.e. cert whose bwts are not voided
    if (certStatusInfo.bwtState == CScCertificateStatusUpdateInfo::BwtState::BWT_ON)
    {
        mapSidechains[certStatusInfo.scId] = certStatusInfo;
        walletdb.WriteSidechain(certStatusInfo);
    }

    std::map<uint256, std::shared_ptr<CWalletTransactionBase>>::iterator itCert = mapWallet.find(certStatusInfo.certHash);
    if (itCert == mapWallet.end())
    {
        LogPrint("cert", "%s():%d - nothing to do, cert not in wallet\n", __func__, __LINE__);
        return;
    }

    assert(itCert->second.get()->getTxBase()->IsCertificate());

    if (!BwtIsMine(dynamic_cast<const CScCertificate&>(*itCert->second.get()->getTxBase())) )
    {
        LogPrint("cert", "%s():%d - nothing to do, cert bwts not in wallet\n", __func__, __LINE__);
        return;
    }
   
    itCert->second.get()->bwtAreStripped = (certStatusInfo.bwtState != CScCertificateStatusUpdateInfo::BwtState::BWT_ON);

    // Write to disk
    if (!itCert->second->WriteToDisk(&walletdb))
        LogPrintf("%s():%d - ERROR in writing to db\n", __func__, __LINE__);
}

bool CWallet::ReadSidechain(const uint256& scId, CScCertificateStatusUpdateInfo& sidechain)
{
    bool res = false;
    if (mapSidechains.count(scId) != 0) {
        sidechain = mapSidechains.at(scId);
        res = true;
    } else {
        CWalletDB walletdb(strWalletFile, "r+", false);
        res = walletdb.ReadSidechain(scId, sidechain);
        if (res) mapSidechains[scId] = sidechain;
    }
    return res;
}

void CWallet::MarkAffectedTransactionsDirty(const CTransactionBase& tx)
{
    // If a transaction changes 'conflicted' state, that changes the balance
    // available of the outputs it spends. So force those to be
    // recomputed, also:
    for(const CTxIn& txin: tx.GetVin())
    {
        if (mapWallet.count(txin.prevout.hash))
            mapWallet[txin.prevout.hash]->MarkDirty();
    }

    for (const JSDescription& jsdesc : tx.GetVjoinsplit()) {
        for (const uint256& nullifier : jsdesc.nullifiers) {
            if (mapNullifiersToNotes.count(nullifier) &&
                    mapWallet.count(mapNullifiersToNotes[nullifier].hash)) {
                mapWallet[mapNullifiersToNotes[nullifier].hash]->MarkDirty();
            }
        }
    }
}

void CWallet::EraseFromWallet(const uint256 &hash)
{
    if (!fFileBacked)
        return;
    {
        LOCK(cs_wallet);
        LogPrint("cert", "%s():%d - called for obj[%s]\n", __func__, __LINE__, hash.ToString());

        if (mapWallet.erase(hash))
            CWalletDB(strWalletFile).EraseWalletTxBase(hash);
    }
    return;
}


/**
 * Returns a nullifier if the SpendingKey is available
 * Throws std::runtime_error if the decryptor doesn't match this note
 */
boost::optional<uint256> CWallet::GetNoteNullifier(const JSDescription& jsdesc,
                                                   const libzcash::PaymentAddress& address,
                                                   const ZCNoteDecryption& dec,
                                                   const uint256& hSig,
                                                   uint8_t n) const
{
    boost::optional<uint256> ret;
    auto note_pt = libzcash::NotePlaintext::decrypt(
        dec,
        jsdesc.ciphertexts[n],
        jsdesc.ephemeralKey,
        hSig,
        (unsigned char) n);
    auto note = note_pt.note(address);

    // Check note plaintext against note commitment
    if (note.cm() != jsdesc.commitments[n]) {
        throw libzcash::note_decryption_failed();
    }

    // SpendingKeys are only available if:
    // - We have them (this isn't a viewing key)
    // - The wallet is unlocked
    libzcash::SpendingKey key;
    if (GetSpendingKey(address, key)) {
        ret = note.nullifier(key);
    }
    return ret;
}

/**
 * Finds all output notes in the given transaction that have been sent to
 * PaymentAddresses in this wallet.
 *
 * It should never be necessary to call this method with a CWalletTx, because
 * the result of FindMyNotes (for the addresses available at the time) will
 * already have been cached in CWalletTx.mapNoteData.
 */
mapNoteData_t CWallet::FindMyNotes(const CTransactionBase& tx) const
{
    LOCK(cs_SpendingKeyStore);
    uint256 hash = tx.GetHash();

    mapNoteData_t noteData;
    for (size_t i = 0; i < tx.GetVjoinsplit().size(); i++) {
        auto hSig = tx.GetVjoinsplit()[i].h_sig(*pzcashParams, tx.GetJoinSplitPubKey());
        for (uint8_t j = 0; j < tx.GetVjoinsplit()[i].ciphertexts.size(); j++) {
            for (const NoteDecryptorMap::value_type& item : mapNoteDecryptors) {
                try {
                    auto address = item.first;
                    JSOutPoint jsoutpt {hash, i, j};
                    auto nullifier = GetNoteNullifier(
                        tx.GetVjoinsplit()[i],
                        address,
                        item.second,
                        hSig, j);
                    if (nullifier) {
                        CNoteData nd {address, *nullifier};
                        noteData.insert(std::make_pair(jsoutpt, nd));
                    } else {
                        CNoteData nd {address};
                        noteData.insert(std::make_pair(jsoutpt, nd));
                    }
                    break;
                } catch (const note_decryption_failed &err) {
                    // Couldn't decrypt with this decryptor
                } catch (const std::exception &exc) {
                    // Unexpected failure
                    LogPrintf("FindMyNotes(): Unexpected error while testing decrypt:\n");
                    LogPrintf("%s\n", exc.what());
                }
            }
        }
    }
    return noteData;
}

bool CWallet::IsFromMe(const uint256& nullifier) const
{
    {
        LOCK(cs_wallet);
        if (mapNullifiersToNotes.count(nullifier) &&
                mapWallet.count(mapNullifiersToNotes.at(nullifier).hash)) {
            return true;
        }
    }
    return false;
}

void CWallet::GetNoteWitnesses(std::vector<JSOutPoint> notes,
                               std::vector<boost::optional<ZCIncrementalWitness>>& witnesses,
                               uint256 &final_anchor)
{
    {
        LOCK(cs_wallet);
        witnesses.resize(notes.size());
        boost::optional<uint256> rt;
        int i = 0;
        for (JSOutPoint note : notes) {
            if (mapWallet.count(note.hash) &&
                    mapWallet[note.hash]->mapNoteData.count(note) &&
                    mapWallet[note.hash]->mapNoteData[note].witnesses.size() > 0) {
                witnesses[i] = mapWallet[note.hash]->mapNoteData[note].witnesses.front();
                if (!rt) {
                    rt = witnesses[i]->root();
                } else {
                    assert(*rt == witnesses[i]->root());
                }
            }
            i++;
        }
        // All returned witnesses have the same anchor
        if (rt) {
            final_anchor = *rt;
        }
    }
}

isminetype CWallet::IsMine(const CTxIn &txin) const
{
    {
        LOCK(cs_wallet);
        auto mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTransactionBase& prev = *(mi->second);
            if (txin.prevout.n < prev.getTxBase()->GetVout().size())
                return IsMine(prev.getTxBase()->GetVout()[txin.prevout.n]);
        }
    }
    return ISMINE_NO;
}

CAmount CWallet::GetDebit(const CTxIn &txin, const isminefilter& filter) const
{
    {
        LOCK(cs_wallet);
        auto mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end()) {
            const CWalletTransactionBase& prev = *(mi->second);
            if (txin.prevout.n < prev.getTxBase()->GetVout().size())
                if (IsMine(prev.getTxBase()->GetVout()[txin.prevout.n]) & filter)
                    return prev.getTxBase()->GetVout()[txin.prevout.n].nValue;
        }
    }
    return 0;
}

isminetype CWallet::IsMine(const CTxOut& txout) const
{
    return ::IsMine(*this, txout.scriptPubKey);
}

CAmount CWallet::GetCredit(const CTxOut& txout, const isminefilter& filter) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error("CWallet::GetCredit(): value out of range");
    return ((IsMine(txout) & filter) ? txout.nValue : 0);
}

bool CWallet::IsChange(const CTxOut& txout) const
{
    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a script that is ours, but is not in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    if (::IsMine(*this, txout.scriptPubKey))
    {
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
            return true;

        LOCK(cs_wallet);
        if (!mapAddressBook.count(address))
            return true;
    }
    return false;
}

CAmount CWallet::GetChange(const CTxOut& txout) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error("CWallet::GetChange(): value out of range");
    return (IsChange(txout) ? txout.nValue : 0);
}

bool CWallet::IsMine(const CTransactionBase& tx) const
{
    BOOST_FOREACH(const CTxOut& txout, tx.GetVout())
        if (IsMine(txout))
            return true;
    return false;
}

bool CWallet::BwtIsMine(const CScCertificate& cert) const
{
    for (unsigned int i = 0; i < cert.GetVout().size(); i++)
    {
        if (cert.IsBackwardTransfer(i) && IsMine(cert.GetVout()[i]))
            return true;
    }
    return false;
}

bool CWallet::IsFromMe(const CTransactionBase& tx) const
{
    if (GetDebit(tx, ISMINE_ALL) > 0) {
        return true;
    }
    for (const JSDescription& jsdesc : tx.GetVjoinsplit()) {
        for (const uint256& nullifier : jsdesc.nullifiers) {
            if (IsFromMe(nullifier)) {
                return true;
            }
        }
    }
    return false;
}

CAmount CWallet::GetDebit(const CTransactionBase& txBase, const isminefilter& filter) const
{
    CAmount nDebit = 0;
    for(const CTxIn& txin: txBase.GetVin()) {
        nDebit += GetDebit(txin, filter);
        if (!MoneyRange(nDebit))
            throw std::runtime_error("CWallet::GetDebit(): value out of range");
    }
    return nDebit;
}

CAmount CWallet::GetCredit(const CWalletTransactionBase& txWalletBase, const isminefilter& filter,
                           bool& fCanBeCached, bool keepImmatureVoutsOnly) const
{
    // If al least one vout is immature, result cannot be cached
    // Sum over mature vouts only or immature vouts only depending on keepImmatureVoutsOnly flag

    CAmount nCredit = 0;
    fCanBeCached = true;
    for(unsigned int pos = 0; pos < txWalletBase.getTxBase()->GetVout().size(); ++pos) {
        CCoins::outputMaturity outputMaturity = txWalletBase.IsOutputMature(pos);

        if (outputMaturity == CCoins::outputMaturity::NOT_APPLICABLE) {
            fCanBeCached = false;
            continue;
        }

        if (outputMaturity == CCoins::outputMaturity::IMMATURE) {
            fCanBeCached = false;
            if (!keepImmatureVoutsOnly) continue;
        } else {
            if (keepImmatureVoutsOnly) continue;
        }

        nCredit += GetCredit(txWalletBase.getTxBase()->GetVout()[pos], filter);
        if (!MoneyRange(nCredit))
            throw std::runtime_error("CWallet::GetCredit(): value out of range");
    }

    return nCredit;
}

CAmount CWallet::GetChange(const CTransactionBase& txBase) const
{
    CAmount nChange = 0;
    for(const CTxOut& txout: txBase.GetVout()) {
        nChange += GetChange(txout);
        if (!MoneyRange(nChange))
            throw std::runtime_error("CWallet::GetChange(): value out of range");
    }
    return nChange;
}

int CWalletTx::GetIndexInBlock(const CBlock& block)
{
    // Locate the index of certificate
    for (nIndex = 0; nIndex < (int)block.vtx.size(); nIndex++)
        if (block.vtx[nIndex] == wrappedTx)
            break;

    if (nIndex == (int)block.vtx.size())
    {
        LogPrintf("ERROR: %s(): couldn't find tx in block\n", __func__);
        return -1;
    }
    return nIndex;
}

CWalletTx::CWalletTx(): CWalletTransactionBase(nullptr), wrappedTx() {}

CWalletTx::CWalletTx(const CWallet* pwalletIn, const CTransaction& txIn):
    CWalletTransactionBase(pwalletIn), wrappedTx(txIn) {}

CWalletTx::CWalletTx(const CWalletTx& rhs):
    CWalletTransactionBase(rhs), wrappedTx(rhs.wrappedTx) {}

CWalletTx& CWalletTx::operator=(const CWalletTx& rhs)
{
    CWalletTransactionBase::operator=(rhs);
    *const_cast<CTransaction*>(&wrappedTx) = rhs.wrappedTx;
    this->mapNoteData = rhs.mapNoteData;
    return *this;
}

void CWalletTx::SetNoteData(mapNoteData_t &noteData)
{
    mapNoteData.clear();
    for (const std::pair<JSOutPoint, CNoteData> nd : noteData) {
        if (nd.first.js < wrappedTx.GetVjoinsplit().size() &&
                nd.first.n < wrappedTx.GetVjoinsplit()[nd.first.js].ciphertexts.size()) {
            // Store the address and nullifier for the Note
            mapNoteData[nd.first] = nd.second;
        } else {
            // If FindMyNotes() was used to obtain noteData,
            // this should never happen
            throw std::logic_error("CWalletTx::SetNoteData(): Invalid note");
        }
    }
}

int64_t CWalletTransactionBase::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

// btw, does anybody use it? Apparently not
int CWalletTransactionBase::GetRequestCount() const
{
    // Returns -1 if it wasn't being tracked
    int nRequests = -1;
    {
        LOCK(pwallet->cs_wallet);
        if (getTxBase()->IsCoinBase())
        {
            // Generated block
            if (!hashBlock.IsNull())
            {
                map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
                if (mi != pwallet->mapRequestCount.end())
                    nRequests = (*mi).second;
            }
        }
        else
        {
            // Did anyone request this transaction?
            map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(getTxBase()->GetHash());
            if (mi != pwallet->mapRequestCount.end())
            {
                nRequests = (*mi).second;

                // How about the block it's in?
                if (nRequests == 0 && !hashBlock.IsNull())
                {
                    map<uint256, int>::const_iterator ki = pwallet->mapRequestCount.find(hashBlock);
                    if (ki != pwallet->mapRequestCount.end())
                        nRequests = (*ki).second;
                    else
                        nRequests = 1; // If it's in someone else's block it must have got out
                }
            }
        }
    }
    return nRequests;
}

// GetAmounts will determine the transparent debits and credits for a given wallet tx.
void CWalletTx::GetAmounts(list<COutputEntry>& listReceived, list<COutputEntry>& listSent,
    CAmount& nFee, string& strSentAccount, const isminefilter& filter) const
{
    nFee = 0;
    listReceived.clear();
    listSent.clear();
    strSentAccount = strFromAccount;

    // Is this tx sent/signed by me?
    CAmount nDebit = GetDebit(filter);
    bool isFromMyTaddr = nDebit > 0; // debit>0 means we signed/sent this transaction

    // Does this tx spend my notes?
    bool isFromMyZaddr = false;
    for (const JSDescription& js : wrappedTx.GetVjoinsplit()) {
        for (const uint256& nullifier : js.nullifiers) {
            if (pwallet->IsFromMe(nullifier)) {
                isFromMyZaddr = true;
                break;
            }
        }
        if (isFromMyZaddr) {
            break;
        }
    }

    // Compute fee if we sent this transaction.
    if (isFromMyTaddr) {
        CAmount nValueOut = wrappedTx.GetValueOut();  // transparent outputs plus all vpub_old
        CAmount nValueIn = 0;
        for (const JSDescription & js : wrappedTx.GetVjoinsplit()) {
            nValueIn += js.vpub_new;
        }
        // nDebit has only vin contribution, we must add the ceased sc with part if any
        CAmount cswInTotAmount = wrappedTx.GetCSWValueIn();

        nFee = (nDebit+cswInTotAmount) - nValueOut + nValueIn;
    }

    // Create output entry for vpub_old/new, if we sent utxos from this transaction
    if (isFromMyTaddr) {
        CAmount myVpubOld = 0;
        CAmount myVpubNew = 0;
        for (const JSDescription& js : wrappedTx.GetVjoinsplit()) {
            bool fMyJSDesc = false;

            // Check input side
            for (const uint256& nullifier : js.nullifiers) {
                if (pwallet->IsFromMe(nullifier)) {
                    fMyJSDesc = true;
                    break;
                }
            }

            // Check output side
            if (!fMyJSDesc) {
                for (const std::pair<JSOutPoint, CNoteData> nd : this->mapNoteData) {
                    if (nd.first.js < wrappedTx.GetVjoinsplit().size() && nd.first.n < wrappedTx.GetVjoinsplit()[nd.first.js].ciphertexts.size()) {
                        fMyJSDesc = true;
                        break;
                    }
                }
            }

            if (fMyJSDesc) {
                myVpubOld += js.vpub_old;
                myVpubNew += js.vpub_new;
            }

            if (!MoneyRange(js.vpub_old) || !MoneyRange(js.vpub_new) || !MoneyRange(myVpubOld) || !MoneyRange(myVpubNew)) {
                 throw std::runtime_error("CWalletTx::GetAmounts: value out of range");
            }
        }

        // Create an output for the value taken from or added to the transparent value pool by JoinSplits
        if (myVpubOld > myVpubNew)
        {
            COutputEntry output = {
                CNoDestination(), myVpubOld - myVpubNew, CCoins::outputMaturity::MATURE,
                (int)wrappedTx.GetVout().size(), /*isBackwardTransfer*/false};
            listSent.push_back(output);
        }
        else if (myVpubNew > myVpubOld)
        {
            COutputEntry output = {
                CNoDestination(), myVpubNew - myVpubOld, CCoins::outputMaturity::MATURE,
                (int)wrappedTx.GetVout().size(), /*isBackwardTransfer*/false};
            listReceived.push_back(output);
        }
    }

    // Sent/received.
    for (unsigned int pos = 0; pos < wrappedTx.GetVout().size(); ++pos)
    {
        const CTxOut& txout = wrappedTx.GetVout()[pos];
        isminetype fIsMine = pwallet->IsMine(txout);
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            // Don't report 'change' txouts
            if (pwallet->IsChange(txout))
                continue;
        }
        else if (!(fIsMine & filter))
            continue;

        // In either case, we need to get a destination address if any is contained in the script
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
        {
            LogPrintf("%s():%d - can not get address for txout %d of tx %s (script %s)\n",
                __func__, __LINE__, pos, wrappedTx.GetHash().ToString(), txout.scriptPubKey.ToString());
            address = CNoDestination();
        }

        COutputEntry output = {
            address, txout.nValue, IsOutputMature(pos), (int)pos, /*isBackwardTransfer*/false};

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(output);

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine & filter)
            listReceived.push_back(output);
    }

    if (wrappedTx.IsScVersion() )
    {
        if (nDebit > 0)
        {
            // Create a single cummulative record for the sc related outputs.
            CAmount totalScOut = 0;

            for(const auto& out : wrappedTx.GetVscCcOut())
                totalScOut += out.nValue;
            for(const auto& out : wrappedTx.GetVftCcOut())
                totalScOut += out.nValue;
            for(const auto& out : wrappedTx.GetVBwtRequestOut())
                totalScOut += out.scFee;

            COutputEntry output = {
                CNoDestination(), totalScOut, CCoins::outputMaturity::MATURE,
                (int)wrappedTx.GetVout().size(), /*isBackwardTransfer*/false};
            listSent.push_back(output);

        }
    }
}

void CWalletTransactionBase::GetMatureAmountsForAccount(const string& strAccount, CAmount& nReceived,
                                  CAmount& nSent, CAmount& nFee, const isminefilter& filter) const
{
    nReceived = nSent = nFee = 0;

    CAmount allFee;
    string strSentAccount;
    list<COutputEntry> listReceived;
    list<COutputEntry> listSent;
    GetAmounts(listReceived, listSent, allFee, strSentAccount, filter);

    if (strAccount == strSentAccount) {
        for(const COutputEntry& s: listSent)
            nSent += s.amount;
        nFee = allFee;
    }
    {
        LOCK(pwallet->cs_wallet);
        for(const COutputEntry& r: listReceived) {
            if (pwallet->mapAddressBook.count(r.destination)) {
                map<CTxDestination, CAddressBookData>::const_iterator mi = pwallet->mapAddressBook.find(r.destination);
                if (mi != pwallet->mapAddressBook.end() &&
                    (*mi).second.name == strAccount &&
                    r.maturity == CCoins::outputMaturity::MATURE)
                    nReceived += r.amount;
            }
            else if (strAccount.empty() && r.maturity == CCoins::outputMaturity::MATURE)
            {
                nReceived += r.amount;
            }
        }
    }
}

bool CWalletTransactionBase::WriteToDisk(CWalletDB *pwalletdb)
{
    return pwalletdb->WriteWalletTxBase(getTxBase()->GetHash(), *this);
}

void CWallet::WitnessNoteCommitment(std::vector<uint256> commitments,
                                    std::vector<boost::optional<ZCIncrementalWitness>>& witnesses,
                                    uint256 &final_anchor)
{
    witnesses.resize(commitments.size());
    CBlockIndex* pindex = chainActive.Genesis();
    ZCIncrementalMerkleTree tree;

    while (pindex) {
        CBlock block;
        ReadBlockFromDisk(block, pindex);

        BOOST_FOREACH(const CTransaction& tx, block.vtx)
        {
            BOOST_FOREACH(const JSDescription& jsdesc, tx.GetVjoinsplit())
            {
                BOOST_FOREACH(const uint256 &note_commitment, jsdesc.commitments)
                {
                    tree.append(note_commitment);

                    BOOST_FOREACH(boost::optional<ZCIncrementalWitness>& wit, witnesses) {
                        if (wit) {
                            wit->append(note_commitment);
                        }
                    }

                    size_t i = 0;
                    BOOST_FOREACH(uint256& commitment, commitments) {
                        if (note_commitment == commitment) {
                            witnesses.at(i) = tree.witness();
                        }
                        i++;
                    }
                }
            }
        }

        uint256 current_anchor = tree.root();

        // Consistency check: we should be able to find the current tree
        // in our CCoins view.
        ZCIncrementalMerkleTree dummy_tree;
        assert(pcoinsTip->GetAnchorAt(current_anchor, dummy_tree));

        pindex = chainActive.Next(pindex);
    }

    // TODO: #93; Select a root via some heuristic.
    final_anchor = tree.root();

    BOOST_FOREACH(boost::optional<ZCIncrementalWitness>& wit, witnesses) {
        if (wit) {
            assert(final_anchor == wit->root());
        }
    }
}

/**
 * Scan the block chain (starting in pindexStart) for transactions
 * from or to us. If fUpdate is true, found transactions that already
 * exist in the wallet will be updated.
 */
int CWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate)
{
    int ret = 0;
    int64_t nNow = GetTime();
    const CChainParams& chainParams = Params();

    CBlockIndex* pindex = pindexStart;
    {
        LOCK2(cs_main, cs_wallet);

        // no need to read and scan block, if block was created before
        // our wallet birthday (as adjusted for block time variability)
        while (pindex && nTimeFirstKey && (pindex->GetBlockTime() < (nTimeFirstKey - TIMESTAMP_WINDOW)))
            pindex = chainActive.Next(pindex);

        ShowProgress(_("Rescanning..."), 0); // show rescan progress in GUI as dialog or on splashscreen, if -rescan on startup
        double dProgressStart = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex, false);
        double dProgressTip = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip(), false);
        while (pindex)
        {
            if (pindex->nHeight % 100 == 0 && dProgressTip - dProgressStart > 0.0)
                ShowProgress(_("Rescanning..."), std::max(1, std::min(99, (int)((Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex, false) - dProgressStart) / (dProgressTip - dProgressStart) * 100))));

            CBlock block;
            ReadBlockFromDisk(block, pindex);
  
            for(const CTransaction& tx: block.vtx)
            {
                if (AddToWalletIfInvolvingMe(tx, &block, -1, fUpdate))
                    ret++;
            }

            std::set<uint256> visitedScIds;
            // It's safe to process certs backward despite possible spending dependencies of certs in block
            // since at this stage no transaction creation is allowed
            for(auto itCert = block.vcert.rbegin(); itCert != block.vcert.rend(); ++itCert)
            {
                // The ReadSidechain() call can fail if no certificates for that sc are currently in the wallet.
                // This can happen for instance when we are called from an importwallet rpc cmd or when the
                // node is started after a while.
                bool prevScDataAvailable = false;
                CScCertificateStatusUpdateInfo prevScData;
                if (ReadSidechain(itCert->GetScId(), prevScData))
                {
                     prevScDataAvailable = true;
                }

                bool bTopQualityCert = visitedScIds.count(itCert->GetScId()) == 0;
                visitedScIds.insert(itCert->GetScId());

                int nHeight = pindex->nHeight;
                CSidechain sidechain;
                assert(pcoinsTip->GetSidechain(itCert->GetScId(), sidechain));
                int bwtMaxDepth = sidechain.GetCertMaturityHeight(itCert->epochNumber) - nHeight;

                if (AddToWalletIfInvolvingMe(*itCert, &block, bwtMaxDepth, fUpdate))
                {
                    ret++;
                    if (fUpdate)
                    {
                        // this call will add sc data into the wallet
                        SyncCertStatusInfo(CScCertificateStatusUpdateInfo(itCert->GetScId(), itCert->GetHash(),
                                                                          itCert->epochNumber, itCert->quality,
                                                                          bTopQualityCert? CScCertificateStatusUpdateInfo::BwtState::BWT_ON:
                                                                                           CScCertificateStatusUpdateInfo::BwtState::BWT_OFF));

                        if (prevScDataAvailable)
                        {
                            if (bTopQualityCert && (prevScData.certEpoch == itCert->epochNumber) && (prevScData.certQuality < itCert->quality))
                            {
                                SyncCertStatusInfo(CScCertificateStatusUpdateInfo(prevScData.scId, prevScData.certHash,
                                                                              prevScData.certEpoch, prevScData.certQuality,
                                                                              CScCertificateStatusUpdateInfo::BwtState::BWT_OFF));
                            }
                        }
                    }
                }
            }

            ZCIncrementalMerkleTree tree;
            // This should never fail: we should always be able to get the tree
            // state on the path to the tip of our chain
            assert(pcoinsTip->GetAnchorAt(pindex->hashAnchor, tree));
            // Increment note witness caches
            IncrementNoteWitnesses(pindex, &block, tree);

            pindex = chainActive.Next(pindex);
            if (pindex)
            {
                if (GetTime() >= nNow + 60) {
                    nNow = GetTime();
                    LogPrintf("Still rescanning. At block %d. Progress=%f\n", pindex->nHeight, Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex));
                }
            }
        }

        // Once processed all blocks till chainActive.Tip(), void last cert of ceased sidechains
        std::set<uint256> allScIds;
        pcoinsTip->GetScIds(allScIds);
        for(const auto& scId: allScIds)
        {
            if (pcoinsTip->GetSidechainState(scId) != CSidechain::State::ALIVE)
            {
                CSidechain sidechain;
                assert(pcoinsTip->GetSidechain(scId, sidechain));
                if (fUpdate)
                    SyncCertStatusInfo(CScCertificateStatusUpdateInfo(scId, sidechain.lastTopQualityCertHash,
                                                                      sidechain.lastTopQualityCertReferencedEpoch,
                                                                      sidechain.lastTopQualityCertQuality,
                                                                      CScCertificateStatusUpdateInfo::BwtState::BWT_OFF));
            }
        }

        ShowProgress(_("Rescanning..."), 100); // hide progress dialog in GUI
    }
    return ret;
}

void CWallet::ReacceptWalletTransactions()
{
    // If transactions aren't being broadcasted, don't let them into local mempool either
    if (!fBroadcastTransactions)
        return;
    LOCK2(cs_main, cs_wallet);
    std::map<int64_t, CWalletTransactionBase*> mapSorted;

    // Sort pending wallet transactions based on their initial wallet insertion order
    for (auto& item: mapWallet)
    {
        const uint256& wtxid = item.first;
        CWalletTransactionBase& wtx = *(item.second);
        assert(wtx.getTxBase()->GetHash() == wtxid);

        int nDepth = wtx.GetDepthInMainChain();

        if (!wtx.getTxBase()->IsCoinBase() && nDepth < 0) {
            mapSorted.insert(std::make_pair(wtx.nOrderPos, &wtx));
        }
    }

    CValidationState stateDummy;
        
    for (auto& item : mapSorted)
    {
        CWalletTransactionBase& wtx = *(item.second);
        LOCK(mempool.cs);
        AcceptTxBaseToMemoryPool(mempool, stateDummy, *wtx.getTxBase(), LimitFreeFlag::OFF, RejectAbsurdFeeFlag::ON,
                                 MempoolProofVerificationFlag::SYNC);
    }
}

bool CWalletTx::RelayWalletTransaction()
{
    assert(pwallet->GetBroadcastTransactions());
    if (!wrappedTx.IsCoinBase())
    {
        if (GetDepthInMainChain() == 0) {
            LogPrintf("Relaying wtx %s\n", wrappedTx.GetHash().ToString());
            wrappedTx.Relay();
            return true;
        }
    }
    return false;
}

void CWalletTransactionBase::addOrderedInputTx(TxItems& txOrdered, const CScript& scriptPubKey) const
{
    for(const CTxIn& txin: getTxBase()->GetVin())
    {
        auto mi = pwallet->getMapWallet().find(txin.prevout.hash);
        if (mi == pwallet->getMapWallet().end()) {
            continue;
        }
        const auto& inputTx = (*mi).second;

        if (txin.prevout.n >= inputTx->getTxBase()->GetVout().size()) {
            continue;
        }
        const CTxOut& utxo = inputTx->getTxBase()->GetVout()[txin.prevout.n];

        auto res = std::search(utxo.scriptPubKey.begin(), utxo.scriptPubKey.end(), scriptPubKey.begin(), scriptPubKey.end());
        if (res == utxo.scriptPubKey.begin()) {
            auto meAsObj = pwallet->getMapWallet().at(getTxBase()->GetHash());
            txOrdered.insert(make_pair(nOrderPos, TxPair(meAsObj.get(), (CAccountingEntry*)0)));
            return;
        }
    }
}

CAmount CWalletTransactionBase::GetDebit(const isminefilter& filter) const
{
    if (getTxBase()->GetVin().empty())
        return 0;

    CAmount debit = 0;
    if(filter & ISMINE_SPENDABLE)
    {
        if (fDebitCached)
            debit += nDebitCached;
        else
        {
            nDebitCached = pwallet->GetDebit(*getTxBase(), ISMINE_SPENDABLE);
            fDebitCached = true;
            debit += nDebitCached;
        }
    }
    if(filter & ISMINE_WATCH_ONLY)
    {
        if(fWatchDebitCached)
            debit += nWatchDebitCached;
        else
        {
            nWatchDebitCached = pwallet->GetDebit(*getTxBase(), ISMINE_WATCH_ONLY);
            fWatchDebitCached = true;
            debit += nWatchDebitCached;
        }
    }
    return debit;
}

bool CWalletTransactionBase::HasImmatureOutputs() const
{
    for(unsigned int pos = 0; pos < getTxBase()->GetVout().size(); ++pos)
    {
        switch(this->IsOutputMature(pos)) {
        case CCoins::outputMaturity::MATURE:
            continue;

        case CCoins::outputMaturity::IMMATURE:
            return true;

        case CCoins::outputMaturity::NOT_APPLICABLE:
            // is OutputMature returns NOT_APPLICABLE even if the output is spent, but others can be spendable
            continue;

        default:
            return false;
        }
    }

    return false;
}

bool CWalletTransactionBase::HasMatureOutputs() const
{
    if (GetDepthInMainChain() < 0)
        return false;

    for(unsigned int pos = 0; pos < getTxBase()->GetVout().size(); ++pos)
    {
        switch(this->IsOutputMature(pos)) {
        case CCoins::outputMaturity::MATURE:
            return true;

        case CCoins::outputMaturity::IMMATURE:
            continue;

        case CCoins::outputMaturity::NOT_APPLICABLE:
            // is OutputMature returns NOT_APPLICABLE even if the output is spent, but others can be spendable
            continue;

        default:
            return false;
        }
    }

    // Check if it is a shielded transaction
    if (getTxBase()->GetVjoinsplit().size() > 0)
    {
        return true;
    }

    return false;
}

CCoins::outputMaturity CWalletTransactionBase::IsOutputMature(unsigned int vOutPos) const
{
    int nDepth = GetDepthInMainChain();
    if (nDepth < 0)
        return CCoins::outputMaturity::NOT_APPLICABLE;

    if (nDepth == 0)
    {
        if (!getTxBase()->IsCoinBase() && !getTxBase()->IsCertificate())
            return CCoins::outputMaturity::MATURE;

        if (!getTxBase()->IsBackwardTransfer(vOutPos))
           return CCoins::outputMaturity::MATURE;
        else
        {
            assert(bwtAreStripped);
            return CCoins::outputMaturity::NOT_APPLICABLE;
        }

    }

    //Hereinafter tx in getTxBase() in mainchain
    if (!getTxBase()->IsCoinBase() && !getTxBase()->IsCertificate())
        return CCoins::outputMaturity::MATURE;

    if (getTxBase()->IsCoinBase())
    {
        if (nDepth <= COINBASE_MATURITY)
            return CCoins::outputMaturity::IMMATURE;
        else
            return CCoins::outputMaturity::MATURE;
    }

    //Hereinafter cert in mainchain
    if (!getTxBase()->IsBackwardTransfer(vOutPos))
        return CCoins::outputMaturity::MATURE;

    if (getTxBase()->IsBackwardTransfer(vOutPos) && bwtAreStripped)
        return CCoins::outputMaturity::NOT_APPLICABLE;

    if (nDepth <= bwtMaturityDepth)
        return CCoins::outputMaturity::IMMATURE;
    else
        return CCoins::outputMaturity::MATURE;
}

CAmount CWalletTransactionBase::GetCredit(const isminefilter& filter) const
{
    int64_t credit = 0;
    if ((this->getTxBase()->IsCoinBase() || this->getTxBase()->IsCertificate()) && this->HasImmatureOutputs())
    {
        fCreditCached = false;
        fWatchCreditCached = false;
    }

    if (filter & ISMINE_SPENDABLE) {
        // It used to be that GetBalance can assume transactions in mapWallet won't change
        // With certificate it is up to the transaction to tell whether its credit
        // won't change anymore
        if (!fCreditCached)
            nCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE, fCreditCached, /*keepImmatureVoutsOnly*/false);

        credit += nCreditCached;
    }

    if (filter & ISMINE_WATCH_ONLY) {
        // Again here, with certificate it is up to the transaction to tell whether its credit
        // won't change anymore
        if (!fWatchCreditCached)
            nWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY, fWatchCreditCached, /*keepImmatureVoutsOnly*/false);

        credit += nWatchCreditCached;
    }

    return credit;
}

CAmount CWalletTransactionBase::GetImmatureCredit(bool fUseCache) const
{
    if ((this->getTxBase()->IsCoinBase() || this->getTxBase()->IsCertificate()) && this->HasImmatureOutputs())
    {
        if (fUseCache && fImmatureCreditCached)
            return nImmatureCreditCached;

        nImmatureCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE, fImmatureCreditCached, /*keepImmatureVoutsOnly*/true);
        return nImmatureCreditCached;
    }

    return CAmount(0);
}

CAmount CWalletTransactionBase::GetImmatureWatchOnlyCredit(const bool& fUseCache) const
{
    if ((this->getTxBase()->IsCoinBase() || this->getTxBase()->IsCertificate()) && this->HasImmatureOutputs())
    {
        if (fUseCache && fImmatureWatchCreditCached)
            return nImmatureWatchCreditCached;

        nImmatureWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY, fImmatureWatchCreditCached, /*keepImmatureVoutsOnly*/true);
        return nImmatureWatchCreditCached;
    }

    return CAmount(0);
}

CAmount CWalletTransactionBase::GetAvailableCredit(bool fUseCache) const
{
    if (pwallet == 0)
        return 0;

    if ((this->getTxBase()->IsCoinBase() || this->getTxBase()->IsCertificate()) && this->HasImmatureOutputs())
    {
        fAvailableCreditCached = false;
    }

    if (fUseCache && fAvailableCreditCached)
        return nAvailableCreditCached;

    CAmount nCredit = 0;
    fAvailableCreditCached = true;
    for (unsigned int pos = 0; pos < getTxBase()->GetVout().size(); pos++)
    {
        CCoins::outputMaturity outputMaturity = this->IsOutputMature(pos);
        if (outputMaturity == CCoins::outputMaturity::NOT_APPLICABLE) {
            continue;
        }

        if (outputMaturity == CCoins::outputMaturity::IMMATURE) {
            fAvailableCreditCached = false;
            continue;
        }

        if (!pwallet->IsSpent(getTxBase()->GetHash(), pos)) {
            nCredit += pwallet->GetCredit(getTxBase()->GetVout()[pos], ISMINE_SPENDABLE);
            if (!MoneyRange(nCredit))
                throw std::runtime_error("CWalletTransactionBase::GetAvailableCredit() : value out of range");
        }
    }

    if (fAvailableCreditCached)
        nAvailableCreditCached = nCredit;

    return nCredit;
}


CAmount CWalletTransactionBase::GetAvailableWatchOnlyCredit(const bool& fUseCache) const
{
    if (pwallet == 0)
        return 0;

    if ((this->getTxBase()->IsCoinBase() || this->getTxBase()->IsCertificate()) && this->HasImmatureOutputs())
    {
        fAvailableWatchCreditCached = false;
    }

    if (fUseCache && fAvailableWatchCreditCached)
        return nAvailableWatchCreditCached;

    CAmount nCredit = 0;
    fAvailableWatchCreditCached = true;
    for (unsigned int pos = 0; pos < getTxBase()->GetVout().size(); pos++) {
        CCoins::outputMaturity outputMaturity = this->IsOutputMature(pos);
        if (outputMaturity == CCoins::outputMaturity::NOT_APPLICABLE) {
            fAvailableWatchCreditCached = false;
            continue;
        }

        if (outputMaturity == CCoins::outputMaturity::IMMATURE) {
            fAvailableWatchCreditCached = false;
            continue;
        }

        if (!pwallet->IsSpent(getTxBase()->GetHash(), pos))
        {
            const CTxOut &txout = getTxBase()->GetVout()[pos];
            nCredit += pwallet->GetCredit(txout, ISMINE_WATCH_ONLY);
            if (!MoneyRange(nCredit))
                throw std::runtime_error("CWalletTTransactionBase:GetAvailableWatchOnlyCredit() : value out of range");
        }
    }

    if (fAvailableWatchCreditCached)
        nAvailableWatchCreditCached = nCredit;

    return nCredit;
}

void CWalletTransactionBase::MarkDirty()
{
    fCreditCached = false;
    fAvailableCreditCached = false;
    fWatchDebitCached = false;
    fWatchCreditCached = false;
    fAvailableWatchCreditCached = false;
    fImmatureWatchCreditCached = false;
    fDebitCached = false;
    fChangeCached = false;
}

void CWalletTransactionBase::Reset(const CWallet* pwalletIn)
{
    hashBlock.SetNull();
    vMerkleBranch.clear();
    nIndex = -1;
    fMerkleVerified = false;
    pwallet = pwalletIn;
    mapValue.clear();
    vOrderForm.clear();
    fTimeReceivedIsTxTime = false;
    nTimeReceived = 0;
    nTimeSmart = 0;
    fFromMe = false;
    strFromAccount.clear();
    fDebitCached = false;
    fCreditCached = false;
    fImmatureCreditCached = false;
    fAvailableCreditCached = false;
    fWatchDebitCached = false;
    fWatchCreditCached = false;
    fImmatureWatchCreditCached = false;
    fAvailableWatchCreditCached = false;
    fChangeCached = false;
    nDebitCached = 0;
    nCreditCached = 0;
    nImmatureCreditCached = 0;
    nAvailableCreditCached = 0;
    nWatchDebitCached = 0;
    nWatchCreditCached = 0;
    nAvailableWatchCreditCached = 0;
    nImmatureWatchCreditCached = 0;
    nChangeCached = 0;
    nOrderPos = -1;

    bwtMaturityDepth = -1;
    bwtAreStripped = false;
}

std::set<uint256> CWalletTransactionBase::GetConflicts() const
{
    set<uint256> result;
    if (pwallet != nullptr) {
        uint256 myHash = getTxBase()->GetHash();
        result = pwallet->GetConflicts(myHash);
        result.erase(myHash);
    }
    return result;
}

CAmount CWalletTransactionBase::GetChange() const
{
    if (fChangeCached)
        return nChangeCached;
    nChangeCached = pwallet->GetChange(*getTxBase());
    fChangeCached = true;
    return nChangeCached;
}

bool CWalletTransactionBase::IsTrusted(bool canSpendZeroConfChange) const
{
    // Quick answer in most cases
    if (!CheckFinalTx(*getTxBase()))
        return false;

    int nDepth = GetDepthInMainChain();
    if (nDepth >= 1)
        return true;
    if (nDepth < 0)
        return false;
    if (!canSpendZeroConfChange || !IsFromMe(ISMINE_ALL)) // using wtx's cached debit
        return false;

    // Trusted if all inputs are from us and are in the mempool:
    BOOST_FOREACH(const CTxIn& txin, getTxBase()->GetVin())
    {
        // Transactions not sent by us: not trusted
        const CWalletTransactionBase* parent = pwallet->GetWalletTx(txin.prevout.hash);
        if (parent == nullptr)
            return false;
        const CTxOut& parentOut = parent->getTxBase()->GetVout()[txin.prevout.n];
        if (pwallet->IsMine(parentOut) != ISMINE_SPENDABLE)
            return false;
    }
    return true;
}


std::vector<uint256> CWallet::ResendWalletTransactionsBefore(int64_t nTime)
{
    std::vector<uint256> result;

    LOCK(cs_wallet);
    // Sort them in chronological order
    multimap<unsigned int, CWalletTransactionBase*> mapSorted;
    for (auto& item: mapWallet)
    {
        CWalletTransactionBase& wtx = *(item.second);
        // Don't rebroadcast if newer than nTime:
        if (wtx.nTimeReceived > nTime)
            continue;
        mapSorted.insert(make_pair(wtx.nTimeReceived, &wtx));
    }
    for (auto& item : mapSorted)
    {
        CWalletTransactionBase& wtx = *(item.second);
        if (wtx.RelayWalletTransaction())
            result.push_back(wtx.getTxBase()->GetHash());
    }
    return result;
}

void CWallet::ResendWalletTransactions(int64_t nBestBlockTime)
{
    // Do this infrequently and randomly to avoid giving away
    // that these are our transactions.
    if (GetTime() < nNextResend || !fBroadcastTransactions)
        return;
    bool fFirst = (nNextResend == 0);
    nNextResend = GetTime() + GetRand(30 * 60);
    if (fFirst)
        return;

    // Only do it if there's been a new block since last time
    if (nBestBlockTime < nLastResend)
        return;
    nLastResend = GetTime();

    // Rebroadcast unconfirmed txes older than 5 minutes before the last
    // block was found:
    std::vector<uint256> relayed = ResendWalletTransactionsBefore(nBestBlockTime-5*60);
    if (!relayed.empty())
        LogPrintf("%s: rebroadcast %u unconfirmed transactions\n", __func__, relayed.size());
}

/** @} */ // end of mapWallet




/** @defgroup Actions
 *
 * @{
 */


CAmount CWallet::GetBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (MAP_WALLET_CONST_IT it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTransactionBase* pcoin = it->second.get();
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableCredit();
        }
    }

    return nTotal;
}

CAmount CWallet::GetUnconfirmedBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (MAP_WALLET_CONST_IT it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTransactionBase* pcoin = it->second.get();
            if (!CheckFinalTx(*pcoin->getTxBase()) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
                nTotal += pcoin->GetAvailableCredit();
        }
    }
    return nTotal;
}

void CWallet::GetUnconfirmedData(const std::string& address, int& numbOfUnconfirmedTx, CAmount& unconfInput,
    CAmount& unconfOutput, CAmount& bwtImmatureOutput, eZeroConfChangeUsage zconfchangeusage, bool fIncludeNonFinal) const
{
    unconfOutput = 0;
    unconfInput = 0;
    bwtImmatureOutput = 0;
    numbOfUnconfirmedTx = 0;

    LogPrint("sc", "%s():%d - called zconfchangeusage[%d], fIncludeNonFinal[%d]\n",
        __func__, __LINE__, (int)zconfchangeusage, fIncludeNonFinal);

    CBitcoinAddress taddr = CBitcoinAddress(address);
    if (!taddr.IsValid())
    {
        // taddr should be checked by the caller
        return;
    }

    // tx are ordered in this vector from the oldest to the newest
    vTxWithInputs txOrdered = OrderedTxWithInputs(address);

    const CScript& scriptToMatch = GetScriptForDestination(taddr.Get(), false);

    {
        LOCK2(cs_main, cs_wallet);

        for (vTxWithInputs::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
        {
            const CWalletTransactionBase* pcoin = (*it);
            
            const CScCertificate* cert = nullptr;
            if (pcoin->getTxBase()->IsCertificate() )
            {
                cert = dynamic_cast<const CScCertificate*>(pcoin->getTxBase());
                assert(cert != nullptr);
            }

            bool trusted = false;
            if (zconfchangeusage == eZeroConfChangeUsage::ZCC_UNDEF)
            {
                // use the default settings from zend option
                trusted = pcoin->IsTrusted();
            }
            else
            {
                trusted = pcoin->IsTrusted(zconfchangeusage == eZeroConfChangeUsage::ZCC_TRUE);
            }

            bool isFinal = CheckFinalTx(*pcoin->getTxBase());
            LogPrint("sc", "%s():%d - isFinal[%d], tx[%s]\n",
                __func__, __LINE__, (int)isFinal, pcoin->getTxBase()->GetHash().ToString());

            if (!fIncludeNonFinal && !isFinal)
                continue;

            // look for outputs
            bool outputFound = false;
            if (!isFinal || (!trusted && pcoin->GetDepthInMainChain() == 0))
            {
                int vout_idx = 0;

                for(const auto& txout : pcoin->getTxBase()->GetVout())
                {
                    if (cert && cert->IsBackwardTransfer(vout_idx) )
                    {
                        // skip any certificate bwt, they must not take part in unconfOutput amount
                        continue;
                    }

                    auto res = std::search(txout.scriptPubKey.begin(), txout.scriptPubKey.end(), scriptToMatch.begin(), scriptToMatch.end());
                    if (res == txout.scriptPubKey.begin())
                    {
                        outputFound = true;
                   
                        if (!IsSpent(pcoin->getTxBase()->GetHash(), vout_idx))
                        {
                            unconfOutput += GetCredit(txout, ISMINE_SPENDABLE);
                            LogPrint("cert", "%s():%d - found out of matching tx[%s] with credit\n",
                                __func__, __LINE__, pcoin->getTxBase()->GetHash().ToString());
                        }
                        else
                        {
                            LogPrint("cert", "%s():%d - found matching tx[%s] but out[%d] is spent: %s\n",
                                __func__, __LINE__, pcoin->getTxBase()->GetHash().ToString(), vout_idx, pcoin->getTxBase()->ToString() );
                        }
                    }
                    vout_idx++;
                }
            }
             
            // look for inputs, and do not consider the trusted flag, since it is related only to tx outputs
            bool inputFound = false;
            if (!isFinal || pcoin->GetDepthInMainChain() == 0)
            {
                for (const CTxIn& txin : pcoin->getTxBase()->GetVin())
                {
                    const uint256& inputTxHash = txin.prevout.hash;

                    auto mi = mapWallet.find(inputTxHash);
                    if (mi != mapWallet.end() )
                    {
                        const CTxOut& txout = (*mi).second->getTxBase()->GetVout()[txin.prevout.n];
                        auto res = std::search(txout.scriptPubKey.begin(), txout.scriptPubKey.end(), scriptToMatch.begin(), scriptToMatch.end());
                        if (res == txout.scriptPubKey.begin())
                        {
                            unconfInput += GetCredit(txout, ISMINE_SPENDABLE);
                            inputFound = true;
                        }
                    }
                }
            }

            if (inputFound || outputFound)
            {
                numbOfUnconfirmedTx++;
            }

            // only for certificates, look for immature amounts of bwts, regardless on number of confirmations
            if (cert != nullptr)
            {
                for (unsigned int i = 0; i < cert->GetVout().size(); i++) {
                    if (cert->IsBackwardTransfer(i) &&
                        pcoin->IsOutputMature(i) == CCoins::outputMaturity::IMMATURE)
                    {
                        const CTxOut& txout = cert->GetVout()[i];

                        auto res = std::search(txout.scriptPubKey.begin(), txout.scriptPubKey.end(), scriptToMatch.begin(), scriptToMatch.end());
                        if (res == txout.scriptPubKey.begin())
                        {
                            bwtImmatureOutput += GetCredit(txout, ISMINE_SPENDABLE);
                            LogPrint("cert", "%s():%d - found bwtout of matching cert[%s] with immature credit\n",
                                __func__, __LINE__, cert->GetHash().ToString());
                        }
                    }
                }
            }
        }
    }
}

CAmount CWallet::GetImmatureBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (MAP_WALLET_CONST_IT it = mapWallet.begin(); it != mapWallet.end(); ++it)
            nTotal += it->second.get()->GetImmatureCredit();

    }
    return nTotal;
}

CAmount CWallet::GetWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (MAP_WALLET_CONST_IT it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTransactionBase* pcoin = it->second.get();
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableWatchOnlyCredit();
        }
    }

    return nTotal;
}

CAmount CWallet::GetUnconfirmedWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (MAP_WALLET_CONST_IT it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTransactionBase* pcoin = it->second.get();
            if (!CheckFinalTx(*pcoin->getTxBase()) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
                nTotal += pcoin->GetAvailableWatchOnlyCredit();
        }
    }
    return nTotal;
}

CAmount CWallet::GetImmatureWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (MAP_WALLET_CONST_IT it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTransactionBase* pcoin = it->second.get();
            nTotal += pcoin->GetImmatureWatchOnlyCredit();
        }
    }
    return nTotal;
}

/**
 * populate vCoins with vector of available COutputs.
 */
void CWallet::AvailableCoins(vector<COutput>& vCoins, bool fOnlyConfirmed, const CCoinControl *coinControl, bool fIncludeZeroValue, bool fIncludeCoinBase, bool fIncludeCommunityFund) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (MAP_WALLET_CONST_IT it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const uint256& wtxid = it->first;
            const CWalletTransactionBase* pcoin = (*it).second.get();
            if (!CheckFinalTx(*pcoin->getTxBase()))
                continue;

            if (fOnlyConfirmed && !pcoin->IsTrusted())
                continue;

            if (pcoin->getTxBase()->IsCoinBase() && !fIncludeCoinBase && !fIncludeCommunityFund)
                continue;

            if (!pcoin->HasMatureOutputs())
                continue;

            for (unsigned int voutPos = 0; voutPos < pcoin->getTxBase()->GetVout().size(); voutPos++) {
                isminetype mine = IsMine(pcoin->getTxBase()->GetVout()[voutPos]);
                if (!IsSpent(wtxid, voutPos) &&
                     mine != ISMINE_NO &&
                    !IsLockedCoin((*it).first, voutPos) &&
                    (pcoin->getTxBase()->GetVout()[voutPos].nValue > 0 || fIncludeZeroValue) &&
                    (!coinControl || !coinControl->HasSelected() ||
                      coinControl->fAllowOtherInputs || coinControl->IsSelected((*it).first, voutPos)
                    ))
                {
                    if (pcoin->getTxBase()->IsCoinBase()) {
                        const CCoins *coins = pcoinsTip->AccessCoins(wtxid);
                        assert(coins);

                        if (IsCommunityFund(coins, voutPos)) {
                            if(!fIncludeCommunityFund)
                                continue;
                        } else {
                            if(!fIncludeCoinBase)
                                continue;
                        }
                    } else if (pcoin->getTxBase()->IsCertificate()) {
                        if (pcoin->IsOutputMature(voutPos) != CCoins::outputMaturity::MATURE)
                            continue;

                        LogPrint("cert", "%s():%d - cert[%s] out[%d], amount=%s, spendable[%s]\n", __func__, __LINE__,
                            pcoin->getTxBase()->GetHash().ToString(), voutPos, FormatMoney(pcoin->getTxBase()->GetVout()[voutPos].nValue), ((mine & ISMINE_SPENDABLE) != ISMINE_NO)?"Y":"N");
                    }
                    int nDepth = pcoin->GetDepthInMainChain();
                    vCoins.push_back(COutput(pcoin, voutPos, nDepth, (mine & ISMINE_SPENDABLE) != ISMINE_NO));
                }

            }
        }
    }
}

static void ApproximateBestSubset(
    vector<pair<CAmount, pair<const CWalletTransactionBase*,unsigned int> > >vValue, const CAmount& nTotalLower, const CAmount& nTargetValue,
    vector<char>& vfBest, CAmount& nBest, int iterations = 1000)
{
    vector<char> vfIncluded;

    vfBest.assign(vValue.size(), true);
    nBest = nTotalLower;

    seed_insecure_rand();

    for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++)
    {
        vfIncluded.assign(vValue.size(), false);
        CAmount nTotal = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
        {
            for (unsigned int i = 0; i < vValue.size(); i++)
            {
                //The solver here uses a randomized algorithm,
                //the randomness serves no real security purpose but is just
                //needed to prevent degenerate behavior and it is important
                //that the rng is fast. We do not use a constant random sequence,
                //because there may be some privacy improvement by making
                //the selection random.
                if (nPass == 0 ? insecure_rand()&1 : !vfIncluded[i])
                {
                    nTotal += vValue[i].first;
                    vfIncluded[i] = true;
                    if (nTotal >= nTargetValue)
                    {
                        fReachedTarget = true;
                        if (nTotal < nBest)
                        {
                            nBest = nTotal;
                            vfBest = vfIncluded;
                        }
                        nTotal -= vValue[i].first;
                        vfIncluded[i] = false;
                    }
                }
            }
        }
    }
}

bool CWallet::SelectCoinsMinConf(const CAmount& nTargetValue, int nConfMine, int nConfTheirs, vector<COutput> vCoins,
                                 set<pair<const CWalletTransactionBase*,unsigned int> >& setCoinsRet, CAmount& nValueRet) const
{
    setCoinsRet.clear();
    nValueRet = 0;

    // List of values less than target
    pair<CAmount, pair<const CWalletTransactionBase*,unsigned int> > coinLowestLarger;
    coinLowestLarger.first = std::numeric_limits<CAmount>::max();
    coinLowestLarger.second.first = NULL;
    vector<pair<CAmount, pair<const CWalletTransactionBase*,unsigned int> > > vValue;
    CAmount nTotalLower = 0;

    std::shuffle(vCoins.begin(), vCoins.end(), ZcashRandomEngine());

    BOOST_FOREACH(const COutput &output, vCoins)
    {
        if (!output.fSpendable)
            continue;

        const CWalletTransactionBase *pcoin = output.tx;

        if (output.nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? nConfMine : nConfTheirs))
            continue;

        CAmount n = pcoin->getTxBase()->GetVout()[output.pos].nValue;

        pair<CAmount,pair<const CWalletTransactionBase*,unsigned int> > coin = make_pair(n,make_pair(pcoin, output.pos));

        if (n == nTargetValue)
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            return true;
        }
        else if (n < nTargetValue + CENT)
        {
            vValue.push_back(coin);
            nTotalLower += n;
        }
        else if (n < coinLowestLarger.first)
        {
            coinLowestLarger = coin;
        }
    }

    if (nTotalLower == nTargetValue)
    {
        for (unsigned int i = 0; i < vValue.size(); ++i)
        {
            setCoinsRet.insert(vValue[i].second);
            nValueRet += vValue[i].first;
        }
        return true;
    }

    if (nTotalLower < nTargetValue)
    {
        if (coinLowestLarger.second.first == NULL)
            return false;
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
        return true;
    }

    // Solve subset sum by stochastic approximation
    sort(vValue.rbegin(), vValue.rend(), CompareValueOnly());
    vector<char> vfBest;
    CAmount nBest;

    ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest, 1000);
    if (nBest != nTargetValue && nTotalLower >= nTargetValue + CENT)
        ApproximateBestSubset(vValue, nTotalLower, nTargetValue + CENT, vfBest, nBest, 1000);

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    if (coinLowestLarger.second.first &&
        ((nBest != nTargetValue && nBest < nTargetValue + CENT) || coinLowestLarger.first <= nBest))
    {
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
    }
    else {
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
            {
                setCoinsRet.insert(vValue[i].second);
                nValueRet += vValue[i].first;
            }

        LogPrint("selectcoins", "SelectCoins() best subset: ");
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
                LogPrint("selectcoins", "%s ", FormatMoney(vValue[i].first));
        LogPrint("selectcoins", "total %s\n", FormatMoney(nBest));
    }

    return true;
}

bool CWallet::SelectCoins(const CAmount& nTargetValue, set<pair<const CWalletTransactionBase*,unsigned int> >& setCoinsRet, CAmount& nValueRet,  bool& fOnlyCoinbaseCoinsRet, bool& fNeedCoinbaseCoinsRet, const CCoinControl* coinControl) const
{
    // If coinbase utxos can only be sent to zaddrs, exclude any coinbase utxos from coin selection.
    bool fProtectCoinbase = Params().GetConsensus().fCoinbaseMustBeProtected;
    bool fProtectCFCoinbase = false;

    // CF exemption allowed only after hfCommunityFundHeight hardfork
    if (!ForkManager::getInstance().canSendCommunityFundsToTransparentAddress(chainActive.Height()))
        fProtectCFCoinbase = fProtectCoinbase;

    // Output parameter fOnlyCoinbaseCoinsRet is set to true when the only available coins are coinbase utxos.
    vector<COutput> vCoinsNoProtectedCoinbase, vCoinsWithProtectedCoinbase;
    AvailableCoins(vCoinsNoProtectedCoinbase, true, coinControl, false, false, !fProtectCFCoinbase);
    AvailableCoins(vCoinsWithProtectedCoinbase, true, coinControl, false, true, true);
    fOnlyCoinbaseCoinsRet = vCoinsNoProtectedCoinbase.size() == 0 && vCoinsWithProtectedCoinbase.size() > 0;

    vector<COutput> vCoins = (fProtectCoinbase) ? vCoinsNoProtectedCoinbase : vCoinsWithProtectedCoinbase;

    // Output parameter fNeedCoinbaseCoinsRet is set to true if coinbase utxos need to be spent to meet target amount
    if (fProtectCoinbase && vCoinsWithProtectedCoinbase.size() > vCoinsNoProtectedCoinbase.size()) {
        CAmount value = 0;
        for (const COutput& out : vCoinsNoProtectedCoinbase) {
            if (!out.fSpendable) {
                continue;
            }
            value += out.tx->getTxBase()->GetVout()[out.pos].nValue;
        }
        if (value <= nTargetValue) {
            CAmount valueWithCoinbase = 0;
            for (const COutput& out : vCoinsWithProtectedCoinbase) {
                if (!out.fSpendable) {
                    continue;
                }
                valueWithCoinbase += out.tx->getTxBase()->GetVout()[out.pos].nValue;
            }
            fNeedCoinbaseCoinsRet = (valueWithCoinbase >= nTargetValue);
        }
    }

    // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
    if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs)
    {
        BOOST_FOREACH(const COutput& out, vCoins)
        {
            if (!out.fSpendable)
                 continue;
            nValueRet += out.tx->getTxBase()->GetVout()[out.pos].nValue;
            setCoinsRet.insert(make_pair(out.tx, out.pos));
        }
        return (nValueRet >= nTargetValue);
    }

    // calculate value from preset inputs and store them
    set<pair<const CWalletTransactionBase*, uint32_t> > setPresetCoins;
    CAmount nValueFromPresetInputs = 0;

    std::vector<COutPoint> vPresetInputs;
    if (coinControl)
        coinControl->ListSelected(vPresetInputs);
    BOOST_FOREACH(const COutPoint& outpoint, vPresetInputs)
    {
        MAP_WALLET_CONST_IT it = mapWallet.find(outpoint.hash);
        if (it != mapWallet.end())
        {
            const CWalletTransactionBase* pcoin = it->second.get();
            // Clearly invalid input, fail
            if (pcoin->getTxBase()->GetVout().size() <= outpoint.n)
                return false;
            nValueFromPresetInputs += pcoin->getTxBase()->GetVout()[outpoint.n].nValue;
            setPresetCoins.insert(make_pair(pcoin, outpoint.n));
        } else
            return false; // TODO: Allow non-wallet inputs
    }

    // remove preset inputs from vCoins
    if((coinControl != nullptr) && coinControl->HasSelected())
    {
        auto isPresent = [&](const COutput& out) {return setPresetCoins.count(make_pair(out.tx, out.pos));};
        std::vector<COutput>::const_iterator it = std::remove_if(vCoins.begin(), vCoins.end(), isPresent);
        vCoins.erase(it, vCoins.end());
    }

    bool res = nTargetValue <= nValueFromPresetInputs ||
        SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, 1, 6, vCoins, setCoinsRet, nValueRet) ||
        SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, 1, 1, vCoins, setCoinsRet, nValueRet) ||
        (bSpendZeroConfChange && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, 0, 1, vCoins, setCoinsRet, nValueRet));

    // because SelectCoinsMinConf clears the setCoinsRet, we now add the possible inputs to the coinset
    setCoinsRet.insert(setPresetCoins.begin(), setPresetCoins.end());

    // add preset inputs to the total value selected
    nValueRet += nValueFromPresetInputs;

    return res;
}

bool CWallet::FundTransaction(CMutableTransaction& tx, CAmount &nFeeRet, int& nChangePosRet, std::string& strFailReason)
{
    vector<CRecipient> vecSend;

    // Turn the txout set into a CRecipient vector
    for(const CTxOut& txOut: tx.getVout())
    {
        CRecipient recipient = {txOut.scriptPubKey, txOut.nValue, false};
        vecSend.push_back(recipient);
    }

    // Turn the ccout set into a CcRecipientVariant vector
    vector<CRecipientScCreation> vecScSend;
    vector<CRecipientForwardTransfer> vecFtSend;
    vector<CRecipientBwtRequest> vecBwtRequest;
    Sidechain::fundCcRecipients(tx, vecScSend, vecFtSend, vecBwtRequest);
    
    CCoinControl coinControl;
    coinControl.fAllowOtherInputs = true;
    for (const CTxIn& txin: tx.vin)
        coinControl.Select(txin.prevout);

    // if we have ceased sc withdrawl inputs, they must be taken into account when doing computations for
    // funding input amount. Such contribution is handled in the CreateTransaction() function

    CReserveKey reservekey(this);
    CWalletTx wtx;
    if (!CreateTransaction(vecSend, vecScSend, vecFtSend, vecBwtRequest, wtx, reservekey, nFeeRet, nChangePosRet, strFailReason, &coinControl, false, tx.vcsw_ccin))
        return false;

    if (nChangePosRet != -1)
        tx.insertAtPos(nChangePosRet, wtx.getWrappedTx().GetVout()[nChangePosRet]);

    // Add new txins (keeping original txin scriptSig/order)
    for(const CTxIn& txin: wtx.getWrappedTx().GetVin())
    {
        bool found = false;
        BOOST_FOREACH(const CTxIn& origTxIn, tx.vin)
        {
            if (txin.prevout.hash == origTxIn.prevout.hash && txin.prevout.n == origTxIn.prevout.n)
            {
                found = true;
                break;
            }
        }
        if (!found)
            tx.vin.push_back(txin);
    }

    // csw inputs vector (if any) remains the same

    return true;
}


template <typename T>
static bool checkAndAddCcOut(const std::vector<T>&  vccout, CAmount& nValue, std::string& strFailReason)
{
    for (const auto& entry : vccout)
    {
        CAmount amount = entry.GetScValue();
        if (nValue < 0 || amount < 0)
        {
            strFailReason = _("Transaction cc out amounts must be positive");
            return false;
        }
        nValue += amount;
    }
    return true;
}

bool CWallet::CreateTransaction(
    const std::vector<CRecipient>& vecSend,
    const std::vector<CRecipientScCreation>& vecScSend,
    const std::vector<CRecipientForwardTransfer>& vecFtSend,
    const std::vector<CRecipientBwtRequest>& vecBwtRequest,
    CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRet,
    int& nChangePosRet, std::string& strFailReason, const CCoinControl* coinControl, bool sign,
    const std::vector<CTxCeasedSidechainWithdrawalInput>& vcsw_input)
{
    CAmount totalOutputValue = 0;
    unsigned int nSubtractFeeFromAmount = 0;
    BOOST_FOREACH (const CRecipient& recipient, vecSend)
    {
        if (totalOutputValue < 0 || recipient.nAmount < 0)
        {
            strFailReason = _("Transaction out amounts must be positive");
            return false;
        }
        totalOutputValue += recipient.nAmount;

        if (recipient.fSubtractFeeFromAmount)
            nSubtractFeeFromAmount++;
    }

    if (!checkAndAddCcOut(vecScSend, totalOutputValue, strFailReason))
        return false;
    if (!checkAndAddCcOut(vecFtSend, totalOutputValue, strFailReason))
        return false;
    if (!checkAndAddCcOut(vecBwtRequest, totalOutputValue, strFailReason))
        return false;

    if ( (vecSend.empty() && vecScSend.empty() && vecFtSend.empty() && vecBwtRequest.empty() ) || totalOutputValue < 0)
    {
        strFailReason = _("Transaction amounts must be positive");
        return false;
    }

    CAmount cswInTotAmount = 0;
    for(const CTxCeasedSidechainWithdrawalInput& cswIn: vcsw_input)
    {
        cswInTotAmount += cswIn.nValue;
    }

    // remove ceased sidechain withdrawal input value if any, since must not be fund by wallet coins
    totalOutputValue -= cswInTotAmount;

    wtxNew.fTimeReceivedIsTxTime = true;
    wtxNew.BindWallet(this);
    CMutableTransaction txNew;

    if (!vecScSend.empty() || !vecFtSend.empty() || !vecBwtRequest.empty() || !vcsw_input.empty())
    {
        // set proper version
        txNew.nVersion = SC_TX_VERSION;
    }

    // Discourage fee sniping.
    //
    // However because of a off-by-one-error in previous versions we need to
    // neuter it by setting nLockTime to at least one less than nBestHeight.
    // Secondly currently propagation of transactions created for block heights
    // corresponding to blocks that were just mined may be iffy - transactions
    // aren't re-accepted into the mempool - we additionally neuter the code by
    // going ten blocks back. Doesn't yet do anything for sniping, but does act
    // to shake out wallet bugs like not showing nLockTime'd transactions at
    // all.
    txNew.nLockTime = std::max(0, chainActive.Height() - 10);

    // Secondly occasionally randomly pick a nLockTime even further back, so
    // that transactions that are delayed after signing for whatever reason,
    // e.g. high-latency mix networks and some CoinJoin implementations, have
    // better privacy.
    if (GetRandInt(10) == 0)
        txNew.nLockTime = std::max(0, (int)txNew.nLockTime - GetRandInt(100));

    assert(txNew.nLockTime <= (unsigned int)chainActive.Height());
    assert(txNew.nLockTime < LOCKTIME_THRESHOLD);

    {
        LOCK2(cs_main, cs_wallet);
        {
            nFeeRet = 0;
            while (true)
            {
                txNew.vin.clear();
                txNew.vcsw_ccin.clear();

                txNew.resizeOut(0);
                txNew.vsc_ccout.clear();
                txNew.vft_ccout.clear();
                txNew.vmbtr_out.clear();

                wtxNew.fFromMe = true;
                nChangePosRet = -1;
                bool fFirst = true;

                CAmount nTotalValue = totalOutputValue;
                if (nSubtractFeeFromAmount == 0)
                    nTotalValue += nFeeRet;
                double dPriority = 0;
                // vouts to the payees
                BOOST_FOREACH (const CRecipient& recipient, vecSend)
                {
                    CTxOut txout(recipient.nAmount, recipient.scriptPubKey);

                    if (recipient.fSubtractFeeFromAmount)
                    {
                        txout.nValue -= nFeeRet / nSubtractFeeFromAmount; // Subtract fee equally from each selected recipient

                        if (fFirst) // first receiver pays the remainder not divisible by output count
                        {
                            fFirst = false;
                            txout.nValue -= nFeeRet % nSubtractFeeFromAmount;
                        }
                    }

                    if (txout.IsDust(::minRelayTxFee))
                    {
                        if (recipient.fSubtractFeeFromAmount && nFeeRet > 0)
                        {
                            if (txout.nValue < 0)
                                strFailReason = _("The transaction amount is too small to pay the fee");
                            else
                                strFailReason = _("The transaction amount is too small to send after the fee has been deducted");
                        }
                        else
                            strFailReason = _("Transaction amount too small");
                        return false;
                    }
                    txNew.addOut(txout);
                }

                // vccouts to the payees
                for (const auto& entry : vecScSend)
                {
                    CTxScCreationOut txccout(entry.nValue, entry.address, entry.ftScFee, entry.mbtrScFee, entry.fixedParams);
                    txNew.add(txccout);
                }

                for (const auto& entry : vecFtSend)
                {
                    CTxForwardTransferOut txccout(entry.scId, entry.nValue, entry.address, entry.mcReturnAddress);
                    txNew.add(txccout);
                }

                for (const auto& entry : vecBwtRequest)
                {
                    CBwtRequestOut txccout(entry.scId, entry.mcDestinationAddress, entry.bwtRequestData);
                    txNew.add(txccout);
                }


                // Choose coins to use
                set<pair<const CWalletTransactionBase*,unsigned int> > setCoins;
                CAmount nValueIn = 0;
                bool fOnlyCoinbaseCoins = false;
                bool fNeedCoinbaseCoins = false;
                if (nTotalValue > 0)
                {
                    if ( !SelectCoins(nTotalValue, setCoins, nValueIn, fOnlyCoinbaseCoins, fNeedCoinbaseCoins, coinControl))
                    {
                        if (fOnlyCoinbaseCoins && Params().GetConsensus().fCoinbaseMustBeProtected) {
                            strFailReason = _("Coinbase funds can only be sent to a zaddr");
                        } else if (fNeedCoinbaseCoins && Params().GetConsensus().fCoinbaseMustBeProtected) {
                            strFailReason = _("Insufficient funds, coinbase funds can only be spent after they have been sent to a zaddr");
                        } else {
                            strFailReason = _("Insufficient funds");
                        }
                        return false;
                    }
                    for (auto& pcoin : setCoins)
                    {
                        CAmount nCredit = pcoin.first->getTxBase()->GetVout()[pcoin.second].nValue;
                        //The coin age after the next block (depth+1) is used instead of the current,
                        //reflecting an assumption the user would accept a bit more delay for
                        //a chance at a free transaction.
                        //But mempool inputs might still be in the mempool, so their age stays 0
                        int age = pcoin.first->GetDepthInMainChain();
                        if (age != 0)
                            age += 1;
                        dPriority += (double)nCredit * age;
                    }
                }

                CAmount nChange = nValueIn - totalOutputValue;
                if (nSubtractFeeFromAmount == 0)
                    nChange -= nFeeRet;

                if (nChange > 0)
                {
                    // Fill a vout to ourself
                    // TODO: pass in scriptChange instead of reservekey so
                    // change transaction isn't always pay-to-bitcoin-address
                    CScript scriptChange;

                    // coin control: send change to custom address
                    if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange))
                        scriptChange = GetScriptForDestination(coinControl->destChange);

                    // no coin control: send change to newly generated address
                    else
                    {
                        // Note: We use a new key here to keep it from being obvious which side is the change.
                        //  The drawback is that by not reusing a previous key, the change may be lost if a
                        //  backup is restored, if the backup doesn't have the new private key for the change.
                        //  If we reused the old key, it would be possible to add code to look for and
                        //  rediscover unknown transactions that were written with keys of ours to recover
                        //  post-backup change.

                        // Reserve a new key pair from key pool
                        CPubKey vchPubKey;
                        bool ret;
                        ret = reservekey.GetReservedKey(vchPubKey);
                        assert(ret); // should never fail, as we just unlocked

                        scriptChange = GetScriptForDestination(vchPubKey.GetID());
                    }

                    CTxOut newTxOut(nChange, scriptChange);

                    // We do not move dust-change to fees, because the sender would end up paying more than requested.
                    // This would be against the purpose of the all-inclusive feature.
                    // So instead we raise the change and deduct from the recipient.
                    if (nSubtractFeeFromAmount > 0 && newTxOut.IsDust(::minRelayTxFee))
                    {
                        CAmount nDust = newTxOut.GetDustThreshold(::minRelayTxFee) - newTxOut.nValue;
                        newTxOut.nValue += nDust; // raise change until no more dust
                        for (unsigned int i = 0; i < vecSend.size(); i++) // subtract from first recipient
                        {
                            if (vecSend[i].fSubtractFeeFromAmount)
                            {
                                txNew.getOut(i).nValue -= nDust;
                                if (txNew.getVout()[i].IsDust(::minRelayTxFee))
                                {
                                    strFailReason = _("The transaction amount is too small to send after the fee has been deducted");
                                    return false;
                                }
                                break;
                            }
                        }
                    }

                    // Never create dust outputs; if we would, just
                    // add the dust to the fee.
                    if (newTxOut.IsDust(::minRelayTxFee))
                    {
                        nFeeRet += nChange;
                        reservekey.ReturnKey();
                    }
                    else
                    {
                        // Insert change txn at random position:
                        nChangePosRet = GetRandInt(txNew.getVout().size()+1);
                        txNew.insertAtPos(nChangePosRet, newTxOut);
                    }
                }
                else
                    reservekey.ReturnKey();

                // Fill vin
                //
                // Note how the sequence number is set to max()-1 so that the
                // nLockTime set above actually works.
                for (const auto& coin : setCoins)
                    txNew.vin.push_back(CTxIn(coin.first->getTxBase()->GetHash(),coin.second,CScript(),
                                              std::numeric_limits<unsigned int>::max()-1));

                // add csw input 
                txNew.vcsw_ccin = vcsw_input;

                // Check mempooltxinputlimit to avoid creating a transaction which the local mempool rejects
                size_t limit = (size_t)GetArg("-mempooltxinputlimit", 0);
                if (limit > 0) {
                    size_t n = txNew.vin.size() + txNew.vcsw_ccin.size();
                    if (n > limit) {
                        strFailReason = _(strprintf("Too many transparent inputs %zu > limit %zu", n, limit).c_str());
                        return false;
                    }
                }

                // Sign
                int nIn = 0;
                CTransaction txNewConst(txNew);
                for (const auto& coin : setCoins)
                {
                    bool signSuccess;
                    const CScript& scriptPubKey = coin.first->getTxBase()->GetVout()[coin.second].scriptPubKey;
                    CScript& scriptSigRes = txNew.vin[nIn].scriptSig;
                    if (sign)
                        signSuccess = ProduceSignature(TransactionSignatureCreator(*this, txNewConst, nIn, SIGHASH_ALL), scriptPubKey, scriptSigRes);
                    else
                        signSuccess = ProduceSignature(DummySignatureCreator(*this), scriptPubKey, scriptSigRes);

                    if (!signSuccess)
                    {
                        strFailReason = _("Signing transaction failed");
                        return false;
                    }
                    nIn++;
                }

                for (auto& cswIn: txNewConst.GetVcswCcIn())
                {
                    bool signSuccess;
                    const CScript& scriptPubKey = cswIn.scriptPubKey();
 
                    CScript& scriptSigRes = txNew.vcsw_ccin.at(nIn - setCoins.size()).redeemScript;

                    if (sign)
                        signSuccess = ProduceSignature(TransactionSignatureCreator(*this, txNewConst, nIn, SIGHASH_ALL), scriptPubKey, scriptSigRes);
                    else
                        signSuccess = ProduceSignature(DummySignatureCreator(*this), scriptPubKey, scriptSigRes);

                    if (!signSuccess)
                    {
                        strFailReason = _("Signing transaction failed");
                        return false;
                    }
                    nIn++;
                }

                unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);

                // Remove scriptSigs if we used dummy signatures for fee calculation
                if (!sign) {
                    for (CTxIn& vin: txNew.vin)
                        vin.scriptSig = CScript();
                    for (auto& cswIn: txNew.vcsw_ccin)
                        cswIn.redeemScript = CScript();
                }

                // Embed the constructed transaction data in wtxNew.
                wtxNew.ResetWrappedTx(CTransaction(txNew));

                // Limit size
                if (nBytes >= MAX_TX_SIZE)
                {
                    LogPrintf("%s():%d - ERROR: tx size %d too large (max allowed = %d)\n",
                        __func__, __LINE__, nBytes, MAX_TX_SIZE);
                    strFailReason = _("Transaction too large");
                    return false;
                }

                dPriority = wtxNew.getWrappedTx().ComputePriority(dPriority, nBytes);

                // Can we complete this as a free transaction?
                if (fSendFreeTransactions && nBytes <= MAX_FREE_TRANSACTION_CREATE_SIZE)
                {
                    // Not enough fee: enough priority?
                    double dPriorityNeeded = mempool.estimatePriority(nTxConfirmTarget);
                    // Not enough mempool history to estimate: use hard-coded AllowFree.
                    if (dPriorityNeeded <= 0 && AllowFree(dPriority))
                        break;

                    // Small enough, and priority high enough, to send for free
                    if (dPriorityNeeded > 0 && dPriority >= dPriorityNeeded)
                        break;
                }

                CAmount nFeeNeeded = GetMinimumFee(nBytes, nTxConfirmTarget, mempool);

                // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
                // because we must be at the maximum allowed fee.
                if (nFeeNeeded < ::minRelayTxFee.GetFee(nBytes))
                {
                    strFailReason = _("Transaction too large for fee policy");
                    return false;
                }

                if (nFeeRet >= nFeeNeeded)
                    break; // Done, enough fee included.

                // Include more fee and try again.
                nFeeRet = nFeeNeeded;
                continue;
            }
        }
    }

    return true;
}

/**
 * Call after CreateTransaction unless you want to abort
 */
bool CWallet::CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey)
{
    {
        LOCK2(cs_main, cs_wallet);
        LogPrintf("CommitTransaction:\n%s", wtxNew.getWrappedTx().ToString());
        {
            // This is only to keep the database open to defeat the auto-flush for the
            // duration of this scope.  This is the only place where this optimization
            // maybe makes sense; please don't do it anywhere else.
            CWalletDB* pwalletdb = fFileBacked ? new CWalletDB(strWalletFile,"r+") : NULL;

            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew, false, pwalletdb);

            // Notify that old coins are spent
            for(const CTxIn& txin: wtxNew.getWrappedTx().GetVin())
            {
                auto& coin = *(mapWallet[txin.prevout.hash]);
                coin.BindWallet(this);
                NotifyTransactionChanged(this, coin.getTxBase()->GetHash(), CT_UPDATED);
            }

            if (fFileBacked)
                delete pwalletdb;
        }

        // Track how many getdata requests our transaction gets
        // not used, nobody is filling the value in map
        // mapRequestCount[wtxNew.GetHash()] = 0;

        if (fBroadcastTransactions)
        {
            // Broadcast
            CValidationState stateDummy;
            if (MempoolReturnValue::VALID != AcceptTxBaseToMemoryPool(mempool, stateDummy, *wtxNew.getTxBase(),
                    LimitFreeFlag::OFF,  RejectAbsurdFeeFlag::ON, MempoolProofVerificationFlag::SYNC))
            {
                // This must not fail. The transaction has already been signed and recorded.
                LogPrintf("CommitTransaction(): Error: Transaction not valid\n");
                return false;
            }
            wtxNew.RelayWalletTransaction();
        }
    }
    return true;
}

bool CWallet::AddAccountingEntry(const CAccountingEntry& acentry, CWalletDB & pwalletdb)
{
    if (!pwalletdb.WriteAccountingEntry_Backend(acentry))
        return false;

    laccentries.push_back(acentry);
    CAccountingEntry & entry = laccentries.back();
    wtxOrdered.insert(make_pair(entry.nOrderPos, TxPair((CWalletTransactionBase*)0, &entry)));

    return true;
}

CAmount CWallet::GetMinimumFee(unsigned int nTxBytes, unsigned int nConfirmTarget, const CTxMemPool& pool)
{
    // payTxFee is user-set "I want to pay this much"
    CAmount nFeeNeeded = payTxFee.GetFee(nTxBytes);
    // user selected total at least (default=true)
    if (fPayAtLeastCustomFee && nFeeNeeded > 0 && nFeeNeeded < payTxFee.GetFeePerK())
        nFeeNeeded = payTxFee.GetFeePerK();
    // User didn't set: use -txconfirmtarget to estimate...
    if (nFeeNeeded == 0)
        nFeeNeeded = pool.estimateFee(nConfirmTarget).GetFee(nTxBytes);
    // ... unless we don't have enough mempool data, in which case fall
    // back to a hard-coded fee
    if (nFeeNeeded == 0)
        nFeeNeeded = minTxFee.GetFee(nTxBytes);
    // prevent user from paying a non-sense fee (like 1 satoshi): 0 < fee < minRelayFee
    if (nFeeNeeded < ::minRelayTxFee.GetFee(nTxBytes))
        nFeeNeeded = ::minRelayTxFee.GetFee(nTxBytes);
    // But always obey the maximum
    if (nFeeNeeded > maxTxFee)
        nFeeNeeded = maxTxFee;
    return nFeeNeeded;
}




DBErrors CWallet::LoadWallet(bool& fFirstRunRet)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    fFirstRunRet = false;
    DBErrors nLoadWalletRet = CWalletDB(strWalletFile,"cr+").LoadWallet(this);
    if (nLoadWalletRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nLoadWalletRet != DB_LOAD_OK)
        return nLoadWalletRet;
    fFirstRunRet = !vchDefaultKey.IsValid();

    uiInterface.LoadWallet(this);

    return DB_LOAD_OK;
}


DBErrors CWallet::ZapWalletTx(std::vector<std::shared_ptr<CWalletTransactionBase> >& vWtx)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    DBErrors nZapWalletTxRet = CWalletDB(strWalletFile,"cr+").ZapWalletTx(this, vWtx);
    if (nZapWalletTxRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nZapWalletTxRet != DB_LOAD_OK)
        return nZapWalletTxRet;

    return DB_LOAD_OK;
}


bool CWallet::SetAddressBook(const CTxDestination& address, const string& strName, const string& strPurpose)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet); // mapAddressBook
        std::map<CTxDestination, CAddressBookData>::iterator mi = mapAddressBook.find(address);
        fUpdated = mi != mapAddressBook.end();
        mapAddressBook[address].name = strName;
        if (!strPurpose.empty()) /* update purpose only if requested */
            mapAddressBook[address].purpose = strPurpose;
    }
    NotifyAddressBookChanged(this, address, strName, ::IsMine(*this, address) != ISMINE_NO,
                             strPurpose, (fUpdated ? CT_UPDATED : CT_NEW) );
    if (!fFileBacked)
        return false;
    if (!strPurpose.empty() && !CWalletDB(strWalletFile).WritePurpose(CBitcoinAddress(address).ToString(), strPurpose))
        return false;
    return CWalletDB(strWalletFile).WriteName(CBitcoinAddress(address).ToString(), strName);
}

bool CWallet::DelAddressBook(const CTxDestination& address)
{
    {
        LOCK(cs_wallet); // mapAddressBook

        if(fFileBacked)
        {
            // Delete destdata tuples associated with address
            std::string strAddress = CBitcoinAddress(address).ToString();
            BOOST_FOREACH(const PAIRTYPE(string, string) &item, mapAddressBook[address].destdata)
            {
                CWalletDB(strWalletFile).EraseDestData(strAddress, item.first);
            }
        }
        mapAddressBook.erase(address);
    }

    NotifyAddressBookChanged(this, address, "", ::IsMine(*this, address) != ISMINE_NO, "", CT_DELETED);

    if (!fFileBacked)
        return false;
    CWalletDB(strWalletFile).ErasePurpose(CBitcoinAddress(address).ToString());
    return CWalletDB(strWalletFile).EraseName(CBitcoinAddress(address).ToString());
}

bool CWallet::SetDefaultKey(const CPubKey &vchPubKey)
{
    if (fFileBacked)
    {
        if (!CWalletDB(strWalletFile).WriteDefaultKey(vchPubKey))
            return false;
    }
    vchDefaultKey = vchPubKey;
    return true;
}

/**
 * Mark old keypool keys as used,
 * and generate all new keys 
 */
bool CWallet::NewKeyPool()
{
    {
        LOCK(cs_wallet);
        CWalletDB walletdb(strWalletFile);
        BOOST_FOREACH(int64_t nIndex, setKeyPool)
            walletdb.ErasePool(nIndex);
        setKeyPool.clear();

        if (IsLocked())
            return false;

        int64_t nKeys = max(GetArg("-keypool", 100), (int64_t)0);
        for (int i = 0; i < nKeys; i++)
        {
            int64_t nIndex = i+1;
            walletdb.WritePool(nIndex, CKeyPool(GenerateNewKey()));
            setKeyPool.insert(nIndex);
        }
        LogPrintf("CWallet::NewKeyPool wrote %d new keys\n", nKeys);
    }
    return true;
}

bool CWallet::TopUpKeyPool(unsigned int kpSize)
{
    {
        LOCK(cs_wallet);

        if (IsLocked())
            return false;

        CWalletDB walletdb(strWalletFile);

        // Top up key pool
        unsigned int nTargetSize;
        if (kpSize > 0)
            nTargetSize = kpSize;
        else
            nTargetSize = max(GetArg("-keypool", 100), (int64_t) 0);

        while (setKeyPool.size() < (nTargetSize + 1))
        {
            int64_t nEnd = 1;
            if (!setKeyPool.empty())
                nEnd = *(--setKeyPool.end()) + 1;
            if (!walletdb.WritePool(nEnd, CKeyPool(GenerateNewKey())))
                throw runtime_error("TopUpKeyPool(): writing generated key failed");
            setKeyPool.insert(nEnd);
            LogPrintf("keypool added key %d, size=%u\n", nEnd, setKeyPool.size());
        }
    }
    return true;
}

void CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool)
{
    nIndex = -1;
    keypool.vchPubKey = CPubKey();
    {
        LOCK(cs_wallet);

        if (!IsLocked())
            TopUpKeyPool();

        // Get the oldest key
        if(setKeyPool.empty())
            return;

        CWalletDB walletdb(strWalletFile);

        nIndex = *(setKeyPool.begin());
        setKeyPool.erase(setKeyPool.begin());
        if (!walletdb.ReadPool(nIndex, keypool))
            throw runtime_error("ReserveKeyFromKeyPool(): read failed");
        if (!HaveKey(keypool.vchPubKey.GetID()))
            throw runtime_error("ReserveKeyFromKeyPool(): unknown key in key pool");
        assert(keypool.vchPubKey.IsValid());
        LogPrintf("keypool reserve %d\n", nIndex);
    }
}

void CWallet::KeepKey(int64_t nIndex)
{
    // Remove from key pool
    if (fFileBacked)
    {
        CWalletDB walletdb(strWalletFile);
        walletdb.ErasePool(nIndex);
    }
    LogPrintf("keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        setKeyPool.insert(nIndex);
    }
    LogPrintf("keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result)
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    {
        LOCK(cs_wallet);
        ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex == -1)
        {
            if (IsLocked()) return false;
            result = GenerateNewKey();
            return true;
        }
        KeepKey(nIndex);
        result = keypool.vchPubKey;
    }
    return true;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    ReserveKeyFromKeyPool(nIndex, keypool);
    if (nIndex == -1)
        return GetTime();
    ReturnKey(nIndex);
    return keypool.nTime;
}

std::map<CTxDestination, CAmount> CWallet::GetAddressBalances()
{
    map<CTxDestination, CAmount> balances;

    {
        LOCK(cs_wallet);
        for (auto& walletEntry: mapWallet)
        {
            auto* pcoin = walletEntry.second.get();
            if (!CheckFinalTx(*pcoin->getTxBase()) || !pcoin->IsTrusted() )
                continue;

            if (!pcoin->HasMatureOutputs())
                continue;

            if (pcoin->GetDepthInMainChain() < (pcoin->IsFromMe(ISMINE_ALL) ? 0 : 1))
                continue;

            for (unsigned int pos = 0; pos < pcoin->getTxBase()->GetVout().size(); pos++)
            {
                CTxDestination addr;
                if (!IsMine(pcoin->getTxBase()->GetVout()[pos]))
                    continue;

                if (pcoin->getTxBase()->IsCertificate()) {
                    if (pcoin->IsOutputMature(pos) != CCoins::outputMaturity::MATURE)
                        continue;
                }

                if(!ExtractDestination(pcoin->getTxBase()->GetVout()[pos].scriptPubKey, addr))
                    continue;

                CAmount n = IsSpent(walletEntry.first, pos) ? 0 : pcoin->getTxBase()->GetVout()[pos].nValue;

                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

set< set<CTxDestination> > CWallet::GetAddressGroupings()
{
    AssertLockHeld(cs_wallet); // mapWallet
    set< set<CTxDestination> > groupings;
    set<CTxDestination> grouping;

    for (auto& walletEntry: mapWallet)
    {
        auto* pcoin = walletEntry.second.get();

        if (pcoin->getTxBase()->GetVin().size() > 0)
        {
            bool any_mine = false;
            // group all input addresses with each other
            BOOST_FOREACH(CTxIn txin, pcoin->getTxBase()->GetVin())
            {
                CTxDestination address;
                if(!IsMine(txin)) /* If this input isn't mine, ignore it */
                    continue;
                if(!ExtractDestination(mapWallet[txin.prevout.hash]->getTxBase()->GetVout()[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                any_mine = true;
            }

            // group change with input addresses
            if (any_mine)
            {
               BOOST_FOREACH(CTxOut txout, pcoin->getTxBase()->GetVout())
                   if (IsChange(txout))
                   {
                       CTxDestination txoutAddr;
                       if(!ExtractDestination(txout.scriptPubKey, txoutAddr))
                           continue;
                       grouping.insert(txoutAddr);
                   }
            }
            if (grouping.size() > 0)
            {
                groupings.insert(grouping);
                grouping.clear();
            }
        }

        // group lone addrs by themselves
        for (unsigned int i = 0; i < pcoin->getTxBase()->GetVout().size(); i++)
            if (IsMine(pcoin->getTxBase()->GetVout()[i]))
            {
                CTxDestination address;
                if(!ExtractDestination(pcoin->getTxBase()->GetVout()[i].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    set< set<CTxDestination>* > uniqueGroupings; // a set of pointers to groups of addresses
    map< CTxDestination, set<CTxDestination>* > setmap;  // map addresses to the unique group containing it
    BOOST_FOREACH(set<CTxDestination> grouping, groupings)
    {
        // make a set of all the groups hit by this new group
        set< set<CTxDestination>* > hits;
        map< CTxDestination, set<CTxDestination>* >::iterator it;
        BOOST_FOREACH(CTxDestination address, grouping)
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group and delete old groups
        set<CTxDestination>* merged = new set<CTxDestination>(grouping);
        BOOST_FOREACH(set<CTxDestination>* hit, hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        BOOST_FOREACH(CTxDestination element, *merged)
            setmap[element] = merged;
    }

    set< set<CTxDestination> > ret;
    BOOST_FOREACH(set<CTxDestination>* uniqueGrouping, uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}

std::set<CTxDestination> CWallet::GetAccountAddresses(const std::string& strAccount) const
{
    LOCK(cs_wallet);
    set<CTxDestination> result;
    BOOST_FOREACH(const PAIRTYPE(CTxDestination, CAddressBookData)& item, mapAddressBook)
    {
        const CTxDestination& address = item.first;
        const string& strName = item.second.name;
        if (strName == strAccount)
            result.insert(address);
    }
    return result;
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey)
{
    if (nIndex == -1)
    {
        if (!pwallet)
            return false;

        CKeyPool keypool;
        pwallet->ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex != -1)
            vchPubKey = keypool.vchPubKey;
        else {
            return false;
        }
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    return true;
}

void CReserveKey::KeepKey()
{
    if (nIndex != -1)
        pwallet->KeepKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    if (nIndex != -1)
        pwallet->ReturnKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CWallet::GetAllReserveKeys(set<CKeyID>& setAddress) const
{
    setAddress.clear();

    CWalletDB walletdb(strWalletFile);

    LOCK2(cs_main, cs_wallet);
    BOOST_FOREACH(const int64_t& id, setKeyPool)
    {
        CKeyPool keypool;
        if (!walletdb.ReadPool(id, keypool))
            throw runtime_error("GetAllReserveKeyHashes(): read failed");
        assert(keypool.vchPubKey.IsValid());
        CKeyID keyID = keypool.vchPubKey.GetID();
        if (!HaveKey(keyID))
            throw runtime_error("GetAllReserveKeyHashes(): unknown key in key pool");
        setAddress.insert(keyID);
    }
}

void CWallet::UpdatedTransaction(const uint256 &hashTx)
{
    {
        LOCK(cs_wallet);
        // Only notify UI if this transaction is in this wallet
        auto mi = mapWallet.find(hashTx);
        if (mi != mapWallet.end())
            NotifyTransactionChanged(this, hashTx, CT_UPDATED);
    }
}

void CWallet::LockCoin(COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.insert(output);
}

void CWallet::UnlockCoin(COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.erase(output);
}

void CWallet::UnlockAllCoins()
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.clear();
}

bool CWallet::IsLockedCoin(uint256 hash, unsigned int n) const
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    COutPoint outpt(hash, n);

    return (setLockedCoins.count(outpt) > 0);
}

void CWallet::ListLockedCoins(std::vector<COutPoint>& vOutpts)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    for (std::set<COutPoint>::iterator it = setLockedCoins.begin();
         it != setLockedCoins.end(); it++) {
        COutPoint outpt = (*it);
        vOutpts.push_back(outpt);
    }
}

/** @} */ // end of Actions

class CAffectedKeysVisitor : public boost::static_visitor<void> {
private:
    const CKeyStore &keystore;
    std::vector<CKeyID> &vKeys;

public:
    CAffectedKeysVisitor(const CKeyStore &keystoreIn, std::vector<CKeyID> &vKeysIn) : keystore(keystoreIn), vKeys(vKeysIn) {}

    void Process(const CScript &script) {
        txnouttype type;
        std::vector<CTxDestination> vDest;
        int nRequired;
        if (ExtractDestinations(script, type, vDest, nRequired)) {
            BOOST_FOREACH(const CTxDestination &dest, vDest)
                boost::apply_visitor(*this, dest);
        }
    }

    void operator()(const CKeyID &keyId) {
        if (keystore.HaveKey(keyId))
            vKeys.push_back(keyId);
    }

    void operator()(const CScriptID &scriptId) {
        CScript script;
        if (keystore.GetCScript(scriptId, script))
            Process(script);
    }

    void operator()(const CNoDestination &none) {}
};

void CWallet::GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const {
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    mapKeyBirth.clear();

    // get birth times for keys with metadata
    for (std::map<CKeyID, CKeyMetadata>::const_iterator it = mapKeyMetadata.begin(); it != mapKeyMetadata.end(); it++)
        if (it->second.nCreateTime)
            mapKeyBirth[it->first] = it->second.nCreateTime;

    // map in which we'll infer heights of other keys
    CBlockIndex *pindexMax = chainActive[std::max(0, chainActive.Height() - 144)]; // the tip can be reorganised; use a 144-block safety margin
    std::map<CKeyID, CBlockIndex*> mapKeyFirstBlock;
    std::set<CKeyID> setKeys;
    GetKeys(setKeys);
    BOOST_FOREACH(const CKeyID &keyid, setKeys) {
        if (mapKeyBirth.count(keyid) == 0)
            mapKeyFirstBlock[keyid] = pindexMax;
    }
    setKeys.clear();

    // if there are no such keys, we're done
    if (mapKeyFirstBlock.empty())
        return;

    // find first block that affects those keys, if there are any left
    std::vector<CKeyID> vAffected;
    for (MAP_WALLET_CONST_IT it = mapWallet.begin(); it != mapWallet.end(); it++) {
        // iterate over all wallet transactions...
        const CWalletTransactionBase &wtx = *((*it).second);
        BlockMap::const_iterator blit = mapBlockIndex.find(wtx.hashBlock);
        if (blit != mapBlockIndex.end() && chainActive.Contains(blit->second)) {
            // ... which are already in a block
            int nHeight = blit->second->nHeight;
            BOOST_FOREACH(const CTxOut &txout, wtx.getTxBase()->GetVout()) {
                // iterate over all their outputs
                CAffectedKeysVisitor(*this, vAffected).Process(txout.scriptPubKey);
                BOOST_FOREACH(const CKeyID &keyid, vAffected) {
                    // ... and all their affected keys
                    std::map<CKeyID, CBlockIndex*>::iterator rit = mapKeyFirstBlock.find(keyid);
                    if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight)
                        rit->second = blit->second;
                }
                vAffected.clear();
            }
        }
    }

    // Extract block timestamps for those keys
    for (std::map<CKeyID, CBlockIndex*>::const_iterator it = mapKeyFirstBlock.begin(); it != mapKeyFirstBlock.end(); it++)
        mapKeyBirth[it->first] = it->second->GetBlockTime() - TIMESTAMP_WINDOW; // block times can be 2h off
}

bool CWallet::AddDestData(const CTxDestination &dest, const std::string &key, const std::string &value)
{
    if (boost::get<CNoDestination>(&dest))
        return false;

    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteDestData(CBitcoinAddress(dest).ToString(), key, value);
}

bool CWallet::EraseDestData(const CTxDestination &dest, const std::string &key)
{
    if (!mapAddressBook[dest].destdata.erase(key))
        return false;
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).EraseDestData(CBitcoinAddress(dest).ToString(), key);
}

bool CWallet::LoadDestData(const CTxDestination &dest, const std::string &key, const std::string &value)
{
    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
    return true;
}

bool CWallet::GetDestData(const CTxDestination &dest, const std::string &key, std::string *value) const
{
    std::map<CTxDestination, CAddressBookData>::const_iterator i = mapAddressBook.find(dest);
    if(i != mapAddressBook.end())
    {
        CAddressBookData::StringMap::const_iterator j = i->second.destdata.find(key);
        if(j != i->second.destdata.end())
        {
            if(value)
                *value = j->second;
            return true;
        }
    }
    return false;
}

CKeyPool::CKeyPool()
{
    nTime = GetTime();
}

CKeyPool::CKeyPool(const CPubKey& vchPubKeyIn)
{
    nTime = GetTime();
    vchPubKey = vchPubKeyIn;
}

CWalletKey::CWalletKey(int64_t nExpires)
{
    nTimeCreated = (nExpires ? GetTime() : 0);
    nTimeExpires = nExpires;
}

int CWalletTransactionBase::GetDepthInMainChainINTERNAL(const CBlockIndex* &pindexRet) const
{
    if (hashBlock.IsNull() || nIndex == -1)
        return 0;
    AssertLockHeld(cs_main);

    // Find the block it claims to be in
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !chainActive.Contains(pindex))
        return 0;

    // Make sure the merkle branch connects to this block
    if (!fMerkleVerified)
    {
        if (CBlock::CheckMerkleBranch(getTxBase()->GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot)
            return 0;
        fMerkleVerified = true;
    }

    pindexRet = pindex;
    return chainActive.Height() - pindex->nHeight + 1;
}

int CWalletTransactionBase::GetDepthInMainChain(const CBlockIndex* &pindexRet) const
{
    AssertLockHeld(cs_main);
    int nResult = GetDepthInMainChainINTERNAL(pindexRet);
    if (nResult == 0 && !mempool.exists(getTxBase()->GetHash()))
        return -1; // Not in chain, not in mempool

    return nResult;
}

/**
 * Find notes in the wallet filtered by payment address, min depth and ability to spend.
 * These notes are decrypted and added to the output parameter vector, outEntries.
 */
void CWallet::GetFilteredNotes(std::vector<CNotePlaintextEntry> & outEntries, std::string address, int minDepth, bool ignoreSpent, bool ignoreUnspendable)
{
    bool fFilterAddress = false;
    libzcash::PaymentAddress filterPaymentAddress;
    if (address.length() > 0) {
        filterPaymentAddress = CZCPaymentAddress(address).Get();
        fFilterAddress = true;
    }

    LOCK2(cs_main, cs_wallet);

    for (auto & p : mapWallet) {
        CWalletTransactionBase& wtx = *(p.second);
        // Filter the transactions before checking for notes
        if (!CheckFinalTx(*wtx.getTxBase()) || (wtx.getTxBase()->IsCoinBase() && !wtx.HasMatureOutputs()) || wtx.GetDepthInMainChain() < minDepth) {
            continue;
        }

        if (wtx.mapNoteData.size() == 0) {
            continue;
        }

        for (auto & pair : wtx.mapNoteData) {
            JSOutPoint jsop = pair.first;
            CNoteData nd = pair.second;
            PaymentAddress pa = nd.address;

            // skip notes which belong to a different payment address in the wallet
            if (fFilterAddress && !(pa == filterPaymentAddress)) {
                continue;
            }

            // skip note which has been spent
            if (ignoreSpent && nd.nullifier && IsSpent(*nd.nullifier)) {
                continue;
            }

            // skip notes which cannot be spent
            if (ignoreUnspendable && !HaveSpendingKey(pa)) {
                continue;
            }

            int i = jsop.js; // Index into CTransaction.GetJoinsSplits()
            int j = jsop.n;  // Index into JSDescription.ciphertexts

            // Get cached decryptor
            ZCNoteDecryption decryptor;
            if (!GetNoteDecryptor(pa, decryptor)) {
                // Note decryptors are created when the wallet is loaded, so it should always exist
                throw std::runtime_error(strprintf("Could not find note decryptor for payment address %s", CZCPaymentAddress(pa).ToString()));
            }

            // determine amount of funds in the note
            auto hSig = wtx.getTxBase()->GetVjoinsplit()[i].h_sig(*pzcashParams, wtx.getTxBase()->GetJoinSplitPubKey());
            try {
                NotePlaintext plaintext = NotePlaintext::decrypt(
                        decryptor,
                        wtx.getTxBase()->GetVjoinsplit()[i].ciphertexts[j],
                        wtx.getTxBase()->GetVjoinsplit()[i].ephemeralKey,
                        hSig,
                        (unsigned char) j);

                outEntries.push_back(CNotePlaintextEntry{jsop, plaintext});

            } catch (const note_decryption_failed &err) {
                // Couldn't decrypt with this spending key
                throw std::runtime_error(strprintf("Could not decrypt note for payment address %s", CZCPaymentAddress(pa).ToString()));
            } catch (const std::exception &exc) {
                // Unexpected failure
                throw std::runtime_error(strprintf("Error while decrypting note for payment address %s: %s", CZCPaymentAddress(pa).ToString(), exc.what()));
            }
        }
    }
}

bool CWalletTransactionBase::HasInputFrom(const CScript& scriptPubKey) const 
{
    for(const auto& txin: getTxBase()->GetVin())
    {
        const auto mi = pwallet->getMapWallet().find(txin.prevout.hash);
        if (mi == pwallet->getMapWallet().end()) {
            continue;
        }

        const auto& inputTx = (*mi).second;
        if (txin.prevout.n >= inputTx->getTxBase()->GetVout().size()) {
            continue;
        }

        const CTxOut& utxo = inputTx->getTxBase()->GetVout()[txin.prevout.n];
 
        auto res = std::search(utxo.scriptPubKey.begin(), utxo.scriptPubKey.end(), scriptPubKey.begin(), scriptPubKey.end());
        if (res == utxo.scriptPubKey.begin())
        {
            return true;
        }
    }
    return false;
}

bool CWalletTransactionBase::HasOutputFor(const CScript& scriptPubKey) const 
{
    for(const auto& txout : getTxBase()->GetVout())
    {
        auto res = std::search(txout.scriptPubKey.begin(), txout.scriptPubKey.end(), scriptPubKey.begin(), scriptPubKey.end());
        if (res == txout.scriptPubKey.begin())
        {
            return true;
        }
    }
    return false;
}

void CWalletTransactionBase::SetMerkleBranch(const CBlock& block)
{
    // Update the tx's hashBlock
    hashBlock = block.GetHash();

    nIndex = GetIndexInBlock(block);

    if (nIndex == -1)
    {
        vMerkleBranch.clear();
        LogPrintf("ERROR: %s(): couldn't find tx in block\n", __func__);
        return;
    }

    // Fill in merkle branch
    vMerkleBranch = block.GetMerkleBranch(nIndex);
}

int CWalletCert::GetIndexInBlock(const CBlock& block)
{
    // Locate the index of certificate
    for (nIndex = 0; nIndex < (int)block.vcert.size(); nIndex++)
        if (block.vcert[nIndex] == wrappedCertificate)
            break;

    if (nIndex == (int)block.vcert.size())
    {
        LogPrintf("ERROR: %s(): couldn't find tx in block\n", __func__);
        return -1;
    }

    // certificates are ideally in a global common vector after all transactions
    nIndex += block.vtx.size();
    return nIndex;
}

CWalletCert::CWalletCert(): CWalletTransactionBase(nullptr), wrappedCertificate() {}

CWalletCert::CWalletCert(const CWallet* pwalletIn, const CScCertificate& certIn):
    CWalletTransactionBase(pwalletIn), wrappedCertificate(certIn) {}

CWalletCert::CWalletCert(const CWalletCert& rhs):
    CWalletTransactionBase(rhs), wrappedCertificate(rhs.wrappedCertificate) {}

CWalletCert& CWalletCert::operator=(const CWalletCert& rhs)
{
    CWalletTransactionBase::operator=(rhs);
    *const_cast<CScCertificate*>(&wrappedCertificate) = rhs.wrappedCertificate;
    return *this;
}

void CWalletCert::GetAmounts(std::list<COutputEntry>& listReceived, std::list<COutputEntry>& listSent,
    CAmount& nFee, std::string& strSentAccount, const isminefilter& filter) const
{
    LogPrint("cert", "%s():%d - called for obj[%s]\n", __func__, __LINE__, wrappedCertificate.GetHash().ToString());

    nFee = 0;
    listReceived.clear();
    listSent.clear();
    strSentAccount = strFromAccount;

    // Is this tx sent/signed by me?
    CAmount nDebit = GetDebit(filter);
    bool isFromMyTaddr = nDebit > 0; // debit>0 means we signed/sent this transaction

    // Compute fee if we sent this transaction.
    if (isFromMyTaddr) {
        nFee = wrappedCertificate.GetFeeAmount(nDebit);
    }

    // Sent/received.
    for (unsigned int pos = 0; pos < wrappedCertificate.GetVout().size(); ++pos) {
        const CTxOut& txout = wrappedCertificate.GetVout()[pos];

        isminetype fIsMine = pwallet->IsMine(txout);
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            // Don't report 'change' txouts
            if (pwallet->IsChange(txout))
                continue;
        }
        else if (!(fIsMine & filter))
            continue;

        // we need to get the destination address
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
        {
            LogPrintf("CWalletCert::GetAmounts: Unknown transaction type found, txid %s\n",
                    wrappedCertificate.GetHash().ToString());
            address = CNoDestination();
        }

        CCoins::outputMaturity outputMaturity = this->IsOutputMature(pos);
        if (outputMaturity == CCoins::outputMaturity::NOT_APPLICABLE)
            continue;

        COutputEntry output = {
            address, txout.nValue, outputMaturity, (int)pos, wrappedCertificate.IsBackwardTransfer(pos)};

        // If we are debited by the transaction, add the output as a "sent" entry
        // unless it is a backward transfer output
        if (nDebit > 0 && !output.isBackwardTransfer)
            listSent.push_back(output);

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine & filter)
            listReceived.push_back(output);
    }
}

bool CWalletCert::RelayWalletTransaction() 
{
    LogPrint("cert", "%s():%d - called for obj[%s]\n", __func__, __LINE__, wrappedCertificate.GetHash().ToString());
    assert(pwallet->GetBroadcastTransactions());
    if (GetDepthInMainChain() == 0) {
        LogPrintf("Relaying cert %s\n", wrappedCertificate.GetHash().ToString());
        wrappedCertificate.Relay();
        return true;
    }
    return false;
}

std::shared_ptr<CWalletTransactionBase> CWalletCert::MakeWalletMapObject() const
{
    return std::shared_ptr<CWalletTransactionBase>( new CWalletCert(*this));
}

std::shared_ptr<CWalletTransactionBase> CWalletTransactionBase::MakeWalletObjectBase(const CTransactionBase& obj, const CWallet* pwallet)
{
    if (obj.IsCertificate() )
    {
        return std::shared_ptr<CWalletTransactionBase>( new CWalletCert(pwallet, dynamic_cast<const CScCertificate&>(obj)) );
    }
    else
    {
        return std::shared_ptr<CWalletTransactionBase>( new CWalletTx(pwallet, dynamic_cast<const CTransaction&>(obj)) );
    }
}
