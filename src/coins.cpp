// Copyright (c) 2012-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "memusage.h"
#include "random.h"
#include "version.h"
#include "policy/fees.h"

#include <assert.h>
#include "utilmoneystr.h"
#include <undo.h>
#include <chainparams.h>

std::string CCoins::ToString() const
{
    std::string ret;
    ret += strprintf("originScId(%s)", originScId.ToString());
    ret += strprintf("version(%d)", nVersion);
    ret += strprintf("fCoinBase(%d)", fCoinBase);
    ret += strprintf("height(%d)", nHeight);
    for (auto o : vout)
    {
        ret += "    " + o.ToString() + "\n";
    }
    return ret;
}

CCoins::CCoins() : fCoinBase(false), vout(0), nHeight(0), nVersion(0), originScId() { }

CCoins::CCoins(const CTransactionBase &tx, int nHeightIn) {
        FromTx(tx, nHeightIn);
    }

void CCoins::FromTx(const CTransactionBase &tx, int nHeightIn) {
    fCoinBase  = tx.IsCoinBase();
    vout       = tx.GetVout();
    nHeight    = nHeightIn;
    nVersion   = tx.nVersion;
    originScId = tx.GetScId();
    ClearUnspendable();
}

void CCoins::Clear() {
    fCoinBase = false;
    std::vector<CTxOut>().swap(vout);
    nHeight = 0;
    nVersion = 0;
    originScId.SetNull();
}

void CCoins::Cleanup() {
    while (vout.size() > 0 && vout.back().IsNull())
        vout.pop_back();

    if (vout.empty())
        std::vector<CTxOut>().swap(vout);
}

void CCoins::ClearUnspendable() {
    BOOST_FOREACH(CTxOut &txout, vout) {
        if (txout.scriptPubKey.IsUnspendable())
            txout.SetNull();
    }
    Cleanup();
}

void CCoins::swap(CCoins &to) {
    std::swap(to.fCoinBase, fCoinBase);
    to.vout.swap(vout);
    std::swap(to.nHeight, nHeight);
    std::swap(to.nVersion, nVersion);
    std::swap(to.originScId, originScId);
}

bool operator==(const CCoins &a, const CCoins &b) {
     // Empty CCoins objects are always equal.
     if (a.IsPruned() && b.IsPruned())
         return true;
     return a.fCoinBase  == b.fCoinBase &&
            a.nHeight    == b.nHeight   &&
            a.nVersion   == b.nVersion  &&
            a.vout       == b.vout      &&
            a.originScId == b.originScId;
}

bool operator!=(const CCoins &a, const CCoins &b) {
    return !(a == b);
}

bool CCoins::IsCoinBase() const {
    return fCoinBase;
}

bool CCoins::IsFromCert() const {
    // when restored from serialization, nVersion, if negative, is populated only with latest 7 bits of the original value!
    // we enforced that no tx/cert can have a version other than a list of well known ones
    // therefore no other 4-bytes signed version will have this 7-bits ending
    return (nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f);
}

bool CCoins::Spend(uint32_t nPos)
{
    if (nPos >= vout.size() || vout[nPos].IsNull())
        return false;
    LogPrint("sc", "%s():%d - @@@@@@@ Spending out[%d], ver=%d, (%s)\n\n", __func__, __LINE__, nPos, nVersion, vout[nPos].ToString());
    vout[nPos].SetNull();
    Cleanup();
    return true;
}

bool CCoins::IsAvailable(unsigned int nPos) const {
    return (nPos < vout.size() && !vout[nPos].IsNull());
}

bool CCoins::IsPruned() const {
    BOOST_FOREACH(const CTxOut &out, vout)
        if (!out.IsNull())
            return false;
    return true;
}

size_t CCoins::DynamicMemoryUsage() const {
    size_t ret = memusage::DynamicUsage(vout);
    BOOST_FOREACH(const CTxOut &out, vout) {
        ret += RecursiveDynamicUsage(out.scriptPubKey);
    }
    return ret;
}

void CCoins::CalcMaskSize(unsigned int &nBytes, unsigned int &nNonzeroBytes) const {
    unsigned int nLastUsedByte = 0;
    for (unsigned int b = 0; 2+b*8 < vout.size(); b++) {
        bool fZero = true;
        for (unsigned int i = 0; i < 8 && 2+b*8+i < vout.size(); i++) {
            if (!vout[2+b*8+i].IsNull()) {
                fZero = false;
                continue;
            }
        }
        if (!fZero) {
            nLastUsedByte = b + 1;
            nNonzeroBytes++;
        }
    }
    nBytes += nLastUsedByte;
}

bool CCoinsView::GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const { return false; }
bool CCoinsView::GetNullifier(const uint256 &nullifier)                        const { return false; }
bool CCoinsView::GetCoins(const uint256 &txid, CCoins &coins)                  const { return false; }
bool CCoinsView::HaveCoins(const uint256 &txid)                                const { return false; }
bool CCoinsView::HaveSidechain(const uint256& scId)                            const { return false; }
bool CCoinsView::GetSidechain(const uint256& scId, CSidechain& info)           const { return false; }
bool CCoinsView::HaveCeasingScs(int height)                                    const { return false; }
bool CCoinsView::GetCeasingScs(int height, CCeasingSidechains& ceasingScs)     const { return false; }
void CCoinsView::queryScIds(std::set<uint256>& scIdsList)                      const { scIdsList.clear(); return; }
bool CCoinsView::HaveCertForEpoch(const uint256& scId, int epochNumber)        const { return false; }
uint256 CCoinsView::GetBestBlock()                                             const { return uint256(); }
uint256 CCoinsView::GetBestAnchor()                                            const { return uint256(); };
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                            const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                            CNullifiersMap &mapNullifiers, CSidechainsMap& mapSidechains,
                            CCeasingScsMap& mapCeasedScs) { return false; }
bool CCoinsView::GetStats(CCoinsStats &stats)                                  const { return false; }


CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) { }

bool CCoinsViewBacked::GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const { return base->GetAnchorAt(rt, tree); }
bool CCoinsViewBacked::GetNullifier(const uint256 &nullifier)                        const { return base->GetNullifier(nullifier); }
bool CCoinsViewBacked::GetCoins(const uint256 &txid, CCoins &coins)                  const { return base->GetCoins(txid, coins); }
bool CCoinsViewBacked::HaveCoins(const uint256 &txid)                                const { return base->HaveCoins(txid); }
bool CCoinsViewBacked::HaveSidechain(const uint256& scId)                            const { return base->HaveSidechain(scId); }
bool CCoinsViewBacked::GetSidechain(const uint256& scId, CSidechain& info)           const { return base->GetSidechain(scId,info); }
bool CCoinsViewBacked::HaveCeasingScs(int height)                                    const { return base->HaveCeasingScs(height); }
bool CCoinsViewBacked::GetCeasingScs(int height, CCeasingSidechains& ceasingScs)     const { return base->GetCeasingScs(height, ceasingScs); }
void CCoinsViewBacked::queryScIds(std::set<uint256>& scIdsList)                      const { return base->queryScIds(scIdsList); }
bool CCoinsViewBacked::HaveCertForEpoch(const uint256& scId, int epochNumber)        const { return base->HaveCertForEpoch(scId, epochNumber); }
uint256 CCoinsViewBacked::GetBestBlock()                                             const { return base->GetBestBlock(); }
uint256 CCoinsViewBacked::GetBestAnchor()                                            const { return base->GetBestAnchor(); }
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                                  const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                                  CNullifiersMap &mapNullifiers, CSidechainsMap& mapSidechains,
                                  CCeasingScsMap& mapCeasedScs) { return base->BatchWrite(mapCoins, hashBlock, hashAnchor,
                                                                                          mapAnchors, mapNullifiers, mapSidechains, mapCeasedScs); }
bool CCoinsViewBacked::GetStats(CCoinsStats &stats)                                  const { return base->GetStats(stats); }

CCoinsKeyHasher::CCoinsKeyHasher() : salt(GetRandHash()) {}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn) : CCoinsViewBacked(baseIn), hasModifier(false), cachedCoinsUsage(0) { }

CCoinsViewCache::~CCoinsViewCache()
{
    assert(!hasModifier);
}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCoins) +
           memusage::DynamicUsage(cacheAnchors) +
           memusage::DynamicUsage(cacheNullifiers) +
           cachedCoinsUsage;
}

CCoinsMap::const_iterator CCoinsViewCache::FetchCoins(const uint256 &txid) const {
    CCoinsMap::iterator it = cacheCoins.find(txid);
    if (it != cacheCoins.end())
        return it;
    CCoins tmp;
    if (!base->GetCoins(txid, tmp))
        return cacheCoins.end();
    CCoinsMap::iterator ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry())).first;
    tmp.swap(ret->second.coins);
    if (ret->second.coins.IsPruned()) {
        // The parent only has an empty entry for this txid; we can consider our
        // version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += ret->second.coins.DynamicMemoryUsage();
    return ret;
}

CSidechainsMap::const_iterator CCoinsViewCache::FetchSidechains(const uint256& scId) const {
    CSidechainsMap::iterator candidateIt = cacheSidechains.find(scId);
    if (candidateIt != cacheSidechains.end())
        return candidateIt;

    CSidechain tmp;
    if (!base->GetSidechain(scId, tmp))
        return cacheSidechains.end();

    //Fill cache and return iterator. The insert in cache below looks cumbersome. However
    //it allows to insert CSidechain and keep iterator to inserted member without extra searches
    CSidechainsMap::iterator ret =
            cacheSidechains.insert(std::make_pair(scId, CSidechainsCacheEntry(tmp, CSidechainsCacheEntry::Flags::DEFAULT ))).first;

    return ret;
}

CCeasingScsMap::const_iterator CCoinsViewCache::FetchCeasingScs(int height) const{
    CCeasingScsMap::iterator candidateIt = cacheCeasingScs.find(height);
    if (candidateIt != cacheCeasingScs.end())
        return candidateIt;

    CCeasingSidechains tmp;
    if (!base->GetCeasingScs(height, tmp))
        return cacheCeasingScs.end();

    //Fill cache and return iterator. The insert in cache below looks cumbersome. However
    //it allows to insert CCeasingSidechains and keep iterator to inserted member without extra searches
    CCeasingScsMap::iterator ret =
            cacheCeasingScs.insert(std::make_pair(height, CCeasingScsCacheEntry(tmp, CCeasingScsCacheEntry::Flags::DEFAULT ))).first;

    return ret;
}

bool CCoinsViewCache::GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const {
    CAnchorsMap::const_iterator it = cacheAnchors.find(rt);
    if (it != cacheAnchors.end()) {
        if (it->second.entered) {
            tree = it->second.tree;
            return true;
        } else {
            return false;
        }
    }

    if (!base->GetAnchorAt(rt, tree)) {
        return false;
    }

    CAnchorsMap::iterator ret = cacheAnchors.insert(std::make_pair(rt, CAnchorsCacheEntry())).first;
    ret->second.entered = true;
    ret->second.tree = tree;
    cachedCoinsUsage += ret->second.tree.DynamicMemoryUsage();

    return true;
}

bool CCoinsViewCache::GetNullifier(const uint256 &nullifier) const {
    CNullifiersMap::iterator it = cacheNullifiers.find(nullifier);
    if (it != cacheNullifiers.end())
        return it->second.entered;

    CNullifiersCacheEntry entry;
    bool tmp = base->GetNullifier(nullifier);
    entry.entered = tmp;

    cacheNullifiers.insert(std::make_pair(nullifier, entry));

    return tmp;
}

void CCoinsViewCache::PushAnchor(const ZCIncrementalMerkleTree &tree) {
    uint256 newrt = tree.root();

    auto currentRoot = GetBestAnchor();

    // We don't want to overwrite an anchor we already have.
    // This occurs when a block doesn't modify mapAnchors at all,
    // because there are no joinsplits. We could get around this a
    // different way (make all blocks modify mapAnchors somehow)
    // but this is simpler to reason about.
    if (currentRoot != newrt) {
        auto insertRet = cacheAnchors.insert(std::make_pair(newrt, CAnchorsCacheEntry()));
        CAnchorsMap::iterator ret = insertRet.first;

        ret->second.entered = true;
        ret->second.tree = tree;
        ret->second.flags = CAnchorsCacheEntry::DIRTY;

        if (insertRet.second) {
            // An insert took place
            cachedCoinsUsage += ret->second.tree.DynamicMemoryUsage();
        }

        hashAnchor = newrt;
    }
}

void CCoinsViewCache::PopAnchor(const uint256 &newrt) {
    auto currentRoot = GetBestAnchor();

    // Blocks might not change the commitment tree, in which
    // case restoring the "old" anchor during a reorg must
    // have no effect.
    if (currentRoot != newrt) {
        // Bring the current best anchor into our local cache
        // so that its tree exists in memory.
        {
            ZCIncrementalMerkleTree tree;
            assert(GetAnchorAt(currentRoot, tree));
        }

        // Mark the anchor as unentered, removing it from view
        cacheAnchors[currentRoot].entered = false;

        // Mark the cache entry as dirty so it's propagated
        cacheAnchors[currentRoot].flags = CAnchorsCacheEntry::DIRTY;

        // Mark the new root as the best anchor
        hashAnchor = newrt;
    }
}

void CCoinsViewCache::SetNullifier(const uint256 &nullifier, bool spent) {
    std::pair<CNullifiersMap::iterator, bool> ret = cacheNullifiers.insert(std::make_pair(nullifier, CNullifiersCacheEntry()));
    ret.first->second.entered = spent;
    ret.first->second.flags |= CNullifiersCacheEntry::DIRTY;
}

bool CCoinsViewCache::GetCoins(const uint256 &txid, CCoins &coins) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    if (it != cacheCoins.end()) {
        coins = it->second.coins;
        return true;
    }
    return false;
}

CCoinsModifier CCoinsViewCache::ModifyCoins(const uint256 &txid) {
    assert(!hasModifier);
    std::pair<CCoinsMap::iterator, bool> ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry()));
    size_t cachedCoinUsage = 0;
    if (ret.second) {
        if (!base->GetCoins(txid, ret.first->second.coins)) {
            // The parent view does not have this entry; mark it as fresh.
            ret.first->second.coins.Clear();
            ret.first->second.flags = CCoinsCacheEntry::FRESH;
        } else if (ret.first->second.coins.IsPruned()) {
            // The parent view only has a pruned entry for this; mark it as fresh.
            ret.first->second.flags = CCoinsCacheEntry::FRESH;
        }
    } else {
        cachedCoinUsage = ret.first->second.coins.DynamicMemoryUsage();
    }
    // Assume that whenever ModifyCoins is called, the entry will be modified.
    ret.first->second.flags |= CCoinsCacheEntry::DIRTY;
    return CCoinsModifier(*this, ret.first, cachedCoinUsage);
}

const CCoins* CCoinsViewCache::AccessCoins(const uint256 &txid) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    if (it == cacheCoins.end()) {
        return NULL;
    } else {
        return &it->second.coins;
    }
}

bool CCoinsViewCache::HaveCoins(const uint256 &txid) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    // We're using vtx.empty() instead of IsPruned here for performance reasons,
    // as we only care about the case where a transaction was replaced entirely
    // in a reorganization (which wipes vout entirely, as opposed to spending
    // which just cleans individual outputs).
    return (it != cacheCoins.end() && !it->second.coins.vout.empty());
}

uint256 CCoinsViewCache::GetBestBlock() const {
    if (hashBlock.IsNull())
        hashBlock = base->GetBestBlock();
    return hashBlock;
}


uint256 CCoinsViewCache::GetBestAnchor() const {
    if (hashAnchor.IsNull())
        hashAnchor = base->GetBestAnchor();
    return hashAnchor;
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    hashBlock = hashBlockIn;
}

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins,
                                 const uint256 &hashBlockIn,
                                 const uint256 &hashAnchorIn,
                                 CAnchorsMap &mapAnchors,
                                 CNullifiersMap &mapNullifiers,
                                 CSidechainsMap& mapSidechains,
                                 CCeasingScsMap& mapCeasedScs) {
    assert(!hasModifier);
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) { // Ignore non-dirty entries (optimization).
            CCoinsMap::iterator itUs = cacheCoins.find(it->first);
            if (itUs == cacheCoins.end()) {
                if (!it->second.coins.IsPruned()) {
                    // The parent cache does not have an entry, while the child
                    // cache does have (a non-pruned) one. Move the data up, and
                    // mark it as fresh (if the grandparent did have it, we
                    // would have pulled it in at first GetCoins).
                    assert(it->second.flags & CCoinsCacheEntry::FRESH);
                    CCoinsCacheEntry& entry = cacheCoins[it->first];
                    entry.coins.swap(it->second.coins);
                    cachedCoinsUsage += entry.coins.DynamicMemoryUsage();
                    entry.flags = CCoinsCacheEntry::DIRTY | CCoinsCacheEntry::FRESH;
                }
            } else {
                if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coins.IsPruned()) {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                    cachedCoinsUsage -= itUs->second.coins.DynamicMemoryUsage();
                    cacheCoins.erase(itUs);
                } else {
                    // A normal modification.
                    cachedCoinsUsage -= itUs->second.coins.DynamicMemoryUsage();
                    itUs->second.coins.swap(it->second.coins);
                    cachedCoinsUsage += itUs->second.coins.DynamicMemoryUsage();
                    itUs->second.flags |= CCoinsCacheEntry::DIRTY;
                }
            }
        }
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }

    for (CAnchorsMap::iterator child_it = mapAnchors.begin(); child_it != mapAnchors.end();)
    {
        if (child_it->second.flags & CAnchorsCacheEntry::DIRTY) {
            CAnchorsMap::iterator parent_it = cacheAnchors.find(child_it->first);

            if (parent_it == cacheAnchors.end()) {
                CAnchorsCacheEntry& entry = cacheAnchors[child_it->first];
                entry.entered = child_it->second.entered;
                entry.tree = child_it->second.tree;
                entry.flags = CAnchorsCacheEntry::DIRTY;

                cachedCoinsUsage += entry.tree.DynamicMemoryUsage();
            } else {
                if (parent_it->second.entered != child_it->second.entered) {
                    // The parent may have removed the entry.
                    parent_it->second.entered = child_it->second.entered;
                    parent_it->second.flags |= CAnchorsCacheEntry::DIRTY;
                }
            }
        }

        CAnchorsMap::iterator itOld = child_it++;
        mapAnchors.erase(itOld);
    }

    for (CNullifiersMap::iterator child_it = mapNullifiers.begin(); child_it != mapNullifiers.end();)
    {
        if (child_it->second.flags & CNullifiersCacheEntry::DIRTY) { // Ignore non-dirty entries (optimization).
            CNullifiersMap::iterator parent_it = cacheNullifiers.find(child_it->first);

            if (parent_it == cacheNullifiers.end()) {
                CNullifiersCacheEntry& entry = cacheNullifiers[child_it->first];
                entry.entered = child_it->second.entered;
                entry.flags = CNullifiersCacheEntry::DIRTY;
            } else {
                if (parent_it->second.entered != child_it->second.entered) {
                    parent_it->second.entered = child_it->second.entered;
                    parent_it->second.flags |= CNullifiersCacheEntry::DIRTY;
                }
            }
        }
        CNullifiersMap::iterator itOld = child_it++;
        mapNullifiers.erase(itOld);
    }

    for (auto& entryToWrite : mapSidechains) {
        CSidechainsMap::iterator itLocalCacheEntry = cacheSidechains.find(entryToWrite.first);

        switch (entryToWrite.second.flag) {
            case CSidechainsCacheEntry::Flags::FRESH:
                assert(
                    itLocalCacheEntry == cacheSidechains.end() ||
                    itLocalCacheEntry->second.flag == CSidechainsCacheEntry::Flags::ERASED
                ); //A fresh entry should not exist in localCache or be already erased
                cacheSidechains[entryToWrite.first] = entryToWrite.second;
                break;
            case CSidechainsCacheEntry::Flags::DIRTY: //A dirty entry may or may not exist in localCache
                cacheSidechains[entryToWrite.first] = entryToWrite.second;
                break;
            case CSidechainsCacheEntry::Flags::ERASED:
                if (itLocalCacheEntry != cacheSidechains.end())
                    itLocalCacheEntry->second.flag = CSidechainsCacheEntry::Flags::ERASED;
                break;
            case CSidechainsCacheEntry::Flags::DEFAULT:
                assert(itLocalCacheEntry != cacheSidechains.end());
                assert(itLocalCacheEntry->second.scInfo == entryToWrite.second.scInfo); //entry declared default is indeed different from backed value
                break; //nothing to do. entry is already persisted and has not been modified
            default:
                assert(false);
        }
    }
    mapSidechains.clear();

    for (auto& entryToWrite : mapCeasedScs) {
        CCeasingScsMap::iterator itLocalCacheEntry = cacheCeasingScs.find(entryToWrite.first);

        switch (entryToWrite.second.flag) {
            case CCeasingScsCacheEntry::Flags::FRESH:
                assert(
                    itLocalCacheEntry == cacheCeasingScs.end() ||
                    itLocalCacheEntry->second.flag == CCeasingScsCacheEntry::Flags::ERASED
                ); //A fresh entry should not exist in localCache or be already erased
                cacheCeasingScs[entryToWrite.first] = entryToWrite.second;
                break;
            case CCeasingScsCacheEntry::Flags::DIRTY: //A dirty entry may or may not exist in localCache
                cacheCeasingScs[entryToWrite.first] = entryToWrite.second;
                break;
            case CCeasingScsCacheEntry::Flags::ERASED:
                if (itLocalCacheEntry != cacheCeasingScs.end())
                    itLocalCacheEntry->second.flag = CCeasingScsCacheEntry::Flags::ERASED;
                break;
            case CCeasingScsCacheEntry::Flags::DEFAULT:
                assert(itLocalCacheEntry != cacheCeasingScs.end());
                assert(itLocalCacheEntry->second.ceasingScs == entryToWrite.second.ceasingScs); //entry declared default is indeed different from backed value
                break; //nothing to do. entry is already persisted and has not been modified
            default:
                assert(false);
        }
    }
    mapCeasedScs.clear();

    hashAnchor = hashAnchorIn;
    hashBlock = hashBlockIn;
    return true;
}

bool CCoinsViewCache::HaveSidechain(const uint256& scId) const
{
    CSidechainsMap::const_iterator it = FetchSidechains(scId);
    return (it != cacheSidechains.end()) && (it->second.flag != CSidechainsCacheEntry::Flags::ERASED);
}

bool CCoinsViewCache::GetSidechain(const uint256 & scId, CSidechain& targetScInfo) const
{
    CSidechainsMap::const_iterator it = FetchSidechains(scId);
    if (it != cacheSidechains.end())
        LogPrint("sc", "%s():%d - FetchedSidechain: scId[%s]\n", __func__, __LINE__, scId.ToString());

    if (it != cacheSidechains.end() && it->second.flag != CSidechainsCacheEntry::Flags::ERASED) {
        targetScInfo = it->second.scInfo;
        return true;
    }
    return false;
}

void CCoinsViewCache::queryScIds(std::set<uint256>& scIdsList) const
{
    base->queryScIds(scIdsList);

    // Note that some of the values above may have been erased in current cache.
    // Also new id may be in current cache but not in persisted
    for (const auto& entry: cacheSidechains)
    {
      if (entry.second.flag == CSidechainsCacheEntry::Flags::ERASED)
          scIdsList.erase(entry.first);
      else
          scIdsList.insert(entry.first);
    }

    return;
}

int CCoinsViewCache::getInitScCoinsMaturity()
{
    if ( (Params().NetworkIDString() == "regtest") )
    {
        int val = (int)(GetArg("-sccoinsmaturity", Params().ScCoinsMaturity() ));
        LogPrint("sc", "%s():%d - %s: using val %d \n", __func__, __LINE__, Params().NetworkIDString(), val);
        return val;
    }
    return Params().ScCoinsMaturity();
}

int CCoinsViewCache::getScCoinsMaturity()
{
    // gets constructed just one time
    static int retVal( getInitScCoinsMaturity() );
    return retVal;
}

bool CCoinsViewCache::UpdateScInfo(const CTransaction& tx, const CBlock& block, int blockHeight)
{
    const uint256& txHash = tx.GetHash();
    LogPrint("sc", "%s():%d - enter tx=%s\n", __func__, __LINE__, txHash.ToString() );

    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = blockHeight + SC_COIN_MATURITY;

    // creation ccout
    for (const auto& cr: tx.GetVscCcOut())
    {
        if (HaveSidechain(cr.scId)) {
            LogPrint("sc", "ERROR: %s():%d - CR: scId=%s already in scView\n", __func__, __LINE__, cr.scId.ToString() );
            return false;
        }

        assert(cacheSidechains.count(cr.scId) == 0);
        cacheSidechains[cr.scId].scInfo.creationBlockHash = block.GetHash();
        cacheSidechains[cr.scId].scInfo.creationBlockHeight = blockHeight;
        cacheSidechains[cr.scId].scInfo.creationTxHash = txHash;
        cacheSidechains[cr.scId].scInfo.lastEpochReferencedByCertificate = CScCertificate::EPOCH_NULL;
        cacheSidechains[cr.scId].scInfo.lastCertificateHash.SetNull();
        cacheSidechains[cr.scId].scInfo.creationData.withdrawalEpochLength = cr.withdrawalEpochLength;
        cacheSidechains[cr.scId].scInfo.creationData.customData = cr.customData;
        cacheSidechains[cr.scId].scInfo.mImmatureAmounts[maturityHeight] = cr.nValue;
        cacheSidechains[cr.scId].flag = CSidechainsCacheEntry::Flags::FRESH;

        LogPrint("sc", "%s():%d - immature balance added in scView (h=%d, amount=%s) %s\n",
            __func__, __LINE__, maturityHeight, FormatMoney(cr.nValue), cr.scId.ToString());

        LogPrint("sc", "%s():%d - scId[%s] added in scView\n", __func__, __LINE__, cr.scId.ToString() );
    }

    // forward transfer ccout
    for(auto& ft: tx.GetVftCcOut())
    {
        if (!HaveSidechain(ft.scId))
        {
            // should not happen
            LogPrint("sc", "%s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, ft.scId.ToString() );
            return false;
        }
        assert(cacheSidechains.count(ft.scId) != 0);

        // add a new immature balance entry in sc info or increment it if already there
        cacheSidechains[ft.scId].scInfo.mImmatureAmounts[maturityHeight] += ft.nValue;
        if (cacheSidechains[ft.scId].flag != CSidechainsCacheEntry::Flags::FRESH)
            cacheSidechains[ft.scId].flag = CSidechainsCacheEntry::Flags::DIRTY;

        LogPrint("sc", "%s():%d - immature balance added in scView (h=%d, amount=%s) %s\n",
            __func__, __LINE__, maturityHeight, FormatMoney(ft.nValue), ft.scId.ToString());
    }

    return true;
}

bool CCoinsViewCache::RevertTxOutputs(const CTransaction& tx, int nHeight)
{
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = nHeight + SC_COIN_MATURITY;

    // revert forward transfers
    for(const auto& entry: tx.GetVftCcOut())
    {
        const uint256& scId = entry.scId;

        LogPrint("sc", "%s():%d - removing fwt for scId=%s\n", __func__, __LINE__, scId.ToString());

        CSidechain targetScInfo;
        if (!GetSidechain(scId, targetScInfo))
        {
            // should not happen
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        if (!DecrementImmatureAmount(scId, targetScInfo, entry.nValue, maturityHeight) )
        {
            // should not happen
            LogPrint("sc", "ERROR %s():%d - scId=%s could not handle immature balance at height%d\n",
                __func__, __LINE__, scId.ToString(), maturityHeight);
            return false;
        }
    }

    // remove sidechain if the case
    for(const auto& entry: tx.GetVscCcOut())
    {
        const uint256& scId = entry.scId;

        LogPrint("sc", "%s():%d - removing scId=%s\n", __func__, __LINE__, scId.ToString());

        CSidechain targetScInfo;
        if (!GetSidechain(scId, targetScInfo))
        {
            // should not happen
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        if (!DecrementImmatureAmount(scId, targetScInfo, entry.nValue, maturityHeight) )
        {
            // should not happen
            LogPrint("sc", "ERROR %s():%d - scId=%s could not handle immature balance at height%d\n",
                __func__, __LINE__, scId.ToString(), maturityHeight);
            return false;
        }

        if (targetScInfo.balance > 0)
        {
            // should not happen either
            LogPrint("sc", "ERROR %s():%d - scId=%s balance not null: %s\n",
                __func__, __LINE__, scId.ToString(), FormatMoney(targetScInfo.balance));
            return false;
        }

        cacheSidechains[scId] = CSidechainsCacheEntry(targetScInfo, CSidechainsCacheEntry::Flags::ERASED);

        LogPrint("sc", "%s():%d - scId=%s removed from scView\n", __func__, __LINE__, scId.ToString() );
    }
    return true;
}

bool CCoinsViewCache::ApplyMatureBalances(int blockHeight, CBlockUndo& blockundo)
{
    LogPrint("sc", "%s():%d - blockHeight=%d, msc_iaundo size=%d\n",
         __func__, __LINE__, blockHeight,  blockundo.msc_iaundo.size() );

    std::set<uint256> allKnowScIds;
    queryScIds(allKnowScIds);
    for(auto it_set = allKnowScIds.begin(); it_set != allKnowScIds.end(); ++it_set)
    {
        const uint256& scId = *it_set;

        assert(HaveSidechain(scId));
        CSidechain& targetScInfo = cacheSidechains.at(scId).scInfo; //in place modifications here

        if (targetScInfo.mImmatureAmounts.size() == 0)
            continue; //no amounts to mature for this sc

        int maturityHeight      = targetScInfo.mImmatureAmounts.begin()->first;
        CAmount candidateAmount = targetScInfo.mImmatureAmounts.begin()->second;

        assert(maturityHeight >= blockHeight);

        if (maturityHeight == blockHeight)
        {
            LogPrint("sc", "%s():%d - scId=%s balance before: %s\n",
                __func__, __LINE__, scId.ToString(), FormatMoney(targetScInfo.balance));

            // if maturity has been reached apply it to balance in scview
            targetScInfo.balance += candidateAmount;

            LogPrint("sc", "%s():%d - scId=%s balance after: %s\n",
                __func__, __LINE__, scId.ToString(), FormatMoney(targetScInfo.balance));

            // scview balance has been updated, remove the entry in scview immature map
            targetScInfo.mImmatureAmounts.erase(targetScInfo.mImmatureAmounts.begin());
            cacheSidechains.at(scId).flag = CSidechainsCacheEntry::Flags::DIRTY;

            LogPrint("sc", "%s():%d - adding immature amount %s for scId=%s in blockundo\n",
                __func__, __LINE__, FormatMoney(candidateAmount), scId.ToString());
        
            // store immature balances into the blockundo obj
            blockundo.msc_iaundo[scId].immAmount = candidateAmount;
        }
    }

    return true;
}

bool CCoinsViewCache::RestoreImmatureBalances(int blockHeight, const CBlockUndo& blockundo)
{
    LogPrint("sc", "%s():%d - blockHeight=%d, msc_iaundo size=%d\n",
        __func__, __LINE__, blockHeight,  blockundo.msc_iaundo.size() );

    // loop in the map of the blockundo and process each sidechain id
    for (auto it_ia_undo_map = blockundo.msc_iaundo.begin(); it_ia_undo_map != blockundo.msc_iaundo.end(); ++it_ia_undo_map)
    {
        const uint256& scId           = it_ia_undo_map->first;
        const std::string& scIdString = scId.ToString();

        if (!HaveSidechain(scId))
        {
            // should not happen
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }
        CSidechain& targetScInfo = cacheSidechains.at(scId).scInfo;

        CAmount amountToRestore = it_ia_undo_map->second.immAmount;
        int blockundoEpoch = it_ia_undo_map->second.certEpoch;
        const uint256& lastCertHash = it_ia_undo_map->second.lastCertificateHash;

        if (amountToRestore > 0)
        {
            LogPrint("sc", "%s():%d - adding immature amount %s into sc view for scId=%s\n",
                __func__, __LINE__, FormatMoney(amountToRestore), scIdString);

            if (targetScInfo.balance < amountToRestore)
            {
                LogPrint("sc", "%s():%d - Can not update balance with amount[%s] for scId=%s, would be negative\n",
                    __func__, __LINE__, FormatMoney(amountToRestore), scId.ToString() );
                return false;
            }

            targetScInfo.mImmatureAmounts[blockHeight] += amountToRestore;

            LogPrint("sc", "%s():%d - scId=%s balance before: %s\n", __func__, __LINE__, scIdString, FormatMoney(targetScInfo.balance));
            targetScInfo.balance -= amountToRestore;
            LogPrint("sc", "%s():%d - scId=%s balance after: %s\n", __func__, __LINE__, scIdString, FormatMoney(targetScInfo.balance));

            cacheSidechains.at(scId).flag = CSidechainsCacheEntry::Flags::DIRTY;
        }

        if (blockundoEpoch != CScCertificate::EPOCH_NOT_INITIALIZED)
        {
            LogPrint("sc", "%s():%d - scId=%s epoch before: %d\n", __func__, __LINE__, scIdString, targetScInfo.lastEpochReferencedByCertificate);
            targetScInfo.lastEpochReferencedByCertificate = it_ia_undo_map->second.certEpoch;
            LogPrint("sc", "%s():%d - scId=%s epoch after: %d\n", __func__, __LINE__, scIdString, targetScInfo.lastEpochReferencedByCertificate);

            targetScInfo.lastCertificateHash = lastCertHash;
            cacheSidechains.at(scId).flag = CSidechainsCacheEntry::Flags::DIRTY;
        }

    }

    return true;
}

bool CCoinsViewCache::HaveCertForEpoch(const uint256& scId, int epochNumber) const {
    CSidechain info;
    if (!GetSidechain(scId, info))
        return false;

    if (info.lastEpochReferencedByCertificate == epochNumber)
        return true;

    return false;
}

#ifdef BITCOIN_TX
int CSidechain::EpochFor(int targetHeight) const { return CScCertificate::EPOCH_NULL; }
int CSidechain::StartHeightForEpoch(int targetEpoch) const { return -1; }
int CSidechain::SafeguardMargin() const { return -1; }
bool CCoinsViewCache::isLegalEpoch(const uint256& scId, int epochNumber, const uint256& endEpochBlockHash) {return true;}
bool CCoinsViewCache::IsCertApplicableToState(const CScCertificate& cert, int nHeight, CValidationState& state) {return true;}
bool CCoinsViewCache::HaveScRequirements(const CTransaction& tx, int height) { return true;}
#else

#include "consensus/validation.h"
#include "main.h"
bool CCoinsViewCache::IsCertApplicableToState(const CScCertificate& cert, int nHeight, CValidationState& state)
{
    const uint256& certHash = cert.GetHash();

    LogPrint("cert", "%s():%d - called: cert[%s], scId[%s], height[%d]\n",
        __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString(), nHeight );

    CSidechain scInfo;
    if (!GetSidechain(cert.GetScId(), scInfo))
    {
        LogPrint("sc", "%s():%d - cert[%s] refers to scId[%s] not yet created\n",
            __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString() );
        return state.Invalid(error("scid does not exist"),
             REJECT_INVALID, "sidechain-certificate-scid");
    }

    // check that epoch data are consistent
    if (!isLegalEpoch(cert.GetScId(), cert.epochNumber, cert.endEpochBlockHash) )
    {
        LogPrint("sc", "%s():%d - invalid cert[%s], scId[%s] invalid epoch data\n",
            __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString() );
        return state.Invalid(error("certificate with invalid epoch considering mempool"),
             REJECT_INVALID, "sidechain-certificate-epoch");
    }

    if (isCeasedAtHeight(cert.GetScId(), nHeight)!= CSidechain::state::ALIVE) {
        LogPrintf("ERROR: certificate[%s] cannot be accepted, sidechain [%s] already ceased at active height = %d\n",
            certHash.ToString(), cert.GetScId().ToString(), chainActive.Height());
        return state.Invalid(error("received a delayed cert"),
                     REJECT_INVALID, "sidechain-certificate-delayed");
    }

    CAmount totalAmount = cert.GetValueOfBackwardTransfers(); 

    if (totalAmount > scInfo.balance)
    {
        LogPrint("sc", "%s():%d - insufficent balance in scId[%s]: balance[%s], cert amount[%s]\n",
            __func__, __LINE__, cert.GetScId().ToString(), FormatMoney(scInfo.balance), FormatMoney(totalAmount) );
        return state.Invalid(error("insufficient balance"),
                     REJECT_INVALID, "sidechain-insufficient-balance");
    }
    LogPrint("sc", "%s():%d - ok, balance in scId[%s]: balance[%s], cert amount[%s]\n",
        __func__, __LINE__, cert.GetScId().ToString(), FormatMoney(scInfo.balance), FormatMoney(totalAmount) );

    return true;
}

bool CCoinsViewCache::isLegalEpoch(const uint256& scId, int epochNumber, const uint256& endEpochBlockHash)
{
    if (epochNumber < 0)
    {
        LogPrint("sc", "%s():%d - invalid epoch number %d\n",
            __func__, __LINE__, epochNumber );
        return false;
    }

    // 1. the referenced block must be in active chain
    LOCK(cs_main);
    if (mapBlockIndex.count(endEpochBlockHash) == 0)
    {
        LogPrint("sc", "%s():%d - endEpochBlockHash %s is not in block index map\n",
            __func__, __LINE__, endEpochBlockHash.ToString() );
        return false;
    }

    CBlockIndex* pblockindex = mapBlockIndex[endEpochBlockHash];
    if (!chainActive.Contains(pblockindex))
    {
        LogPrint("sc", "%s():%d - endEpochBlockHash %s refers to a valid block but is not in active chain\n",
            __func__, __LINE__, endEpochBlockHash.ToString() );
        return false;
    }

    // 2. combination of epoch number and epoch length, specified in creating sc, must point to that block
    CSidechain info;
    if (!GetSidechain(scId, info))
    {
        // should not happen
        LogPrint("sc", "%s():%d - scId[%s] not found\n",
            __func__, __LINE__, scId.ToString() );
        return false;
    }

    int endEpochHeight = info.StartHeightForEpoch(epochNumber+1) -1;
    pblockindex = chainActive[endEpochHeight];

    if (!pblockindex)
    {
        LogPrint("sc", "%s():%d - calculated height %d (createHeight=%d/epochNum=%d/epochLen=%d) is out of active chain\n",
            __func__, __LINE__, endEpochHeight, info.creationBlockHeight, epochNumber, info.creationData.withdrawalEpochLength);
        return false;
    }

    const uint256& hash = pblockindex->GetBlockHash();
    if (hash != endEpochBlockHash)
    {
        LogPrint("sc", "%s():%d - bock hash mismatch: endEpochBlockHash[%s] / calculated[%s]\n",
            __func__, __LINE__, endEpochBlockHash.ToString(), hash.ToString());
        return false;
    }

    return true;
}

bool CCoinsViewCache::HaveScRequirements(const CTransaction& tx, int height)
{
    if (tx.IsCoinBase())
        return true;

    const uint256& txHash = tx.GetHash();

    // check creation
    for (const auto& sc: tx.GetVscCcOut())
    {
        const uint256& scId = sc.scId;
        if (HaveSidechain(scId))
        {
            LogPrint("sc", "%s():%d - ERROR: Invalid tx[%s] : scid[%s] already created\n",
                __func__, __LINE__, txHash.ToString(), scId.ToString());
            return false;
        }
        LogPrint("sc", "%s():%d - OK: tx[%s] is creating scId[%s]\n",
            __func__, __LINE__, txHash.ToString(), scId.ToString());
    }

    // check fw tx
    for (const auto& ft: tx.GetVftCcOut())
    {
        const uint256& scId = ft.scId;
        if (HaveSidechain(scId))
        {
            if (isCeasedAtHeight(scId, height)!= CSidechain::state::ALIVE) {
                LogPrintf("ERROR: tx[%s] tries to send funds to scId[%s] already ceased at height = %d\n",
                            txHash.ToString(), scId.ToString(), height);
                return false;
                }
        } else {
            if (!Sidechain::hasScCreationOutput(tx, scId)) {
                LogPrint("sc", "%s():%d - ERROR: tx [%s] tries to send funds to scId[%s] not yet created\n",
                        __func__, __LINE__, txHash.ToString(), scId.ToString() );
                return false;
            }
        }

        LogPrint("sc", "%s():%d - OK: tx[%s] is sending [%s] to scId[%s]\n",
            __func__, __LINE__, txHash.ToString(), FormatMoney(ft.nValue), scId.ToString());
    }

    return true;
}

#endif

bool CCoinsViewCache::UpdateScInfo(const CScCertificate& cert, CBlockUndo& blockundo)
{
    const uint256& certHash = cert.GetHash();
    const uint256& scId = cert.GetScId();
    const CAmount& totalAmount = cert.GetValueOfBackwardTransfers();

    LogPrint("cert", "%s():%d - cert=%s\n", __func__, __LINE__, certHash.ToString() );

    CSidechain targetScInfo;
    if (!GetSidechain(scId, targetScInfo))
    {
        // should not happen
        LogPrint("cert", "%s():%d - Can not update balance, could not find scId=%s\n",
            __func__, __LINE__, scId.ToString() );
        return false;
    }

    if (targetScInfo.balance < totalAmount)
    {
        LogPrint("cert", "%s():%d - Can not update balance %s with amount[%s] for scId=%s, would be negative\n",
            __func__, __LINE__, FormatMoney(targetScInfo.balance), FormatMoney(totalAmount), scId.ToString() );
        return false;
    }

    // if an entry already exists, update only cert epoch with current value
    // if it is a brand new entry, amount will be init as 0 by default
    blockundo.msc_iaundo[scId].certEpoch = targetScInfo.lastEpochReferencedByCertificate;
    blockundo.msc_iaundo[scId].lastCertificateHash = targetScInfo.lastCertificateHash;

    targetScInfo.balance -= totalAmount;
    targetScInfo.lastEpochReferencedByCertificate = cert.epochNumber;
    targetScInfo.lastCertificateHash = certHash;
    cacheSidechains[scId] = CSidechainsCacheEntry(targetScInfo, CSidechainsCacheEntry::Flags::DIRTY);

    LogPrint("cert", "%s():%d - amount removed from scView (amount=%s, resulting bal=%s) %s\n",
        __func__, __LINE__, FormatMoney(totalAmount), FormatMoney(targetScInfo.balance), scId.ToString());

    return true;
}

bool CCoinsViewCache::RevertCertOutputs(const CScCertificate& cert)
{
    const uint256& scId = cert.GetScId();
    const CAmount& totalAmount = cert.GetValueOfBackwardTransfers();

    LogPrint("cert", "%s():%d - removing cert for scId=%s\n", __func__, __LINE__, scId.ToString());

    CSidechain targetScInfo;
    if (!GetSidechain(scId, targetScInfo))
    {
        // should not happen
        LogPrint("cert", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
        return false;
    }

    targetScInfo.balance += totalAmount;
    cacheSidechains[scId] = CSidechainsCacheEntry(targetScInfo, CSidechainsCacheEntry::Flags::DIRTY);

    LogPrint("cert", "%s():%d - amount restored to scView (amount=%s, resulting bal=%s) %s\n",
        __func__, __LINE__, FormatMoney(totalAmount), FormatMoney(targetScInfo.balance), scId.ToString());

    return true;
}

bool CCoinsViewCache::HaveCeasingScs(int height) const
{
    CCeasingScsMap::const_iterator it = FetchCeasingScs(height);
    return (it != cacheCeasingScs.end()) && (it->second.flag != CCeasingScsCacheEntry::Flags::ERASED);
}

bool CCoinsViewCache::GetCeasingScs(int height, CCeasingSidechains& ceasingScs) const
{
    CCeasingScsMap::const_iterator it = FetchCeasingScs(height);
    if (it != cacheCeasingScs.end() && it->second.flag != CCeasingScsCacheEntry::Flags::ERASED) {
        ceasingScs = it->second.ceasingScs;
        return true;
    }
    return false;
}

bool CCoinsViewCache::UpdateCeasingScs(const CTxScCreationOut& scCreationOut)
{
    CSidechain scInfo;
    if (!this->GetSidechain(scCreationOut.scId, scInfo)) {
        LogPrint("cert", "%s():%d - attempt to update ceasing sidechain map with unknown scId[%s]\n",
            __func__, __LINE__, scCreationOut.scId.ToString());
        return false;
    }

    int currentEpoch = scInfo.EpochFor(scInfo.creationBlockHeight);
    int nextCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch + 1) + scInfo.SafeguardMargin() +1;

    CCeasingSidechains ceasingScIdSet;
    if (!GetCeasingScs(nextCeasingHeight,ceasingScIdSet)) {
        ceasingScIdSet.ceasingScs.insert(scCreationOut.scId);
        cacheCeasingScs[nextCeasingHeight] = CCeasingScsCacheEntry(ceasingScIdSet, CCeasingScsCacheEntry::Flags::FRESH);
    } else {
        ceasingScIdSet.ceasingScs.insert(scCreationOut.scId);
        cacheCeasingScs[nextCeasingHeight] = CCeasingScsCacheEntry(ceasingScIdSet, CCeasingScsCacheEntry::Flags::DIRTY);
    }

    LogPrint("cert", "%s():%d - CEASING HEIGHTS: scId[%s]: creation sets nextCeasingHeight to [%d]\n",
            __func__, __LINE__, scCreationOut.scId.ToString(), nextCeasingHeight);

    return true;
}

bool CCoinsViewCache::UndoCeasingScs(const CTxScCreationOut& scCreationOut)
{
    CSidechain restoredScInfo;
    if (!this->GetSidechain(scCreationOut.scId, restoredScInfo)) {
        LogPrint("cert", "%s():%d - attempt to undo ceasing sidechain map with unknown scId[%s]\n",
            __func__, __LINE__, scCreationOut.scId.ToString());
        return false;
    }

    int restoredEpoch = restoredScInfo.EpochFor(restoredScInfo.creationBlockHeight);
    int currentCeasingHeight = restoredScInfo.StartHeightForEpoch(restoredEpoch + 1) + restoredScInfo.SafeguardMargin() +1;

    //remove current ceasing Height
    CCeasingSidechains currentCeasingScId;
    if (!GetCeasingScs(currentCeasingHeight,currentCeasingScId)) {
        LogPrint("cert", "%s():%d - CEASING HEIGHTS: scId[%s] misses current ceasing height; expected value was [%d]\n",
            __func__, __LINE__, scCreationOut.scId.ToString(), currentCeasingHeight);
        return false;
    }

    currentCeasingScId.ceasingScs.erase(scCreationOut.scId);
    if (currentCeasingScId.ceasingScs.size() != 0)
        cacheCeasingScs[currentCeasingHeight] = CCeasingScsCacheEntry(currentCeasingScId, CCeasingScsCacheEntry::Flags::DIRTY);
    else
        cacheCeasingScs[currentCeasingHeight] = CCeasingScsCacheEntry(currentCeasingScId, CCeasingScsCacheEntry::Flags::ERASED);

    LogPrint("cert", "%s():%d - CEASING HEIGHTS: scId[%s]: undo of creation removes currentCeasingHeight [%d]\n",
            __func__, __LINE__, scCreationOut.scId.ToString(), currentCeasingHeight);

    return true;
}

bool CCoinsViewCache::UpdateCeasingScs(const CScCertificate& cert)
{
    CSidechain scInfo;
    if (!this->GetSidechain(cert.GetScId(), scInfo)) {
        LogPrint("cert", "%s():%d - attempt to update ceasing sidechain map with cert to unknown scId[%s]\n",
            __func__, __LINE__, cert.GetScId().ToString());
        return false;
    }

    int nextCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2) + scInfo.SafeguardMargin()+1;
    int prevCeasingHeight = nextCeasingHeight - scInfo.creationData.withdrawalEpochLength;

    //clear up prev ceasing height, if any
    CCeasingSidechains prevCeasingScId;
    if (GetCeasingScs(prevCeasingHeight,prevCeasingScId)) {
        prevCeasingScId.ceasingScs.erase(cert.GetScId());
        if (prevCeasingScId.ceasingScs.size() != 0) //still other sc ceasing at that height
            cacheCeasingScs[prevCeasingHeight] = CCeasingScsCacheEntry(prevCeasingScId, CCeasingScsCacheEntry::Flags::DIRTY);
        else
            cacheCeasingScs[prevCeasingHeight] = CCeasingScsCacheEntry(prevCeasingScId, CCeasingScsCacheEntry::Flags::ERASED);

        LogPrint("cert", "%s():%d - CEASING HEIGHTS: scId[%s]: cert [%s] removes prevCeasingHeight [%d]\n",
                __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), prevCeasingHeight);
    } else {
        LogPrint("cert", "%s():%d - CEASING HEIGHTS: scId[%s]: cert [%s] finds not prevCeasingHeight [%d] to remove\n",
                __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), prevCeasingHeight);
    }

    //add next ceasing Height
    CCeasingSidechains nextCeasingScId;
    if (!GetCeasingScs(nextCeasingHeight,nextCeasingScId)) {
        nextCeasingScId.ceasingScs.insert(cert.GetScId());
        cacheCeasingScs[nextCeasingHeight] = CCeasingScsCacheEntry(nextCeasingScId, CCeasingScsCacheEntry::Flags::FRESH);
    } else {
        nextCeasingScId.ceasingScs.insert(cert.GetScId());
        cacheCeasingScs[nextCeasingHeight] = CCeasingScsCacheEntry(nextCeasingScId, CCeasingScsCacheEntry::Flags::DIRTY);
    }

    LogPrint("cert", "%s():%d - CEASING HEIGHTS: scId[%s]: cert [%s] sets nextCeasingHeight to [%d]\n",
            __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), nextCeasingHeight);

    return true;
}

bool CCoinsViewCache::UndoCeasingScs(const CScCertificate& cert)
{
    CSidechain restoredScInfo;
    if (!this->GetSidechain(cert.GetScId(), restoredScInfo)) {
        LogPrint("cert", "%s():%d - attempt to undo ceasing sidechain map with cert to unknown scId[%s]\n",
            __func__, __LINE__, cert.GetScId().ToString());
        return false;
    }

    int currentCeasingHeight = restoredScInfo.StartHeightForEpoch(cert.epochNumber+2) + restoredScInfo.SafeguardMargin()+1;
    int restoredCeasingHeight = currentCeasingHeight - restoredScInfo.creationData.withdrawalEpochLength;

    //remove current ceasing Height
    CCeasingSidechains currentCeasingScId;
    if (!GetCeasingScs(currentCeasingHeight,currentCeasingScId)) {
        LogPrint("cert", "%s():%d - CEASING HEIGHTS: scId[%s] misses current ceasing height; expected value was [%d]\n",
            __func__, __LINE__, cert.GetScId().ToString(), currentCeasingHeight);
        return false;
    }

    currentCeasingScId.ceasingScs.erase(cert.GetScId());
    if (currentCeasingScId.ceasingScs.size() != 0) //still other sc ceasing at that height
        cacheCeasingScs[currentCeasingHeight] = CCeasingScsCacheEntry(currentCeasingScId, CCeasingScsCacheEntry::Flags::DIRTY);
    else
        cacheCeasingScs[currentCeasingHeight] = CCeasingScsCacheEntry(currentCeasingScId, CCeasingScsCacheEntry::Flags::ERASED);

    LogPrint("cert", "%s():%d - CEASING HEIGHTS: scId[%s]: undo of cert [%s] removes currentCeasingHeight [%d]\n",
            __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), currentCeasingHeight);

    //restore previous ceasing Height
    CCeasingSidechains restoredCeasingScId;
    if (!GetCeasingScs(restoredCeasingHeight,restoredCeasingScId)) {
        restoredCeasingScId.ceasingScs.insert(cert.GetScId());
        cacheCeasingScs[restoredCeasingHeight] = CCeasingScsCacheEntry(restoredCeasingScId, CCeasingScsCacheEntry::Flags::FRESH);
    } else {
        restoredCeasingScId.ceasingScs.insert(cert.GetScId());
        cacheCeasingScs[restoredCeasingHeight] = CCeasingScsCacheEntry(restoredCeasingScId, CCeasingScsCacheEntry::Flags::DIRTY);
    }

    LogPrint("cert", "%s():%d - CEASING HEIGHTS: scId[%s]: undo of cert [%s] set nextCeasingHeight to [%d]\n",
            __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), restoredCeasingHeight);

    return true;
}

bool CCoinsViewCache::HandleCeasingScs(int height, CBlockUndo& blockUndo)
{
    if (!HaveCeasingScs(height))
        return true;

    CCeasingSidechains ceasingScList;
    GetCeasingScs(height, ceasingScList);

    for (const uint256& ceasingScId : ceasingScList.ceasingScs)
    {
        LogPrint("cert", "%s():%d - CEASING HEIGHTS: about to handle scId[%s] and ceasingHeight [%d]\n",
                __func__, __LINE__, ceasingScId.ToString(), height);

        CSidechain scInfo;
        assert(GetSidechain(ceasingScId, scInfo));

        LogPrint("cert", "%s():%d - CEASING HEIGHTS: lastCertEpoch [%d], lastCertHash [%s]\n",
                __func__, __LINE__, scInfo.lastEpochReferencedByCertificate, scInfo.lastCertificateHash.ToString());

        if (scInfo.lastEpochReferencedByCertificate == CScCertificate::EPOCH_NULL) {
            assert(scInfo.lastCertificateHash.IsNull());
            continue;
        }

        if (!this->HaveCoins(scInfo.lastCertificateHash))
            continue; //in case the cert had not bwt nor change, there won't be any coin generated by cert. Nothing to handle

        CCoinsModifier coins = this->ModifyCoins(scInfo.lastCertificateHash);
        assert(!coins->originScId.IsNull());

        //null all bwt outputs and add related txundo in block
        bool foundFirstBwt = false;
        for(unsigned int pos = 0; pos < coins->vout.size(); ++pos)
        {
            if (!coins->IsAvailable(pos))
                continue;
            if (!coins->vout[pos].isFromBackwardTransfer)
                continue;

            if (!foundFirstBwt) {
                blockUndo.vtxundo.push_back(CTxUndo());
                blockUndo.vtxundo.back().refTx = scInfo.lastCertificateHash;
                blockUndo.vtxundo.back().firstBwtPos = pos;
                LogPrint("cert", "%s():%d - set refTx[%s], pos[%d]\n",
                    __func__, __LINE__, scInfo.lastCertificateHash.ToString(), pos);
                foundFirstBwt = true;
            }

            blockUndo.vtxundo.back().vprevout.push_back(CTxInUndo(coins->vout[pos]));
            CTxInUndo& undo = blockUndo.vtxundo.back().vprevout.back();
            undo.nHeight    = coins->nHeight;
            undo.fCoinBase  = coins->fCoinBase;
            undo.nVersion   = coins->nVersion;
            undo.originScId = coins->originScId;

            coins->Spend(pos);
        }
    }

    LogPrint("sc", "%s():%d Exiting: CBlockUndo: %s\n",
        __func__, __LINE__, blockUndo.ToString());

    return true;
}

bool CCoinsViewCache::RevertCeasingScs(const CTxUndo& ceasedCertUndo)
{
    bool fClean = true;

    const uint256& coinHash = ceasedCertUndo.refTx;
    if(coinHash.IsNull())
    {
        fClean = fClean && error("%s: malformed undo data, ", __func__);
        LogPrint("cert", "%s():%d - returning fClean[%d]\n", __func__, __LINE__, fClean);
        return fClean;
    }
    CCoinsModifier coins = this->ModifyCoins(coinHash);
    LogPrint("cert", "%s():%d - PRE :%s\n", __func__, __LINE__, coins->ToString());
    unsigned int firstBwtPos = ceasedCertUndo.firstBwtPos;

    const std::vector<CTxInUndo>& outVec = ceasedCertUndo.vprevout;
    LogPrint("cert", "%s():%d - PRE : outVec.size() = %d\n", __func__, __LINE__, outVec.size());

    for (size_t bwtOutPos = outVec.size(); bwtOutPos-- > 0;)
    {
        LogPrint("cert", "%s():%d - PRE : bwtOutPos= %d\n", __func__, __LINE__, bwtOutPos);
        if (outVec.at(bwtOutPos).nHeight != 0)
        {
            coins->fCoinBase  = outVec.at(bwtOutPos).fCoinBase;
            coins->nHeight    = outVec.at(bwtOutPos).nHeight;
            coins->nVersion   = outVec.at(bwtOutPos).nVersion;
            coins->originScId = outVec.at(bwtOutPos).originScId;
        } else {
            LogPrint("cert", "%s():%d - returning false\n", __func__, __LINE__);
            return false;
        }

        if(coins->IsAvailable(firstBwtPos + bwtOutPos))
            fClean = fClean && error("%s: undo data overwriting existing output", __func__);
        if (coins->vout.size() < (firstBwtPos + bwtOutPos+1))
            coins->vout.resize(firstBwtPos + bwtOutPos+1);
        coins->vout.at(firstBwtPos + bwtOutPos) = outVec.at(bwtOutPos).txout;
    }

    LogPrint("cert", "%s():%d - POST:%s\n", __func__, __LINE__, coins->ToString());
    return fClean;
}

CSidechain::state CCoinsViewCache::isCeasedAtHeight(const uint256& scId, int height) const
{
    if (!HaveSidechain(scId))
        return CSidechain::state::NOT_APPLICABLE;

    CSidechain scInfo;
    GetSidechain(scId, scInfo);

    if (height < scInfo.creationBlockHeight)
        return CSidechain::state::NOT_APPLICABLE;

    int currentEpoch = scInfo.EpochFor(height);

    if (currentEpoch > scInfo.lastEpochReferencedByCertificate + 2)
        return CSidechain::state::CEASED;

    if (currentEpoch == scInfo.lastEpochReferencedByCertificate + 2)
    {
        int targetEpochSafeguardHeight = scInfo.StartHeightForEpoch(currentEpoch) + scInfo.SafeguardMargin();
        if (height > targetEpochSafeguardHeight)
            return CSidechain::state::CEASED;
    }

    return  CSidechain::state::ALIVE;
}

bool CCoinsViewCache::Flush() {
    bool fOk = base->BatchWrite(cacheCoins, hashBlock, hashAnchor, cacheAnchors, cacheNullifiers, cacheSidechains, cacheCeasingScs);
    cacheCoins.clear();
    cacheSidechains.clear();
    cacheCeasingScs.clear();
    cacheAnchors.clear();
    cacheNullifiers.clear();
    cachedCoinsUsage = 0;
    return fOk;
}

bool CCoinsViewCache::DecrementImmatureAmount(const uint256& scId, CSidechain& targetScInfo, CAmount nValue, int maturityHeight)
{
    // get the map of immature amounts, they are indexed by height
    auto& iaMap = targetScInfo.mImmatureAmounts;

    if (!iaMap.count(maturityHeight) )
    {
        // should not happen
        LogPrint("sc", "ERROR %s():%d - could not find immature balance at height%d\n",
            __func__, __LINE__, maturityHeight);
        return false;
    }

    LogPrint("sc", "%s():%d - immature amount before: %s\n",
        __func__, __LINE__, FormatMoney(iaMap[maturityHeight]));

    if (iaMap[maturityHeight] < nValue)
    {
        // should not happen either
        LogPrint("sc", "ERROR %s():%d - negative balance at height=%d\n",
            __func__, __LINE__, maturityHeight);
        return false;
    }

    iaMap[maturityHeight] -= nValue;
    cacheSidechains[scId] = CSidechainsCacheEntry(targetScInfo, CSidechainsCacheEntry::Flags::DIRTY);

    LogPrint("sc", "%s():%d - immature amount after: %s\n",
        __func__, __LINE__, FormatMoney(iaMap[maturityHeight]));

    if (iaMap[maturityHeight] == 0)
    {
        iaMap.erase(maturityHeight);
        cacheSidechains[scId] = CSidechainsCacheEntry(targetScInfo, CSidechainsCacheEntry::Flags::DIRTY);
        LogPrint("sc", "%s():%d - removed entry height=%d from immature amounts in memory\n",
            __func__, __LINE__, maturityHeight );
    }
    return true;
}

void CCoinsViewCache::Dump_info() const
{
    std::set<uint256> scIdsList;
    queryScIds(scIdsList);
    LogPrint("sc", "-- number of side chains found [%d] ------------------------\n", scIdsList.size());
    for(const auto& scId: scIdsList)
    {
        LogPrint("sc", "-- side chain [%s] ------------------------\n", scId.ToString());
        CSidechain info;
        if (!GetSidechain(scId, info))
        {
            LogPrint("sc", "===> No such side chain\n");
            return;
        }

        LogPrint("sc", "  created in block[%s] (h=%d)\n", info.creationBlockHash.ToString(), info.creationBlockHeight );
        LogPrint("sc", "  creationTx[%s]\n", info.creationTxHash.ToString());
        LogPrint("sc", "  lastEpochReferencedByCertificate[%d]\n", info.lastEpochReferencedByCertificate);
        LogPrint("sc", "  balance[%s]\n", FormatMoney(info.balance));
        LogPrint("sc", "  ----- creation data:\n");
        LogPrint("sc", "      withdrawalEpochLength[%d]\n", info.creationData.withdrawalEpochLength);
        LogPrint("sc", "      customData[%s]\n", HexStr(info.creationData.customData));
        LogPrint("sc", "  immature amounts size[%d]\n", info.mImmatureAmounts.size());
    }

    return;
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    return cacheCoins.size();
}

const CTxOut &CCoinsViewCache::GetOutputFor(const CTxIn& input) const
{
    const CCoins* coins = AccessCoins(input.prevout.hash);
    assert(coins && coins->IsAvailable(input.prevout.n));
    return coins->vout[input.prevout.n];
}

CAmount CCoinsViewCache::GetValueIn(const CTransactionBase& txBase) const
{
    if (txBase.IsCoinBase())
        return 0;

    CAmount nResult = 0;
    for (const CTxIn& in : txBase.GetVin())
        nResult += GetOutputFor(in).nValue;

    nResult += txBase.GetJoinSplitValueIn();

    return nResult;
}

CCoinsViewCache::outputMaturity CCoinsViewCache::IsCertOutputMature(const uint256& txHash, unsigned int pos, int spendHeight) const
{
    CCoins refCoin;
    if(!GetCoins(txHash,refCoin))
        return outputMaturity::NOT_APPLICABLE;

    assert(refCoin.IsFromCert());

    if (!refCoin.IsAvailable(pos))
        return outputMaturity::NOT_APPLICABLE;

    if (!refCoin.vout[pos].isFromBackwardTransfer) //change outputs are always mature
        return outputMaturity::MATURE;

    // Hereinafter, we have a certificate, hence we can assert existence of its sidechain
    CSidechain targetSc;
    assert(GetSidechain(refCoin.originScId, targetSc));

    int coinEpoch = targetSc.EpochFor(refCoin.nHeight);

    if (coinEpoch < targetSc.lastEpochReferencedByCertificate)
        return outputMaturity::MATURE;

    if (coinEpoch == (targetSc.lastEpochReferencedByCertificate)) {
        int nextEpochSafeguardHeight = targetSc.StartHeightForEpoch(coinEpoch+1) + targetSc.SafeguardMargin();
        if (spendHeight < nextEpochSafeguardHeight)
            return outputMaturity::IMMATURE;
        else
            return outputMaturity::MATURE;
    }

    if (coinEpoch > targetSc.lastEpochReferencedByCertificate) {
        if (isCeasedAtHeight(refCoin.originScId, spendHeight) == CSidechain::state::ALIVE)
            return outputMaturity::IMMATURE;
        else
            return outputMaturity::NOT_APPLICABLE;
    }

    return outputMaturity::NOT_APPLICABLE;
}

bool CCoinsViewCache::HaveJoinSplitRequirements(const CTransactionBase& txBase) const
{
    boost::unordered_map<uint256, ZCIncrementalMerkleTree, CCoinsKeyHasher> intermediates;

    for(const JSDescription &joinsplit: txBase.GetVjoinsplit()) {
        for(const uint256& nullifier: joinsplit.nullifiers) {
            if (GetNullifier(nullifier)) {
                // If the nullifier is set, this transaction
                // double-spends!
                return false;
            }
        }

        ZCIncrementalMerkleTree tree;
        auto it = intermediates.find(joinsplit.anchor);
        if (it != intermediates.end()) {
            tree = it->second;
        } else if (!GetAnchorAt(joinsplit.anchor, tree)) {
            return false;
        }

        for(const uint256& commitment: joinsplit.commitments) {
            tree.append(commitment);
        }

        intermediates.insert(std::make_pair(tree.root(), tree));
    }

    return true;
}

bool CCoinsViewCache::HaveInputs(const CTransactionBase& txBase) const
{
    if (!txBase.IsCoinBase()) {
        for(const CTxIn & in: txBase.GetVin()) {
            const CCoins* coins = AccessCoins(in.prevout.hash);
            if (!coins || !coins->IsAvailable(in.prevout.n)) {
                return false;
            }
        }
    }
    return true;
}

double CCoinsViewCache::GetPriority(const CTransactionBase &tx, int nHeight) const
{
    if (tx.IsCoinBase())
        return 0.0;

    // Joinsplits do not reveal any information about the value or age of a note, so we
    // cannot apply the priority algorithm used for transparent utxos.  Instead, we just
    // use the maximum priority whenever a transaction contains any JoinSplits.
    // (Note that coinbase transactions cannot contain JoinSplits.)
    // FIXME: this logic is partially duplicated between here and CreateNewBlock in miner.cpp.

    if (tx.GetVjoinsplit().size() > 0) {
        return MAXIMUM_PRIORITY;
    }

    if (tx.IsCertificate() ) {
        return MAXIMUM_PRIORITY;
    }

    double dResult = 0.0;
    BOOST_FOREACH(const CTxIn& txin, tx.GetVin())
    {
        const CCoins* coins = AccessCoins(txin.prevout.hash);
        assert(coins);
        if (!coins->IsAvailable(txin.prevout.n)) continue;
        if (coins->nHeight < nHeight) {
            dResult += coins->vout[txin.prevout.n].nValue * (nHeight-coins->nHeight);
        }
    }

    return tx.ComputePriority(dResult);
}

CCoinsModifier::CCoinsModifier(CCoinsViewCache& cache_, CCoinsMap::iterator it_, size_t usage):
        cache(cache_), it(it_), cachedCoinUsage(usage) {
    assert(!cache.hasModifier);
    cache.hasModifier = true;
}

CCoinsModifier::~CCoinsModifier()
{
    assert(cache.hasModifier);
    cache.hasModifier = false;
    it->second.coins.Cleanup();
    cache.cachedCoinsUsage -= cachedCoinUsage; // Subtract the old usage
    if ((it->second.flags & CCoinsCacheEntry::FRESH) && it->second.coins.IsPruned()) {
        cache.cacheCoins.erase(it);
    } else {
        // If the coin still exists after the modification, add the new usage
        cache.cachedCoinsUsage += it->second.coins.DynamicMemoryUsage();
    }
}
