#include "sc/sidechain.h"
#include "primitives/transaction.h"
#include "utilmoneystr.h"
#include "txmempool.h"
#include "chainparams.h"
#include "base58.h"
#include "script/standard.h"
#include "univalue.h"
#include "consensus/validation.h"

extern CChain chainActive;
extern UniValue ValueFromAmount(const CAmount& amount);

static const char DB_SC_INFO = 'i';

using namespace Sidechain;

std::string ScInfo::ToString() const
{
    return "Stll to be implemented";
}

ScMgr& ScMgr::instance()
{
    static ScMgr test;
    return test;
}

bool ScMgr::sidechainExists(const uint256& scId)
{
    ScInfoMap::iterator it = mScInfo.find(scId);
    return (it != mScInfo.end() );
}

bool ScMgr::getScInfo(const uint256& scId, ScInfo& info)
{
    ScInfoMap::iterator it = mScInfo.find(scId);
    if (it == mScInfo.end() )
    {
        return false;
    }

    info = it->second;
    return true;
}

bool ScMgr::updateSidechainBalance(const uint256& scId, const CAmount& amount)
{
    LOCK(sc_lock);
    ScInfoMap::iterator it = mScInfo.find(scId);
    if (it == mScInfo.end() )
    {
        // caller should have checked it
        return false;
    }

    it->second.balance += amount;

    return writeToDb(scId, it->second);
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

bool ScMgr::addSidechain(const uint256& scId, ScInfo& info)
{
    LOCK(sc_lock);
    // checks should be done by caller
    mScInfo[scId] = info;

    return writeToDb(scId, info);
}

void ScMgr::removeSidechain(const uint256& scId)
{
    LOCK(sc_lock);
    // checks should be done by caller
    int num = mScInfo.erase(scId);
    if (num)
    {
        eraseFromDb(scId);
    }
    LogPrint("sc", "%s():%d - (erased=%d) scId=%s\n", __func__, __LINE__, num, scId.ToString() );
}

bool ScMgr::checkSidechainCreation(const CTransaction& tx)
{
    if (tx.vsc_ccout.size() )
    {
        const uint256 txHash = tx.GetHash();

        BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
        {
            ScInfo info;
            if (getScInfo(sc.scId, info) )
            {
                if (info.ownerTxHash != txHash )
                {
                    LogPrint("sc", "%s():%d - Invalid tx[%s] : scid[%s] already created by tx[%s]\n",
                        __func__, __LINE__, txHash.ToString(), sc.scId.ToString(), info.ownerTxHash.ToString() );
                    return false;
                }
                else
                {
                    LogPrint("sc", "%s():%d - OK tx[%s] : scid[%s] creation detected\n",
                        __func__, __LINE__, txHash.ToString(), sc.scId.ToString() );
                }
            }
            else
            {
                // brand new sc
                LogPrint("sc", "%s():%d - No such scid[%s], tx[%s] is creating it\n",
                    __func__, __LINE__, sc.scId.ToString(), txHash.ToString() );
            }
        }
    }
    return true;
}

bool ScMgr::checkSidechainForwardTransaction(const CTransaction& tx)
{
    if (tx.vft_ccout.size() )
    {
        const uint256 txHash = tx.GetHash();

        BOOST_FOREACH(const auto& sc, tx.vft_ccout)
        {
            ScInfo info;
            if (!getScInfo(sc.scId, info) )
            {
                LogPrint("sc", "%s():%d - tx[%s]: scid[%s] not found\n",
                    __func__, __LINE__, txHash.ToString(), sc.scId.ToString() );
                return false;
            }
        }
    }
    return true;
}

bool ScMgr::checkTransaction(const CTransaction& tx, CValidationState& state)
{
    // check version consistency
    if (tx.nVersion != SC_TX_VERSION )
    {
        if (tx.vsc_ccout.size() != 0 || tx.vcl_ccout.size() != 0 || tx.vft_ccout.size() != 0)
        {
            return state.DoS(100,
                error("mismatch between transaction version and sidechain output presence"), 
                REJECT_INVALID, "sidechain-tx-version");
        }
    }

    if (!checkSidechainCreation(tx) )
    {
        return state.DoS(10,
            error("transaction tries to create scid already created"),
            REJECT_INVALID, "sidechain-creation");
    }

    if (!checkSidechainForwardTransaction(tx) )
    {
        return state.DoS(10,
            error("transaction tries to forward trasnfer to a scid not yet created"),
            REJECT_INVALID, "sidechain-forward-transfer");
    }

    return true;
}


bool ScMgr::onBlockConnected(const CBlock& block, int nHeight)
{
    uint256 hash = block.GetHash();

    LogPrint("sc", "%s():%d - Entering with block [%s]\n", __func__, __LINE__, hash.ToString() );

    int txIndex = 0;
    BOOST_FOREACH(const CTransaction& tx, block.vtx)
    {
        if (tx.nVersion == SC_TX_VERSION)
        {
            LogPrint("sc", "%s():%d - tx=%s\n", __func__, __LINE__, tx.GetHash().ToString() );

            BOOST_FOREACH(auto& sc, tx.vsc_ccout)
            {
                if (sidechainExists(sc.scId) )
                {
                    // should not happen at this point due to previous checks
                    LogPrint("sc", "#### %s():%d - CR: scId=%s already in map #######\n", __func__, __LINE__, sc.scId.ToString() );
                    return false;
                }

                ScInfo scInfo;
                scInfo.ownerBlockHash = block.GetHash();
                scInfo.creationBlockHeight = nHeight;
                scInfo.creationTxIndex = txIndex;
                scInfo.ownerTxHash = tx.GetHash();
                scInfo.creationData.startBlockHeight = sc.startBlockHeight;

                addSidechain(sc.scId, scInfo);
                LogPrint("sc", "%s():%d - scId[%s] added in map\n", __func__, __LINE__, sc.scId.ToString() );
            }

            BOOST_FOREACH(auto& ft, tx.vft_ccout)
            {
                if (!sidechainExists(ft.scId))
                {
                    // should not happen at this point due to previous checks
                    LogPrint("sc", "#### %s():%d - FW: scId=%s not in map #######\n", __func__, __LINE__, ft.scId.ToString() );
                    return false;
                }

                LogPrint("sc", "@@@ %s():%d - scId=%s balance before: %s\n",
                    __func__, __LINE__, ft.scId.ToString(), FormatMoney(getSidechainBalance(ft.scId)));

                updateSidechainBalance(ft.scId, ft.nValue);

                LogPrint("sc", "@@@ %s():%d - scId=%s balance after:  %s\n",
                    __func__, __LINE__, ft.scId.ToString(), FormatMoney(getSidechainBalance(ft.scId)));
            }

        }
        txIndex++;
    }

    // TODO test, remove it
    dump_info();

    return true;
}

bool ScMgr::onBlockDisconnected(const CBlock& block)
{
    BOOST_FOREACH(const CTransaction& tx, block.vtx)
    {
        if (tx.nVersion == SC_TX_VERSION)
        {
            uint256 txHash = tx.GetHash();
            LogPrint("sc", "%s():%d - tx=%s\n", __func__, __LINE__, txHash.ToString() );
            int c = 0;

            // remove sc creation, check that their balance is 0
            BOOST_FOREACH(auto& sc, tx.vsc_ccout)
            {
                ScInfo info; 
                if (!getScInfo(sc.scId, info) )
                {
                    // should not happen 
                    LogPrint("sc", "#### %s():%d - CR: scId=%s not in map #######\n", __func__, __LINE__, sc.scId.ToString() );
                    return false;
                }

                if (info.balance > 0)
                {
                    // should not happen either 
                    LogPrint("sc", "#### %s():%d - scId=%s balance not null: %s\n",
                        __func__, __LINE__, sc.scId.ToString(), FormatMoney(info.balance));
                    return false;
                }

                removeSidechain(sc.scId);
            }

            // decrement side chain balance
            BOOST_FOREACH(auto& ft, tx.vft_ccout)
            {
                if (!sidechainExists(ft.scId))
                {
                    // should not happen
                    LogPrint("sc", "#### %s():%d - FW: scId=%s not in map #######\n", __func__, __LINE__, ft.scId.ToString() );
                    return false;
                }

                LogPrint("sc", "@@@ %s():%d - scId=%s balance before: %s\n",
                    __func__, __LINE__, ft.scId.ToString(), FormatMoney(getSidechainBalance(ft.scId)));

                updateSidechainBalance(ft.scId, (-ft.nValue));

                LogPrint("sc", "@@@ %s():%d - scId=%s balance after:  %s\n",
                    __func__, __LINE__, ft.scId.ToString(), FormatMoney(getSidechainBalance(ft.scId)));
            }
        }
    }

    return true;
}

bool ScMgr::checkMemPool(CTxMemPool& pool, const CTransaction& tx, CValidationState& state)
{
    if (!checkCreationInMemPool(pool, tx) )
    {
        return state.Invalid(error("transaction tries to create scid already created in mempool"),
             REJECT_INVALID, "sidechain-creation");
    }
    return true;
}

bool ScMgr::checkCreationInMemPool(CTxMemPool& pool, const CTransaction& tx)
{
    if (tx.nVersion == SC_TX_VERSION && tx.vsc_ccout.size() )
    {
        BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
        {
            for (std::map<uint256, CTxMemPoolEntry>::const_iterator it = pool.mapTx.begin(); it != pool.mapTx.end(); ++it)
            {
                const CTransaction& mpTx = it->second.GetTx();

                if (mpTx.nVersion == SC_TX_VERSION)
                {
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
        }
    }
    return true;
}

int ScMgr::evalAddCreationFeeOut(CMutableTransaction& tx)
{
    if (tx.vsc_ccout.size() == 0 )
    {
        // no sc creation here
        return -1;
    }

    const CChainParams& chainparams = Params();
    CAmount totalReward = 0;
    BOOST_FOREACH(const auto& txccout, tx.vsc_ccout)
    {
        totalReward += SC_CREATION_FEE;
    }

    // can not be calculated just once because depends on height
    CBitcoinAddress address(
        chainparams.GetCommunityFundAddressAtHeight(chainActive.Height(), Fork::CommunityFundType::FOUNDATION));

    assert(address.IsValid());
    assert(address.IsScript());

    CScript scriptFund = GetScriptForDestination(address.Get());

    tx.vout.push_back( CTxOut(totalReward, scriptFund));
    return tx.vout.size() -1;
}

bool ScMgr::fillRawCreation(UniValue& sc_crs, CMutableTransaction& rawTx, CTxMemPool& mempool, std::string& error) 
{
    rawTx.nVersion = SC_TX_VERSION;

    for (size_t i = 0; i < sc_crs.size(); i++)
    {
        const UniValue& input = sc_crs[i];
        const UniValue& o = input.get_obj();
 
        std::string inputString = find_value(o, "scid").get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid scid format: not an hex";
            return false;
        }
    
        uint256 scId;
        scId.SetHex(inputString);
 
        const UniValue& sh_v = find_value(o, "start_height");
        if (!sh_v.isNum())
        {
            error = "Invalid parameter, missing start_height key";
            return false;
        }
        int nHeight = sh_v.get_int();
        if (nHeight < 0)
        {
            error = "Invalid parameter, start_height must be positive";
            return false;
        }
 
        // Amount and address
        CAmount nAmount(Sidechain::SC_CREATION_FEE);
        uint256 hrzAddress(Sidechain::SC_CREATION_PAYEE_ADDRESS);
 
        CTxScCreationCrosschainOut txccout(nAmount, hrzAddress, scId, nHeight);
        rawTx.vsc_ccout.push_back(txccout);
 
        CValidationState state;
        // if this tx creates a sc, check that no other tx are doing the same in the mempool
        if (!ScMgr::instance().checkMemPool(mempool, rawTx, state) )
        {
            error = "Sc already created by a tx in mempool";
            return false;
        }
    }
    
    // add output for the foundation
    ScMgr::instance().evalAddCreationFeeOut(rawTx);
    return true;
}

void ScMgr::fillFundCcRecipients(const CTransaction& tx, std::vector<CcRecipientVariant >& vecCcSend)
{
    BOOST_FOREACH(auto& entry, tx.vsc_ccout)
    {
        CRecipientScCreation sc;
        sc.scId = entry.scId;
        // when funding a tx with sc creation, the amount is already contained in vcout to foundation
        sc.nValue = 0;
        sc.address = entry.address;
        sc.creationData.startBlockHeight = entry.startBlockHeight;

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

CRecipientForwardTransfer::CRecipientForwardTransfer(const CTxForwardTransferCrosschainOut& ccout)
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

    //    assert(it->status().ok());  // Check for any errors found during the scan
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
            LogPrint("sc", "%s():%d - erased scId=%s in db\n", __func__, __LINE__, scId.ToString() );
        }
        else
        {
            LogPrint("sc", "%s():%d - Error: could not erase scId=%s in db\n", __func__, __LINE__, scId.ToString() );
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


    LogPrint("sc", "  created in block[%s] (h=%d)\n", info.ownerBlockHash.ToString(), info.creationBlockHeight );
    LogPrint("sc", "  ownerTx[%s] (index in block=%d)\n", info.ownerTxHash.ToString(), info.creationTxIndex);
    LogPrint("sc", "  balance[%s]\n", FormatMoney(info.balance));
    LogPrint("sc", "  ----- creation data:\n");
    LogPrint("sc", "      startBlockHeight[%d]\n", info.creationData.startBlockHeight);
    LogPrint("sc", "      ...more to come...\n");

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
                << "  creating block hash: " << info.ownerBlockHash.ToString()
                << " (height: " << info.creationBlockHeight << ")" << std::endl
                << "  creating tx hash: " << info.ownerTxHash.ToString() << std::endl
                << "  tx idx in block: " << info.creationTxIndex << std::endl
                << "  ==> balance: " << ValueFromAmount(info.balance).get_real() << std::endl;
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
    sc.push_back(Pair("created in block", info.ownerBlockHash.ToString()));
    sc.push_back(Pair("created at block height", info.creationBlockHeight));
    sc.push_back(Pair("creating tx index in block", info.creationTxIndex));
    sc.push_back(Pair("creating tx hash", info.ownerTxHash.GetHex()));
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
    BOOST_FOREACH(const auto& entry, mScInfo)
    {
        UniValue sc(UniValue::VOBJ);
        fillJSON(entry.first, entry.second, sc);
        result.push_back(sc);
    }
}
