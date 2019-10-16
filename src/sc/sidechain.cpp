#include "sc/sidechaincore.h"
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

bool ScMgr::sidechainExists(const uint256& scId) const
{
    const auto it = mScInfo.find(scId);
    return (it != mScInfo.end() );
}

bool ScMgr::getScInfo(const uint256& scId, ScInfo& info) const
{
    const auto it = mScInfo.find(scId);
    if (it == mScInfo.end() )
    {
        return false;
    }

    // create a copy
    info = ScInfo(it->second);
    return true;
}

CAmount ScMgr::getSidechainBalance(const uint256& scId)
{
    ScInfoMap::iterator it = mScInfo.find(scId);
    if (it == mScInfo.end() )
    {
        // caller should have checked it
        return -1;
    }

    return it->second.balance;
}

bool ScMgr::checkSidechainState(const CTransaction& tx)
{
    const uint256& txHash = tx.GetHash();

    // check creation
    BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
    {
        ScInfo info;
        if (getScInfo(sc.scId, info) )
        {
            if (info.creationTxHash != txHash )
            {
                LogPrint("sc", "%s():%d - Invalid tx[%s] : scid[%s] already created by tx[%s]\n",
                    __func__, __LINE__, txHash.ToString(), sc.scId.ToString(), info.creationTxHash.ToString() );
                return false;
            }

            // this tx is the owner, go on without error. Can happen also in check performed at
            // startup in VerifyDB 
            LogPrint("sc", "%s():%d - OK tx[%s] : scid[%s] creation detected\n",
                __func__, __LINE__, txHash.ToString(), sc.scId.ToString() );

        }
        else
        {
            // this is a brand new sc
            LogPrint("sc", "%s():%d - No such scid[%s], tx[%s] is creating it\n",
                __func__, __LINE__, sc.scId.ToString(), txHash.ToString() );
        }
    }

    // check fw tx
    CValidationState state;
    if (!checkSidechainOutputs(tx, state) )
    {
        return false;
    }

    return true;
}

bool ScMgr::checkSidechainCreation(const CTransaction& tx, CValidationState& state)
{
    const uint256& txHash = tx.GetHash();

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

bool ScMgr::hasSidechainCreationOutput(const CTransaction& tx, const uint256& scId)
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

bool ScMgr::checkSidechainOutputs(const CTransaction& tx, CValidationState& state)
{
    const uint256& txHash = tx.GetHash();

    BOOST_FOREACH(const auto& ft, tx.vft_ccout)
    {
        const uint256& scId = ft.scId;
        if (!sidechainExists(scId) )
        {
            // return error unless we are creating this sc in the current tx
            if (!hasSidechainCreationOutput(tx, scId) )
            {
                LogPrint("sc", "%s():%d - tx[%s]: scid[%s] not found\n",
                    __func__, __LINE__, txHash.ToString(), scId.ToString() );
                return state.DoS(10,
                    error("transaction tries to forward transfer to a scid not yet created"),
                    REJECT_SCID_NOT_FOUND, "sidechain-forward-transfer");
            }
            else
            {
                LogPrint("sc", "%s():%d - tx[%s]: scid[%s] is being created in this tx\n",
                    __func__, __LINE__, txHash.ToString(), scId.ToString() );
            }
        }
        LogPrint("sc", "%s():%d - tx[%s]: scid[%s], fw[%s]\n",
            __func__, __LINE__, txHash.ToString(), scId.ToString(), FormatMoney(ft.nValue) );
    }
    return true;
}

bool ScMgr::checkTransaction(const CTransaction& tx, CValidationState& state)
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

    LogPrint("sc", "%s():%d - tx=%s\n", __func__, __LINE__, tx.GetHash().ToString() );

    if (!checkSidechainCreation(tx, state) )
    {
        return false;
    }

    return true;
}

bool ScMgr::IsTxAllowedInMempool(const CTxMemPool& pool, const CTransaction& tx, CValidationState& state)
{
    if (!hasSCCreationConflictsInMempool(pool, tx) )
    {
        return state.Invalid(error("transaction tries to create scid already created in mempool"),
             REJECT_INVALID, "sidechain-creation");
    }
    return true;
}

bool ScMgr::hasSCCreationConflictsInMempool(const CTxMemPool& pool, const CTransaction& tx)
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

void ScMgr::fundCcRecipients(const CTransaction& tx, std::vector<CcRecipientVariant >& vecCcSend)
{
    BOOST_FOREACH(auto& entry, tx.vsc_ccout)
    {
        CRecipientScCreation sc;
        sc.scId = entry.scId;
        // when funding a tx with sc creation, the amount is already contained in vcout to foundation
        sc.creationData.withdrawalEpochLength = entry.withdrawalEpochLength;

        vecCcSend.push_back(CcRecipientVariant(sc));
    }

    BOOST_FOREACH(auto& entry, tx.vcl_ccout)
    {
        CRecipientCertLock cl;
        cl.scId = entry.scId;
        cl.nValue = entry.nValue;
        cl.address = entry.address;
        cl.epoch = entry.activeFromWithdrawalEpoch;

        vecCcSend.push_back(CcRecipientVariant(cl));
    }

    BOOST_FOREACH(auto& entry, tx.vft_ccout)
    {
        CRecipientForwardTransfer ft;
        ft.scId = entry.scId;
        ft.address = entry.address;
        ft.nValue = entry.nValue;

        vecCcSend.push_back(CcRecipientVariant(ft));
    }
}

CRecipientForwardTransfer::CRecipientForwardTransfer(const CTxForwardTransferOut& ccout)
{
    scId    = ccout.scId;
    nValue  = ccout.nValue;
    address = ccout.address;
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

void ScMgr::fillJSON(const uint256& scId, const ScInfo& info, UniValue& sc)
{
    sc.push_back(Pair("scid", scId.GetHex()));
    sc.push_back(Pair("balance", ValueFromAmount(info.balance)));
    sc.push_back(Pair("creating tx hash", info.creationTxHash.GetHex()));
    sc.push_back(Pair("created in block", info.creationBlockHash.ToString()));
    sc.push_back(Pair("created at block height", info.creationBlockHeight));
    // creation parameters
    sc.push_back(Pair("withdrawalEpochLength", info.creationData.withdrawalEpochLength));
}

bool ScMgr::fillJSON(const uint256& scId, UniValue& sc)
{
    ScInfo info;
    if (!ScMgr::instance().getScInfo(scId, info) )
    {
        LogPrint("sc", "scid[%s] not yet created\n", scId.ToString() );
        return false; 
    }
 
    fillJSON(scId, info, sc);
    return true;
}

void ScMgr::fillJSON(UniValue& result)
{
	LOCK(sc_lock);
    BOOST_FOREACH(const auto& entry, mScInfo)
    {
        UniValue sc(UniValue::VOBJ);
        fillJSON(entry.first, entry.second, sc);
        result.push_back(sc);
    }
}

bool ScCoinsViewCache::UpdateScCoins(const CTransaction& tx, const CBlock& block, int nHeight)
{
    LogPrint("sc", "%s():%d - enter tx=%s\n", __func__, __LINE__, tx.GetHash().ToString() );
    if (!createSidechain(tx, block, nHeight) )
    {
        // should never fail at this point
        LogPrint("sc", "%s():%d - ERROR: tx=%s\n", __func__, __LINE__, tx.GetHash().ToString() );
        return false;
    }

    BOOST_FOREACH(auto& ft, tx.vft_ccout)
    {
        if (!updateSidechainBalance(ft.scId, ft.nValue) )
        {
            LogPrint("sc", "ERROR: %s():%d - could not update sc balance: scId=%s\n",
                __func__, __LINE__, ft.scId.ToString() );
            return false;
        }
    }
    return true;
}

bool ScCoinsViewCache::UpdateScCoins(const CTxUndo& txundo)
{
    LogPrint("sc", "%s():%d - enter\n", __func__, __LINE__);

    // update sc balance
    BOOST_FOREACH(const auto& ftUndo, txundo.vft_ccout)
    {
        if (!updateSidechainBalance(ftUndo.scId, (-ftUndo.nValue)) )
        {
            return false;
        }
    }
    // remove sidechain if the case
    BOOST_FOREACH(const auto& crUndo, txundo.vsc_ccout)
    {
        LogPrint("sc", "%s():%d - removing scId=%s\n", __func__, __LINE__, crUndo.scId.ToString());
        if (!deleteSidechain(crUndo.scId) )
        {
            return false;
        }
    }
    return true;
}

bool ScCoinsViewCache::updateSidechainBalance(const uint256& scId, const CAmount& amount)
{
    ScInfo info;
    ScInfoMap::iterator it = mUpdate.find(scId);
    if (it == mUpdate.end() )
    {
        LogPrint("sc", "%s():%d - sc not in scView, checking memory\n", __func__, __LINE__);
        if (!ScMgr::instance().getScInfo(scId, info) )
        {
            // should not happen
            LogPrint("sc", "%s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, scId.ToString() );
            return false;
        }
        LogPrint("sc", "%s():%d - sc found in memory\n", __func__, __LINE__);
    }
    else
    {
        LogPrint("sc", "%s():%d - sc found in scView\n", __func__, __LINE__);
        info = it->second;
    }

    LogPrint("sc", "%s():%d - scId=%s balance before: %s\n",
        __func__, __LINE__, scId.ToString(), FormatMoney(info.balance));

    info.balance += amount;
    if (info.balance < 0)
    {
        LogPrint("sc", "%s():%d - Can not update balance with amount[%s] for scId=%s, would be negative\n",
            __func__, __LINE__, FormatMoney(amount), scId.ToString() );
        return false;
    }

    LogPrint("sc", "%s():%d - scId=%s balance after: %s\n",
        __func__, __LINE__, scId.ToString(), FormatMoney(info.balance));

    // store copy in scView
    mUpdate[scId] = info;

    return true; 
}

bool ScCoinsViewCache::addSidechain(const uint256& scId, const ScInfo& info)
{
    return (mUpdate.insert(std::make_pair(scId, info)).second);
}

void ScCoinsViewCache::removeSidechain(const uint256& scId)
{
    LogPrint("sc", "%s():%d - adding scId=%s in scView candidates for erasure\n", __func__, __LINE__, scId.ToString() );
    sErase.insert(scId);
}

bool ScCoinsViewCache::createSidechain(const CTransaction& tx, const CBlock& block, int nHeight)
{
    const uint256& blockHash = block.GetHash();
    const uint256& txHash = tx.GetHash();

    BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
    {
        if (ScMgr::instance().sidechainExists(sc.scId) )
        {
            // should not happen at this point due to previous checks
            LogPrint("sc", "ERROR: %s():%d - CR: scId=%s already in map\n", __func__, __LINE__, sc.scId.ToString() );
            return false;
        }
 
        ScInfo scInfo;
        scInfo.creationBlockHash = blockHash;
        scInfo.creationBlockHeight = nHeight;
        scInfo.creationTxHash = txHash;
        scInfo.creationData.withdrawalEpochLength = sc.withdrawalEpochLength;
 
        if (addSidechain(sc.scId, scInfo) )
        {
            LogPrint("sc", "%s():%d - scId[%s] added in scView\n", __func__, __LINE__, sc.scId.ToString() );
        }
        else
        {
            // should never fail
            LogPrint("sc", "ERROR: %s():%d - scId=%s could not add to scView\n", __func__, __LINE__, sc.scId.ToString() );
            return false;
        }
    }
    return true;
}

bool ScCoinsViewCache::deleteSidechain(const uint256& scId)
{
    if (!ScMgr::instance().sidechainExists(scId) )
    {
        // should not happen 
        LogPrint("sc", "ERROR: %s():%d - CR: scId=%s not in memory\n", __func__, __LINE__, scId.ToString() );
        return false;
    }

    const auto it = mUpdate.find(scId);
    if (it == mUpdate.end() )
    {
        // should not happen 
        LogPrint("sc", "ERROR: %s():%d - CR: scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
        return false;
    }

    if (it->second.balance > 0)
    {
        // should not happen either 
        LogPrint("sc", "ERROR %s():%d - scId=%s balance not null: %s\n",
            __func__, __LINE__, scId.ToString(), FormatMoney(it->second.balance));
        return false;
    }

    removeSidechain(scId);
    return true;
}

bool ScCoinsViewCache::Flush()
{
    LogPrint("sc", "%s():%d - called\n", __func__, __LINE__);

    LOCK(ScMgr::instance().sc_lock);
    // 1. update the entries with current balance
    BOOST_FOREACH(const auto& entry, mUpdate)
    {
        // write to db
        if (!ScMgr::instance().writeToDb(entry.first, entry.second) )
        {
            return false;
        }

        // update memory
        ScMgr::instance().mScInfo[entry.first] = entry.second;
        LogPrint("sc", "%s():%d - wrote scId=%s in memory\n", __func__, __LINE__, entry.first.ToString() );
    }

    // 2. process the entries to be erased
    BOOST_FOREACH(const auto& entry, sErase)
    {
        // update memory
        int num = ScMgr::instance().mScInfo.erase(entry);
        if (num)
        {
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

