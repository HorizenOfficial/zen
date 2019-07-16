#include "sc/sidechain.h"
#include "primitives/transaction.h"
#include "utilmoneystr.h"
#include "txmempool.h"
#include "chainparams.h"
#include "base58.h"
#include "script/standard.h"

extern CChain chainActive;


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
    ScInfoMap::iterator it = mScInfo.find(scId);
    if (it == mScInfo.end() )
    {
        // caller should have checked it
        return false;
    }

    it->second.balance += amount;
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

void ScMgr::addSidechain(const uint256& id, ScInfo& info)
{
    // checks should be done by caller
    mScInfo[id] = info;
}

void ScMgr::removeSidechain(const uint256& id)
{
    // checks should be done by caller
    int num = mScInfo.erase(id);
    LogPrint("sc", "%s():%d - (erased=%d) scId=%s\n", __func__, __LINE__, num, id.ToString() );
}

bool ScMgr::checkSidechainTxCreation(const CTransaction& tx)
{
    if (tx.nVersion == SC_TX_VERSION && tx.vsc_ccout.size() )
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

bool ScMgr::updateAmountsFromCache()
{
    // TODO optimize
    BOOST_FOREACH(auto& entry, _cachedFwTransfers)
    {
        BOOST_FOREACH(auto& ft, entry.second)
        {
            if (!updateSidechainBalance(ft.scId, ft.nValue))
            {
                // should not happen, handle the disaster
                return false;
            }
        }
    }
    return true;
}

bool ScMgr::addBlockScTransactions(const CBlock& block, const CBlockIndex* pindex)
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
                scInfo.creationBlockIndex = pindex;
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

bool ScMgr::removeBlockScTransactions(const CBlock& block)
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
    // TODO test, remove it
    dump_info();

    return true;
}

void ScMgr::addSidechainsAndCacheAmounts(const CBlock& block, const CBlockIndex* pindex)
{
    int txIndex = 0;
    BOOST_FOREACH(const CTransaction& tx, block.vtx)
    {
        if (tx.nVersion == SC_TX_VERSION)
        {
            // get sc creations and add them to the map
            BOOST_FOREACH(auto& sc, tx.vsc_ccout)
            {
                ScInfo scInfo;
                scInfo.creationBlockIndex = pindex;
                scInfo.creationTxIndex = txIndex;
                scInfo.ownerTxHash = tx.GetHash();
                scInfo.creationData.startBlockHeight = sc.startBlockHeight;

                addSidechain(sc.scId, scInfo);
                LogPrint("sc", "%s():%d - scId[%s] added in map\n", __func__, __LINE__, sc.scId.ToString() );
            }

            // get fw transfers and cache them. 
            BOOST_FOREACH(auto& ft, tx.vft_ccout)
            {
                CRecipientForwardTransfer ftObj(ft);
                _cachedFwTransfers[ftObj.scId].push_back(ftObj);
            }
        }
        txIndex++;
    }
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

void ScMgr::evalSendCreationFee(CMutableTransaction& tx)
{
    if (tx.vsc_ccout.size() == 0 )
    {
        return;
    }

    const CChainParams& chainparams = Params();
    CAmount totalReward = 0;
    BOOST_FOREACH(const auto& txccout, tx.vsc_ccout)
    {
        totalReward += SC_CREATION_FEE;
    }

    CBitcoinAddress address(
        chainparams.GetCommunityFundAddressAtHeight(chainActive.Height(), Fork::CommunityFundType::FOUNDATION));

    assert(address.IsValid());
    assert(address.IsScript());

    CScriptID scriptId = boost::get<CScriptID>(address.Get());
    CScript scriptFund = GetScriptForDestination(scriptId);

    tx.vout.push_back( CTxOut(totalReward, scriptFund));
}



CRecipientForwardTransfer::CRecipientForwardTransfer(const CTxForwardTransferCrosschainOut& ccout)
{
    scId    = ccout.scId;
    nValue  = ccout.nValue;
    address = ccout.address;
}

ScMgr::ScMgr() {}

void ScMgr::dump_info(const uint256& scId)
{
    LogPrint("sc", "-- side chain [%s] ------------------------\n", scId.ToString());
    ScInfo info;
    if (!getScInfo(scId, info) )
    {
        LogPrint("sc", "===> No such side chain\n");
        return;
    }

    LogPrint("sc", "  created in block[%s] (h=%d)\n", info.creationBlockIndex->GetBlockHash().ToString(), info.creationBlockIndex->nHeight );
    LogPrint("sc", "  ownerTx[%s] (index in block=%d)\n", info.ownerTxHash.ToString(), info.creationTxIndex);
    LogPrint("sc", "  balance[%s]\n", FormatMoney(info.balance));
    LogPrint("sc", "  ----- creation data:\n");
    LogPrint("sc", "      startBlockHeight[%d]\n", info.creationData.startBlockHeight);
    LogPrint("sc", "      ...more to come...\n");
}


void ScMgr::dump_info()
{
    BOOST_FOREACH(const auto& entry, mScInfo)
    {
        dump_info(entry.first);
    }
}
