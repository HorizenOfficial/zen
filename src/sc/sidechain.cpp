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
    // TODO
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
    if (it->second.balance < 0)
    {
        LogPrint("sc", "%s():%d - Can not update balance with amount[%s] for scId=%s, would be negative\n",
            __func__, __LINE__, FormatMoney(amount), scId.ToString() );
        return false;
    }

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

bool ScMgr::addSidechain(const uint256& scId, const ScInfo& info)
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
        LogPrint("sc", "%s():%d - (erased=%d) scId=%s\n", __func__, __LINE__, num, scId.ToString() );
    }
    else
    {
        LogPrint("sc", "WARNING: %s():%d - scId=%s not in map\n", __func__, __LINE__, scId.ToString() );
    }
}

bool ScMgr::addScBackwardTx(const uint256& scId, const uint256& txHash)
{
    LOCK(sc_lock);
    ScInfoMap::iterator it = mScInfo.find(scId);
    if (it == mScInfo.end() )
    {
        // caller should have checked it
        return false;
    }

    ScInfo& info = it->second;

    // check that tx is not already there
    if (!info.sBackwardTransfers.insert(txHash).second)
    {
        LogPrint("sc", "%s():%d - ERROR: tx[%s] is already there!\n", __func__, __LINE__, txHash.ToString() );
        return false;
    }

    return writeToDb(scId, info);
}

bool ScMgr::removeScBackwardTx(const uint256& scId, const uint256& txHash)
{
    LOCK(sc_lock);

    ScInfoMap::iterator it = mScInfo.find(scId);
    if (it == mScInfo.end() )
    {
        // caller should have checked it
        return false;
    }

    ScInfo& info = it->second;

    int num = info.sBackwardTransfers.erase(txHash);
    if (num)
    {
        return writeToDb(scId, info);
    }

    LogPrint("sc", "%s():%d - ERROR: tx[%s] not found\n", __func__, __LINE__, txHash.ToString() );
    return false;
}

bool ScMgr::containsScBackwardTx(const uint256& scId, const uint256& txHash)
{
    ScInfo info;
    if (!getScInfo(scId, info) )
    {
        LogPrint("sc", "%s():%d - ERROR: scId[%s] not found\n", __func__, __LINE__, scId.ToString() );
        return false;
    }

    return info.sBackwardTransfers.count(txHash);
}

bool ScMgr::checkSidechainCreationFunds(const CTransaction& tx, int nHeight)
{
    if (tx.vsc_ccout.size() )
    {
        const uint256& txHash = tx.GetHash();

        const CScript& comScript = Params().GetCommunityFundScriptAtHeight(nHeight, Fork::CommunityFundType::FOUNDATION);
        LogPrint("sc2", "%s():%d - com script[%s]\n", __func__, __LINE__, comScript.ToString() );

        CAmount totalFund = tx.vsc_ccout.size()*SC_CREATION_FEE;
        LogPrint("sc2", "%s():%d - total funds should be %s for the creation of %d sidechains\n",
            __func__, __LINE__, FormatMoney(totalFund), tx.vsc_ccout.size() );

        bool ret = false;
        BOOST_FOREACH(const auto& output, tx.vout)
        {
            const CScript& outScript = output.scriptPubKey;
            LogPrint("sc2", "%s():%d - out script[%s]\n", __func__, __LINE__, outScript.ToString() );

            // we have the 'check block at height' condition in the out script which community fund script does not have
            // therefore we do not check the equality of scripts but the containment of the latter in the former
            auto res = search(begin(outScript), end(outScript), begin(comScript), end(comScript));

            if (res != end(outScript) )
            {
                // check also the consistency of the fund; we are also happy with amounts greater than the default
                if (output.nValue >= totalFund )
                {
                    LogPrint("sc2", "%s():%d - OK tx[%s] funds the foundation with %s for the creation of %d sidechains\n",
                        __func__, __LINE__, txHash.ToString(), FormatMoney(output.nValue), tx.vsc_ccout.size() );
                    ret = true;
                    break;
                }
                else
                {
                    LogPrint("sc", "%s():%d - BAD tx[%s] funds %s for the creation of %d sidechains\n",
                        __func__, __LINE__, txHash.ToString(), FormatMoney(output.nValue), tx.vsc_ccout.size() );
                }
            }
        }

        if (!ret)
        {
            LogPrint("sc", "%s():%d - ERROR: tx[%s] does not fund correctly the foundation\n",
                __func__, __LINE__, txHash.ToString());
            return false;
        }
    }
    return true;
}

bool ScMgr::checkSidechainCreation(const CTransaction& tx, CValidationState& state)
{
    if (tx.vsc_ccout.size() )
    {
        const uint256& txHash = tx.GetHash();
        int nHeight = -1;

        BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
        {
            ScInfo info;
            if (getScInfo(sc.scId, info) )
            {
                if (info.ownerTxHash != txHash )
                {
                    LogPrint("sc", "%s():%d - Invalid tx[%s] : scid[%s] already created by tx[%s]\n",
                        __func__, __LINE__, txHash.ToString(), sc.scId.ToString(), info.ownerTxHash.ToString() );
                    return state.DoS(10,
                        error("transaction tries to create scid already created"),
                        REJECT_INVALID, "sidechain-creation-id-already-created");
                }

                // this tx is the owner, go on without error. Can happen also in check performed at
                // startup in VerifyDB 
                LogPrint("sc", "%s():%d - OK tx[%s] : scid[%s] creation detected\n",
                    __func__, __LINE__, txHash.ToString(), sc.scId.ToString() );

                nHeight = info.creationBlockHeight;
            }
            else
            {
                nHeight = chainActive.Height();
                // this is a brand new sc
                LogPrint("sc", "%s():%d - No such scid[%s], tx[%s] is creating it\n",
                    __func__, __LINE__, sc.scId.ToString(), txHash.ToString() );
            }

            // check there is at least one fwt associated with this scId
            if (!anyForwardTransaction(tx, sc.scId) )
            {
                LogPrint("sc", "%s():%d - Invalid tx[%s] : no fwd transactions associated to this creation\n",
                    __func__, __LINE__, txHash.ToString() );
                return state.DoS(100, error("%s: no fwd transactions associated to this creation",
                    __func__), REJECT_INVALID, "sidechain-creation-missing-fwd-transfer");
            }

            // check the fee has been payed to the community, but skip it when we do not have a valid
            // nHeight to check against
            if (nHeight > 0 && !checkSidechainCreationFunds(tx, nHeight) )
            {
                LogPrint("sc", "%s():%d - Invalid tx[%s] : community fund missing\n",
                    __func__, __LINE__, txHash.ToString() );
                return state.DoS(100, error("%s: community fund missing or not valid at block h %d",
                    __func__, nHeight), REJECT_INVALID, "sidechain-creation-wrong-community-fund");
            }
        }
    }
    return true;
}

bool ScMgr::anyForwardTransaction(const CTransaction& tx, const uint256& scId)
{
    if (tx.vft_ccout.size() )
    {
        BOOST_FOREACH(const auto& fwd, tx.vft_ccout)
        {
            if (fwd.scId == scId)
            {
                return true;
            }
        }
    }
    return false;
}

#if 0
bool ScMgr::handlingSameScid(const CTransaction& txa, const CTransaction& txb)
{
    std::set<uint256> sa;
    std::set<uint256> sb;

    ScMgr::getHandledSidechainList(txa, sa);
    ScMgr::getHandledSidechainList(txb, sb);

    BOOST_FOREACH(const auto& entry, sa)
    {
        if (sb.count(entry) )
        {
            return true;
        }
    }
    return false;
}

void ScMgr::getHandledSidechainList(const CTransaction& tx, std::set<uint256>& scIdSet)
{
    if (tx.nVersion == SC_TX_VERSION)
    {
        BOOST_FOREACH(const auto& cr, tx.vsc_ccout)
        {
            scIdSet.insert(cr.scId);
        }
        BOOST_FOREACH(const auto& ft, tx.vft_ccout)
        {
            scIdSet.insert(ft.scId);
        }
        BOOST_FOREACH(const auto& cert, tx.vsc_cert)
        {
            scIdSet.insert(cert.scId);
        }
    }
}
#endif

bool ScMgr::isCreating(const CTransaction& tx, const uint256& scId)
{
    if (tx.vsc_ccout.size() )
    {
        BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
        {
            if (sc.scId == scId)
            {
                return true;
            }
        }
    }
    return false;
}

bool ScMgr::checkSidechainForwardTransaction(const CTransaction& tx, CValidationState& state, ScAmountMap* mScAmounts)
{
    if (tx.vft_ccout.size() )
    {
        const uint256& txHash = tx.GetHash();

        BOOST_FOREACH(const auto& ft, tx.vft_ccout)
        {
            const uint256& scId = ft.scId;
            if (!sidechainExists(scId) )
            {
                // return error unless we are creating this sc in the current tx
                if (!isCreating(tx, scId) )
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
            else
            {
                if (mScAmounts && (mScAmounts->find(scId) != mScAmounts->end() ) )
                {
                    LogPrint("sc", "= FWD = %s():%d - scId=%s balance before: %s\n",
                        __func__, __LINE__, scId.ToString(), FormatMoney((*mScAmounts)[scId]));
                    (*mScAmounts)[scId] += ft.nValue;
                    LogPrint("sc", "= FWD = %s():%d - scId=%s balance after: %s\n",
                        __func__, __LINE__, scId.ToString(), FormatMoney((*mScAmounts)[scId]));
                }
            }
            LogPrint("sc", "%s():%d - tx[%s]: scid[%s], fw[%s]\n",
                __func__, __LINE__, txHash.ToString(), scId.ToString(), FormatMoney(ft.nValue) );
        }
    }
    return true;
}

bool ScMgr::checkSidechainBackwardTransaction(const CTransaction& tx, CValidationState& state,
    ScAmountMap* mScAmounts, bool fVerifyingDB)
{
    if (tx.IsCoinCertified() )
    {
        const uint256& txHash = tx.GetHash();

        BOOST_FOREACH(const auto& entry, tx.vsc_cert)
        {
            const uint256& scId = entry.scId;
            const CAmount& totalAmount = entry.totalAmount;
 
            ScInfo info;
            if (!getScInfo(scId, info) )
            {
                LogPrint("sc", "%s():%d - tx[%s]: scid[%s] not found\n",
                    __func__, __LINE__, txHash.ToString(), scId.ToString() );
                return state.DoS(10,
                    error("transaction tries to backward transfer to a scid not yet created"),
                    REJECT_SCID_NOT_FOUND, "sidechain-backward-transfer");
            }
 
            LogPrint("sc", "%s():%d - tx[%s]: scid[%s], bw[%s]\n",
                __func__, __LINE__, txHash.ToString(), scId.ToString(), FormatMoney(totalAmount) );
 
//            if (verifyingDb() )
            if (fVerifyingDB )
            {
                LogPrint("sc", "%s():%d - Verifying DB\n", __func__, __LINE__);
 
                // this is because at startup:
                //   1. when loading blocks db most recent blocks are checked from the tip down
                //   2. when loading wallet db, tx are checked after chain is loaded
                // in these cases sc is in its final state, and might have an insufficient amount compared to the value of
                // certified tx which is being currently checked
 
                // check that DB has it
                if (!containsScBackwardTx(scId, txHash) )
                {
                    LogPrint("sc", "%s():%d - tx[%s] not found in scid[%s]\n",
                        __func__, __LINE__, txHash.ToString(), scId.ToString() );
                    return state.DoS(100,
                        error("transaction with backward transfer not found in sc"),
                        REJECT_INVALID, "sidechain-backward-transfer");
                }
            }
            else
            {
                if (mScAmounts && (mScAmounts->find(scId) != mScAmounts->end() ) )
                {
                    CAmount& balance = (*mScAmounts)[scId];
                    if ( balance < totalAmount)
                    {
                        LogPrint("sc", "%s():%d - tx[%s]: insufficent balance in scid[%s]: balance[%s], bkw amount[%s]\n",
                            __func__, __LINE__, txHash.ToString(), scId.ToString(), FormatMoney(balance), FormatMoney(totalAmount) );
                        return state.DoS(100,
                            error("transaction tries to backward transfer to a scid with insufficient balance"),
                            REJECT_INSUFFICIENT_SCID_FUNDS, "sidechain-backward-transfer");
                    }
                    LogPrint("sc", "= BWD = %s():%d - scId=%s balance before: %s\n",
                        __func__, __LINE__, scId.ToString(), FormatMoney((*mScAmounts)[scId]));
                    balance -= totalAmount;
                    LogPrint("sc", "= BWD = %s():%d - scId=%s balance after: %s\n",
                        __func__, __LINE__, scId.ToString(), FormatMoney((*mScAmounts)[scId]));
                }
                else
                {
                    // this is not a conservative approach since the fee for the miner has been carved out from
                    // the total amount of the certificate
                    if (info.balance < totalAmount)
                    {
                        LogPrint("sc", "%s():%d - tx[%s]: insufficent balance in scid[%s]: balance[%s], bkw amount[%s]\n",
                            __func__, __LINE__, txHash.ToString(), scId.ToString(), FormatMoney(info.balance), FormatMoney(totalAmount) );
                        return state.DoS(100,
                            error("transaction tries to backward transfer to a scid with insufficient balance"),
                            REJECT_INSUFFICIENT_SCID_FUNDS, "sidechain-backward-transfer");
                    }
                }
            }
 
            // check consistency with total amount
            CAmount actualTransfer = 0;
            BOOST_FOREACH(const auto& out, tx.vout)
            {
                actualTransfer += out.nValue;
            }
            if (actualTransfer >= totalAmount)
            {
                LogPrint("sc", "%s():%d - tx[%s]: inconsistency in tx data, totalAmount[%s], sum of vout amount[%s]\n",
                    __func__, __LINE__, txHash.ToString(), FormatMoney(totalAmount), FormatMoney(actualTransfer) );
                return state.DoS(100,
                    error("transaction tries to backward transfer with data inconsistent"),
                    REJECT_INVALID, "sidechain-backward-transfer");
            }
            // TODO add sanity check for vbt_ccout contents
        }
    }
    return true;
}

bool ScMgr::checkTransaction(const CTransaction& tx, CValidationState& state,
    ScAmountMap* mScAmounts, bool fVerifyingDB)
{
    // check version consistency
    if (tx.nVersion != SC_TX_VERSION )
    {
        if (!tx.ccIsNull() || !tx.vsc_cert.empty() )
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

    if (!checkSidechainForwardTransaction(tx, state, mScAmounts) )
    {
        return false;
    }

    if (!checkSidechainBackwardTransaction(tx, state, mScAmounts, fVerifyingDB) )
    {
        return false;
    }

    return true;
}

bool ScMgr::hasCrosschainTransfers(const CBlock& block, std::vector<CTransaction>& vTxReord, std::set<uint256>& sScId)
{
    // note that this function is called also by miner when testing correctness of a generated block candidate,
    // in that case merkle tree attributes are both not set yet. In that case we can not tell here
    // if there is any sc related tx.
    if (block.vtx.size() <= 1 || (block.hashScMerkleRootsMap == uint256() && block.hashMerkleRoot != uint256()))
    {
        // nothing to do, either we have just the coinbase or no sc related txes anyway
        //---
        LogPrint("sc", "%s():%d - block[%s] needs no tx reorder\n", __func__, __LINE__, block.GetHash().ToString() );
        return false;
    }

    // clone input into output
    vTxReord.reserve(block.vtx.size());
    vTxReord.insert(vTxReord.end(), block.vtx.begin(), block.vtx.end());

    std::vector<CTransaction> certificates;
    auto it = vTxReord.begin();

    while(it != vTxReord.end() )
    {
        const CTransaction& tx = *it;
        if (tx.IsCoinCertified() )
        {
            BOOST_FOREACH(const auto& cert, tx.vsc_cert)
            {
                // backward transfer
                sScId.insert(cert.scId);
            }

            // remove this certificate and put it in the helper vector
            certificates.push_back(tx);
            it = vTxReord.erase(it);
        }
        else
        {
            if (tx.vft_ccout.size() )
            {
                BOOST_FOREACH(const auto& fwd, tx.vft_ccout)
                {
                    // forward transfer
                    sScId.insert(fwd.scId);
                }
            }

            ++it;
        }
    }

    // join the two chunks if any certificate ha been found
    if (certificates.size() > 0)
    {
        LogPrint("sc", "%s():%d - Moved %d certificates at the back of block tx vector\n",
            __func__, __LINE__, certificates.size() );
        vTxReord.insert(vTxReord.end(), certificates.begin(), certificates.end());
    }

    LogPrint("sc", "%s():%d - Found %d sidechains handling fw/bw in block[%s]\n",
        __func__, __LINE__, sScId.size(), block.GetHash().ToString() );
    return true;
}

void ScMgr::initScAmounts(ScAmountMap& mScAmounts, const std::set<uint256>* sScId)
{
    LOCK(sc_lock);
    BOOST_FOREACH(const auto& entry, mScInfo)
    {
        if (sScId)
        {
            if (!sScId->count(entry.first) )
            {
                // this is not interesting
                continue;
            }
        }
        mScAmounts[entry.first] = entry.second.balance;
    }
}


bool ScMgr::onBlockConnected(const CBlock& block, int nHeight)
{
    const uint256& blockHash = block.GetHash();

    LogPrint("sc", "%s():%d - Entering with block [%s]\n", __func__, __LINE__, blockHash.ToString() );

    const std::vector<CTransaction>* blockVtx = &block.vtx;
    std::vector<CTransaction> vtxReordered;
    std::set<uint256> sUnused;
    if (hasCrosschainTransfers(block, vtxReordered, sUnused) )
    {
        blockVtx = &vtxReordered;
    }

    int txIndex = 0;
    BOOST_FOREACH(const CTransaction& tx, *blockVtx)
    {
        if (tx.nVersion == SC_TX_VERSION)
        {
            const uint256& txHash = tx.GetHash();

            LogPrint("sc", "%s():%d - tx=%s\n", __func__, __LINE__, txHash.ToString() );

            BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
            {
                if (sidechainExists(sc.scId) )
                {
                    // should not happen at this point due to previous checks
                    LogPrint("sc", "ERROR: %s():%d - CR: scId=%s already in map\n", __func__, __LINE__, sc.scId.ToString() );
                    return false;
                }

                ScInfo scInfo;
                scInfo.ownerBlockHash = blockHash;
                scInfo.creationBlockHeight = nHeight;
                scInfo.creationTxIndex = txIndex;
                scInfo.ownerTxHash = txHash;
                scInfo.creationData.withdrawalEpochLength = sc.withdrawalEpochLength;

                if (addSidechain(sc.scId, scInfo) )
                {
                    LogPrint("sc", "%s():%d - scId[%s] added in map\n", __func__, __LINE__, sc.scId.ToString() );
                }
                else
                {
                    // should never fail
                    LogPrint("sc", "ERROR: %s():%d - scId=%s could not add to DB\n", __func__, __LINE__, sc.scId.ToString() );
                    return false;
                }
            }

            BOOST_FOREACH(const auto& ft, tx.vft_ccout)
            {
                if (!sidechainExists(ft.scId))
                {
                    // should not happen at this point due to previous checks
                    LogPrint("sc", "ERROR: %s():%d - FW: scId=%s not in map\n", __func__, __LINE__, ft.scId.ToString() );
                    return false;
                }

                LogPrint("sc", "%s():%d - scId=%s balance before: %s\n",
                    __func__, __LINE__, ft.scId.ToString(), FormatMoney(getSidechainBalance(ft.scId)));

                // add to sc amount
                if (!updateSidechainBalance(ft.scId, ft.nValue) )
                {
                    return false;
                }

                LogPrint("sc", "%s():%d - scId=%s balance after:  %s\n",
                    __func__, __LINE__, ft.scId.ToString(), FormatMoney(getSidechainBalance(ft.scId)));
            }

            if (tx.IsCoinCertified() )
            {
                BOOST_FOREACH(const auto& entry, tx.vsc_cert)
                {
                    const uint256& scId = entry.scId;
                    const CAmount& totalAmount = entry.totalAmount;
 
                    if (!sidechainExists(scId))
                    {
                        // should not happen at this point due to previous checks
                        LogPrint("sc", "ERROR: %s():%d - BW: scId=%s not in map\n", __func__, __LINE__, scId.ToString() );
                        return false;
                    }
  
                    LogPrint("sc", "%s():%d - scId=%s balance before: %s\n",
                        __func__, __LINE__, scId.ToString(), FormatMoney(getSidechainBalance(scId)));
 
                    // subtract from sc amount, this include the carved fee
                    if (!updateSidechainBalance(scId, -totalAmount) )
                    {
                        return false;
                    }
 
                    LogPrint("sc", "%s():%d - scId=%s balance after:  %s\n",
                        __func__, __LINE__, scId.ToString(), FormatMoney(getSidechainBalance(scId)));
 
                    if (!addScBackwardTx(scId, txHash) )
                    {
                        LogPrint("sc", "%s():%d - ERROR: tx[%s] could not be added to DB!\n", __func__, __LINE__, txHash.ToString() );
                        return false;
                    }
                }
            }
        }
        txIndex++;
    }

    // TODO test, remove it
    // dump_info();

    return true;
}

bool ScMgr::onBlockDisconnected(const CBlock& block, int nHeight)
{
    const uint256& blockHash = block.GetHash();

    LogPrint("sc", "%s():%d - Entering with block [%s]\n", __func__, __LINE__, blockHash.ToString() );

    const std::vector<CTransaction>* blockVtx = &block.vtx;
    std::vector<CTransaction> vtxReordered;
    std::set<uint256> sUnused;
    if (hasCrosschainTransfers(block, vtxReordered, sUnused) )
    {
        blockVtx = &vtxReordered;
    }

    // do it in reverse order in block txes, they are sorted with bkw txes at the bottom, therefore
    // they will be processed beforehand
    BOOST_REVERSE_FOREACH(const CTransaction& tx, *blockVtx)
    {
        if (tx.nVersion == SC_TX_VERSION)
        {
            const uint256& txHash = tx.GetHash();

            LogPrint("sc", "%s():%d - tx=%s\n", __func__, __LINE__, txHash.ToString() );
            int c = 0;

            // first update side chain balance with the proper order 
            if (tx.IsCoinCertified() )
            {
                BOOST_FOREACH(const auto& entry, tx.vsc_cert)
                {
                    const uint256& scId = entry.scId;
                    const CAmount& totalAmount = entry.totalAmount;
 
                    if (!sidechainExists(scId))
                    {
                        // should not happen at this point due to previous checks
                        LogPrint("sc", "ERROR: %s():%d - BW: scId=%s not in map\n", __func__, __LINE__, scId.ToString() );
                        return false;
                    }
  
                    if (!removeScBackwardTx(scId, txHash) )
                    {
                        return false;
                    }
 
                    LogPrint("sc", "%s():%d - scId=%s balance before: %s\n",
                        __func__, __LINE__, scId.ToString(), FormatMoney(getSidechainBalance(scId)));
 
                    // subtract from sc amount, this include the carved fee
                    if (!updateSidechainBalance(scId, totalAmount) )
                    {
                        return false;
                    }
 
                    LogPrint("sc", "%s():%d - scId=%s balance after:  %s\n",
                        __func__, __LINE__, scId.ToString(), FormatMoney(getSidechainBalance(scId)));
                }
            }
            BOOST_FOREACH(auto& ft, tx.vft_ccout)
            {
                if (!sidechainExists(ft.scId))
                {
                    // should not happen
                    LogPrint("sc", "ERROR: %s():%d - FW: scId=%s not in map\n", __func__, __LINE__, ft.scId.ToString() );
                    return false;
                }

                LogPrint("sc", "%s():%d - scId=%s balance before: %s\n",
                    __func__, __LINE__, ft.scId.ToString(), FormatMoney(getSidechainBalance(ft.scId)));

                if (!updateSidechainBalance(ft.scId, (-ft.nValue)) )
                {
                    return false;
                }

                LogPrint("sc", "%s():%d - scId=%s balance after:  %s\n",
                    __func__, __LINE__, ft.scId.ToString(), FormatMoney(getSidechainBalance(ft.scId)));
            }

            // remove sc creation, check that their balance is 0
            BOOST_FOREACH(auto& sc, tx.vsc_ccout)
            {
                ScInfo info; 
                if (!getScInfo(sc.scId, info) )
                {
                    // should not happen 
                    LogPrint("sc", "ERROR: %s():%d - CR: scId=%s not in map\n", __func__, __LINE__, sc.scId.ToString() );
                    return false;
                }

                if (info.balance > 0)
                {
                    // should not happen either 
                    LogPrint("sc", "ERROR %s():%d - scId=%s balance not null: %s\n",
                        __func__, __LINE__, sc.scId.ToString(), FormatMoney(info.balance));
                    return false;
                }

                removeSidechain(sc.scId);
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

    if (!checkCertificateInMemPool(pool, tx) )
    {
        return state.Invalid(error("transaction tries to create a certificate with no sc balance enough considering mempool"),
             REJECT_INVALID, "sidechain-certificate");
    }
    return true;
}

bool ScMgr::checkCreationInMemPool(CTxMemPool& pool, const CTransaction& tx)
{
    if (tx.nVersion == SC_TX_VERSION && tx.vsc_ccout.size() )
    {
        BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
        {
            for (auto it = pool.mapTx.begin(); it != pool.mapTx.end(); ++it)
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

bool ScMgr::checkCertificateInMemPool(CTxMemPool& pool, const CTransaction& tx)
{
    if (tx.IsCoinCertified() )
    {
        const uint256& txHash = tx.GetHash();

        BOOST_FOREACH(const auto& entry, tx.vsc_cert)
        {
            const uint256& scId = entry.scId;
            const CAmount& certAmount = entry.totalAmount;
 
            CAmount scBalance = getSidechainBalance(scId);
            if (scBalance == -1)
            {
                // should not happen at this point due to previous checks
                LogPrint("sc", "%s():%d - tx[%s]: scid[%s] not found\n",
                    __func__, __LINE__, txHash.ToString(), scId.ToString() );
                return false;
            }
            LogPrint("sc", "%s():%d - scid consolidated balance: %s\n", __func__, __LINE__, FormatMoney(scBalance) );
 
            for (auto it = pool.mapTx.begin(); it != pool.mapTx.end(); ++it)
            {
                const CTransaction& mpTx = it->second.GetTx();
 
                if (mpTx.nVersion == SC_TX_VERSION)
                {
                    // backward tx to the same sc
                    if (mpTx.IsCoinCertified() )
                    {
                        BOOST_FOREACH(const auto& mpEntry, mpTx.vsc_cert)
                        {
                            if (scId == mpEntry.scId)
                            {
                                LogPrint("sc", "%s():%d - tx[%s]: bwd[%s]\n",
                                    __func__, __LINE__, mpTx.GetHash().ToString(), FormatMoney(mpEntry.totalAmount) );
                                scBalance -= mpEntry.totalAmount;
                            }
                        }
                    }
  
                    BOOST_FOREACH(auto& ft, mpTx.vft_ccout)
                    {
                        if (scId == ft.scId)
                        {
                            LogPrint("sc", "%s():%d - tx[%s]: fwd[%s]\n",
                                __func__, __LINE__, mpTx.GetHash().ToString(), FormatMoney(ft.nValue) );
                            scBalance += ft.nValue;
                        }
                    }
                }
            }
 
            if (certAmount > scBalance)
            {
                LogPrint("sc", "%s():%d - invalid tx[%s]: scid[%s] has not balance enough: %s\n",
                    __func__, __LINE__, txHash.ToString(), scId.ToString(), FormatMoney(scBalance) );
                return false;
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
    CAmount totalFund = 0;
    BOOST_FOREACH(const auto& txccout, tx.vsc_ccout)
    {
        totalFund += SC_CREATION_FEE;
    }

    // can not be calculated just once because depends on height
    CBitcoinAddress address(
        chainparams.GetCommunityFundAddressAtHeight(chainActive.Height(), Fork::CommunityFundType::FOUNDATION));

    assert(address.IsValid());
    assert(address.IsScript());

    const CScript& scriptFund = GetScriptForDestination(address.Get());

    tx.vout.push_back( CTxOut(totalFund, scriptFund));
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
 
        const UniValue& sh_v = find_value(o, "epoch_length");
        if (!sh_v.isNum())
        {
            error = "Invalid parameter, missing epoch_length key";
            return false;
        }
        int nHeight = sh_v.get_int();
        if (nHeight < 0)
        {
            error = "Invalid parameter, epoch_length must be positive";
            return false;
        }
 
        CTxScCreationCrosschainOut txccout(scId, nHeight);
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
    evalAddCreationFeeOut(rawTx);
    return true;
}

void ScMgr::fillFundCcRecipients(const CTransaction& tx, std::vector<CcRecipientVariant >& vecCcSend)
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
                << "  creating block hash: " << info.ownerBlockHash.ToString() <<
                   " (height: " << info.creationBlockHeight << ")" << std::endl
                << "  creating tx hash: " << info.ownerTxHash.ToString() << std::endl
                << "  tx idx in block: " << info.creationTxIndex << std::endl
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
    sc.push_back(Pair("creating tx hash", info.ownerTxHash.GetHex()));
    sc.push_back(Pair("creating tx index in block", info.creationTxIndex));
    sc.push_back(Pair("created in block", info.ownerBlockHash.ToString()));
    sc.push_back(Pair("created at block height", info.creationBlockHeight));
    // creation parameters
    sc.push_back(Pair("withdrawalEpochLength", info.creationData.withdrawalEpochLength));

    if (info.sBackwardTransfers.size() )
    {
        UniValue arr(UniValue::VARR);
        BOOST_FOREACH(const auto& entry, info.sBackwardTransfers)
        {
            arr.push_back(entry.GetHex());
        }
        sc.push_back(Pair("certificate txes", arr));
    }
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
