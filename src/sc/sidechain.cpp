#include "sc/sidechain.h"
#include <amount.h>
#include "primitives/transaction.h"
#include "utilmoneystr.h"
#include "txmempool.h"
#include "chainparams.h"
#include "base58.h"
#include "consensus/validation.h"
#include <boost/thread.hpp>
#include <undo.h>
#include <main.h>

namespace Sidechain
{

/*************************** CSidechainsView INTERFACE ****************************/
std::string ScInfo::ToString() const
{
    std::string str;
    str += strprintf("\nScInfo(balance=%s, creatingTxHash=%s, createdInBlock=%s, createdAtBlockHeight=%d, withdrawalEpochLength=%d)\n",
            FormatMoney(balance),
            creationTxHash.ToString().substr(0,10),
            creationBlockHash.ToString().substr(0,10),
            creationBlockHeight,
            creationData.withdrawalEpochLength);

    if (mImmatureAmounts.size() )
    {
        str += "immature amounts:\n";
        for (auto& entry : mImmatureAmounts)
        {
            str += strprintf("   maturityHeight=%d -> amount=%s\n", entry.first, FormatMoney(entry.second));
        }
    }

    return str;
}

/*************************** VALIDATION FUNCTIONS ****************************/
bool checkTxSemanticValidity(const CTransaction& tx, CValidationState& state)
{
    // check version consistency
    if (!tx.IsScVersion() )
    {
        if (!tx.ccIsNull() )
        {
            return state.DoS(100,
                error("mismatch between transaction version and sidechain output presence"),
                REJECT_INVALID, "sidechain-tx-version");
        }

        // anyway skip non sc related tx
        return true;
    }
    else
    {
        // we do not support joinsplit as of now
        if (tx.vjoinsplit.size() > 0)
        {
            return state.DoS(100,
                error("mismatch between transaction version and joinsplit presence"),
                REJECT_INVALID, "sidechain-tx-version");
        }
    }

    const uint256& txHash = tx.GetHash();

    LogPrint("sc", "%s():%d - tx=%s\n", __func__, __LINE__, txHash.ToString() );

    for (const auto& sc: tx.vsc_ccout)
    {
        // check there is at least one fwt associated with this scId
        if (!anyForwardTransaction(tx, sc.scId) )
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : no fwd transactions associated to this creation\n",
                __func__, __LINE__, txHash.ToString() );
            return state.DoS(100, error("%s: no fwd transactions associated to this creation",
                __func__), REJECT_INVALID, "sidechain-creation-missing-fwd-transfer");
        }
    }

    CAmount cumulatedFwdAmount = 0;
    for (const auto& sc: tx.vft_ccout)
    {
        if (sc.nValue == CAmount(0) || !MoneyRange(sc.nValue))
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : fwd trasfer amount is non-positive or larger than %s\n",
                __func__, __LINE__, txHash.ToString(), FormatMoney(MAX_MONEY) );
            return state.DoS(100, error("%s: fwd trasfer amount is outside range",
                __func__), REJECT_INVALID, "sidechain-fwd-transfer-amount-outside-range");
        }

        cumulatedFwdAmount += sc.nValue;
        if (!MoneyRange(cumulatedFwdAmount))
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : cumulated fwd trasfers amount is outside range\n",
                __func__, __LINE__, txHash.ToString() );
            return state.DoS(100, error("%s: cumulated fwd trasfers amount is outside range",
                __func__), REJECT_INVALID, "sidechain-fwd-transfer-amount-outside-range");

        }
    }

    return true;
}

bool anyForwardTransaction(const CTransaction& tx, const uint256& scId)
{
    for (const auto& fwd: tx.vft_ccout)
    {
        if (fwd.scId == scId)
        {
            return true;
        }
    }
    return false;
}

bool hasScCreationOutput(const CTransaction& tx, const uint256& scId)
{
    for (const auto& sc: tx.vsc_ccout)
    {
        if (sc.scId == scId)
        {
            return true;
        }
    }
    return false;
}

bool existsInMempool(const CTxMemPool& pool, const CTransaction& tx, CValidationState& state)
{
    //Check for conflicts in mempool
    for (const auto& sc: tx.vsc_ccout)
    {
        for (auto it = pool.mapTx.begin(); it != pool.mapTx.end(); ++it)
        {
            const CTransaction& mpTx = it->second.GetTx();

            for (const auto& mpSc: mpTx.vsc_ccout)
            {
                if (mpSc.scId == sc.scId)
                {
                    LogPrint("sc", "%s():%d - invalid tx[%s]: scid[%s] already created by tx[%s]\n",
                        __func__, __LINE__, tx.GetHash().ToString(), sc.scId.ToString(), mpTx.GetHash().ToString() );
                    return state.Invalid(error("transaction tries to create scid already created in mempool"),
                    REJECT_INVALID, "sidechain-creation");
                }
            }
        }
    }
    return true;
}

/********************** CSidechainsViewCache **********************/
CSidechainsViewCache::CSidechainsViewCache(CCoinsView* scView): CSidechainsViewBacked(scView) {}

bool CSidechainsViewCache::HaveDependencies(const CTransaction& tx)
{
    const uint256& txHash = tx.GetHash();

    // check creation
    for (const auto& sc: tx.vsc_ccout)
    {
        const uint256& scId = sc.scId;
        if (HaveScInfo(scId))
        {
            LogPrint("sc", "%s():%d - ERROR: Invalid tx[%s] : scid[%s] already created\n",
                __func__, __LINE__, txHash.ToString(), scId.ToString());
            return false;
        }
        LogPrint("sc", "%s():%d - OK: tx[%s] is creating scId[%s]\n",
            __func__, __LINE__, txHash.ToString(), scId.ToString());
    }

    // check fw tx
    for (const auto& ft: tx.vft_ccout)
    {
        const uint256& scId = ft.scId;
        if (!HaveScInfo(scId))
        {
            // return error unless we are creating this sc in the current tx
            if (!hasScCreationOutput(tx, scId) )
            {
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

CSidechainsMap::const_iterator CSidechainsViewCache::FetchSidechains(const uint256& scId) const {
    CSidechainsMap::iterator candidateIt = cacheSidechains.find(scId);
    if (candidateIt != cacheSidechains.end())
        return candidateIt;

    ScInfo tmp;
    if (!baseView->GetScInfo(scId, tmp))
        return cacheSidechains.end();

    //Fill cache and return iterator. The insert in cache below looks cumbersome. However
    //it allows to insert ScInfo and keep iterator to inserted member without extra searches
    CSidechainsMap::iterator ret =
            cacheSidechains.insert(std::make_pair(scId, CSidechainsCacheEntry(tmp, CSidechainsCacheEntry::Flags::DEFAULT ))).first;

    return ret;
}

bool CSidechainsViewCache::HaveScInfo(const uint256& scId) const
{
    CSidechainsMap::const_iterator it = FetchSidechains(scId);
    return (it != cacheSidechains.end()) && (it->second.flag != CSidechainsCacheEntry::Flags::ERASED);
}

bool CSidechainsViewCache::GetScInfo(const uint256 & scId, ScInfo& targetScInfo) const
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

bool CSidechainsViewCache::queryScIds(std::set<uint256>& scIdsList) const
{
    if(!baseView->queryScIds(scIdsList))
        return false;

    // Note that some of the values above may have been erased in current cache.
    // Also new id may be in current cache but not in persisted
    for (const auto& entry: cacheSidechains)
    {
      if (entry.second.flag == CSidechainsCacheEntry::Flags::ERASED)
          scIdsList.erase(entry.first);
      else
          scIdsList.insert(entry.first);
    }

    return true;
}

bool CSidechainsViewCache::UpdateScInfo(const CTransaction& tx, const CBlock& block, int blockHeight)
{
    const uint256& txHash = tx.GetHash();
    LogPrint("sc", "%s():%d - enter tx=%s\n", __func__, __LINE__, txHash.ToString() );

    // creation ccout
    for (const auto& cr: tx.vsc_ccout)
    {
        if (HaveScInfo(cr.scId))
        {
            LogPrint("sc", "ERROR: %s():%d - CR: scId=%s already in scView\n", __func__, __LINE__, cr.scId.ToString() );
            return false;
        }

        ScInfo scInfo;
        scInfo.creationBlockHash = block.GetHash();
        scInfo.creationBlockHeight = blockHeight;
        scInfo.creationTxHash = txHash;
        scInfo.creationData.withdrawalEpochLength = cr.withdrawalEpochLength;

        cacheSidechains[cr.scId] = CSidechainsCacheEntry(scInfo, CSidechainsCacheEntry::Flags::FRESH);

        LogPrint("sc", "%s():%d - scId[%s] added in scView\n", __func__, __LINE__, cr.scId.ToString() );
    }

    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = blockHeight + SC_COIN_MATURITY;

    // forward transfer ccout
    for(auto& ft: tx.vft_ccout)
    {
        ScInfo targetScInfo;
        if (!GetScInfo(ft.scId, targetScInfo))
        {
            // should not happen
            LogPrint("sc", "%s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, ft.scId.ToString() );
            return false;
        }

        // add a new immature balance entry in sc info or increment it if already there
        targetScInfo.mImmatureAmounts[maturityHeight] += ft.nValue;
        cacheSidechains[ft.scId] = CSidechainsCacheEntry(targetScInfo, CSidechainsCacheEntry::Flags::DIRTY);

        LogPrint("sc", "%s():%d - immature balance added in scView (h=%d, amount=%s) %s\n",
            __func__, __LINE__, maturityHeight, FormatMoney(ft.nValue), ft.scId.ToString());
    }

    return true;
}

bool CSidechainsViewCache::ApplyMatureBalances(int blockHeight, CBlockUndo& blockundo)
{
    LogPrint("sc", "%s():%d - blockHeight=%d, msc_iaundo size=%d\n", __func__, __LINE__, blockHeight,  blockundo.msc_iaundo.size() );

    std::set<uint256> allKnowScIds;
    if (!queryScIds(allKnowScIds))
        return false;

    for(auto it_set = allKnowScIds.begin(); it_set != allKnowScIds.end(); ++it_set)
    {
        const uint256& scId = *it_set;
        ScInfo info;
        assert(GetScInfo(scId, info));

        for(auto it_ia_map = info.mImmatureAmounts.begin(); it_ia_map != info.mImmatureAmounts.end(); )
        {
            int maturityHeight = it_ia_map->first;
            CAmount a = it_ia_map->second;

            LogPrint("sc", "%s():%d - adding immature balance entry into blockundo (h=%d, amount=%s) for scid=%s\n",
                __func__, __LINE__, maturityHeight, FormatMoney(a), scId.ToString());

            // store immature balances into the blockundo obj
            blockundo.msc_iaundo[scId][maturityHeight] = a;

            if (maturityHeight == blockHeight)
            {
                LogPrint("sc", "%s():%d - scId=%s balance before: %s\n",
                    __func__, __LINE__, scId.ToString(), FormatMoney(info.balance));

                // if maturity has been reached apply it to balance in scview
                info.balance += a;

                LogPrint("sc", "%s():%d - scId=%s balance after: %s\n",
                    __func__, __LINE__, scId.ToString(), FormatMoney(info.balance));

                // scview balance has been updated, remove the entry in scview immature map
                it_ia_map = info.mImmatureAmounts.erase(it_ia_map);
                cacheSidechains[scId] = CSidechainsCacheEntry(info, CSidechainsCacheEntry::Flags::DIRTY);
            }
            else
            if (maturityHeight < blockHeight)
            {
                // should not happen
                LogPrint("sc", "ERROR: %s():%d - scId=%s maturuty(%d) < blockHeight(%d)\n",
                    __func__, __LINE__, scId.ToString(), maturityHeight, blockHeight);
                return false;
            }
            else
            {
                ++it_ia_map;
            }
        }
    }
    return true;
}

bool CSidechainsViewCache::RevertTxOutputs(const CTransaction& tx, int nHeight)
{
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = nHeight + SC_COIN_MATURITY;

    // revert forward transfers
    for(const auto& entry: tx.vft_ccout)
    {
        const uint256& scId = entry.scId;

        LogPrint("sc", "%s():%d - removing fwt for scId=%s\n", __func__, __LINE__, scId.ToString());

        ScInfo targetScInfo;
        if (!GetScInfo(scId, targetScInfo))
        {
            // should not happen
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        // get the map of immature amounts, they are indexed by height
        auto& iaMap = targetScInfo.mImmatureAmounts;

        if (!iaMap.count(maturityHeight) )
        {
            // should not happen
            LogPrint("sc", "ERROR %s():%d - scId=%s could not find immature balance at height%d\n",
                __func__, __LINE__, scId.ToString(), maturityHeight);
            return false;
        }

        LogPrint("sc", "%s():%d - immature amount before: %s\n",
            __func__, __LINE__, FormatMoney(iaMap[maturityHeight]));

        if (iaMap[maturityHeight] < entry.nValue)
        {
            // should not happen either
            LogPrint("sc", "ERROR %s():%d - scId=%s negative balance at height=%d\n",
                __func__, __LINE__, scId.ToString(), maturityHeight);
            return false;
        }

        iaMap[maturityHeight] -= entry.nValue;
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
    }

    // remove sidechain if the case
    for(const auto& entry: tx.vsc_ccout)
    {
        const uint256& scId = entry.scId;

        LogPrint("sc", "%s():%d - removing scId=%s\n", __func__, __LINE__, scId.ToString());

        ScInfo targetScInfo;
        if (!GetScInfo(scId, targetScInfo))
        {
            // should not happen
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
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

bool CSidechainsViewCache::RestoreImmatureBalances(int blockHeight, const CBlockUndo& blockundo)
{
    LogPrint("sc", "%s():%d - blockHeight=%d, msc_iaundo size=%d\n", __func__, __LINE__, blockHeight,  blockundo.msc_iaundo.size() );

    // loop in the map of the blockundo and process each sidechain id
    for (auto it_ia_undo_map = blockundo.msc_iaundo.begin(); it_ia_undo_map != blockundo.msc_iaundo.end(); )
    {
        const uint256& scId = it_ia_undo_map->first;

        ScInfo targetScInfo;
        if (!GetScInfo(scId, targetScInfo))
        {
            // should not happen
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        // replace (undo) the map of immature amounts of this sc in scview with the blockundo contents
        targetScInfo.mImmatureAmounts = it_ia_undo_map->second;

        // process the entry with key=height if it exists.
        auto it_ia_map = targetScInfo.mImmatureAmounts.find(blockHeight);

        if (it_ia_map != targetScInfo.mImmatureAmounts.end() )
        {
            // For such an entry the maturity border is exactly matched, therefore decrement sc balance in scview

            CAmount a = it_ia_map->second;
            LogPrint("sc", "%s():%d - entry ( %s / %d / %s) \n",
                __func__, __LINE__, scId.ToString(), blockHeight, FormatMoney(a));

            LogPrint("sc", "%s():%d - scId=%s balance before: %s\n",
                __func__, __LINE__, scId.ToString(), FormatMoney(targetScInfo.balance));

            if (targetScInfo.balance < a)
            {
                LogPrint("sc", "%s():%d - Can not update balance with amount[%s] for scId=%s, would be negative\n",
                    __func__, __LINE__, FormatMoney(a), scId.ToString() );
                return false;
            }

            targetScInfo.balance -= a;
            cacheSidechains[scId] = CSidechainsCacheEntry(targetScInfo, CSidechainsCacheEntry::Flags::DIRTY);

            LogPrint("sc", "%s():%d - scId=%s balance after: %s\n",
                __func__, __LINE__, scId.ToString(), FormatMoney(targetScInfo.balance));
        }

        ++it_ia_undo_map;
    }

    return true;
}

bool CSidechainsViewCache::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
        const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
        CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap)
{
    for (auto& entryToWrite : sidechainMap) {
        CSidechainsMap::iterator itLocalCacheEntry = cacheSidechains.find(entryToWrite.first);

        switch (entryToWrite.second.flag) {
            case CSidechainsCacheEntry::Flags::FRESH:
                assert(itLocalCacheEntry == cacheSidechains.end()); //A fresh entry should not exist in localCache
                cacheSidechains[entryToWrite.first] = entryToWrite.second;
                break;
            case CSidechainsCacheEntry::Flags::DIRTY:               //A dirty entry may or may not exist in localCache
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

    sidechainMap.clear();
    return true;
}

bool CSidechainsViewCache::Flush()
{
    LogPrint("sc", "%s():%d - called\n", __func__, __LINE__);

    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;

    if (!baseView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, cacheSidechains))
        return false;

    cacheSidechains.clear();
    return true;
}

void CSidechainsViewCache::Dump_info() const
{
    std::set<uint256> scIdsList;
    queryScIds(scIdsList);
    LogPrint("sc", "-- number of side chains found [%d] ------------------------\n", scIdsList.size());
    for(const auto& scId: scIdsList)
    {
        LogPrint("sc", "-- side chain [%s] ------------------------\n", scId.ToString());
        ScInfo info;
        if (!GetScInfo(scId, info) )
        {
            LogPrint("sc", "===> No such side chain\n");
            return;
        }

        LogPrint("sc", "  created in block[%s] (h=%d)\n", info.creationBlockHash.ToString(), info.creationBlockHeight );
        LogPrint("sc", "  creationTx[%s]\n", info.creationTxHash.ToString());
        LogPrint("sc", "  balance[%s]\n", FormatMoney(info.balance));
        LogPrint("sc", "  ----- creation data:\n");
        LogPrint("sc", "      withdrawalEpochLength[%d]\n", info.creationData.withdrawalEpochLength);
        LogPrint("sc", "  immature amounts size[%d]\n", info.mImmatureAmounts.size());
    }

    return;
}

} // end of namespace
