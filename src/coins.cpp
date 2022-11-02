// Copyright (c) 2012-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "random.h"
#include "version.h"
#include "policy/fees.h"

#include <assert.h>
#include "utilmoneystr.h"
#include <undo.h>
#include <chainparams.h>

#include <main.h>
#include <consensus/validation.h>

#include <sc/proofverifier.h>

#include "txdb.h"
#include "maturityheightindex.h"

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

CCoins::CCoins(const CScCertificate &cert, int nHeightIn, int bwtMaturityHeight, bool isBlockTopQualityCert)
{
    From(cert, nHeightIn, bwtMaturityHeight, isBlockTopQualityCert);
}

void CCoins::From(const CTransaction &tx, int nHeightIn) {
    fCoinBase          = tx.IsCoinBase();
    vout               = tx.GetVout();
    nHeight            = nHeightIn;
    nVersion           = tx.nVersion;
    nFirstBwtPos       = BWT_POS_UNSET;
    nBwtMaturityHeight = 0;
    ClearUnspendable();
}

void CCoins::From(const CScCertificate &cert, int nHeightIn, int bwtMaturityHeight, bool isBlockTopQualityCert) {
    fCoinBase          = cert.IsCoinBase();
    vout               = cert.GetVout();
    nHeight            = nHeightIn;
    nVersion           = cert.nVersion;
    nFirstBwtPos       = cert.nFirstBwtPos;
    nBwtMaturityHeight = bwtMaturityHeight;

    if (!isBlockTopQualityCert) //drop bwts of low q certs
    {
        for(unsigned int bwtPos = nFirstBwtPos; bwtPos < vout.size(); ++bwtPos)
            Spend(bwtPos);
    }

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

bool CCoins::isOutputMature(unsigned int outPos, int nSpendingHeigh) const
{
    if (!IsCoinBase() && !IsFromCert())
        return true;

    if (IsCoinBase())
        return nSpendingHeigh >= (nHeight + COINBASE_MATURITY);

    //Hereinafter a cert
    if (outPos >= nFirstBwtPos)
        return nSpendingHeigh >= nBwtMaturityHeight;
    else
        return true;
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

bool CCoinsView::GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree)  const { return false; }
bool CCoinsView::GetNullifier(const uint256 &nullifier)                         const { return false; }
bool CCoinsView::GetCoins(const uint256 &txid, CCoins &coins)                   const { return false; }
bool CCoinsView::HaveCoins(const uint256 &txid)                                 const { return false; }
bool CCoinsView::HaveSidechain(const uint256& scId)                             const { return false; }
bool CCoinsView::GetSidechain(const uint256& scId, CSidechain& info)            const { return false; }
bool CCoinsView::HaveSidechainEvents(int height)                                const { return false; }
bool CCoinsView::GetSidechainEvents(int height, CSidechainEvents& scEvent)      const { return false; }
void CCoinsView::GetScIds(std::set<uint256>& scIdsList)                         const { scIdsList.clear(); return; }
bool CCoinsView::CheckQuality(const CScCertificate& cert)                       const { return false; }
uint256 CCoinsView::GetBestBlock()                                              const { return uint256(); }
uint256 CCoinsView::GetBestAnchor()                                             const { return uint256(); }
bool CCoinsView::HaveCswNullifier(const uint256& scId,
                                  const CFieldElement &nullifier)               const { return false; }

bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                            const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                            CNullifiersMap &mapNullifiers, CSidechainsMap& mapSidechains,
                            CSidechainEventsMap& mapSidechainEvents,
                            CCswNullifiersMap& cswNullifiers)                         { return false; }
bool CCoinsView::GetStats(CCoinsStats &stats)                                   const { return false; }


CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) { }

bool CCoinsViewBacked::GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree)   const { return base->GetAnchorAt(rt, tree); }
bool CCoinsViewBacked::GetNullifier(const uint256 &nullifier)                          const { return base->GetNullifier(nullifier); }
bool CCoinsViewBacked::GetCoins(const uint256 &txid, CCoins &coins)                    const { return base->GetCoins(txid, coins); }
bool CCoinsViewBacked::HaveCoins(const uint256 &txid)                                  const { return base->HaveCoins(txid); }
bool CCoinsViewBacked::HaveSidechain(const uint256& scId)                              const { return base->HaveSidechain(scId); }
bool CCoinsViewBacked::GetSidechain(const uint256& scId, CSidechain& info)             const { return base->GetSidechain(scId, info); }
bool CCoinsViewBacked::HaveSidechainEvents(int height)                                 const { return base->HaveSidechainEvents(height); }
bool CCoinsViewBacked::GetSidechainEvents(int height, CSidechainEvents& scEvents)      const { return base->GetSidechainEvents(height, scEvents); }
void CCoinsViewBacked::GetScIds(std::set<uint256>& scIdsList)                          const { return base->GetScIds(scIdsList); }
bool CCoinsViewBacked::CheckQuality(const CScCertificate& cert)                        const { return base->CheckQuality(cert); }
uint256 CCoinsViewBacked::GetBestBlock()                                               const { return base->GetBestBlock(); }
uint256 CCoinsViewBacked::GetBestAnchor()                                              const { return base->GetBestAnchor(); }

bool CCoinsViewBacked::HaveCswNullifier(const uint256& scId,
                                        const CFieldElement &nullifier)                const { return base->HaveCswNullifier(scId, nullifier); }

void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                                  const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                                  CNullifiersMap &mapNullifiers, CSidechainsMap& mapSidechains,
                                  CSidechainEventsMap& mapSidechainEvents,
                                  CCswNullifiersMap& cswNullifiers) { return base->BatchWrite(mapCoins, hashBlock, hashAnchor,
                                                                                              mapAnchors, mapNullifiers, mapSidechains,
                                                                                              mapSidechainEvents, cswNullifiers); }
bool CCoinsViewBacked::GetStats(CCoinsStats &stats)                                  const { return base->GetStats(stats); }

CCoinsKeyHasher::CCoinsKeyHasher() : salt(GetRandHash()) {}
CCswNullifiersKeyHasher::CCswNullifiersKeyHasher() : salt() {GetRandBytes(reinterpret_cast<unsigned char*>(salt), BUF_LEN);}

size_t CCswNullifiersKeyHasher::operator()(const std::pair<uint256, CFieldElement>& key) const {
    uint32_t buf[BUF_LEN];

    // nullifiers are already checked by the caller, but let's assert it too
    assert(!key.second.IsNull());

    // note: we may consider buf as a raw data, so bytes size of buf is (BUF_LEN * 4)
    memcpy(buf, key.first.begin(), sizeof(uint256));
    memcpy((buf + sizeof(uint256)/sizeof(uint32_t)), &(key.second.GetByteArray()[0]), CFieldElement::ByteSize());
    return CalculateHash(buf, BUF_LEN, salt);
}

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
           memusage::DynamicUsage(cacheCswNullifiers) +
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

    cachedCoinsUsage += ret->second.sidechain.DynamicMemoryUsage();
    return ret;
}

CSidechainsMap::iterator CCoinsViewCache::ModifySidechain(const uint256& scId) {
    CSidechainsMap::iterator candidateIt = cacheSidechains.find(scId);
    if (candidateIt != cacheSidechains.end())
        return candidateIt;

    CSidechainsMap::iterator ret = cacheSidechains.end();
    CSidechain tmp;
    if (base->GetSidechain(scId, tmp))
        ret = cacheSidechains.insert(std::make_pair(scId, CSidechainsCacheEntry(tmp, CSidechainsCacheEntry::Flags::DEFAULT ))).first;
    else
        ret = cacheSidechains.insert(std::make_pair(scId, CSidechainsCacheEntry(tmp, CSidechainsCacheEntry::Flags::FRESH ))).first;

    cachedCoinsUsage += ret->second.sidechain.DynamicMemoryUsage();
    return ret;
}

const CSidechain* const CCoinsViewCache::AccessSidechain(const uint256& scId) const {
    CSidechainsMap::const_iterator it = FetchSidechains(scId);
    if (it == cacheSidechains.end())
        return nullptr;
    else
        return &it->second.sidechain;
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

CSidechainEventsMap::iterator CCoinsViewCache::ModifySidechainEvents(int height)
{
    CSidechainEventsMap::iterator candidateIt = cacheSidechainEvents.find(height);
    if (candidateIt != cacheSidechainEvents.end())
        return candidateIt;

    CSidechainEventsMap::iterator ret = cacheSidechainEvents.end();
    CSidechainEvents tmp;
    if (!base->GetSidechainEvents(height, tmp))
        ret = cacheSidechainEvents.insert(std::make_pair(height, CSidechainEventsCacheEntry(tmp, CSidechainEventsCacheEntry::Flags::FRESH ))).first;
    else
        ret = cacheSidechainEvents.insert(std::make_pair(height, CSidechainEventsCacheEntry(tmp, CSidechainEventsCacheEntry::Flags::DEFAULT ))).first;

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

bool CCoinsViewCache::HaveCswNullifier(const uint256& scId, const CFieldElement &nullifier) const {
    std::pair<uint256, CFieldElement> key = std::make_pair(scId, nullifier);

    CCswNullifiersMap::iterator it = cacheCswNullifiers.find(key);
    if (it != cacheCswNullifiers.end())
        return (it->second.flag != CCswNullifiersCacheEntry::Flags::ERASED);

    if (!base->HaveCswNullifier(scId, nullifier))
        return false;

    cacheCswNullifiers.insert(std::make_pair(key, CCswNullifiersCacheEntry{CCswNullifiersCacheEntry::Flags::DEFAULT}));
    return true;
}

bool CCoinsViewCache::AddCswNullifier(const uint256& scId, const CFieldElement &nullifier) {
    if (HaveCswNullifier(scId, nullifier))
        return false;

    std::pair<uint256, CFieldElement> key = std::make_pair(scId, nullifier);
    cacheCswNullifiers.insert(std::make_pair(key, CCswNullifiersCacheEntry{CCswNullifiersCacheEntry::Flags::FRESH}));
    return true;
}

bool CCoinsViewCache::RemoveCswNullifier(const uint256& scId, const CFieldElement &nullifier) {
    if (!HaveCswNullifier(scId, nullifier))
        return false;

    cacheCswNullifiers.at(std::make_pair(scId, nullifier)).flag = CCswNullifiersCacheEntry::Flags::ERASED;
    return true;
}

size_t CCoinsViewCache::WriteCoins(const uint256& key, CCoinsCacheEntry& value)
{
    size_t res = 0;
    if (value.flags & CCoinsCacheEntry::DIRTY)
    { // Ignore non-dirty entries (optimization).
        CCoinsMap::iterator itUs = this->cacheCoins.find(key);
        if (itUs == this->cacheCoins.end())
        {
            if (!value.coins.IsPruned())
            {
                    // The parent cache does not have an entry, while the child
                    // cache does have (a non-pruned) one. Move the data up, and
                    // mark it as fresh (if the grandparent did have it, we
                    // would have pulled it in at first GetCoins).
                assert(value.flags & CCoinsCacheEntry::FRESH);
                CCoinsCacheEntry& entry = this->cacheCoins[key];
                entry.coins.swap(value.coins);
                res += entry.coins.DynamicMemoryUsage();
                    entry.flags = CCoinsCacheEntry::DIRTY | CCoinsCacheEntry::FRESH;
                }
        } else 
        {
            if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && value.coins.IsPruned())
            {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                res -= itUs->second.coins.DynamicMemoryUsage();
                this->cacheCoins.erase(itUs);
            } else
            {
                    // A normal modification.
                res -= itUs->second.coins.DynamicMemoryUsage();
                itUs->second.coins.swap(value.coins);
                res += itUs->second.coins.DynamicMemoryUsage();
                    itUs->second.flags |= CCoinsCacheEntry::DIRTY;
                }
            }
        }
    return res;
    }

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins,
                                 const uint256 &hashBlockIn,
                                 const uint256 &hashAnchorIn,
                                 CAnchorsMap &mapAnchors,
                                 CNullifiersMap &mapNullifiers,
                                 CSidechainsMap& mapSidechains,
                                 CSidechainEventsMap& mapSidechainEvents,
                                 CCswNullifiersMap& cswNullifiers) {
    assert(!hasModifier);
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end(); ++it)
        cachedCoinsUsage += WriteCoins(it->first, it->second);

    mapCoins.clear();

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

    // Sidechain related section
    for (auto& entryToWrite : mapSidechains)
        WriteMutableEntry(entryToWrite.first, entryToWrite.second, cacheSidechains);

    for (auto& entryToWrite : mapSidechainEvents)
        WriteMutableEntry(entryToWrite.first, entryToWrite.second, cacheSidechainEvents);

    for (auto& entryToWrite : cswNullifiers)
        WriteImmutableEntry(entryToWrite.first, entryToWrite.second, cacheCswNullifiers);

    mapSidechains.clear();
    mapSidechainEvents.clear();
    cswNullifiers.clear();
    // End of sidechain related section

    hashAnchor = hashAnchorIn;
    hashBlock = hashBlockIn;
    return true;
}

bool CCoinsViewCache::HaveSidechain(const uint256& scId) const
{
    CSidechainsMap::const_iterator it = FetchSidechains(scId);
    return (it != cacheSidechains.end()) && (it->second.flag != CSidechainsCacheEntry::Flags::ERASED);
}

bool CCoinsViewCache::GetSidechain(const uint256 & scId, CSidechain& targetSidechain) const
{
    CSidechainsMap::const_iterator it = FetchSidechains(scId);
    if (it != cacheSidechains.end())
        LogPrint("sc", "%s():%d - FetchedSidechain: scId[%s]\n", __func__, __LINE__, scId.ToString());

    if (it != cacheSidechains.end() && it->second.flag != CSidechainsCacheEntry::Flags::ERASED) {
        targetSidechain = it->second.sidechain;
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

bool CCoinsViewCache::CheckQuality(const CScCertificate& cert) const
{
    // check in blockchain if a better cert is already there for this epoch
    CSidechain info;
    if (GetSidechain(cert.GetScId(), info))
    {
        if (info.lastTopQualityCertHash != cert.GetHash() &&
            info.lastTopQualityCertReferencedEpoch == cert.epochNumber &&
            info.lastTopQualityCertQuality >= cert.quality)
        {
            LogPrint("cert", "%s.%s():%d - NOK, cert %s q=%d : a cert q=%d for same sc/epoch is already in blockchain\n",
                __FILE__, __func__, __LINE__, cert.GetHash().ToString(), cert.quality, info.lastTopQualityCertQuality);
            return false;
        }
    }
    else
    {
        LogPrint("cert", "%s.%s():%d - cert %s has no scid in blockchain\n",
            __FILE__, __func__, __LINE__, cert.GetHash().ToString());
    }

    LogPrint("cert", "%s.%s():%d - cert %s q=%d : OK, no better quality certs for same sc/epoch are in blockchain\n",
        __FILE__, __func__, __LINE__, cert.GetHash().ToString(), cert.quality);
    return true;
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

bool CCoinsViewCache::UpdateSidechain(const CTransaction& tx, const CBlock& block, int blockHeight)
{
    const uint256& txHash = tx.GetHash();
    LogPrint("sc", "%s():%d - enter tx=%s\n", __func__, __LINE__, txHash.ToString() );

    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = blockHeight + SC_COIN_MATURITY;

    // ceased sidechain withdrawal ccin
    for (const CTxCeasedSidechainWithdrawalInput& csw: tx.GetVcswCcIn())
    {
        if (!HaveSidechain(csw.scId))
        {
            // should not happen
            LogPrintf("ERROR: %s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, csw.scId.ToString() );
            return false;
        }

        CSidechainsMap::iterator scIt = ModifySidechain(csw.scId);

        // decrease SC balance
        scIt->second.sidechain.balance -= csw.nValue;
        assert(scIt->second.sidechain.balance >= 0);
        scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;

        LogPrint("sc", "%s():%d - sidechain balance decreased by CSW in scView csw_amount=%s scId=%s\n",
            __func__, __LINE__, FormatMoney(csw.nValue), csw.scId.ToString());
    }

    // creation ccout
    for (const auto& cr: tx.GetVscCcOut())
    {
        const uint256& scId = cr.GetScId();
        if (HaveSidechain(scId)) {
            LogPrintf("ERROR: %s():%d - CR: scId=%s already in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        CSidechainsMap::iterator scIt = ModifySidechain(scId);
        scIt->second.sidechain.creationBlockHeight = blockHeight;
        scIt->second.sidechain.creationTxHash = txHash;
        scIt->second.sidechain.lastTopQualityCertReferencedEpoch = CScCertificate::EPOCH_NULL;
        scIt->second.sidechain.lastTopQualityCertHash.SetNull();
        scIt->second.sidechain.lastTopQualityCertQuality = CScCertificate::QUALITY_NULL;
        scIt->second.sidechain.lastTopQualityCertBwtAmount = 0;

        scIt->second.sidechain.lastTopQualityCertView.forwardTransferScFee = cr.forwardTransferScFee;
        scIt->second.sidechain.lastTopQualityCertView.mainchainBackwardTransferRequestScFee = cr.mainchainBackwardTransferRequestScFee;

        scIt->second.sidechain.fixedParams.version = cr.version;
        scIt->second.sidechain.fixedParams.withdrawalEpochLength = cr.withdrawalEpochLength;
        scIt->second.sidechain.fixedParams.customData = cr.customData;
        scIt->second.sidechain.fixedParams.constant = cr.constant;
        scIt->second.sidechain.fixedParams.wCertVk = cr.wCertVk;
        scIt->second.sidechain.fixedParams.wCeasedVk = cr.wCeasedVk;
        scIt->second.sidechain.fixedParams.vFieldElementCertificateFieldConfig = cr.vFieldElementCertificateFieldConfig;
        scIt->second.sidechain.fixedParams.vBitVectorCertificateFieldConfig = cr.vBitVectorCertificateFieldConfig;
        scIt->second.sidechain.fixedParams.mainchainBackwardTransferRequestDataLength = cr.mainchainBackwardTransferRequestDataLength;

        scIt->second.sidechain.mImmatureAmounts[maturityHeight] = cr.nValue;
        scIt->second.sidechain.InitScFees();

        scIt->second.flag = CSidechainsCacheEntry::Flags::FRESH;

        LogPrint("sc", "%s():%d - immature balance added in scView (h=%d, amount=%s) %s\n",
            __func__, __LINE__, maturityHeight, FormatMoney(cr.nValue), scId.ToString());

        LogPrint("sc", "%s():%d - scId[%s] added in scView\n", __func__, __LINE__, scId.ToString() );

        CSidechainEventsMap::iterator scMaturingEventIt = ModifySidechainEvents(maturityHeight);
        if (scMaturingEventIt->second.flag == CSidechainEventsCacheEntry::Flags::FRESH) {
           scMaturingEventIt->second.scEvents.maturingScs.insert(cr.GetScId());
        } else {
           scMaturingEventIt->second.scEvents.maturingScs.insert(cr.GetScId());
           scMaturingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        }

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: scCreation next maturing height [%d]\n",
                __func__, __LINE__, cr.GetScId().ToString(), maturityHeight);

        // Schedule Ceasing Sidechains
        int nextCeasingHeight = scIt->second.sidechain.GetScheduledCeasingHeight();

        CSidechainEventsMap::iterator scCeasingEventIt = ModifySidechainEvents(nextCeasingHeight);
        if (scCeasingEventIt->second.flag == CSidechainEventsCacheEntry::Flags::FRESH) {
           scCeasingEventIt->second.scEvents.ceasingScs.insert(cr.GetScId());
        } else {
           scCeasingEventIt->second.scEvents.ceasingScs.insert(cr.GetScId());
           scCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        }

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: scCreation next ceasing height [%d]\n",
               __func__, __LINE__, cr.GetScId().ToString(), nextCeasingHeight);
    }

    // forward transfer ccout
    for(auto& ft: tx.GetVftCcOut())
    {
        if (!HaveSidechain(ft.scId))
        {
            // should not happen
            LogPrintf("%s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, ft.scId.ToString() );
            return false;
        }
        CSidechainsMap::iterator scIt = ModifySidechain(ft.scId);

        // add a new immature balance entry in sc info or increment it if already there
        scIt->second.sidechain.mImmatureAmounts[maturityHeight] += ft.nValue;
        if (scIt->second.flag != CSidechainsCacheEntry::Flags::FRESH)
            scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;

        LogPrint("sc", "%s():%d - immature balance added in scView (h=%d, amount=%s) %s\n",
            __func__, __LINE__, maturityHeight, FormatMoney(ft.GetScValue()), ft.scId.ToString());

        CSidechainEventsMap::iterator scMaturingEventIt = ModifySidechainEvents(maturityHeight);
        if (scMaturingEventIt->second.flag == CSidechainEventsCacheEntry::Flags::FRESH) {
          scMaturingEventIt->second.scEvents.maturingScs.insert(ft.GetScId());
        } else {
          scMaturingEventIt->second.scEvents.maturingScs.insert(ft.GetScId());
          scMaturingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        }

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: fwd Transfer next maturing height [%d]\n",
               __func__, __LINE__, ft.GetScId().ToString(), maturityHeight);
    }

    // mc backward transfer request ccout
    for(auto& mbtr: tx.GetVBwtRequestOut())
    {
        if (!HaveSidechain(mbtr.scId))
        {
            // should not happen
            LogPrint("sc", "%s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, mbtr.scId.ToString() );
            return false;
        }
        CSidechainsMap::iterator scIt = ModifySidechain(mbtr.scId);

        // add a new immature balance entry in sc info or increment it if already there
        scIt->second.sidechain.mImmatureAmounts[maturityHeight] += mbtr.GetScValue();
        if (scIt->second.flag != CSidechainsCacheEntry::Flags::FRESH)
            scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;

        LogPrint("sc", "%s():%d - immature balance added in scView (h=%d, amount=%s) %s\n",
            __func__, __LINE__, maturityHeight, FormatMoney(mbtr.GetScValue()), mbtr.scId.ToString());

        CSidechainEventsMap::iterator scMaturingEventIt = ModifySidechainEvents(maturityHeight);
        if (scMaturingEventIt->second.flag == CSidechainEventsCacheEntry::Flags::FRESH) {
          scMaturingEventIt->second.scEvents.maturingScs.insert(mbtr.scId);
        } else {
          scMaturingEventIt->second.scEvents.maturingScs.insert(mbtr.scId);
          scMaturingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        }

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: mbtr scFees next maturing height [%d]\n",
               __func__, __LINE__, mbtr.scId.ToString(), maturityHeight);
    }

    return true;
}

bool CCoinsViewCache::RevertTxOutputs(const CTransaction& tx, int nHeight)
{
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = nHeight + SC_COIN_MATURITY;

    // backward transfer request 
    for(const auto& entry: tx.GetVBwtRequestOut())
    {
        const uint256& scId = entry.scId;
        LogPrint("sc", "%s():%d - removing fwt for scId=%s\n", __func__, __LINE__, scId.ToString());

        if (!HaveSidechain(scId))
        {
            // should not happen
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        CSidechainsMap::iterator scIt = ModifySidechain(scId);
        if (!DecrementImmatureAmount(scId, scIt, entry.GetScValue(), maturityHeight) )
        {
            // should not happen
            LogPrint("sc", "ERROR %s():%d - scId=%s could not handle immature balance at height%d\n",
                __func__, __LINE__, scId.ToString(), maturityHeight);
            return false;
        }

        CSidechainEventsMap::iterator scMaturingEventIt = ModifySidechainEvents(maturityHeight);
        scMaturingEventIt->second.scEvents.maturingScs.erase(entry.scId);
        if (!scMaturingEventIt->second.scEvents.IsNull()) //still other sc ceasing at that height or fwds
           scMaturingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        else
           scMaturingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] cancelled maturing height [%s] for fwd amount.\n",
           __func__, __LINE__, entry.scId.ToString(), maturityHeight);
    }

    // revert forward transfers
    for(const auto& entry: tx.GetVftCcOut())
    {
        const uint256& scId = entry.scId;
        LogPrint("sc", "%s():%d - removing fwt for scId=%s\n", __func__, __LINE__, scId.ToString());

        if (!HaveSidechain(scId))
        {
            // should not happen
            LogPrintf("ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        CSidechainsMap::iterator scIt = ModifySidechain(scId);
        if (!DecrementImmatureAmount(scId, scIt, entry.nValue, maturityHeight) )
        {
            // should not happen
            LogPrintf("ERROR %s():%d - scId=%s could not handle immature balance at height%d\n",
                __func__, __LINE__, scId.ToString(), maturityHeight);
            return false;
        }

        CSidechainEventsMap::iterator scMaturingEventIt = ModifySidechainEvents(maturityHeight);
        scMaturingEventIt->second.scEvents.maturingScs.erase(entry.scId);
        if (!scMaturingEventIt->second.scEvents.IsNull()) //still other sc ceasing at that height or fwds
          scMaturingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        else
          scMaturingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] cancelled maturing height [%s] for fwd amount.\n",
          __func__, __LINE__, entry.scId.ToString(), maturityHeight);

    }

    // remove sidechain if the case
    for(const auto& entry: tx.GetVscCcOut())
    {
        const uint256& scId = entry.GetScId();
        LogPrint("sc", "%s():%d - removing scId=%s\n", __func__, __LINE__, scId.ToString());

        if (!HaveSidechain(scId))
        {
            // should not happen
            LogPrintf("ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        CSidechainsMap::iterator scIt = ModifySidechain(scId);
        int ceasingHeightToErase = scIt->second.sidechain.GetScheduledCeasingHeight();
        if (!DecrementImmatureAmount(scId, scIt, entry.nValue, maturityHeight) )
        {
            // should not happen
            LogPrintf("ERROR %s():%d - scId=%s could not handle immature balance at height%d\n",
                __func__, __LINE__, scId.ToString(), maturityHeight);
            return false;
        }

        if (scIt->second.sidechain.balance > 0)
        {
            // should not happen either
            LogPrintf("ERROR %s():%d - scId=%s balance not null: %s\n",
                __func__, __LINE__, scId.ToString(), FormatMoney(scIt->second.sidechain.balance));
            return false;
        }

        scIt->second.flag = CSidechainsCacheEntry::Flags::ERASED;
        LogPrint("sc", "%s():%d - scId=%s removed from scView\n", __func__, __LINE__, scId.ToString() );


        if (HaveSidechainEvents(maturityHeight))
        {
            CSidechainEventsMap::iterator scMaturityEventIt = ModifySidechainEvents(maturityHeight);
            scMaturityEventIt->second.scEvents.maturingScs.erase(entry.GetScId());
            if (!scMaturityEventIt->second.scEvents.IsNull()) //still other sc ceasing at that height or fwds
                scMaturityEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
            else
                scMaturityEventIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;

            LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] deleted maturing height [%d] for creation amount.\n",
                __func__, __LINE__, entry.GetScId().ToString(), maturityHeight);
        } else
            LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] nothing to do for scCreation amount maturing canceling at height [%d].\n",
                __func__, __LINE__, entry.GetScId().ToString(), maturityHeight);


        // Cancel Ceasing Sidechains Event
        if (!HaveSidechainEvents(ceasingHeightToErase)) {
            return error("%s():%d - ERROR-SIDECHAIN-EVENT: scId[%s] misses current ceasing height; expected value was [%d]\n",
                __func__, __LINE__, entry.GetScId().ToString(), ceasingHeightToErase);
        }

        CSidechainEventsMap::iterator scCurCeasingEventIt = ModifySidechainEvents(ceasingHeightToErase);
        scCurCeasingEventIt->second.scEvents.ceasingScs.erase(entry.GetScId());
        if (!scCurCeasingEventIt->second.scEvents.IsNull())
            scCurCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        else
            scCurCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: undo of creation removes currentCeasingHeight [%d]\n",
                __func__, __LINE__, entry.GetScId().ToString(), ceasingHeightToErase);
    }

    // revert sidechain balances for CSWs
    for (const CTxCeasedSidechainWithdrawalInput& csw: tx.GetVcswCcIn())
    {
        LogPrint("sc", "%s():%d - removing CSW for scId=%s\n", __func__, __LINE__, csw.scId.ToString());

        if (!HaveSidechain(csw.scId))
        {
            // should not happen
            LogPrintf("ERROR: %s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, csw.scId.ToString() );
            return false;
        }

        CSidechainsMap::iterator scIt = ModifySidechain(csw.scId);

        // increase SC balance
        scIt->second.sidechain.balance += csw.nValue;
        scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;

        LogPrint("sc", "%s():%d - sidechain balance increased by CSW in scView csw_amount=%s scId=%s\n",
            __func__, __LINE__, FormatMoney(csw.nValue), csw.scId.ToString());
    }

    return true;
}

#ifdef BITCOIN_TX
int CCoinsViewCache::GetHeight() const {return -1;}
CValidationState::Code CCoinsViewCache::IsCertApplicableToState(const CScCertificate& cert, bool* banSenderNode) const {return CValidationState::Code::OK;}
CValidationState::Code CCoinsViewCache::IsScTxApplicableToState(const CTransaction& tx, Sidechain::ScFeeCheckFlag scFeeCheckType, bool* banSenderNode) const { return CValidationState::Code::OK;}

void CCoinsViewCache::HandleTxIndexSidechainEvents(int height, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<uint256, CTxIndexValue>>& txIndex)
{
    return;
}

void CCoinsViewCache::RevertTxIndexSidechainEvents(int height, CBlockUndo& blockUndo, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<uint256, CTxIndexValue>>& txIndex)
{
    return;
}

void CCoinsViewCache::HandleMaturityHeightIndexSidechainEvents(int height, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<CMaturityHeightKey,CMaturityHeightValue>>& maturityHeightIndex)
{
    return;
} 

void CCoinsViewCache::RevertMaturityHeightIndexSidechainEvents(int height, CBlockUndo& blockUndo, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<CMaturityHeightKey,CMaturityHeightValue>>& maturityHeightIndex)
{
    return;
}

#ifdef ENABLE_ADDRESS_INDEXING
void CCoinsViewCache::HandleIndexesSidechainEvents(int height, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<CAddressIndexKey, CAddressIndexValue>>& addressIndex,
                                                   std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>& addressUnspentIndex)
{
    return;
}

void CCoinsViewCache::RevertIndexesSidechainEvents(int height, CBlockUndo& blockUndo, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<CAddressIndexKey, CAddressIndexValue>>& addressIndex,
                                                   std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>& addressUnspentIndex)
{
    return;
}
#endif // ENABLE_ADDRESS_INDEXING

#else

int CCoinsViewCache::GetHeight() const
{
    LOCK(cs_main);
    BlockMap::const_iterator itBlockIdx = mapBlockIndex.find(this->GetBestBlock());
    CBlockIndex* pindexPrev = (itBlockIdx == mapBlockIndex.end()) ? nullptr : itBlockIdx->second;
    return pindexPrev->nHeight;
}

bool CCoinsViewCache::CheckCertTiming(const uint256& scId, int certEpoch) const
{
    CSidechain sidechain;
    if (!GetSidechain(scId, sidechain))
    {
        return error("%s():%d - ERROR: certificate cannot be accepted, scId[%s] not yet created\n",
           __func__, __LINE__, scId.ToString());
    }

    if (this->GetSidechainState(scId) != CSidechain::State::ALIVE)
    {
        return error("%s():%d - ERROR: certificate cannot be accepted, sidechain [%s] already ceased\n",
            __func__, __LINE__, scId.ToString());
    }

    // Adding handling of quality, we can have also certificates for the same epoch of the last certificate
    // The epoch number must be consistent with the sc certificate history (no old epoch allowed)
    if (certEpoch != sidechain.lastTopQualityCertReferencedEpoch &&
        certEpoch != sidechain.lastTopQualityCertReferencedEpoch + 1)
    {
        return error("%s():%d - ERROR: certificate cannot be accepted, wrong epoch. Certificate Epoch %d (expected: %d or %d)\n",
            __func__, __LINE__, certEpoch, sidechain.lastTopQualityCertReferencedEpoch, sidechain.lastTopQualityCertReferencedEpoch+1);
    }

    int certWindowStartHeight = sidechain.GetCertSubmissionWindowStart(certEpoch);
    int certWindowEndHeight   = sidechain.GetCertSubmissionWindowEnd(certEpoch);

    int inclusionHeight = this->GetHeight() + 1;
     
    if ((inclusionHeight < certWindowStartHeight) || (inclusionHeight > certWindowEndHeight))
    {
        return error("%s():%d - ERROR: certificate cannot be accepted, cert received outside safeguard\n",
            __func__, __LINE__);
    }

    return true;
}

CValidationState::Code CCoinsViewCache::IsCertApplicableToState(const CScCertificate& cert, bool* banSenderNode) const
{
    const uint256& certHash = cert.GetHash();

    LogPrint("cert", "%s():%d - called: cert[%s], scId[%s]\n",
        __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString());

    CSidechain sidechain;
    if (!GetSidechain(cert.GetScId(), sidechain))
    {
        LogPrintf("%s():%d - ERROR: cert[%s] refers to scId[%s] not yet created\n",
            __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString());
        return CValidationState::Code::SCID_NOT_FOUND;
    }

    if (!CheckCertTiming(cert.GetScId(), cert.epochNumber))
    {
        LogPrintf("%s():%d - ERROR: cert %s timing is not valid\n", __func__, __LINE__, certHash.ToString());
        return CValidationState::Code::INVALID;
    }

    if (!Sidechain::checkCertCustomFields(sidechain, cert) )
    {
        LogPrintf("%s():%d - ERROR: invalid cert[%s], scId[%s] invalid custom data cfg\n",
            __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString());
        if (banSenderNode)
            *banSenderNode = true;
        return CValidationState::Code::INVALID;
    }

    CValidationState::Code ret =
        CheckEndEpochCumScTxCommTreeRoot(sidechain, cert.epochNumber, cert.endEpochCumScTxCommTreeRoot);

    if (ret != CValidationState::Code::OK )
    {
        LogPrintf("%s():%d - ERROR: cert[%s], scId[%s], faild checking sc cum commitment tree hash\n",
            __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString());
        return ret;
    }

    if (!CheckQuality(cert))
    {
        LogPrintf("%s():%d - ERROR: cert %s with invalid quality %d\n", __func__, __LINE__, certHash.ToString(), cert.quality);
        return CValidationState::Code::INVALID;
    }

    CAmount bwtTotalAmount = cert.GetValueOfBackwardTransfers(); 
    CAmount scBalance = sidechain.balance;
    if (cert.epochNumber == sidechain.lastTopQualityCertReferencedEpoch) 
    {
        // if we are targeting the same epoch of an existing certificate, add
        // to the sidechain.balance the amount of the former top-quality cert if any
        scBalance += sidechain.lastTopQualityCertBwtAmount;
    }

    if (bwtTotalAmount > scBalance)
    {
        LogPrintf("%s():%d - ERROR: insufficent balance in scId[%s]: balance[%s], cert amount[%s]\n",
            __func__, __LINE__, cert.GetScId().ToString(), FormatMoney(scBalance), FormatMoney(bwtTotalAmount));
        return CValidationState::Code::INSUFFICIENT_SCID_FUNDS;
    }

    size_t proof_plus_vk_size = sidechain.fixedParams.wCertVk.GetByteArray().size() + cert.scProof.GetByteArray().size();
    if(proof_plus_vk_size > Sidechain::MAX_PROOF_PLUS_VK_SIZE)
    {
        LogPrintf("%s():%d - ERROR: Cert [%s]\n proof plus vk size (%d) exceeded the limit %d\n",
            __func__, __LINE__, certHash.ToString(), proof_plus_vk_size, Sidechain::MAX_PROOF_PLUS_VK_SIZE);
        if (banSenderNode)
            *banSenderNode = true;
        return CValidationState::Code::INVALID;
    }

    LogPrint("sc", "%s():%d - ok, balance in scId[%s]: balance[%s], cert amount[%s]\n",
        __func__, __LINE__, cert.GetScId().ToString(), FormatMoney(scBalance), FormatMoney(bwtTotalAmount) );

    return  CValidationState::Code::OK;
}

CValidationState::Code CCoinsViewCache::CheckEndEpochCumScTxCommTreeRoot(
    const CSidechain& sidechain, int epochNumber, const CFieldElement& endEpochCumScTxCommTreeRoot) const
{
    LOCK(cs_main);
    int endEpochHeight = sidechain.GetEndHeightForEpoch(epochNumber);
    CBlockIndex* pblockindex = chainActive[endEpochHeight];

    if (!pblockindex)
    {
        LogPrintf("%s():%d - ERROR: end height %d for certificate epoch %d is not in current chain active (height %d)\n",
            __func__, __LINE__, endEpochHeight, epochNumber, chainActive.Height());
        return CValidationState::Code::INVALID;
    }

    if (pblockindex->scCumTreeHash != endEpochCumScTxCommTreeRoot)
    {
        LogPrintf("%s():%d - ERROR: cert cumulative commitment tree root does not match the value found at block hight[%d]\n",
            __func__, __LINE__, endEpochHeight);
        return CValidationState::Code::SC_CUM_COMM_TREE;
    }

    return CValidationState::Code::OK;
}

bool CCoinsViewCache::CheckScTxTiming(const uint256& scId) const
{
    auto s = GetSidechainState(scId);
    if (s != CSidechain::State::ALIVE && s != CSidechain::State::UNCONFIRMED)
    {
        return error("%s():%d - ERROR: attempt to send scTx to scId[%s] in state[%s]\n",
            __func__, __LINE__, scId.ToString(), CSidechain::stateToString(s));
    }

    return true;
}

/**
 * @brief Checks whether a Forward Transfer output is still valid based on sidechain current FT fee.
 * 
 * @param ftOutput The Forward Transfer output to be checked.
 * @return true if ftOutput is still valid, false otherwise.
 */
bool CCoinsViewCache::IsFtScFeeApplicable(const CTxForwardTransferOut& ftOutput) const
{
    CScCertificateView certView = GetActiveCertView(ftOutput.scId);
    return ftOutput.nValue > certView.forwardTransferScFee;
}

bool CCoinsViewCache::CheckMinimumFtScFee(const CTxForwardTransferOut& ftOutput, CAmount* minValPtr) const
{
    if (minValPtr)
        *minValPtr = MAX_MONEY;

    const CSidechain* const pSidechain = this->AccessSidechain(ftOutput.scId);

    if (pSidechain == nullptr)
        return false;
    if (this->GetSidechainState(ftOutput.scId) != CSidechain::State::ALIVE)
        return false;

    CAmount minVal = pSidechain->GetMinFtScFee();

    if (minValPtr)
        *minValPtr = minVal;

    return (ftOutput.nValue > minVal);
}

/**
 * @brief Checks whether a Mainchain Backward Transfer Request output is still valid based on sidechain current MBTR fee.
 * 
 * @param mbtrOutput The Mainchain Backward Transfer Request output to be checked.
 * @return true if mbtrOutput is still valid, false otherwise.
 */
bool CCoinsViewCache::IsMbtrScFeeApplicable(const CBwtRequestOut& mbtrOutput) const
{
    CScCertificateView certView = GetActiveCertView(mbtrOutput.scId);
    return mbtrOutput.scFee >= certView.mainchainBackwardTransferRequestScFee;
}

bool CCoinsViewCache::CheckMinimumMbtrScFee(const CBwtRequestOut& mbtrOutput, CAmount* minValPtr) const
{
    if (minValPtr)
        *minValPtr = MAX_MONEY;

    const CSidechain* const pSidechain = this->AccessSidechain(mbtrOutput.scId);

    if (pSidechain == nullptr)
        return false;
    if (this->GetSidechainState(mbtrOutput.scId) != CSidechain::State::ALIVE)
        return false;

    CAmount minVal = pSidechain->GetMinMbtrScFee();

    if (minValPtr)
        *minValPtr = minVal;

    return (mbtrOutput.scFee >= minVal);
}

CValidationState::Code CCoinsViewCache::IsScTxApplicableToState(const CTransaction& tx, Sidechain::ScFeeCheckFlag scFeeCheckType, bool* banSenderNode) const
{
    if (tx.IsCoinBase())
    {
        return CValidationState::Code::OK;
    }

    const uint256& txHash = tx.GetHash();

    // check creation
    for (const auto& sc: tx.GetVscCcOut())
    {
        const uint256& scId = sc.GetScId();
        if (HaveSidechain(scId))
        {
            LogPrintf("%s():%d - ERROR: Invalid tx[%s] : scid[%s] already created\n",
                __func__, __LINE__, txHash.ToString(), scId.ToString());
            return CValidationState::Code::INVALID;
        }

        LogPrint("sc", "%s():%d - OK: tx[%s] is creating scId[%s]\n",
            __func__, __LINE__, txHash.ToString(), scId.ToString());
    }

    // check fw tx
    for (const auto& ft: tx.GetVftCcOut())
    {
        const uint256& scId = ft.scId;

        // While in principle we allow fts to be sent in the very same tx where scCreationOutput is
        // in practice this is not feasible. This is because scId is created hashing the whole tx
        // containing scCreationOutput and ft, so ft.scId cannot be specified before the whole tx
        // has been created. Therefore here we do not bother checking whether tx contains scId creation

        if (!CheckScTxTiming(scId))
        {
            LogPrintf("%s():%d - ERROR: tx %s timing is not valid\n", __func__, __LINE__, txHash.ToString());
            return CValidationState::Code::INVALID;
        }

        /**
         * Check that the Forward Transfer amount is strictly greater than the
         * Sidechain Forward Transfer Fee.
         */
        if (!IsFtScFeeApplicable(ft))
        {
            if (scFeeCheckType == Sidechain::ScFeeCheckFlag::MINIMUM_IN_A_RANGE)
            {
                // connecting a block
                CAmount minScFee;
                if (!CheckMinimumFtScFee(ft, &minScFee))
                {
                    LogPrintf("%s():%d - ERROR: Invalid tx[%s] to scId[%s]: FT amount [%s] must be greater than minimum SC FT fee [%s]\n",
                        __func__, __LINE__, txHash.ToString(), scId.ToString(), FormatMoney(ft.nValue), FormatMoney(minScFee));
                    return CValidationState::Code::INVALID;
                }
                LogPrintf("%s():%d - Warning: tx[%s] to scId[%s]: FT amount [%s] is not greater than act cert fee [%s], but is greater than minimum SC FT fee [%s]\n",
                    __func__, __LINE__, txHash.ToString(), scId.ToString(), FormatMoney(ft.nValue),
                    FormatMoney(GetActiveCertView(scId).forwardTransferScFee), FormatMoney(minScFee));
            }
            else
            {
                LogPrintf("%s():%d - ERROR: Invalid tx[%s] to scId[%s]: FT amount [%s] must be greater than SC FT fee [%s]\n",
                    __func__, __LINE__, txHash.ToString(), scId.ToString(), FormatMoney(ft.nValue), FormatMoney(GetActiveCertView(scId).forwardTransferScFee));
                return CValidationState::Code::INVALID;
            }
        }

        LogPrint("sc", "%s():%d - OK: tx[%s] is sending [%s] to scId[%s]\n",
            __func__, __LINE__, txHash.ToString(), FormatMoney(ft.nValue), scId.ToString());
    }

    // check mbtr
    for(size_t idx = 0; idx < tx.GetVBwtRequestOut().size(); ++idx)
    {
        const CBwtRequestOut& mbtr = tx.GetVBwtRequestOut().at(idx);
        const uint256& scId = mbtr.scId;

        // While in principle we allow mbtrs to be sent in the very same tx where scCreationOutput is
        // in practice this is not feasible. This is because scId is created hashing the whole tx
        // containing scCreationOutput and mbtr, so mbtr.scId cannot be specified before the whole tx
        // has been created. Therefore here we do not bother checking whether tx contains scId creation
        if (!CheckScTxTiming(scId))
        {
            LogPrintf("%s():%d - ERROR: tx %s timing is not valid\n", __func__, __LINE__, txHash.ToString());
            return CValidationState::Code::INVALID;
        }

        CSidechain sidechain;

        /**
         * Check that the sidechain exists.
         */
        if (!GetSidechain(scId, sidechain))
        {
            LogPrintf("%s():%d - ERROR: tx[%s] MBTR output [%s] refers to unknown scId[%s]\n",
                __func__, __LINE__, tx.ToString(), mbtr.ToString(), scId.ToString());
            return CValidationState::Code::INVALID;
        }

        /**
         * Check that the size of the Request Data field element is the same specified
         * during sidechain creation.
         */
        if (mbtr.vScRequestData.size() != sidechain.fixedParams.mainchainBackwardTransferRequestDataLength)
        {
            LogPrintf("%s():%d - ERROR: Invalid tx[%s] : MBTR request data size [%d] must be equal to the size specified "
                         "during sidechain creation [%d] for scId[%s]\n",
                    __func__, __LINE__, txHash.ToString(), mbtr.vScRequestData.size(), sidechain.fixedParams.mainchainBackwardTransferRequestDataLength, scId.ToString());
            if (banSenderNode)
                *banSenderNode = true;
            return CValidationState::Code::INVALID;
        }

        /**
         * Check that MBTRs are allowed (i.e. the MBTR data length is greater than zero).
         */
        if (sidechain.fixedParams.mainchainBackwardTransferRequestDataLength == 0)
        {
            LogPrintf("%s():%d - ERROR: mbtr is not allowed for scId[%s]\n",  __func__, __LINE__, scId.ToString());
            if (banSenderNode)
                *banSenderNode = true;
            return CValidationState::Code::INVALID;
        }

        /**
         * Check that the Mainchain Backward Transfer Request amount is greater than or equal to the
         * Sidechain Mainchain Backward Transfer Request fee.
         */
        if (!IsMbtrScFeeApplicable(mbtr))
        {
            if (scFeeCheckType == Sidechain::ScFeeCheckFlag::MINIMUM_IN_A_RANGE)
            {
                // connecting a block
                CAmount minScFee;
                if (!CheckMinimumMbtrScFee(mbtr, &minScFee))
                {
                    LogPrintf("%s():%d - ERROR: Invalid tx[%s] to scId[%s]: MBTR fee [%s] can not be less than minimum SC MBTR fee [%s]\n",
                        __func__, __LINE__, txHash.ToString(), scId.ToString(), FormatMoney(mbtr.scFee), FormatMoney(minScFee));
                    return CValidationState::Code::INVALID;
                }
                LogPrintf("%s():%d - Warning: tx[%s] to scId[%s]: MBTR fee [%s] is lesser than act cert fee [%s], but is not lesser than minimum SC MBTR fee [%s]\n",
                    __func__, __LINE__, txHash.ToString(), scId.ToString(), FormatMoney(mbtr.scFee),
                    FormatMoney(GetActiveCertView(scId).forwardTransferScFee), FormatMoney(minScFee));
            }
            else
            {
                LogPrintf("%s():%d - ERROR: Invalid tx[%s] : MBTR fee [%s] cannot be less than SC MBTR fee [%s] for scId[%s]\n",
                        __func__, __LINE__, txHash.ToString(), FormatMoney(mbtr.scFee),
                        FormatMoney(GetActiveCertView(scId).mainchainBackwardTransferRequestScFee), scId.ToString());
                return CValidationState::Code::INVALID;
            }
        }

        LogPrint("sc", "%s():%d - OK: tx[%s] contains bwt transfer request for scId[%s]\n",
            __func__, __LINE__, txHash.ToString(), scId.ToString());
    }

    // Check CSW inputs
    // Key is Sc id, value - total amount of coins to be withdrawn by Tx CSWs for given sidechain
    std::map<uint256, CAmount> cswTotalBalances;
    for(const CTxCeasedSidechainWithdrawalInput& csw: tx.GetVcswCcIn())
    {
        CSidechain sidechain;
        if (!GetSidechain(csw.scId, sidechain))
        {
            LogPrintf("%s():%d - ERROR: tx[%s] CSW input [%s]\n refers to unknown scId\n",
                __func__, __LINE__, tx.ToString(), csw.ToString());
            return CValidationState::Code::SCID_NOT_FOUND;
        }

        auto s = this->GetSidechainState(csw.scId);
        if (s != CSidechain::State::CEASED)
        {
            LogPrintf("%s():%d - ERROR: Tx[%s] CSW input [%s]\n cannot be accepted, sidechain is not ceased\n",
                __func__, __LINE__, tx.ToString(), csw.ToString());
            return CValidationState::Code::INVALID;
        }

        if(!sidechain.fixedParams.wCeasedVk.is_initialized())
        {
            LogPrintf("%s():%d - ERROR: Tx[%s] CSW input [%s]\n refers to SC without CSW support\n",
                __func__, __LINE__, tx.GetHash().ToString(), csw.ToString());
            if (banSenderNode)
                *banSenderNode = true;
            return CValidationState::Code::INVALID;
        }

        size_t proof_plus_vk_size = sidechain.fixedParams.wCeasedVk.get().GetByteArray().size() + csw.scProof.GetByteArray().size();
        if(proof_plus_vk_size > Sidechain::MAX_PROOF_PLUS_VK_SIZE)
        {
            LogPrintf("%s():%d - ERROR: Tx[%s] CSW input [%s]\n proof plus vk size (%d) exceeded the limit %d\n",
                __func__, __LINE__, tx.GetHash().ToString(), csw.ToString(), proof_plus_vk_size, Sidechain::MAX_PROOF_PLUS_VK_SIZE);
            if (banSenderNode)
                *banSenderNode = true;
            return CValidationState::Code::INVALID;
        }

        // add a new balance entry in the map or increment it if already there
        cswTotalBalances[csw.scId] += csw.nValue;

        // Check that SC CSW balances don't exceed the SC balance
        if(cswTotalBalances[csw.scId] > sidechain.balance)
        {
            LogPrintf("%s():%d - ERROR: Tx[%s] CSW inputs total amount[%s] is greater than sc[%s] total balance[%s]\n",
                __func__, __LINE__, tx.ToString(), FormatMoney(cswTotalBalances[csw.scId]), csw.scId.ToString(), FormatMoney(sidechain.balance));
            return CValidationState::Code::INSUFFICIENT_SCID_FUNDS;
        }

        if (this->HaveCswNullifier(csw.scId, csw.nullifier)) {
            LogPrintf("%s():%d - ERROR: Tx[%s] CSW input [%s] nullifier had been already used\n",
                __func__, __LINE__, tx.ToString(), csw.ToString());
            return CValidationState::Code::INVALID;
        }

        CScCertificateView certView = this->GetActiveCertView(csw.scId);
        // note: it's also fine to have an empty actCertDataHash fe obj 
        // in this case both certView.certDataHash, csw.actCertDataHash have to be == CFieldElement() to be valid
        if (certView.certDataHash != csw.actCertDataHash)
        {
            LogPrintf("%s():%d - ERROR: Tx[%s] CSW input [%s]\n active cert data hash does not match\n",
                __func__, __LINE__, tx.ToString(), csw.ToString());
            return CValidationState::Code::ACTIVE_CERT_DATA_HASH;
        }

        if (GetCeasingCumTreeHash(csw.scId) != csw.ceasingCumScTxCommTree)
        {
            LogPrintf("%s():%d - ERROR: Tx[%s] CSW input [%s]\n ceased cum Tree hash does not match\n",
                __func__, __LINE__, tx.ToString(), csw.ToString());
            return CValidationState::Code::SC_CUM_COMM_TREE;
        }
    }

    return CValidationState::Code::OK;
}

void CCoinsViewCache::HandleTxIndexSidechainEvents(int height, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<uint256, CTxIndexValue>>& txIndex)
{
    if (!HaveSidechainEvents(height))
        return;

    CSidechainEvents scEvents;
    GetSidechainEvents(height, scEvents);

    //Handle Ceasing Sidechain
    for (const uint256& ceasingScId : scEvents.ceasingScs)
    {
        CSidechain sidechain;
        assert(GetSidechain(ceasingScId, sidechain));

        if (sidechain.lastTopQualityCertReferencedEpoch == CScCertificate::EPOCH_NULL) {
            assert(sidechain.lastTopQualityCertHash.IsNull());
            continue;
        }

        CTxIndexValue txIndexVal;
        assert(pblocktree->ReadTxIndex(sidechain.lastTopQualityCertHash, txIndexVal));

        // Set lastTopQualityCert as superseded
        txIndexVal.maturityHeight *= -1;
        txIndex.push_back(std::make_pair(sidechain.lastTopQualityCertHash, txIndexVal));
    }
}

void CCoinsViewCache::RevertTxIndexSidechainEvents(int height, CBlockUndo& blockUndo, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<uint256, CTxIndexValue>>& txIndex)
{
    if (!HaveSidechainEvents(height))
        return;

    // Reverting ceasing sidechains
    for (auto it = blockUndo.scUndoDatabyScId.begin(); it != blockUndo.scUndoDatabyScId.end(); ++it)
    {
        if ((it->second.contentBitMask & CSidechainUndoData::AvailableSections::CEASED_CERT_DATA) == 0)
            continue;

        const uint256& scId = it->first;
        const CSidechain* const pSidechain = AccessSidechain(scId);

        if (pSidechain->lastTopQualityCertReferencedEpoch != CScCertificate::EPOCH_NULL)
        {
            CTxIndexValue txIndexVal;
            assert(pblocktree->ReadTxIndex(pSidechain->lastTopQualityCertHash, txIndexVal));

            // Restore lastTopQualityCert as valid (not superseded)
            txIndexVal.maturityHeight *= -1;
            txIndex.push_back(std::make_pair(pSidechain->lastTopQualityCertHash, txIndexVal));
        }
    }
}

void CCoinsViewCache::HandleMaturityHeightIndexSidechainEvents(int height, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<CMaturityHeightKey,CMaturityHeightValue>>& maturityHeightIndex)
{
    if (!HaveSidechainEvents(height))
        return;

    CSidechainEvents scEvents;
    GetSidechainEvents(height, scEvents);

    //Handle Ceasing Sidechain
    for (const uint256& ceasingScId : scEvents.ceasingScs)
    {
        CSidechain sidechain;
        assert(GetSidechain(ceasingScId, sidechain));

        if (sidechain.lastTopQualityCertReferencedEpoch == CScCertificate::EPOCH_NULL) {
            assert(sidechain.lastTopQualityCertHash.IsNull());
            continue;
        }

        //Remove the certificate from the MaturityHeight DB
        CMaturityHeightKey maturityHeightKey = CMaturityHeightKey(height, sidechain.lastTopQualityCertHash);
        maturityHeightIndex.push_back(std::make_pair(maturityHeightKey, CMaturityHeightValue()));
    }
}

void CCoinsViewCache::RevertMaturityHeightIndexSidechainEvents(int height, CBlockUndo& blockUndo, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<CMaturityHeightKey,CMaturityHeightValue>>& maturityHeightIndex)
{
    if (!HaveSidechainEvents(height))
        return;

    // Reverting ceasing sidechains
    for (auto it = blockUndo.scUndoDatabyScId.begin(); it != blockUndo.scUndoDatabyScId.end(); ++it)
    {
        if ((it->second.contentBitMask & CSidechainUndoData::AvailableSections::CEASED_CERT_DATA) == 0)
            continue;

        const uint256& scId = it->first;
        const CSidechain* const pSidechain = AccessSidechain(scId);

        if (pSidechain->lastTopQualityCertReferencedEpoch != CScCertificate::EPOCH_NULL)
        {
            // Restore lastTopQualityCert as valid (not superseded)
            CMaturityHeightKey maturityHeightKey = CMaturityHeightKey(height, pSidechain->lastTopQualityCertHash);
            maturityHeightIndex.push_back(std::make_pair(maturityHeightKey, CMaturityHeightValue(static_cast<char>(1))));
        }
    }
}


#ifdef ENABLE_ADDRESS_INDEXING
void CCoinsViewCache::HandleIndexesSidechainEvents(int height, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<CAddressIndexKey, CAddressIndexValue>>& addressIndex,
                                                   std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>& addressUnspentIndex)
{
    if (!HaveSidechainEvents(height))
        return;

    CSidechainEvents scEvents;
    GetSidechainEvents(height, scEvents);

    //Handle Ceasing Sidechain
    for (const uint256& ceasingScId : scEvents.ceasingScs)
    {
        CSidechain sidechain;
        assert(GetSidechain(ceasingScId, sidechain));

        if (sidechain.lastTopQualityCertReferencedEpoch == CScCertificate::EPOCH_NULL) {
            assert(sidechain.lastTopQualityCertHash.IsNull());
            continue;
        }

        CTxIndexValue txIndexVal;
        assert(pblocktree->ReadTxIndex(sidechain.lastTopQualityCertHash, txIndexVal));

        // Set the lower quality BTs as superseded
        UpdateBackwardTransferIndexes(sidechain.lastTopQualityCertHash, txIndexVal.txIndex, addressIndex, addressUnspentIndex,
                                      CCoinsViewCache::flagIndexesUpdateType::SUPERSEDE_CERTIFICATE);
    }
}

void CCoinsViewCache::RevertIndexesSidechainEvents(int height, CBlockUndo& blockUndo, CBlockTreeDB* pblocktree,
                                                   std::vector<std::pair<CAddressIndexKey, CAddressIndexValue>>& addressIndex,
                                                   std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>& addressUnspentIndex)
{
    if (!HaveSidechainEvents(height))
        return;

    // Reverting ceasing sidechains
    for (auto it = blockUndo.scUndoDatabyScId.begin(); it != blockUndo.scUndoDatabyScId.end(); ++it)
    {
        if ((it->second.contentBitMask & CSidechainUndoData::AvailableSections::CEASED_CERT_DATA) == 0)
            continue;

        const uint256& scId = it->first;
        const CSidechain* const pSidechain = AccessSidechain(scId);

        if (pSidechain->lastTopQualityCertReferencedEpoch != CScCertificate::EPOCH_NULL)
        {
            CTxIndexValue txIndexVal;
            assert(pblocktree->ReadTxIndex(pSidechain->lastTopQualityCertHash, txIndexVal));

            // Set the old top quality BTs as valid (even not mature yet)
            UpdateBackwardTransferIndexes(pSidechain->lastTopQualityCertHash, txIndexVal.txIndex, addressIndex, addressUnspentIndex,
                                          CCoinsViewCache::flagIndexesUpdateType::RESTORE_CERTIFICATE);
        }
    }
}
#endif // ENABLE_ADDRESS_INDEXING
#endif

bool CCoinsViewCache::UpdateSidechain(const CScCertificate& cert, CBlockUndo& blockUndo)
{
    const uint256& certHash       = cert.GetHash();
    const uint256& scId           = cert.GetScId();
    const CAmount& bwtTotalAmount = cert.GetValueOfBackwardTransfers();

    //UpdateSidechain must be called only once per block and scId, with top qualiy cert only
    assert(blockUndo.scUndoDatabyScId[scId].prevTopCommittedCertHash.IsNull());

    if (!HaveSidechain(scId)) // should not happen
    {
        return error("%s():%d - ERROR: cannot update balance, could not find scId=%s\n",
            __func__, __LINE__, scId.ToString());
    }

    CSidechainsMap::iterator scIt = ModifySidechain(scId);
    CSidechain& currentSc = scIt->second.sidechain;
    CSidechainUndoData& scUndoData = blockUndo.scUndoDatabyScId[scId];

    LogPrint("cert", "%s():%d - cert to be connected %s\n", __func__, __LINE__, cert.ToString());
    LogPrint("cert", "%s():%d - SidechainUndoData %s\n", __func__, __LINE__, scUndoData.ToString());
    LogPrint("cert", "%s():%d - current sc state %s\n", __func__, __LINE__, currentSc.ToString());

    int prevCeasingHeight = currentSc.GetScheduledCeasingHeight();

    if (cert.epochNumber == (currentSc.lastTopQualityCertReferencedEpoch+1))
    {
        //Lazy update of pastEpochTopQualityCertView
        scUndoData.pastEpochTopQualityCertView = currentSc.pastEpochTopQualityCertView;
        scUndoData.scFees                      = currentSc.scFees;
        scUndoData.contentBitMask |= CSidechainUndoData::AvailableSections::CROSS_EPOCH_CERT_DATA;

        currentSc.pastEpochTopQualityCertView = currentSc.lastTopQualityCertView;
        currentSc.UpdateScFees(currentSc.pastEpochTopQualityCertView);
    } else if (cert.epochNumber == currentSc.lastTopQualityCertReferencedEpoch)
    {
        if (cert.quality <= currentSc.lastTopQualityCertQuality) // should never happen
        {
            return error("%s():%d - ERROR: cert quality %d not greater than last seen %d",
                __func__, __LINE__, cert.quality, currentSc.lastTopQualityCertQuality);
        }

        currentSc.balance += currentSc.lastTopQualityCertBwtAmount;
    } else
        return error("%s():%d - ERROR: bad epoch value: %d (should be %d)\n",
            __func__, __LINE__, cert.epochNumber, currentSc.lastTopQualityCertReferencedEpoch+1);

    if (currentSc.balance < bwtTotalAmount)
    {
        return error("%s():%d - ERROR: Can not update balance %s with amount[%s] for scId=%s, would be negative\n",
            __func__, __LINE__, FormatMoney(currentSc.balance), FormatMoney(bwtTotalAmount), scId.ToString());
    }
    currentSc.balance                          -= bwtTotalAmount;

    scUndoData.prevTopCommittedCertHash            = currentSc.lastTopQualityCertHash;
    scUndoData.prevTopCommittedCertReferencedEpoch = currentSc.lastTopQualityCertReferencedEpoch;
    scUndoData.prevTopCommittedCertQuality         = currentSc.lastTopQualityCertQuality;
    scUndoData.prevTopCommittedCertBwtAmount       = currentSc.lastTopQualityCertBwtAmount;
    scUndoData.lastTopQualityCertView              = currentSc.lastTopQualityCertView;
    scUndoData.contentBitMask |= CSidechainUndoData::AvailableSections::ANY_EPOCH_CERT_DATA;

    currentSc.lastTopQualityCertHash            = certHash;
    currentSc.lastTopQualityCertReferencedEpoch = cert.epochNumber;
    currentSc.lastTopQualityCertQuality         = cert.quality;
    currentSc.lastTopQualityCertBwtAmount       = bwtTotalAmount;
    currentSc.lastTopQualityCertView            = CScCertificateView(cert, currentSc.fixedParams);

    LogPrint("cert", "%s():%d - updated sc state %s\n", __func__, __LINE__, currentSc.ToString());

    scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;

    if(cert.epochNumber != scUndoData.prevTopCommittedCertReferencedEpoch)
    {
        int nextCeasingHeight = currentSc.GetScheduledCeasingHeight();

        //clear up current ceasing height
        if (!HaveSidechainEvents(prevCeasingHeight))
        {
            return error("%s():%d - ERROR-SIDECHAIN-EVENT: scId[%s]: Could not find scheduling for current ceasing height [%d] nor next ceasing height [%d]\n",
                __func__, __LINE__, cert.GetScId().ToString(), prevCeasingHeight, nextCeasingHeight);
        }

        CSidechainEventsMap::iterator scPrevCeasingEventIt = ModifySidechainEvents(prevCeasingHeight);
        scPrevCeasingEventIt->second.scEvents.ceasingScs.erase(cert.GetScId());
        if (!scPrevCeasingEventIt->second.scEvents.IsNull()) //still other sc ceasing at that height or fwds maturing
            scPrevCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        else
            scPrevCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: cert [%s] removes prevCeasingHeight [%d] (certEp=%d, currentEp=%d)\n",
                __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), prevCeasingHeight, cert.epochNumber,
                currentSc.lastTopQualityCertReferencedEpoch);

        //add next ceasing Height
        CSidechainEventsMap::iterator scNextCeasingEventIt = ModifySidechainEvents(nextCeasingHeight);
        if (scNextCeasingEventIt->second.flag == CSidechainEventsCacheEntry::Flags::FRESH) {
            scNextCeasingEventIt->second.scEvents.ceasingScs.insert(cert.GetScId());
        } else {
            scNextCeasingEventIt->second.scEvents.ceasingScs.insert(cert.GetScId());
            scNextCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        }

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: cert [%s] sets nextCeasingHeight to [%d]\n",
                __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), nextCeasingHeight);
    }

    return true;
}

void CCoinsViewCache::NullifyBackwardTransfers(const uint256& certHash, std::vector<CTxInUndo>& nullifiedOuts)
{
    LogPrint("cert", "%s():%d - called for cert %s\n", __func__, __LINE__, certHash.ToString());
    if (certHash.IsNull())
        return;
 
    if (!this->HaveCoins(certHash))
    {
        //in case the cert had not bwt nor change, there won't be any coin generated by cert. Nothing to handle
        LogPrint("cert", "%s():%d - cert has no bwt nor change\n", __func__, __LINE__);
        return;
    }

    CCoinsModifier coins = this->ModifyCoins(certHash);
    assert(coins->nBwtMaturityHeight != 0);

    //null all bwt outputs and add related txundo in block
    for(int pos = coins->nFirstBwtPos; pos < coins->vout.size(); ++pos)
    {
        nullifiedOuts.push_back(CTxInUndo(coins->vout.at(pos)));
        LogPrint("cert", "%s():%d - nullifying %s amount, pos=%d, cert %s\n", __func__, __LINE__,
            FormatMoney(coins->vout.at(pos).nValue), pos, certHash.ToString());
        coins->Spend(pos);
        if (coins->vout.size() == 0)
        {
            CTxInUndo& undo         = nullifiedOuts.back();
            undo.nHeight            = coins->nHeight;
            undo.fCoinBase          = coins->fCoinBase;
            undo.nVersion           = coins->nVersion;
            undo.nFirstBwtPos       = coins->nFirstBwtPos;
            undo.nBwtMaturityHeight = coins->nBwtMaturityHeight;
        }
    }
}

bool CCoinsViewCache::RestoreBackwardTransfers(const uint256& certHash, const std::vector<CTxInUndo>& outsToRestore)
{
    bool fClean = true;
    LogPrint("cert", "%s():%d - called for cert %s\n", __func__, __LINE__, certHash.ToString());

    CCoinsModifier coins = this->ModifyCoins(certHash);

    for (size_t idx = outsToRestore.size(); idx-- > 0;)
    {
        if (outsToRestore.at(idx).nHeight != 0)
        {
            coins->fCoinBase          = outsToRestore.at(idx).fCoinBase;
            coins->nHeight            = outsToRestore.at(idx).nHeight;
            coins->nVersion           = outsToRestore.at(idx).nVersion;
            coins->nFirstBwtPos       = outsToRestore.at(idx).nFirstBwtPos;
            coins->nBwtMaturityHeight = outsToRestore.at(idx).nBwtMaturityHeight;
        }
        else
        {
            if (coins->IsPruned())
            {
                LogPrint("cert", "%s():%d - idx=%d coin is pruned\n", __func__, __LINE__, idx);
                fClean = fClean && error("%s: undo data idx=%d adding output to missing transaction", __func__, idx);
            }
        }
 
        if (coins->IsAvailable(coins->nFirstBwtPos + idx))
        {
            LogPrint("cert", "%s():%d - idx=%d coin is available\n", __func__, __LINE__, idx);
            fClean = fClean && error("%s: undo data idx=%d overwriting existing output", __func__, idx);
        }

        if (coins->vout.size() < (coins->nFirstBwtPos + idx+1))
        {
            coins->vout.resize(coins->nFirstBwtPos + idx+1);
        }
        coins->vout.at(coins->nFirstBwtPos + idx) = outsToRestore.at(idx).txout;
    }
 
    return fClean;
}

#ifdef ENABLE_ADDRESS_INDEXING
void CCoinsViewCache::UpdateBackwardTransferIndexes(const uint256& certHash,
                                                    int certIndex,
                                                    std::vector<std::pair<CAddressIndexKey, CAddressIndexValue>>& addressIndex,
                                                    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>& addressUnspentIndex,
                                                    flagIndexesUpdateType updateType)
{
    LogPrint("cert", "%s():%d - called for cert %s\n", __func__, __LINE__, certHash.ToString());
    if (certHash.IsNull())
        return;
 
    if (!this->HaveCoins(certHash))
    {
        //in case the cert had not bwt nor change, there won't be any coin generated by cert. Nothing to handle
        LogPrint("cert", "%s():%d - cert has no bwt nor change\n", __func__, __LINE__);
        return;
    }

    const CCoins* coins = this->AccessCoins(certHash);
    assert(coins->nBwtMaturityHeight != 0);

    const int multiplier = updateType == flagIndexesUpdateType::SUPERSEDE_CERTIFICATE ? -1 : 1;

    for(int pos = coins->nFirstBwtPos; pos < coins->vout.size(); ++pos)
    {
        const CTxOut& btOut = coins->vout.at(pos);
        
        const CScript::ScriptType scriptType = btOut.scriptPubKey.GetType();
        
        if (scriptType != CScript::ScriptType::UNKNOWN) {
            uint160 const addrHash = btOut.scriptPubKey.AddressHash();
            const AddressType addressType = fromScriptTypeToAddressType(scriptType);

            // update receiving activity
            addressIndex.push_back(std::make_pair(CAddressIndexKey(addressType, addrHash, coins->nHeight, certIndex, certHash, pos, false),
                                                  CAddressIndexValue(btOut.nValue, coins->nBwtMaturityHeight * multiplier)));

            // Add unspent output (to be removed)
            addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(addressType, addrHash, certHash, pos),
                                                         CAddressUnspentValue(btOut.nValue, btOut.scriptPubKey, coins->nHeight, coins->nBwtMaturityHeight * multiplier)));
        }
    }
}
#endif // ENABLE_ADDRESS_INDEXING

bool CCoinsViewCache::RestoreSidechain(const CScCertificate& certToRevert, const CSidechainUndoData& sidechainUndo)
{
    const uint256& certHash       = certToRevert.GetHash();
    const uint256& scId           = certToRevert.GetScId();
    const CAmount& bwtTotalAmount = certToRevert.GetValueOfBackwardTransfers();

    if (!HaveSidechain(scId)) // should not happen
    {
        return error("%s():%d - ERROR: cannot restore sidechain, could not find scId=%s\n",
            __func__, __LINE__, scId.ToString());
    }

    CSidechainsMap::iterator scIt = ModifySidechain(scId);
    CSidechain& currentSc = scIt->second.sidechain;

    LogPrint("cert", "%s():%d - cert to be reverted %s\n", __func__, __LINE__, certToRevert.ToString());
    LogPrint("cert", "%s():%d - SidechainUndoData %s\n", __func__, __LINE__, sidechainUndo.ToString());
    LogPrint("cert", "%s():%d - current sc state %s\n", __func__, __LINE__, currentSc.ToString());

    // RestoreSidechain should be called only once per block and scId, with top qualiy cert only
    assert(certHash == currentSc.lastTopQualityCertHash);

    int ceasingHeightToErase = currentSc.GetScheduledCeasingHeight();

    currentSc.balance += bwtTotalAmount;

    if (certToRevert.epochNumber == (sidechainUndo.prevTopCommittedCertReferencedEpoch + 1))
    {
        assert(sidechainUndo.contentBitMask & CSidechainUndoData::AvailableSections::CROSS_EPOCH_CERT_DATA);
        currentSc.scFees                      = sidechainUndo.scFees;
        currentSc.pastEpochTopQualityCertView = sidechainUndo.pastEpochTopQualityCertView;
    }
    else if (certToRevert.epochNumber == sidechainUndo.prevTopCommittedCertReferencedEpoch)
    {
        // if we are restoring a cert for the same epoch it must have a lower quality than us
        assert(certToRevert.quality > sidechainUndo.prevTopCommittedCertQuality);

        // in this case we have to update the sc balance with undo amount
        currentSc.balance -= sidechainUndo.prevTopCommittedCertBwtAmount;
    }
    else
    {
        return false;  //Inconsistent data
    }

    assert(sidechainUndo.contentBitMask & CSidechainUndoData::AvailableSections::ANY_EPOCH_CERT_DATA);
    currentSc.lastTopQualityCertHash            = sidechainUndo.prevTopCommittedCertHash;
    currentSc.lastTopQualityCertReferencedEpoch = sidechainUndo.prevTopCommittedCertReferencedEpoch;
    currentSc.lastTopQualityCertQuality         = sidechainUndo.prevTopCommittedCertQuality;
    currentSc.lastTopQualityCertBwtAmount       = sidechainUndo.prevTopCommittedCertBwtAmount;
    currentSc.lastTopQualityCertView            = sidechainUndo.lastTopQualityCertView;

    scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;
    LogPrint("cert", "%s():%d - updated sc state %s\n", __func__, __LINE__, currentSc.ToString());

    //we need to modify the ceasing height only if we removed the very first certificate of the epoch
    if(certToRevert.epochNumber != currentSc.lastTopQualityCertReferencedEpoch)
    {
        int ceasingHeightToRestore = currentSc.GetScheduledCeasingHeight();

        //remove current ceasing Height
        if (!HaveSidechainEvents(ceasingHeightToErase))
        {
            return error("%s():%d - ERROR-SIDECHAIN-EVENT: scId[%s]: Could not find scheduling for current ceasing height [%d] nor previous ceasing height [%d]\n",
                __func__, __LINE__, certToRevert.GetScId().ToString(), ceasingHeightToErase, ceasingHeightToRestore);
        }

        CSidechainEventsMap::iterator scCeasingEventToEraseIt = ModifySidechainEvents(ceasingHeightToErase);
        scCeasingEventToEraseIt->second.scEvents.ceasingScs.erase(certToRevert.GetScId());
        if (!scCeasingEventToEraseIt->second.scEvents.IsNull()) //still other sc ceasing at that height or fwds
            scCeasingEventToEraseIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        else
            scCeasingEventToEraseIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT:: scId[%s]: undo of cert [%s] removes currentCeasingHeight [%d]\n",
               __func__, __LINE__, certToRevert.GetScId().ToString(), certToRevert.GetHash().ToString(), ceasingHeightToErase);

        //restore previous ceasing Height
        CSidechainEventsMap::iterator scRestoredCeasingEventIt = ModifySidechainEvents(ceasingHeightToRestore);
        if (scRestoredCeasingEventIt->second.flag == CSidechainEventsCacheEntry::Flags::FRESH) {
           scRestoredCeasingEventIt->second.scEvents.ceasingScs.insert(certToRevert.GetScId());
        } else {
           scRestoredCeasingEventIt->second.scEvents.ceasingScs.insert(certToRevert.GetScId());
           scRestoredCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        }

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: undo of cert [%s] set nextCeasingHeight to [%d]\n",
           __func__, __LINE__, certToRevert.GetScId().ToString(), certToRevert.GetHash().ToString(), ceasingHeightToRestore);
    }

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


bool CCoinsViewCache::HandleSidechainEvents(int height, CBlockUndo& blockUndo, std::vector<CScCertificateStatusUpdateInfo>* pCertsStateInfo)
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
        CSidechainsMap::iterator scMaturingIt = ModifySidechain(maturingScId);
        assert(scMaturingIt->second.sidechain.mImmatureAmounts.count(height));

        scMaturingIt->second.sidechain.balance += scMaturingIt->second.sidechain.mImmatureAmounts.at(height);
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId=%s balance updated to: %s\n",
            __func__, __LINE__, maturingScId.ToString(), FormatMoney(scMaturingIt->second.sidechain.balance));

        blockUndo.scUndoDatabyScId[maturingScId].appliedMaturedAmount = scMaturingIt->second.sidechain.mImmatureAmounts[height];
        blockUndo.scUndoDatabyScId[maturingScId].contentBitMask |= CSidechainUndoData::AvailableSections::MATURED_AMOUNTS;
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: adding immature amount %s for scId=%s in blockundo\n",
            __func__, __LINE__, FormatMoney(scMaturingIt->second.sidechain.mImmatureAmounts[height]), maturingScId.ToString());

        scMaturingIt->second.sidechain.mImmatureAmounts.erase(height);
        scMaturingIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;
    }

    //Handle Ceasing Sidechain
    for (const uint256& ceasingScId : scEvents.ceasingScs)
    {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: about to handle scId[%s] and ceasingHeight [%d]\n",
                __func__, __LINE__, ceasingScId.ToString(), height);

        CSidechain sidechain;
        assert(GetSidechain(ceasingScId, sidechain));

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: lastCertEpoch [%d], lastCertHash [%s]\n",
                __func__, __LINE__, sidechain.lastTopQualityCertReferencedEpoch, sidechain.lastTopQualityCertHash.ToString());

        LogPrint("sc", "%s():%d - set voidedCertHash[%s], ceasingScId = %s\n",
            __func__, __LINE__, sidechain.lastTopQualityCertHash.ToString(), ceasingScId.ToString());

        blockUndo.scUndoDatabyScId[ceasingScId].contentBitMask |= CSidechainUndoData::AvailableSections::CEASED_CERT_DATA;

        if (sidechain.lastTopQualityCertReferencedEpoch == CScCertificate::EPOCH_NULL) {
            assert(sidechain.lastTopQualityCertHash.IsNull());
            continue;
        }

        NullifyBackwardTransfers(sidechain.lastTopQualityCertHash, blockUndo.scUndoDatabyScId[ceasingScId].ceasedBwts);
        if (pCertsStateInfo != nullptr)
            pCertsStateInfo->push_back(CScCertificateStatusUpdateInfo(ceasingScId, sidechain.lastTopQualityCertHash,
                                       sidechain.lastTopQualityCertReferencedEpoch,
                                       sidechain.lastTopQualityCertQuality,
                                       CScCertificateStatusUpdateInfo::BwtState::BWT_OFF));
    }

    CSidechainEventsMap::iterator scCeasingIt = ModifySidechainEvents(height);
    scCeasingIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;
    return true;
}

bool CCoinsViewCache::RevertSidechainEvents(const CBlockUndo& blockUndo, int height, std::vector<CScCertificateStatusUpdateInfo>* pCertsStateInfo)
{
    if (HaveSidechainEvents(height)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT:: attempt to recreate sidechain event at height [%d], but there is one already\n",
            __func__, __LINE__, height);
        return false;
    }

    CSidechainEvents recreatedScEvent;

    // Reverting amount maturing
    for (auto it = blockUndo.scUndoDatabyScId.begin(); it != blockUndo.scUndoDatabyScId.end(); ++it)
    {
        if ((it->second.contentBitMask & CSidechainUndoData::AvailableSections::MATURED_AMOUNTS) == 0)
            continue;

        const uint256& scId = it->first;
        const std::string& scIdString = scId.ToString();

        if (!HaveSidechain(scId))
        {
            // should not happen
            LogPrintf("ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        CAmount amountToRestore = it->second.appliedMaturedAmount;
        CSidechainsMap::iterator scIt = ModifySidechain(scId);
        if (amountToRestore > 0)
        {
            LogPrint("sc", "%s():%d - adding immature amount %s into sc view for scId=%s\n",
                __func__, __LINE__, FormatMoney(amountToRestore), scIdString);

            if (scIt->second.sidechain.balance < amountToRestore)
            {
                LogPrint("sc", "%s():%d - Can not update balance with amount[%s] for scId=%s, would be negative\n",
                    __func__, __LINE__, FormatMoney(amountToRestore), scId.ToString() );
                return false;
            }

            scIt->second.sidechain.mImmatureAmounts[height] += amountToRestore;

            LogPrint("sc", "%s():%d - scId=%s balance before: %s\n", __func__, __LINE__, scIdString, FormatMoney(scIt->second.sidechain.balance));
            scIt->second.sidechain.balance -= amountToRestore;
            LogPrint("sc", "%s():%d - scId=%s balance after: %s\n", __func__, __LINE__, scIdString, FormatMoney(scIt->second.sidechain.balance));

            scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;
        }

        recreatedScEvent.maturingScs.insert(scId);
    }

    // Reverting ceasing sidechains
    for (auto it = blockUndo.scUndoDatabyScId.begin(); it != blockUndo.scUndoDatabyScId.end(); ++it)
    {
        if ((it->second.contentBitMask & CSidechainUndoData::AvailableSections::CEASED_CERT_DATA) == 0)
            continue;

        const uint256& scId = it->first;
        const CSidechain* const pSidechain = AccessSidechain(scId);

        if (pSidechain->lastTopQualityCertReferencedEpoch != CScCertificate::EPOCH_NULL)
        {
            if (!RestoreBackwardTransfers(pSidechain->lastTopQualityCertHash, blockUndo.scUndoDatabyScId.at(scId).ceasedBwts))
                return false;
 
            if (pCertsStateInfo != nullptr)
                pCertsStateInfo->push_back(CScCertificateStatusUpdateInfo(scId, pSidechain->lastTopQualityCertHash,
                                           pSidechain->lastTopQualityCertReferencedEpoch,
                                           pSidechain->lastTopQualityCertQuality,
                                           CScCertificateStatusUpdateInfo::BwtState::BWT_ON));
        }

        recreatedScEvent.ceasingScs.insert(scId);
    }

    if (!recreatedScEvent.IsNull())
    {
        CSidechainEventsMap::iterator scEventIt = ModifySidechainEvents(height);
        scEventIt->second.scEvents = recreatedScEvent;
        scEventIt->second.flag = CSidechainEventsCacheEntry::Flags::FRESH;
    }

    return true;
}

CSidechain::State CCoinsViewCache::GetSidechainState(const uint256& scId) const
{
    CSidechain sidechain;
    if (!GetSidechain(scId, sidechain))
        return CSidechain::State::NOT_APPLICABLE;

    if (!sidechain.isCreationConfirmed())
        return CSidechain::State::UNCONFIRMED;

    if (this->GetHeight() >= sidechain.GetScheduledCeasingHeight())
        return CSidechain::State::CEASED;
    else
        return CSidechain::State::ALIVE;
}


const CScCertificateView& CCoinsViewCache::GetActiveCertView(const uint256& scId) const
{
    static const CScCertificateView nullView;

    const CSidechain* const pSidechain = this->AccessSidechain(scId);

    if (pSidechain == nullptr)
    {
        return nullView;
    }

    if (this->GetSidechainState(scId) == CSidechain::State::CEASED)
        return pSidechain->pastEpochTopQualityCertView;

    if (this->GetSidechainState(scId) == CSidechain::State::UNCONFIRMED)
        return pSidechain->lastTopQualityCertView;

    int certReferencedEpoch = pSidechain->EpochFor(this->GetHeight() + 1 - pSidechain->GetCertSubmissionWindowLength()) - 1;

    if (pSidechain->lastTopQualityCertReferencedEpoch == certReferencedEpoch)
        return pSidechain->lastTopQualityCertView;
    else if (pSidechain->lastTopQualityCertReferencedEpoch - 1 == certReferencedEpoch)
        return pSidechain->pastEpochTopQualityCertView;
    else
        assert(false);

    // just for compiler warning, should never reach this line
    return nullView;
}

CFieldElement CCoinsViewCache::GetCeasingCumTreeHash(const uint256& scId) const
{
    const CSidechain* const pSidechain = this->AccessSidechain(scId);

    if (pSidechain == nullptr)
        return CFieldElement{};

    CFieldElement ceasedBlockCum;
    if (!pSidechain->GetCeasingCumTreeHash(ceasedBlockCum))
        return CFieldElement{};

    return ceasedBlockCum;
}

bool CCoinsViewCache::Flush() {
    bool fOk = base->BatchWrite(cacheCoins, hashBlock, hashAnchor, cacheAnchors, cacheNullifiers, cacheSidechains, cacheSidechainEvents, cacheCswNullifiers);
    cacheCoins.clear();
    cacheSidechains.clear();
    cacheSidechainEvents.clear();
    cacheAnchors.clear();
    cacheNullifiers.clear();
    cachedCoinsUsage = 0;
    cacheCswNullifiers.clear();
    return fOk;
}

bool CCoinsViewCache::DecrementImmatureAmount(const uint256& scId, const CSidechainsMap::iterator& targetEntry, CAmount nValue, int maturityHeight)
{
    // get the map of immature amounts, they are indexed by height
    auto& iaMap = targetEntry->second.sidechain.mImmatureAmounts;

    if (!iaMap.count(maturityHeight) )
    {
        // should not happen
        LogPrintf("ERROR %s():%d - could not find immature balance at height%d\n",
            __func__, __LINE__, maturityHeight);
        return false;
    }

    LogPrint("sc", "%s():%d - immature amount before: %s\n",
        __func__, __LINE__, FormatMoney(iaMap[maturityHeight]));

    if (iaMap[maturityHeight] < nValue)
    {
        // should not happen either
        LogPrintf("ERROR %s():%d - negative balance at height=%d\n",
            __func__, __LINE__, maturityHeight);
        return false;
    }

    iaMap[maturityHeight] -= nValue;
    targetEntry->second.flag = CSidechainsCacheEntry::Flags::DIRTY;

    LogPrint("sc", "%s():%d - immature amount after: %s\n",
        __func__, __LINE__, FormatMoney(iaMap[maturityHeight]));

    if (iaMap[maturityHeight] == 0)
    {
        iaMap.erase(maturityHeight);
        targetEntry->second.flag = CSidechainsCacheEntry::Flags::DIRTY;
        LogPrint("sc", "%s():%d - removed entry height=%d from immature amounts in memory\n",
            __func__, __LINE__, maturityHeight );
    }
    return true;
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

    nResult += txBase.GetJoinSplitValueIn() + txBase.GetCSWValueIn();

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

    // As per csw inputs, we assign them depth zero (i.e. their creation height matches containing tx height)
    // They won't contribute to initial priority, but they will to priority in mempool

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
