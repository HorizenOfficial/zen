#include "sc/sidechain.h"
#include "primitives/transaction.h"
#include "utilmoneystr.h"
#include "txmempool.h"
#include "chainparams.h"
#include "base58.h"
#include "script/standard.h"
#include "univalue.h"
#include "consensus/validation.h"
#include <boost/thread.hpp>
#include <undo.h>
#include <main.h>

extern CChain chainActive;
extern UniValue ValueFromAmount(const CAmount& amount);

static const char DB_SC_INFO = 'i';

using namespace Sidechain;

std::string ScInfo::ToString() const
{
    // TODO
    return "Stll to be implemented";
}

ScMgr& ScMgr::instance()
{
    static ScMgr _instance;
    return _instance;
}

bool ScMgr::sidechainExists(const uint256& scId, const ScCoinsViewCache* scView) const
{
    bool ret = false;
    if (scView)
    {
        ret = scView->sidechainExists(scId);
    }
    else
    {
        LOCK(sc_lock);
        const auto it = mScInfo.find(scId);
        ret = (it != mScInfo.end() );
    }
    return ret;
}

void ScMgr::getScIdSet(std::set<uint256>& sScIds) const
{
    LOCK(sc_lock);
    BOOST_FOREACH(const auto& entry, mScInfo)
    {
        sScIds.insert(entry.first);
    }
}

void ScMgr::copyScInfoMap(ScInfoMap& mapCopy) const
{
    LOCK(sc_lock);
    BOOST_FOREACH(const auto& entry, mScInfo)
    {
        mapCopy[entry.first] = entry.second;
    }
}


bool ScMgr::getScInfo(const uint256& scId, ScInfo& info) const
{
    LOCK(sc_lock);
    const auto it = mScInfo.find(scId);
    if (it == mScInfo.end() )
    {
        return false;
    }

    // create a copy
    info = ScInfo(it->second);
    return true;
}

CAmount ScMgr::getScBalance(const uint256& scId)
{
    LOCK(sc_lock);
    ScInfoMap::iterator it = mScInfo.find(scId);
    if (it == mScInfo.end() )
    {
        // caller should have checked it
        return -1;
    }

    return it->second.balance;
}

bool ScMgr::IsTxApplicableToState(const CTransaction& tx, const ScCoinsViewCache* const scView)
{
    const uint256& txHash = tx.GetHash();

    // check creation
    BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
    {
        const uint256& scId = sc.scId;
        if (sidechainExists(scId, scView) )
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : scid[%s] already created\n",
                __func__, __LINE__, txHash.ToString(), scId.ToString());
            return false;
        }
        LogPrint("sc", "%s():%d - OK: tx[%s] is creating scId[%s]\n",
            __func__, __LINE__, txHash.ToString(), scId.ToString());
    }

    // check fw tx
    BOOST_FOREACH(const auto& ft, tx.vft_ccout)
    {
        const uint256& scId = ft.scId;
        if (!sidechainExists(scId, scView) )
        {
            // return error unless we are creating this sc in the current tx
            if (!hasScCreationOutput(tx, scId) )
            {
                LogPrint("sc", "%s():%d - tx[%s] tries to send funds to scId[%s] not yet created\n",
                    __func__, __LINE__, txHash.ToString(), scId.ToString() );
                return false;
            }
        }
        LogPrint("sc", "%s():%d - OK: tx[%s] is sending [%s] to scId[%s]\n",
            __func__, __LINE__, txHash.ToString(), FormatMoney(ft.nValue), scId.ToString());
    }
    return true;
}

bool ScMgr::anyForwardTransaction(const CTransaction& tx, const uint256& scId)
{
    BOOST_FOREACH(const auto& fwd, tx.vft_ccout)
    {
        if (fwd.scId == scId)
        {
            return true;
        }
    }
    return false;
}

bool ScMgr::hasScCreationOutput(const CTransaction& tx, const uint256& scId)
{
    BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
    {
        if (sc.scId == scId)
        {
            return true;
        }
    }
    return false;
}

bool ScMgr::checkTxSemanticValidity(const CTransaction& tx, CValidationState& state)
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

    BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
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

    return true;
}
bool ScMgr::IsTxAllowedInMempool(const CTxMemPool& pool, const CTransaction& tx, CValidationState& state)
{
    if (!hasScCreationConflictsInMempool(pool, tx) )
    {
        return state.Invalid(error("transaction tries to create scid already created in mempool"),
             REJECT_INVALID, "sidechain-creation");
    }
    return true;
}

bool ScMgr::hasScCreationConflictsInMempool(const CTxMemPool& pool, const CTransaction& tx)
{
    BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
    {
        for (auto it = pool.mapTx.begin(); it != pool.mapTx.end(); ++it)
        {
            const CTransaction& mpTx = it->second.GetTx();

            BOOST_FOREACH(const auto& mpSc, mpTx.vsc_ccout)
            {
                if (mpSc.scId == sc.scId)
                {
                    LogPrint("sc", "%s():%d - invalid tx[%s]: scid[%s] already created by tx[%s]\n",
                        __func__, __LINE__, tx.GetHash().ToString(), sc.scId.ToString(), mpTx.GetHash().ToString() );
                    return false;
                }
            }
        }
    }
    return true;
}

bool ScMgr::initialUpdateFromDb(size_t cacheSize, bool fWipe)
{
    static bool initDone = false;

    if (initDone)
    {
        LogPrintf("%s():%d - Error: could not init from db mpre than once!\n", __func__, __LINE__);
        return false;
    }

    db = new CLevelDBWrapper(GetDataDir() / "sidechains", cacheSize, false, fWipe);

    initDone = true;

    LOCK(sc_lock);

    boost::scoped_ptr<leveldb::Iterator> it(db->NewIterator());

    for (it->SeekToFirst(); it->Valid(); it->Next())
    {
        boost::this_thread::interruption_point();

        try {
            leveldb::Slice slKey = it->key();
            CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            uint256 keyScId;
            ssKey >> chType;
            ssKey >> keyScId;;
 
            if (chType == DB_SC_INFO)
            {
                leveldb::Slice slValue = it->value();
                CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);
                ScInfo info;
                ssValue >> info;
 
                mScInfo[keyScId] = info;
                LogPrint("sc", "%s():%d - scId[%s] added in map\n", __func__, __LINE__, keyScId.ToString() );
            }
            else
            {
                // should never happen
                LogPrintf("%s():%d - Error: could not read from db, invalid record type %c\n", __func__, __LINE__, chType);
                return false;
            }
        }
        catch (const std::exception& e)
        {
            return error("%s: Deserialize or I/O error - %s", __func__, e.what());
        }
    }

    if (!it->status().ok())
    {
        return error("%s():%d - error occurred during db scan", __func__, __LINE__);
    }
    return true;
}

void ScMgr::eraseFromDb(const uint256& scId)
{
    if (db == NULL)
    {
        LogPrintf("%s():%d - Error: sc db not initialized\n", __func__, __LINE__);
        return;
    }

    // erase from level db
    CLevelDBBatch batch;

    try {
        batch.Erase(std::make_pair(DB_SC_INFO, scId));
        bool ret = db->WriteBatch(batch, true);
        if (ret)
        {
            LogPrint("sc", "%s():%d - erased scId=%s from db\n", __func__, __LINE__, scId.ToString() );
        }
        else
        {
            LogPrint("sc", "%s():%d - Error: could not erase scId=%s from db\n", __func__, __LINE__, scId.ToString() );
        }
    }
    catch (const std::exception& e)
    {
        LogPrintf("%s():%d - Error: could not erase scId=%s in db - %s\n", __func__, __LINE__, scId.ToString(), e.what());
    }
    catch (...)
    {
        LogPrintf("%s():%d - Error: could not erase scId=%s in db\n", __func__, __LINE__, scId.ToString());
    }
}


bool ScMgr::writeToDb(const uint256& scId, const ScInfo& info)
{
    if (db == NULL)
    {
        LogPrintf("%s():%d - Error: sc db not initialized\n", __func__, __LINE__);
        return false;
    }

    // write into level db
    CLevelDBBatch batch;
    bool ret = true;

    try {
        batch.Write(std::make_pair(DB_SC_INFO, scId), info );
        // do it synchronously (true)
        ret = db->WriteBatch(batch, true);
        if (ret)
        {
            LogPrint("sc", "%s():%d - wrote scId=%s in db\n", __func__, __LINE__, scId.ToString() );
        }
        else
        {
            LogPrint("sc", "%s():%d - Error: could not write scId=%s in db\n", __func__, __LINE__, scId.ToString() );
        }
    }
    catch (const std::exception& e)
    {
        LogPrintf("%s():%d - Error: could not write scId=%s in db - %s\n", __func__, __LINE__, scId.ToString(), e.what());
        ret = false;
    }
    catch (...)
    {
        LogPrintf("%s():%d - Error: could not write scId=%s in db\n", __func__, __LINE__, scId.ToString());
        ret = false;
    }
    return ret;
}

bool ScMgr::dump_info(const uint256& scId)
{
    LogPrint("sc", "-- side chain [%s] ------------------------\n", scId.ToString());
    ScInfo info;
    if (!getScInfo(scId, info) )
    {
        LogPrint("sc", "===> No such side chain\n");
        return false;
    }

    LogPrint("sc", "  created in block[%s] (h=%d)\n", info.creationBlockHash.ToString(), info.creationBlockHeight );
    LogPrint("sc", "  creationTx[%s]\n", info.creationTxHash.ToString());
    LogPrint("sc", "  balance[%s]\n", FormatMoney(info.balance));
    LogPrint("sc", "  ----- creation data:\n");
    LogPrint("sc", "      withdrawalEpochLength[%d]\n", info.creationData.withdrawalEpochLength);
    LogPrint("sc", "  immature amounts size[%d]\n", info.vImmatureAmounts.size());
// TODO    LogPrint("sc", "      ...more to come...\n");

    return true;
}

void ScMgr::dump_info()
{
    LogPrint("sc", "-- number of side chains found [%d] ------------------------\n", mScInfo.size());
    BOOST_FOREACH(const auto& entry, mScInfo)
    {
        dump_info(entry.first);
    }

    if (db == NULL)
    {
        return;
    }

    // dump leveldb contents on stdout
    boost::scoped_ptr<leveldb::Iterator> it(db->NewIterator());

    for (it->SeekToFirst(); it->Valid(); it->Next())
    {
        leveldb::Slice slKey = it->key();
        CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
        char chType;
        uint256 keyScId;
        ssKey >> chType;
        ssKey >> keyScId;;

        if (chType == DB_SC_INFO)
        {
            leveldb::Slice slValue = it->value();
            CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);
            ScInfo info;
            ssValue >> info;

            std::cout
                << "scId[" << keyScId.ToString() << "]" << std::endl
                << "  ==> balance: " << FormatMoney(info.balance) << std::endl
                << "  creating block hash: " << info.creationBlockHash.ToString() <<
                   " (height: " << info.creationBlockHeight << ")" << std::endl
                << "  creating tx hash: " << info.creationTxHash.ToString() << std::endl
                // creation parameters
                << "  withdrawalEpochLength: " << info.creationData.withdrawalEpochLength << std::endl;
        }
        else
        {
            std::cout << "unknown type " << chType << std::endl;
        }
    }
}

bool ScCoinsViewCache::UpdateScCoins(const CTransaction& tx, const CBlock& block, int blockHeight, CTxUndo& txundo)
{
    const uint256& txHash = tx.GetHash();
    LogPrint("sc", "%s():%d - enter tx=%s\n", __func__, __LINE__, txHash.ToString() );

    const uint256& blockHash = block.GetHash();

    // creation ccout
    BOOST_FOREACH(const auto& cr, tx.vsc_ccout)
    {
        if (sidechainExists(cr.scId))
        {
            LogPrint("sc", "ERROR: %s():%d - CR: scId=%s already in scView\n", __func__, __LINE__, cr.scId.ToString() );
            return false;
        }
 
        ScInfo scInfo;
        scInfo.creationBlockHash = blockHash;
        scInfo.creationBlockHeight = blockHeight;
        scInfo.creationTxHash = txHash;
        scInfo.creationData.withdrawalEpochLength = cr.withdrawalEpochLength;
 
        mUpdate.insert(std::make_pair(cr.scId, scInfo));

        LogPrint("sc", "%s():%d - scId[%s] added in scView\n", __func__, __LINE__, cr.scId.ToString() );

        // store creation undo information in txundo
        txundo.vsc_ccout.push_back( CScCreationOutUndo(cr.scId));

        // mark this sc to be flushed in memory
        sDirty.insert(cr.scId);

    }

    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    int maturityHeight = blockHeight + SC_COIN_MATURITY;

    // forward transfer ccout
    BOOST_FOREACH(auto& ft, tx.vft_ccout)
    {
        auto it_map = mUpdate.find(ft.scId);
        if (it_map == mUpdate.end() )
        {
            // should not happen
            LogPrint("sc", "%s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, ft.scId.ToString() );
            return false;
        }

        // add a new immature balance entry in sc info
        it_map->second.vImmatureAmounts.push_back( ScImmatureAmount( maturityHeight, ft.nValue));

        LogPrint("sc", "%s():%d - immature balance added in scView (h=%d, amount=%s) %s\n",
            __func__, __LINE__, maturityHeight, FormatMoney(ft.nValue), ft.scId.ToString());

        // mark this sc to be flushed in memory
        sDirty.insert(ft.scId);
    }

    return true;
}

ScCoinsViewCache::ScCoinsViewCache()
{
    mUpdate.clear();
    sErase.clear();
    sDirty.clear();
    ScMgr::instance().copyScInfoMap(mUpdate);
}

bool ScCoinsViewCache::UndoScCreation(const CTxUndo& txundo)
{
    // remove sidechain if the case
    BOOST_FOREACH(const auto& crUndo, txundo.vsc_ccout)
    {
        const uint256& scId = crUndo.scId;

        LogPrint("sc", "%s():%d - removing scId=%s\n", __func__, __LINE__, scId.ToString());

        const auto it = mUpdate.find(scId);
        if (it == mUpdate.end() )
        {
            // should not happen 
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }
 
        if (it->second.balance > 0)
        {
            // should not happen either 
            LogPrint("sc", "ERROR %s():%d - scId=%s balance not null: %s\n",
                __func__, __LINE__, scId.ToString(), FormatMoney(it->second.balance));
            return false;
        }
 
        if (mUpdate.erase(scId))
        {
            LogPrint("sc", "%s():%d - scId=%s removed from scView\n", __func__, __LINE__, scId.ToString() );
        }

        // do not update in memory
        sDirty.erase(scId);

        // mark this sc to be removed from in memory
        sErase.insert(scId);
    }
    return true;
}

bool ScCoinsViewCache::IncrementScBalance(int blockHeight, CBlockUndo& blockundo)
{
    LogPrint("sc", "%s():%d - blockHeight=%d, msc_iaundo size=%d\n", __func__, __LINE__, blockHeight,  blockundo.msc_iaundo.size() );

    auto it_map = mUpdate.begin();

    while (it_map != mUpdate.end() )
    {
        const uint256& scId = it_map->first;
        ScInfo& info = it_map->second;
        const std::string& scIdString = scId.ToString();

        auto it_immAmount = info.vImmatureAmounts.begin();

        while (it_immAmount != info.vImmatureAmounts.end() )
        {
            int maturityHeight = it_immAmount->maturityHeight;
            CAmount a = it_immAmount->amount;

            // store immature balances into the blockundo
            LogPrint("sc", "%s():%d - adding immature balance entry into blockundo (h=%d, amount=%s) for scid=%s\n",
                __func__, __LINE__, maturityHeight, FormatMoney(a), scIdString);
            blockundo.msc_iaundo[scId].push_back( CScImmatureAmountUndo(maturityHeight, a));

            if (maturityHeight <= blockHeight)
            {
                LogPrint("sc", "%s():%d - scId=%s balance before: %s\n",
                    __func__, __LINE__, scIdString, FormatMoney(info.balance));

                info.balance += a;

                LogPrint("sc", "%s():%d - scId=%s balance after: %s\n",
                    __func__, __LINE__, scIdString, FormatMoney(info.balance));

                // balance has been updated, remove the entry
                it_immAmount = info.vImmatureAmounts.erase(it_immAmount);

                // mark this sc to be flushed in memory
                sDirty.insert(scId);
            }
            else
            {
                ++it_immAmount;
            }
        }
        ++it_map;
    }
    return true;
}

bool ScCoinsViewCache::DecrementScBalance(int blockHeight, const CBlockUndo& blockundo)
{
    LogPrint("sc", "%s():%d - blockHeight=%d, msc_iaundo size=%d\n", __func__, __LINE__, blockHeight,  blockundo.msc_iaundo.size() );

    auto it_mapundo = blockundo.msc_iaundo.begin();

    while (it_mapundo != blockundo.msc_iaundo.end() )
    {
        const uint256& scId = it_mapundo->first;
        const std::string& scIdString = scId.ToString();

        const auto it_map = mUpdate.find(scId);
        if (it_map == mUpdate.end() )
        {
            // should not happen 
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scIdString );
            return false;
        }

        ScInfo& info = it_map->second;
      
        // replace (undo) the vect of immature amounts of this sc in scview with the blockundo contents
        info.vImmatureAmounts = it_mapundo->second;

        auto it_immAmount = info.vImmatureAmounts.begin();

        while (it_immAmount != info.vImmatureAmounts.end() )
        {
            int maturityHeight = it_immAmount->maturityHeight;
            CAmount a = it_immAmount->amount;

            LogPrint("sc", "%s():%d - entry ( %s / %d / %s) \n", __func__, __LINE__, scIdString, maturityHeight, FormatMoney(a));

            // if maturity border is exactly matched, then decrement sc amount 
            if (maturityHeight == blockHeight)
            {
                LogPrint("sc", "%s():%d - scId=%s balance before: %s\n",
                    __func__, __LINE__, scIdString, FormatMoney(info.balance));
                
                info.balance -= a;

                if (info.balance < 0)
                {
                    LogPrint("sc", "%s():%d - Can not update balance with amount[%s] for scId=%s, would be negative\n",
                        __func__, __LINE__, FormatMoney(a), scIdString );
                    return false;
                }

                LogPrint("sc", "%s():%d - scId=%s balance after: %s\n",
                    __func__, __LINE__, scIdString, FormatMoney(info.balance));
            }
            ++it_immAmount;
        }
 
        // mark this sc to be flushed in memory
        sDirty.insert(scId);
        ++it_mapundo;
    }
    return true;
}

bool ScCoinsViewCache::Flush()
{
    LogPrint("sc", "%s():%d - called\n", __func__, __LINE__);

    LOCK(ScMgr::instance().sc_lock);
    // 1. update the entries that have been added or modified
    BOOST_FOREACH(const auto& entry, mUpdate)
    {
        if (!sDirty.count(entry.first) )
        {
            // skip unmodified records
            continue;
        }

        // write to db
        if (!ScMgr::instance().writeToDb(entry.first, entry.second) )
        {
            return false;
        }

        // update memory
        ScMgr::instance().mScInfo[entry.first] = entry.second;
        LogPrint("sc", "%s():%d - wrote scId=%s in memory\n", __func__, __LINE__, entry.first.ToString() );
        sDirty.erase(entry.first);
    }
    assert(sDirty.size() == 0);

    // 2. process the entries to be erased
    BOOST_FOREACH(const auto& entry, sErase)
    {
        // update memory
        int num = ScMgr::instance().mScInfo.erase(entry);
        if (num)
        {
            // erase from db
            LogPrint("sc", "%s():%d - erased scId=%s from memory\n", __func__, __LINE__, entry.ToString() );
            ScMgr::instance().eraseFromDb(entry);
        }
        else
        {
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in map\n", __func__, __LINE__, entry.ToString() );
            return false;
        }
    }
    return true;
}

bool ScCoinsViewCache::sidechainExists(const uint256& scId) const
{
    const auto it = mUpdate.find(scId);
    return (it != mUpdate.end() );
}


