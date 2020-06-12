// Copyright (c) 2012-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "memusage.h"
#include "random.h"
#include "version.h"
#include "policy/fees.h"
#include "sc/proofverifier.h"

#include <assert.h>
#include "utilmoneystr.h"
#include <undo.h>
#include <chainparams.h>

std::string CCoins::ToString() const
{
    std::string ret;
    ret += strprintf("\n version           (%d)", nVersion);
    ret += strprintf("\n fCoinBase         (%d)", fCoinBase);
    ret += strprintf("\n height            (%d)", nHeight);
    ret += strprintf("\n nFirstBwtPos      (%d)", nFirstBwtPos);
    ret += strprintf("\n nBwtMaturityHeight(%d)", nBwtMaturityHeight);
    for (const CTxOut& out : vout)
    {
        ret += "\n    " + out.ToString();
    }
    return ret;
}

CCoins::CCoins() : fCoinBase(false), vout(0), nHeight(0), nVersion(0), nFirstBwtPos(BWT_POS_UNSET), nBwtMaturityHeight(0) { }

CCoins::CCoins(const CTransaction &tx, int nHeightIn) { From(tx, nHeightIn); }

CCoins::CCoins(const CScCertificate &cert, int nHeightIn, int bwtMaturityHeight) { From(cert, nHeightIn, bwtMaturityHeight); }

void CCoins::From(const CTransaction &tx, int nHeightIn) {
    fCoinBase          = tx.IsCoinBase();
    vout               = tx.GetVout();
    nHeight            = nHeightIn;
    nVersion           = tx.nVersion;
    nFirstBwtPos       = BWT_POS_UNSET;
    nBwtMaturityHeight = 0;
    ClearUnspendable();
}

void CCoins::From(const CScCertificate &cert, int nHeightIn, int bwtMaturityHeight) {
    fCoinBase          = cert.IsCoinBase();
    vout               = cert.GetVout();
    nHeight            = nHeightIn;
    nVersion           = cert.nVersion;
    nFirstBwtPos       = cert.nFirstBwtPos;
    nBwtMaturityHeight = bwtMaturityHeight;
    ClearUnspendable();
}

void CCoins::Clear() {
    fCoinBase = false;
    std::vector<CTxOut>().swap(vout);
    nHeight = 0;
    nVersion = 0;
    nFirstBwtPos = BWT_POS_UNSET;
    nBwtMaturityHeight = 0;
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
    std::swap(to.nFirstBwtPos, nFirstBwtPos);
    std::swap(to.nBwtMaturityHeight, nBwtMaturityHeight);
}

bool operator==(const CCoins &a, const CCoins &b) {
     // Empty CCoins objects are always equal.
     if (a.IsPruned() && b.IsPruned())
         return true;
     return a.fCoinBase          == b.fCoinBase          &&
            a.nHeight            == b.nHeight            &&
            a.nVersion           == b.nVersion           &&
            a.vout               == b.vout               &&
            a.nFirstBwtPos       == b.nFirstBwtPos       &&
            a.nBwtMaturityHeight == b.nBwtMaturityHeight;
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

int CCoins::GetMaturityHeightForOutput(unsigned int outPos) const
{
    if (!IsCoinBase() && !IsFromCert())
        return nHeight;

    if (IsCoinBase())
        return nHeight + COINBASE_MATURITY;

    //Hereinafter a cert
    // We check output availability only for certs since spent outputs for other txs 
    // are handled in wallet in their own way
    if(!IsAvailable(outPos))
        return -1; //This may happen in wallet when you check credit on certificate whose bwt has been erased
                   
    if (outPos >= nFirstBwtPos)
        return nBwtMaturityHeight;
    else
        return nHeight;
}

bool CCoins::Spend(uint32_t nPos)
{
    if (nPos >= vout.size() || vout[nPos].IsNull())
        return false;

    vout[nPos].SetNull();
    Cleanup();
    return true;
}

bool CCoins::IsAvailable(unsigned int nPos) const {
    return (nPos < vout.size() && !vout[nPos].IsNull());
}

bool CCoins::IsPruned() const {
    for(const CTxOut &out: vout)
        if (!out.IsNull())
            return false;
    return true;
}

size_t CCoins::DynamicMemoryUsage() const {
    size_t ret = memusage::DynamicUsage(vout);
    for(const CTxOut &out: vout) {
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
bool CCoinsView::HaveSidechainEvents(int height)                               const { return false; }
bool CCoinsView::GetSidechainEvents(int height, CSidechainEvents& scEvent)     const { return false; }
void CCoinsView::GetScIds(std::set<uint256>& scIdsList)                        const { scIdsList.clear(); return; }
uint256 CCoinsView::GetBestBlock()                                             const { return uint256(); }
uint256 CCoinsView::GetBestAnchor()                                            const { return uint256(); };
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                            const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                            CNullifiersMap &mapNullifiers, CSidechainsMap& mapSidechains,
                            CSidechainEventsMap& mapSidechainEvents)                 { return false; }
bool CCoinsView::GetStats(CCoinsStats &stats)                                  const { return false; }


CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) { }

bool CCoinsViewBacked::GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const { return base->GetAnchorAt(rt, tree); }
bool CCoinsViewBacked::GetNullifier(const uint256 &nullifier)                        const { return base->GetNullifier(nullifier); }
bool CCoinsViewBacked::GetCoins(const uint256 &txid, CCoins &coins)                  const { return base->GetCoins(txid, coins); }
bool CCoinsViewBacked::HaveCoins(const uint256 &txid)                                const { return base->HaveCoins(txid); }
bool CCoinsViewBacked::HaveSidechain(const uint256& scId)                            const { return base->HaveSidechain(scId); }
bool CCoinsViewBacked::GetSidechain(const uint256& scId, CSidechain& info)           const { return base->GetSidechain(scId,info); }
bool CCoinsViewBacked::HaveSidechainEvents(int height)                               const { return base->HaveSidechainEvents(height); }
bool CCoinsViewBacked::GetSidechainEvents(int height, CSidechainEvents& scEvents)    const { return base->GetSidechainEvents(height, scEvents); }
void CCoinsViewBacked::GetScIds(std::set<uint256>& scIdsList)                        const { return base->GetScIds(scIdsList); }
uint256 CCoinsViewBacked::GetBestBlock()                                             const { return base->GetBestBlock(); }
uint256 CCoinsViewBacked::GetBestAnchor()                                            const { return base->GetBestAnchor(); }
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                                  const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                                  CNullifiersMap &mapNullifiers, CSidechainsMap& mapSidechains,
                                  CSidechainEventsMap& mapSidechainEvents) { return base->BatchWrite(mapCoins, hashBlock, hashAnchor,
                                                                                          mapAnchors, mapNullifiers, mapSidechains, mapSidechainEvents); }
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
           memusage::DynamicUsage(cacheSidechains) +
           memusage::DynamicUsage(cacheSidechainEvents) +
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

    cachedCoinsUsage += ret->second.scInfo.DynamicMemoryUsage();
    return ret;
}

CSidechainEventsMap::const_iterator CCoinsViewCache::FetchSidechainEvents(int height) const {
    CSidechainEventsMap::iterator candidateIt = cacheSidechainEvents.find(height);
    if (candidateIt != cacheSidechainEvents.end())
        return candidateIt;

    CSidechainEvents tmp;
    if (!base->GetSidechainEvents(height, tmp))
        return cacheSidechainEvents.end();

    //Fill cache and return iterator. The insert in cache below looks cumbersome. However
    //it allows to insert CCeasingSidechains and keep iterator to inserted member without extra searches
    CSidechainEventsMap::iterator ret =
            cacheSidechainEvents.insert(std::make_pair(height, CSidechainEventsCacheEntry(tmp, CSidechainEventsCacheEntry::Flags::DEFAULT ))).first;

    cachedCoinsUsage += ret->second.scEvents.DynamicMemoryUsage();
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
                                 CSidechainEventsMap& mapSidechainEvents) {
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

    for (auto& entryToWrite : mapSidechainEvents) {
        CSidechainEventsMap::iterator itLocalCacheEntry = cacheSidechainEvents.find(entryToWrite.first);

        switch (entryToWrite.second.flag) {
            case CSidechainEventsCacheEntry::Flags::FRESH:
                assert(
                    itLocalCacheEntry == cacheSidechainEvents.end() ||
                    itLocalCacheEntry->second.flag == CSidechainEventsCacheEntry::Flags::ERASED
                ); //A fresh entry should not exist in localCache or be already erased
                cacheSidechainEvents[entryToWrite.first] = entryToWrite.second;
                break;
            case CSidechainEventsCacheEntry::Flags::DIRTY: //A dirty entry may or may not exist in localCache
                cacheSidechainEvents[entryToWrite.first] = entryToWrite.second;
                break;
            case CSidechainEventsCacheEntry::Flags::ERASED:
                if (itLocalCacheEntry != cacheSidechainEvents.end())
                    itLocalCacheEntry->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;
                break;
            case CSidechainEventsCacheEntry::Flags::DEFAULT:
                assert(itLocalCacheEntry != cacheSidechainEvents.end());
                assert(itLocalCacheEntry->second.scEvents == entryToWrite.second.scEvents); //entry declared default is indeed different from backed value
                break; //nothing to do. entry is already persisted and has not been modified
            default:
                assert(false);
        }
    }
    mapSidechainEvents.clear();

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

void CCoinsViewCache::GetScIds(std::set<uint256>& scIdsList) const
{
    base->GetScIds(scIdsList);

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

bool CCoinsViewCache::UpdateScInfo(const CTransaction& tx, const CBlock& block, int blockHeight)
{
    const uint256& txHash = tx.GetHash();
    LogPrint("sc", "%s():%d - enter tx=%s\n", __func__, __LINE__, txHash.ToString() );

    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = blockHeight + SC_COIN_MATURITY;

    // creation ccout
    for (const auto& cr: tx.GetVscCcOut())
    {
        const uint256& scId = cr.GetScId();
        if (HaveSidechain(scId)) {
            LogPrint("sc", "ERROR: %s():%d - CR: scId=%s already in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        assert(cacheSidechains.count(scId) == 0);
        cacheSidechains[scId].scInfo.creationBlockHash = block.GetHash();
        cacheSidechains[scId].scInfo.creationBlockHeight = blockHeight;
        cacheSidechains[scId].scInfo.creationTxHash = txHash;
        cacheSidechains[scId].scInfo.lastEpochReferencedByCertificate = CScCertificate::EPOCH_NULL;
        cacheSidechains[scId].scInfo.lastCertificateHash.SetNull();
        cacheSidechains[scId].scInfo.creationData.withdrawalEpochLength = cr.withdrawalEpochLength;
        cacheSidechains[scId].scInfo.creationData.customData = cr.customData;
        cacheSidechains[scId].scInfo.creationData.constant = cr.constant;
        cacheSidechains[scId].scInfo.creationData.wCertVk = cr.wCertVk;
        cacheSidechains[scId].scInfo.mImmatureAmounts[maturityHeight] = cr.nValue;
        cacheSidechains[scId].flag = CSidechainsCacheEntry::Flags::FRESH;

        LogPrint("sc", "%s():%d - immature balance added in scView (h=%d, amount=%s) %s\n",
            __func__, __LINE__, maturityHeight, FormatMoney(cr.nValue), scId.ToString());

        LogPrint("sc", "%s():%d - scId[%s] added in scView\n", __func__, __LINE__, scId.ToString() );
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

bool CCoinsViewCache::RevertTxOutputs(const CTransaction& tx, int maturityHeight, std::set<uint256>* sScIds)
{
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
        if (sScIds)
            sScIds->insert(scId);

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
        const uint256& scId = entry.GetScId();

        LogPrint("sc", "%s():%d - removing scId=%s\n", __func__, __LINE__, scId.ToString());

        CSidechain targetScInfo;
        if (!GetSidechain(scId, targetScInfo))
        {
            // should not happen
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }
        if (sScIds)
            sScIds->insert(scId);

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

#ifdef BITCOIN_TX
int CSidechain::EpochFor(int targetHeight) const { return CScCertificate::EPOCH_NULL; }
int CSidechain::StartHeightForEpoch(int targetEpoch) const { return -1; }
int CSidechain::SafeguardMargin() const { return -1; }
size_t CSidechain::DynamicMemoryUsage() const { return 0; }
bool CCoinsViewCache::isEpochDataValid(const CSidechain& info, int epochNumber, const uint256& endEpochBlockHash) {return true;}
bool CCoinsViewCache::IsCertApplicableToState(const CScCertificate& cert, int nHeight, CValidationState& state, libzendoomc::CScProofVerifier& scVerifier) {return true;}
bool libzendoomc::CScProofVerifier::verifyCScCertificate(              
    const libzendoomc::ScConstant& constant,
    const libzendoomc::ScVk& wCertVk,
    const uint256& prev_end_epoch_block_hash,
    const CScCertificate& scCert
) const { return true; }
bool CCoinsViewCache::HaveScRequirements(const CTransaction& tx, int height) { return true;}
size_t CSidechainEvents::DynamicMemoryUsage() const { return 0;}

#else

#include "consensus/validation.h"
#include "main.h"
bool CCoinsViewCache::IsCertApplicableToState(const CScCertificate& cert, int nHeight, CValidationState& state, libzendoomc::CScProofVerifier& scVerifier)
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
    if (!isEpochDataValid(scInfo, cert.epochNumber, cert.endEpochBlockHash) )
    {
        LogPrint("sc", "%s():%d - invalid cert[%s], scId[%s] invalid epoch data\n",
            __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString() );
        return state.Invalid(error("certificate with invalid epoch considering mempool"),
             REJECT_INVALID, "sidechain-certificate-epoch");
    }

    if (isCeasedAtHeight(cert.GetScId(), nHeight)!= CSidechain::State::ALIVE) {
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

    // Retrieve previous end epoch block hash for certificate proof verification
    int targetHeight = scInfo.StartHeightForEpoch(cert.epochNumber) - 1;
    uint256 prev_end_epoch_block_hash = chainActive[targetHeight] -> GetBlockHash();

    // Verify certificate proof
    if (!scVerifier.verifyCScCertificate(scInfo.creationData.constant, scInfo.creationData.wCertVk, prev_end_epoch_block_hash, cert)){
        LogPrintf("ERROR: certificate[%s] cannot be accepted for sidechain [%s]: proof verification failed\n",
            certHash.ToString(), cert.GetScId().ToString());
        return state.Invalid(error("proof not verified"),
                     REJECT_INVALID, "sidechain-certificate-proof-not-verified");
    }

    return true;
}

bool CCoinsViewCache::isEpochDataValid(const CSidechain& scInfo, int epochNumber, const uint256& endEpochBlockHash)
{
    if (epochNumber < 0 || endEpochBlockHash.IsNull())
    {
        LogPrint("sc", "%s():%d - invalid epoch data %d/%s\n", __func__, __LINE__, epochNumber, endEpochBlockHash.ToString() );
        return false;
    }

    // 1. the epoch number must be consistent with the sc certificate history (no old epoch allowed)
    if (epochNumber != scInfo.lastEpochReferencedByCertificate+1)
    {
        LogPrint("sc", "%s():%d - can not receive a certificate for epoch %d (expected: %d)\n",
            __func__, __LINE__, epochNumber, scInfo.lastEpochReferencedByCertificate+1 );
        return false;
    }

    // 2. the referenced end-epoch block must be in active chain
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

    // 3. combination of epoch number and epoch length, specified in sc info, must point to that end-epoch block
    int endEpochHeight = scInfo.StartHeightForEpoch(epochNumber+1) -1;
    pblockindex = chainActive[endEpochHeight];

    if (!pblockindex)
    {
        LogPrint("sc", "%s():%d - calculated height %d (createHeight=%d/epochNum=%d/epochLen=%d) is out of active chain\n",
            __func__, __LINE__, endEpochHeight, scInfo.creationBlockHeight, epochNumber, scInfo.creationData.withdrawalEpochLength);
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
        const uint256& scId = sc.GetScId();
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
            if (isCeasedAtHeight(scId, height)!= CSidechain::State::ALIVE) {
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

bool CCoinsViewCache::UpdateScInfo(const CScCertificate& cert, CTxUndo& certUndoEntry)
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

    certUndoEntry.replacedLastCertEpoch = targetScInfo.lastEpochReferencedByCertificate;
    certUndoEntry.replacedLastCertHash = targetScInfo.lastCertificateHash;

    targetScInfo.balance -= totalAmount;
    targetScInfo.lastEpochReferencedByCertificate = cert.epochNumber;
    targetScInfo.lastCertificateHash = certHash;
    cacheSidechains[scId] = CSidechainsCacheEntry(targetScInfo, CSidechainsCacheEntry::Flags::DIRTY);

    LogPrint("cert", "%s():%d - amount removed from scView (amount=%s, resulting bal=%s) %s\n",
        __func__, __LINE__, FormatMoney(totalAmount), FormatMoney(targetScInfo.balance), scId.ToString());

    return true;
}

bool CCoinsViewCache::RevertCertOutputs(const CScCertificate& cert, const CTxUndo &certUndoEntry)
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
    targetScInfo.lastEpochReferencedByCertificate = certUndoEntry.replacedLastCertEpoch;
    targetScInfo.lastCertificateHash = certUndoEntry.replacedLastCertHash;
    cacheSidechains.at(scId).flag = CSidechainsCacheEntry::Flags::DIRTY;

    cacheSidechains[scId] = CSidechainsCacheEntry(targetScInfo, CSidechainsCacheEntry::Flags::DIRTY);

    LogPrint("cert", "%s():%d - amount restored to scView (amount=%s, resulting bal=%s) %s\n",
        __func__, __LINE__, FormatMoney(totalAmount), FormatMoney(targetScInfo.balance), scId.ToString());

    return true;
}

bool CCoinsViewCache::HaveSidechainEvents(int height) const
{
    CSidechainEventsMap::const_iterator it = FetchSidechainEvents(height);
    return (it != cacheSidechainEvents.end()) && (it->second.flag != CSidechainEventsCacheEntry::Flags::ERASED);
}

bool CCoinsViewCache::GetSidechainEvents(int height, CSidechainEvents& scEvents) const
{
    CSidechainEventsMap::const_iterator it = FetchSidechainEvents(height);
    if (it != cacheSidechainEvents.end() && it->second.flag != CSidechainEventsCacheEntry::Flags::ERASED) {
        scEvents = it->second.scEvents;
        return true;
    }
    return false;
}

bool CCoinsViewCache::ScheduleSidechainEvent(const CTxScCreationOut& scCreationOut, int creationHeight)
{
    CSidechain scInfo;
    if (!this->GetSidechain(scCreationOut.GetScId(), scInfo)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: attempt schedule maturing scCreation for unknown scId\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString());
        return false;
    }

    // Schedule maturing amount
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = creationHeight + SC_COIN_MATURITY;

    if (!HaveSidechainEvents(maturityHeight)) {
        cacheSidechainEvents[maturityHeight].scEvents.maturingScs.insert(scCreationOut.GetScId());
        cacheSidechainEvents[maturityHeight].flag = CSidechainEventsCacheEntry::Flags::FRESH;
    } else {
        cacheSidechainEvents[maturityHeight].scEvents.maturingScs.insert(scCreationOut.GetScId());
        cacheSidechainEvents[maturityHeight].flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    }

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: scCreation next maturing height [%d]\n",
             __func__, __LINE__, scCreationOut.GetScId().ToString(), maturityHeight);

    // Schedule Ceasing Sidechains
    int nextCeasingHeight = scInfo.StartHeightForEpoch(1) + scInfo.SafeguardMargin() +1;

    if (!HaveSidechainEvents(nextCeasingHeight)) {
        cacheSidechainEvents[nextCeasingHeight].scEvents.ceasingScs.insert(scCreationOut.GetScId());
        cacheSidechainEvents[nextCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::FRESH;
    } else {
        cacheSidechainEvents[nextCeasingHeight].scEvents.ceasingScs.insert(scCreationOut.GetScId());
        cacheSidechainEvents[nextCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    }

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: scCreation next ceasing height [%d]\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString(), nextCeasingHeight);

    return true;
}

bool CCoinsViewCache::ScheduleSidechainEvent(const CTxForwardTransferOut& forwardOut, int fwdHeight)
{
    CSidechain scInfo;
    if (!this->GetSidechain(forwardOut.GetScId(), scInfo)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: attempt to undo schedule maturing fwd for unknown scId[%s]\n",
            __func__, __LINE__, forwardOut.scId.ToString());
        return false;
    }

    // Schedule maturing amount
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = fwdHeight + SC_COIN_MATURITY;

    if (!HaveSidechainEvents(maturityHeight)) {
        cacheSidechainEvents[maturityHeight].scEvents.maturingScs.insert(forwardOut.GetScId());
        cacheSidechainEvents[maturityHeight].flag = CSidechainEventsCacheEntry::Flags::FRESH;
    } else {
        cacheSidechainEvents[maturityHeight].scEvents.maturingScs.insert(forwardOut.GetScId());
        cacheSidechainEvents[maturityHeight].flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    }

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: fwd Transfer next maturing height [%d]\n",
             __func__, __LINE__, forwardOut.scId.ToString(), maturityHeight);

    return true;
}

bool CCoinsViewCache::ScheduleSidechainEvent(const CScCertificate& cert)
{
    CSidechain scInfo;
    if (!this->GetSidechain(cert.GetScId(), scInfo)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT:: attempt to update ceasing sidechain map with cert to unknown scId[%s]\n",
            __func__, __LINE__, cert.GetScId().ToString());
        return false;
    }

    int curCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+1) + scInfo.SafeguardMargin()+1;
    int nextCeasingHeight = curCeasingHeight + scInfo.creationData.withdrawalEpochLength;

    //clear up current ceasing height, if any
    if (HaveSidechainEvents(curCeasingHeight))
    {
        cacheSidechainEvents[curCeasingHeight].scEvents.ceasingScs.erase(cert.GetScId());
        if (!cacheSidechainEvents[curCeasingHeight].scEvents.IsNull()) //still other sc ceasing at that height or fwds maturing
            cacheSidechainEvents[curCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        else
            cacheSidechainEvents[curCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::ERASED;

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: cert [%s] removes prevCeasingHeight [%d]\n",
                __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), curCeasingHeight);
    } else {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: cert [%s] finds not prevCeasingHeight [%d] to remove\n",
                __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), curCeasingHeight);
        // we always must have previous ceasing height record (at list one for current cert sc id).
        return false;
    }

    //add next ceasing Height
    if (!HaveSidechainEvents(nextCeasingHeight)) {
        cacheSidechainEvents[nextCeasingHeight].scEvents.ceasingScs.insert(cert.GetScId());
        cacheSidechainEvents[nextCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::FRESH;
    } else {
        cacheSidechainEvents[nextCeasingHeight].scEvents.ceasingScs.insert(cert.GetScId());
        cacheSidechainEvents[nextCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    }

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: cert [%s] sets nextCeasingHeight to [%d]\n",
            __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), nextCeasingHeight);

    return true;
}

bool CCoinsViewCache::CancelSidechainEvent(const CTxScCreationOut& scCreationOut, int creationHeight)
{
    CSidechain restoredScInfo;
    if (!this->GetSidechain(scCreationOut.GetScId(), restoredScInfo)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: attempt to undo ScCreation amount maturing for unknown scId[%s]\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString());
        return false;
    }

    // Cancel maturing amount
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = creationHeight + SC_COIN_MATURITY;

    if (HaveSidechainEvents(maturityHeight))
    {
        cacheSidechainEvents[maturityHeight].scEvents.maturingScs.erase(scCreationOut.GetScId());
        if (!cacheSidechainEvents[maturityHeight].scEvents.IsNull()) //still other sc ceasing at that height or fwds
            cacheSidechainEvents[maturityHeight].flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        else
            cacheSidechainEvents[maturityHeight].flag = CSidechainEventsCacheEntry::Flags::ERASED;

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] deleted maturing height [%d] for creation amount.\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString(), maturityHeight);
    } else
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] nothing to do for scCreation amount maturing canceling at height [%d].\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString(), maturityHeight);


    //remove current ceasing Height
    int currentCeasingHeight = restoredScInfo.StartHeightForEpoch(1) + restoredScInfo.SafeguardMargin() +1;

    // Cancel Ceasing Sidechains
    if (!HaveSidechainEvents(currentCeasingHeight)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] misses current ceasing height; expected value was [%d]\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString(), currentCeasingHeight);
        return false;
    } else
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] maturing height [%d] already deleted. This may happen in case of concurrent fwd\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString(), maturityHeight);


    cacheSidechainEvents[currentCeasingHeight].scEvents.ceasingScs.erase(scCreationOut.GetScId());
    if (!cacheSidechainEvents[currentCeasingHeight].scEvents.IsNull())
        cacheSidechainEvents[currentCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    else
        cacheSidechainEvents[currentCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::ERASED;

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: undo of creation removes currentCeasingHeight [%d]\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString(), currentCeasingHeight);

    return true;
}

bool CCoinsViewCache::CancelSidechainEvent(const CTxForwardTransferOut& forwardOut, int fwdHeight)
{
    // Cancel maturing amount
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = fwdHeight + SC_COIN_MATURITY;

    if (!HaveSidechainEvents(maturityHeight)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] maturing height [%d] already deleted. This may happen in case of concurrent fwd\n",
            __func__, __LINE__, forwardOut.scId.ToString(), maturityHeight);
        return true;
    }

    cacheSidechainEvents[maturityHeight].scEvents.maturingScs.erase(forwardOut.scId);
    if (!cacheSidechainEvents[maturityHeight].scEvents.IsNull()) //still other sc ceasing at that height or fwds
        cacheSidechainEvents[maturityHeight].flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    else
        cacheSidechainEvents[maturityHeight].flag = CSidechainEventsCacheEntry::Flags::ERASED;

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] cancelled maturing height [%s] for fwd amount.\n",
        __func__, __LINE__, forwardOut.scId.ToString(), maturityHeight);

    return true;
}

bool CCoinsViewCache::CancelSidechainEvent(const CScCertificate& cert)
{
    CSidechain restoredScInfo;
    if (!this->GetSidechain(cert.GetScId(), restoredScInfo)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT:: attempt to undo ceasing sidechain map with cert to unknown scId[%s]\n",
            __func__, __LINE__, cert.GetScId().ToString());
        return false;
    }

    int currentCeasingHeight = restoredScInfo.StartHeightForEpoch(cert.epochNumber+2) + restoredScInfo.SafeguardMargin()+1;
    int restoredCeasingHeight = currentCeasingHeight - restoredScInfo.creationData.withdrawalEpochLength;

    //remove current ceasing Height
    if (!HaveSidechainEvents(currentCeasingHeight)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] misses current ceasing height; expected value was [%d]\n",
            __func__, __LINE__, cert.GetScId().ToString(), currentCeasingHeight);
        return false;
    }

    cacheSidechainEvents[currentCeasingHeight].scEvents.ceasingScs.erase(cert.GetScId());
    if (!cacheSidechainEvents[currentCeasingHeight].scEvents.IsNull()) //still other sc ceasing at that height or fwds
        cacheSidechainEvents[currentCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    else
        cacheSidechainEvents[currentCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::ERASED;

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT:: scId[%s]: undo of cert [%s] removes currentCeasingHeight [%d]\n",
            __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), currentCeasingHeight);

    //restore previous ceasing Height
    if (!HaveSidechainEvents(restoredCeasingHeight)) {
        cacheSidechainEvents[restoredCeasingHeight].scEvents.ceasingScs.insert(cert.GetScId());
        cacheSidechainEvents[restoredCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::FRESH;
    } else {
        cacheSidechainEvents[restoredCeasingHeight].scEvents.ceasingScs.insert(cert.GetScId());
        cacheSidechainEvents[restoredCeasingHeight].flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    }

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: undo of cert [%s] set nextCeasingHeight to [%d]\n",
            __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), restoredCeasingHeight);

    return true;
}

bool CCoinsViewCache::HandleSidechainEvents(int height, CBlockUndo& blockUndo, std::vector<uint256>* pVoidedCertsList)
{
    if (!HaveSidechainEvents(height))
        return true;

    CSidechainEvents scEvents;
    GetSidechainEvents(height, scEvents);

    //Handle Maturing amounts
    for (const uint256& maturingScId : scEvents.maturingScs)
    {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: about to mature scId[%s] amount at height [%d]\n",
                __func__, __LINE__, maturingScId.ToString(), height);

        assert(HaveSidechain(maturingScId));
        assert(cacheSidechains.at(maturingScId).scInfo.mImmatureAmounts.count(height));

        cacheSidechains.at(maturingScId).scInfo.balance += cacheSidechains.at(maturingScId).scInfo.mImmatureAmounts[height];
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId=%s balance updated to: %s\n",
            __func__, __LINE__, maturingScId.ToString(), FormatMoney(cacheSidechains.at(maturingScId).scInfo.balance));

        blockUndo.scUndoMap[maturingScId].appliedMaturedAmount = cacheSidechains.at(maturingScId).scInfo.mImmatureAmounts[height];
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: adding immature amount %s for scId=%s in blockundo\n",
            __func__, __LINE__, FormatMoney(cacheSidechains.at(maturingScId).scInfo.mImmatureAmounts[height]), maturingScId.ToString());

        cacheSidechains.at(maturingScId).scInfo.mImmatureAmounts.erase(height);
        cacheSidechains.at(maturingScId).flag = CSidechainsCacheEntry::Flags::DIRTY;
    }

    //Handle Ceasing Sidechain
    for (const uint256& ceasingScId : scEvents.ceasingScs)
    {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: about to handle scId[%s] and ceasingHeight [%d]\n",
                __func__, __LINE__, ceasingScId.ToString(), height);

        CSidechain scInfo;
        assert(GetSidechain(ceasingScId, scInfo));

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: lastCertEpoch [%d], lastCertHash [%s]\n",
                __func__, __LINE__, scInfo.lastEpochReferencedByCertificate, scInfo.lastCertificateHash.ToString());

        if (scInfo.lastEpochReferencedByCertificate == CScCertificate::EPOCH_NULL) {
            assert(scInfo.lastCertificateHash.IsNull());
            continue;
        }

        if (!this->HaveCoins(scInfo.lastCertificateHash))
            continue; //in case the cert had not bwt nor change, there won't be any coin generated by cert. Nothing to handle

        CCoinsModifier coins = this->ModifyCoins(scInfo.lastCertificateHash);
        assert(coins->nBwtMaturityHeight != 0);

        //null all bwt outputs and add related txundo in block
        bool foundFirstBwt = false;

        for(int pos = coins->nFirstBwtPos; pos < coins->vout.size(); ++pos)
        {
            if (!foundFirstBwt)
            {
                blockUndo.vVoidedCertUndo.push_back(CVoidedCertUndo());
                blockUndo.vVoidedCertUndo.back().voidedCertHash = scInfo.lastCertificateHash;
                LogPrint("sc", "%s():%d - set voidedCertHash[%s], firstBwtPos[%d]\n",
                    __func__, __LINE__, scInfo.lastCertificateHash.ToString(), pos);
                foundFirstBwt = true;
            }

            blockUndo.vVoidedCertUndo.back().voidedOuts.push_back(CTxInUndo(coins->vout.at(pos)));
            coins->Spend(pos);
            if (coins->vout.size() == 0)
            {
                CTxInUndo& undo = blockUndo.vVoidedCertUndo.back().voidedOuts.back();
                undo.nHeight            = coins->nHeight;
                undo.fCoinBase          = coins->fCoinBase;
                undo.nVersion           = coins->nVersion;
                undo.nFirstBwtPos       = coins->nFirstBwtPos;
                undo.nBwtMaturityHeight = coins->nBwtMaturityHeight;
            }
        }

        if (pVoidedCertsList != nullptr)
            pVoidedCertsList->push_back(scInfo.lastCertificateHash);
    }

    return true;
}

bool CCoinsViewCache::RevertSidechainEvents(const CBlockUndo& blockUndo, int height, std::vector<uint256>* pVoidedCertsList)
{
    // Reverting amount maturing
    for (std::map<uint256, ScUndoData>::const_iterator it = blockUndo.scUndoMap.begin(); it != blockUndo.scUndoMap.end(); ++it)
    {
        const uint256& scId = it->first;
        const std::string& scIdString = scId.ToString();

        if (!HaveSidechain(scId))
        {
            // should not happen
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        CAmount amountToRestore = it->second.appliedMaturedAmount;
        if (amountToRestore > 0)
        {
            CSidechain& targetScInfo = cacheSidechains.at(scId).scInfo;

            LogPrint("sc", "%s():%d - adding immature amount %s into sc view for scId=%s\n",
                __func__, __LINE__, FormatMoney(amountToRestore), scIdString);

            if (targetScInfo.balance < amountToRestore)
            {
                LogPrint("sc", "%s():%d - Can not update balance with amount[%s] for scId=%s, would be negative\n",
                    __func__, __LINE__, FormatMoney(amountToRestore), scId.ToString() );
                return false;
            }

            targetScInfo.mImmatureAmounts[height] += amountToRestore;

            LogPrint("sc", "%s():%d - scId=%s balance before: %s\n", __func__, __LINE__, scIdString, FormatMoney(targetScInfo.balance));
            targetScInfo.balance -= amountToRestore;
            LogPrint("sc", "%s():%d - scId=%s balance after: %s\n", __func__, __LINE__, scIdString, FormatMoney(targetScInfo.balance));

            cacheSidechains.at(scId).flag = CSidechainsCacheEntry::Flags::DIRTY;
        }
    }

    // Reverting ceasing sidechains
    for(const CVoidedCertUndo& voidedCertUndo: blockUndo.vVoidedCertUndo)
    {
        bool fClean = true;

        const uint256& coinHash = voidedCertUndo.voidedCertHash;
        if(coinHash.IsNull())
        {
            fClean = fClean && error("%s: malformed undo data, missing voided certificate hash ", __func__);
            return fClean;
        }
        LogPrint("sc", "%s():%d - reverting voiding of bwt for certificate [%s]\n", __func__, __LINE__, coinHash.ToString());

        CCoinsModifier coins = this->ModifyCoins(coinHash);
        const std::vector<CTxInUndo>& voidedOuts = voidedCertUndo.voidedOuts;
        for (size_t idx = voidedOuts.size(); idx-- > 0;)
        {
            if (voidedOuts.at(idx).nHeight != 0)
            {
                coins->fCoinBase          = voidedOuts.at(idx).fCoinBase;
                coins->nHeight            = voidedOuts.at(idx).nHeight;
                coins->nVersion           = voidedOuts.at(idx).nVersion;
                coins->nFirstBwtPos       = voidedOuts.at(idx).nFirstBwtPos;
                coins->nBwtMaturityHeight = voidedOuts.at(idx).nBwtMaturityHeight;
            } else
            {
                if (coins->IsPruned())
                    fClean = fClean && error("%s: undo data adding output to missing transaction", __func__);
            }

            if(coins->IsAvailable(coins->nFirstBwtPos + idx))
                fClean = fClean && error("%s: undo data overwriting existing output", __func__);
            if (coins->vout.size() < (coins->nFirstBwtPos + idx+1))
                coins->vout.resize(coins->nFirstBwtPos + idx+1);
            coins->vout.at(coins->nFirstBwtPos + idx) = voidedOuts.at(idx).txout;
        }

        if (pVoidedCertsList != nullptr)
            pVoidedCertsList->push_back(voidedCertUndo.voidedCertHash);

        if (!fClean) return false;
    }

    return true;
}

CSidechain::State CCoinsViewCache::isCeasedAtHeight(const uint256& scId, int height) const
{
    if (!HaveSidechain(scId))
        return CSidechain::State::NOT_APPLICABLE;

    CSidechain scInfo;
    GetSidechain(scId, scInfo);

    if (height < scInfo.creationBlockHeight)
        return CSidechain::State::NOT_APPLICABLE;

    int currentEpoch = scInfo.EpochFor(height);

    if (currentEpoch > scInfo.lastEpochReferencedByCertificate + 2)
        return CSidechain::State::CEASED;

    if (currentEpoch == scInfo.lastEpochReferencedByCertificate + 2)
    {
        int targetEpochSafeguardHeight = scInfo.StartHeightForEpoch(currentEpoch) + scInfo.SafeguardMargin();
        if (height > targetEpochSafeguardHeight)
            return CSidechain::State::CEASED;
    }

    return  CSidechain::State::ALIVE;
}

bool CCoinsViewCache::Flush() {
    bool fOk = base->BatchWrite(cacheCoins, hashBlock, hashAnchor, cacheAnchors, cacheNullifiers, cacheSidechains, cacheSidechainEvents);
    cacheCoins.clear();
    cacheSidechains.clear();
    cacheSidechainEvents.clear();
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
    GetScIds(scIdsList);
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
        LogPrint("sc", "      constant[%s]\n", HexStr(info.creationData.constant));
        LogPrint("sc", "      wCertVk[%s]\n", HexStr(info.creationData.wCertVk));
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
