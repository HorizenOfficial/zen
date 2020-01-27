#ifndef _SIDECHAIN_CORE_H
#define _SIDECHAIN_CORE_H

#include "amount.h"
#include "chain.h"
#include "hash.h"
#include <boost/unordered_map.hpp>
#include "sync.h"

#include "sc/sidechaintypes.h"
#include <coins.h> //temp for merging sidechains with coins

//------------------------------------------------------------------------------------
class CTxMemPool;
class CBlockUndo;
class UniValue;
class CValidationState;
class CLevelDBWrapper;

namespace Sidechain
{
// Validation functions
bool checkTxSemanticValidity(const CTransaction& tx, CValidationState& state);
bool anyForwardTransaction(const CTransaction& tx, const uint256& scId);
bool hasScCreationOutput(const CTransaction& tx, const uint256& scId); // return true if the tx is creating the scid

bool existsInMempool(const CTxMemPool& pool, const CTransaction& tx, CValidationState& state);
// End of Validation functions

class CSidechainsView
{
public:
    CSidechainsView() = default;
    virtual ~CSidechainsView() = default;

    virtual bool HaveScInfo(const uint256& scId) const = 0;
    virtual bool GetScInfo(const uint256& scId, ScInfo& info) const = 0;
    virtual bool queryScIds(std::set<uint256>& scIdsList) const = 0;

    virtual bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap) = 0;
};

class CSidechainsViewBacked : public CSidechainsView
{
public:
    CSidechainsViewBacked(CSidechainsView &viewIn): baseView(viewIn) {}
    bool HaveScInfo(const uint256& scId)              const {return baseView.HaveScInfo(scId);}
    bool GetScInfo(const uint256& scId, ScInfo& info) const {return baseView.GetScInfo(scId,info);}
    bool queryScIds(std::set<uint256>& scIdsList)     const {return baseView.queryScIds(scIdsList);}
    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap)
                    {return baseView.BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, sidechainMap);}

protected:
    CSidechainsView &baseView;
};

class CSidechainsViewCache : public CSidechainsViewBacked
{
public:
    CSidechainsViewCache(CSidechainsView& scView);
    CSidechainsViewCache(const CSidechainsViewCache&) = delete;             //as in coins, forbid building cache on top of another
    CSidechainsViewCache& operator=(const CSidechainsViewCache &) = delete;
    bool HaveDependencies(const CTransaction& tx);

    bool HaveScInfo(const uint256& scId) const;
    bool GetScInfo(const uint256 & scId, ScInfo& targetScInfo) const;
    bool queryScIds(std::set<uint256>& scIdsList) const; //Similar to queryHashes

    bool UpdateScInfo(const CTransaction& tx, const CBlock&, int nHeight);
    bool ApplyMatureBalances(int nHeight, CBlockUndo& blockundo);

    bool RevertTxOutputs(const CTransaction& tx, int nHeight);
    bool RestoreImmatureBalances(int nHeight, const CBlockUndo& blockundo);

    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap);
    bool Flush();

private:
    mutable CSidechainsMap cacheSidechains;
    CSidechainsMap::const_iterator FetchSidechains(const uint256& scId) const;
    void Dump_info() const;
};

class CSidechainViewDB : public CSidechainsView
{
public:
    static CSidechainViewDB& instance();

    bool initPersistence(size_t cacheSize, bool fWipe);
    void reset(); //utility for dtor and unit tests, hence public

    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap);

    bool HaveScInfo(const uint256& scId) const;
    bool GetScInfo(const uint256& scId, ScInfo& info) const;
    bool queryScIds(std::set<uint256>& scIdsList) const;

protected:
    // Disallow instantiation outside of the class. Only UTs should be allowed to inherit and build a fakeDB
    CSidechainViewDB();
    ~CSidechainViewDB();

    bool Persist(const uint256& scId, const ScInfo& info) const;
    bool Erase(const uint256& scId) const;
    void Dump_info() const;

private:
    mutable CCriticalSection sc_lock;
    CLevelDBWrapper* scDb;
}; 

}; // end of namespace

#endif // _SIDECHAIN_CORE_H
