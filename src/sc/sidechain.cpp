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
#include "leveldbwrapper.h"

static const char DB_SC_INFO = 'i';

using namespace Sidechain;

/*************************** SCCOINVIEW INTERFACE ****************************/
std::string ScInfo::ToString() const
{
    std::string str;
    str += strprintf("\nScInfo(balance=%s, creatingTxHash=%s, lastReceivedCertEpoch=%d, createdInBlock=%s, createdAtBlockHeight=%d, withdrawalEpochLength=%d)\n",
            FormatMoney(balance),
            creationTxHash.ToString().substr(0,10),
            lastReceivedCertificateEpoch,
            creationBlockHash.ToString().substr(0,10),
            creationBlockHeight,
            creationData.withdrawalEpochLength);

    if (mImmatureAmounts.size() )
    {
        str += "immature amounts:\n";
        BOOST_FOREACH(const auto& entry, mImmatureAmounts)
        {
            str += strprintf("   maturityHeight=%d -> amount=%s\n", entry.first, FormatMoney(entry.second));
        }
    }

    return str;
}

/*************************** SCCOINVIEW INTERFACE ****************************/
bool ScCoinsView::checkTxSemanticValidity(const CTransaction& tx, CValidationState& state)
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

    CAmount cumulatedFwdAmount = 0;
    BOOST_FOREACH(const auto& sc, tx.vft_ccout)
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

bool ScCoinsView::checkCertSemanticValidity(const CScCertificate& cert, CValidationState& state)
{
    const uint256& certHash = cert.GetHash();

    LogPrint("sc", "%s():%d - cert=%s\n", __func__, __LINE__, certHash.ToString() );

    if (!cert.IsScVersion() )
    {
        LogPrint("sc", "%s():%d - Invalid cert[%s] : certificate bad version %d\n",
            __func__, __LINE__, certHash.ToString(), cert.nVersion );
        return state.DoS(100, error("version too low"), REJECT_INVALID, "bad-cert-version-too-low");
    }

    if (!MoneyRange(cert.totalAmount) || !MoneyRange(cert.GetValueOut()))
    {
        LogPrint("sc", "%s():%d - Invalid cert[%s] : certificate amount is outside range\n",
            __func__, __LINE__, certHash.ToString() );
        return state.DoS(100, error("%s: certificate amount is outside range",
            __func__), REJECT_INVALID, "sidechain-bwd-transfer-amount-outside-range");
    }

    CAmount minimumFee = ::minRelayTxFee.GetFee(cert.CalculateSize());
    CAmount fee = cert.totalAmount - cert.GetValueOut();

    if ( fee < minimumFee)
    {
        LogPrint("sc", "%s():%d - Invalid cert[%s] : fee %s is less than minimum: %s\n",
            __func__, __LINE__, cert.GetHash().ToString(), FormatMoney(fee), FormatMoney(minimumFee) );

        return state.DoS(100, error("invalid amount or fee"), REJECT_INVALID, "bad-cert-amount-or-fee");
    }

    // TODO cert: add check on vbt_ccout whenever they have data

    return true;
}

bool ScCoinsView::IsCertAllowedInMempool(const CTxMemPool& pool, const CScCertificate& cert, CValidationState& state)
{
    const uint256& certHash = cert.GetHash();

    const uint256& scId = cert.scId;
    const CAmount& certAmount = cert.totalAmount;
 
    // do not use a sccoinsview since we are only reading

    CAmount scBalance    = 0;
    CAmount scBalanceImm = 0;

    // check that epoch data are consistent
    if (!ScMgr::isLegalEpoch(cert.scId, cert.epochNumber, cert.endEpochBlockHash) )
    {
        LogPrint("sc", "%s():%d - invalid cert[%s], scId[%s] invalid epoch data\n",
            __func__, __LINE__, certHash.ToString(), scId.ToString() );
        return state.Invalid(error("certificate with invalid epoch considering mempool"),
             REJECT_INVALID, "sidechain-certificate-epoch");
    }

    // a certificate can not be received after a fixed amount of blocks (for the time being it is epoch length / 5) from the end of epoch (TODO)
    int maxHeight = ScMgr::getCertificateMaxIncomingHeight(cert.scId, cert.epochNumber);
    if (maxHeight < 0)
    {
        LogPrintf("ERROR: certificate %s, can not calculate max recv height\n", certHash.ToString());
        return state.Invalid(error("can not calculate max recv height for cert"),
             REJECT_INVALID, "sidechain-certificate-error");
    }

    if (maxHeight < chainActive.Height())
    {
        LogPrintf("ERROR: delayed certificate[%s], max height for receiving = %d, active height = %d\n",
            certHash.ToString(), maxHeight, chainActive.Height());
        return state.Invalid(error("received a delayed cert"),
             REJECT_INVALID, "sidechain-certificate-delayed");
    }

    // when called for checking the contents of mempool we can find the certificate itself, which is OK
    uint256 conflictingCertHash;
    if (ScMgr::epochAlreadyHasCertificate(scId, cert.epochNumber, pool, conflictingCertHash)
        && (conflictingCertHash != cert.GetHash() ))
    {
        LogPrintf("ERROR: certificate %s for epoch %d is already been issued\n", 
            (conflictingCertHash == uint256())?"":conflictingCertHash.ToString(), cert.epochNumber);
        return state.Invalid(error("A certificate with the same scId/epoch is already issued"),
             REJECT_INVALID, "sidechain-certificate-epoch");
    }

    if (ScMgr::instance().sidechainExists(cert.scId))
    {
        scBalance =    ScMgr::instance().getSidechainBalance(cert.scId);
        scBalanceImm = ScMgr::instance().getSidechainBalanceImmature(cert.scId);
        LogPrint("cert", "%s():%d - scId balance mature[%s], immature[%s]\n",
            __func__, __LINE__, FormatMoney(scBalance), FormatMoney(scBalanceImm) );

        // we also consider immature amounts that will turn ito mature one in one of next blocks
        scBalance += scBalanceImm;
    }
    else
    {
        LogPrint("sc", "%s():%d - cert[%s]: scId[%s] not found in db\n",
            __func__, __LINE__, certHash.ToString(), cert.scId.ToString() );

        // maybe the creation is in the mempool (chain reorg)
        bool creationIsInMempool = false;

        for (auto it = pool.mapTx.begin(); it != pool.mapTx.end(); ++it)
        {
            if (creationIsInMempool)
            {
                break;
            }

            const CTransaction& mpTx = it->second.GetTx();

            BOOST_FOREACH(const auto& mpSc, mpTx.vsc_ccout)
            {
                if (mpSc.scId == scId)
                {
                    LogPrint("sc", "%s():%d - cert[%s]: scId[%s] is going to be created by tx [%s] which is still in mempool\n",
                        __func__, __LINE__, certHash.ToString(), scId.ToString(), mpTx.GetHash().ToString() );
                    creationIsInMempool = true;
                    break;
                }
            }
        }

        if (!creationIsInMempool)
        {
            // scId creation not found, should never happen
            LogPrintf("%s():%d - ERROR: cert[%s]: scId[%s] not found in db and no tx in mempool is creating it\n",
                __func__, __LINE__, certHash.ToString(), scId.ToString() );
            return state.Invalid(error("certificate with invalid scId considering mempool"),
                 REJECT_INVALID, "sidechain-certificate");
        }
    }

    LogPrint("cert", "%s():%d - scId balance: %s\n", __func__, __LINE__, FormatMoney(scBalance) );
 
    // consider fw transfers even if they can not be mature yet, and let a miner sort things
    // out in terms of dependancy
    for (auto it = pool.mapTx.begin(); it != pool.mapTx.end(); ++it)
    {
        const CTransaction& mpTx = it->second.GetTx();
        if (mpTx.nVersion == SC_TX_VERSION)
        {
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
 
    for (auto it = pool.mapCertificate.begin(); it != pool.mapCertificate.end(); ++it)
    {
        const CScCertificate& mpCert = it->second.GetCertificate();

        if (mpCert.GetHash() == cert.GetHash())
        {
            // this might happen when called for checking the contents of mempool
            continue;
        }

        // TODO cert: check if this should not be allowed instead, since there can not be two certificates in mempool
        // referring to the same sc
        if (scId == mpCert.scId)
        {
            LogPrint("sc", "%s():%d - #### found cert[%s] for the same scId: amount[%s]\n",
                __func__, __LINE__, mpCert.GetHash().ToString(), FormatMoney(mpCert.totalAmount) );
            scBalance -= mpCert.totalAmount;
        }
    }
 
    if (certAmount > scBalance)
    {
        LogPrint("sc", "%s():%d - invalid cert[%s]: scId[%s] has not balance enough: %s\n",
            __func__, __LINE__, certHash.ToString(), scId.ToString(), FormatMoney(scBalance) );
        return state.Invalid(error("certificate with no sc balance enough considering mempool"),
             REJECT_INVALID, "sidechain-certificate");
    }
    return true;
}

bool ScCoinsView::IsTxAllowedInMempool(const CTxMemPool& pool, const CTransaction& tx, CValidationState& state)
{
    //Check for conflicts in mempool
    BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
    {
        for (auto it = pool.mapTx.begin(); it != pool.mapTx.end(); ++it)
        {
            const CTransaction& mpTx = it->second.GetTx();

            if (mpTx.GetHash() == tx.GetHash())
            {
                // this might happen when called for checking the contents of mempool
                continue;
            }

            BOOST_FOREACH(const auto& mpSc, mpTx.vsc_ccout)
            {
                if (mpSc.scId == sc.scId)
                {
                    LogPrint("sc", "%s():%d - invalid tx[%s]: scId[%s] already created by tx[%s]\n",
                        __func__, __LINE__, tx.GetHash().ToString(), sc.scId.ToString(), mpTx.GetHash().ToString() );
                            return state.Invalid(error("transaction tries to create scId already created in mempool"),
                            REJECT_INVALID, "sidechain-creation");
                }
            }
        }
    }
    return true;
}

bool ScCoinsView::IsTxApplicableToState(const CTransaction& tx)
{
    const uint256& txHash = tx.GetHash();

    // check creation
    BOOST_FOREACH(const auto& sc, tx.vsc_ccout)
    {
        const uint256& scId = sc.scId;
        if (sidechainExists(scId))
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : scId[%s] already created\n",
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
        if (!sidechainExists(scId))
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

bool ScCoinsView::IsCertApplicableToState(const CScCertificate& cert)
{
    if (!sidechainExists(cert.scId) )
    {
        LogPrint("sc", "%s():%d - cert[%s] refers to scId[%s] not yet created\n",
            __func__, __LINE__, cert.GetHash().ToString(), cert.scId.ToString() );
        return false;
    }

    CAmount curBalance = getSidechainBalance(cert.scId);
    if (cert.totalAmount > curBalance)
    {
        LogPrint("sc", "%s():%d - insufficent balance in scId[%s]: balance[%s], cert amount[%s]\n",
            __func__, __LINE__, cert.scId.ToString(), FormatMoney(curBalance), FormatMoney(cert.totalAmount) );
        return false;
    }
    LogPrint("sc", "%s():%d - ok, balance in scId[%s]: balance[%s], cert amount[%s]\n",
        __func__, __LINE__, cert.scId.ToString(), FormatMoney(curBalance), FormatMoney(cert.totalAmount) );

    return true;
}

bool ScCoinsView::hasScCreationOutput(const CTransaction& tx, const uint256& scId)
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

bool ScCoinsView::anyForwardTransaction(const CTransaction& tx, const uint256& scId)
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

/********************** ScCoinsViewCache IMPLEMENTATION **********************/
ScCoinsViewCache::ScCoinsViewCache(ScCoinsPersistedView& _persistedView): persistedView(_persistedView) {}

bool ScCoinsViewCache::sidechainExists(const uint256& scId) const
{
    return mUpdatedOrNewScInfoList.count(scId) || (persistedView.sidechainExists(scId) && !sDeletedScList.count(scId));
}

bool ScCoinsViewCache::getScInfo(const uint256 & scId, ScInfo& targetScInfo) const
{
    const auto it = mUpdatedOrNewScInfoList.find(scId);
    if (it != mUpdatedOrNewScInfoList.end() )
    {
        targetScInfo = ScInfo(it->second);
        return true;
    }

    if (persistedView.getScInfo(scId,targetScInfo) && sDeletedScList.count(scId) == 0)
    {
        return true;
    }

    return false;
}

CAmount ScCoinsViewCache::getSidechainBalance(const uint256 & scId) const
{
    const auto it = mUpdatedOrNewScInfoList.find(scId);
    if (it != mUpdatedOrNewScInfoList.end() )
    {
        return it->second.balance;
    }

    if (sDeletedScList.count(scId) == 0)
    {
        return persistedView.getSidechainBalance(scId);
    }

    return -1;
}

CAmount ScCoinsViewCache::getSidechainBalanceImmature(const uint256 & scId) const
{
    const auto it = mUpdatedOrNewScInfoList.find(scId);
    if (it != mUpdatedOrNewScInfoList.end() )
    {
        CAmount tot = 0;
        BOOST_FOREACH(const auto& entry, it->second.mImmatureAmounts)
        {
            tot += entry.second;
        }
        return tot;
    }

    if (sDeletedScList.count(scId) == 0)
    {
        return persistedView.getSidechainBalanceImmature(scId);
    }

    return -1;
}

std::set<uint256> ScCoinsViewCache::getScIdSet() const
{
    std::set<uint256> res;

    BOOST_FOREACH(const auto& entry, mUpdatedOrNewScInfoList) {
      res.insert(entry.first);
    }

    std::set<uint256> persistedScIds = persistedView.getScIdSet();
    persistedScIds.erase(sDeletedScList.begin(),sDeletedScList.end());

    res.insert(persistedScIds.begin(), persistedScIds.end());

    return res;
}

bool ScCoinsViewCache::UpdateScInfo(const CTransaction& tx, const CBlock& block, int blockHeight)
{
    const uint256& txHash = tx.GetHash();
    LogPrint("sc", "%s():%d - enter tx=%s\n", __func__, __LINE__, txHash.ToString() );

    // creation ccout
    BOOST_FOREACH(const auto& cr, tx.vsc_ccout)
    {
        if (sidechainExists(cr.scId))
        {
            LogPrint("sc", "ERROR: %s():%d - CR: scId=%s already in scView\n", __func__, __LINE__, cr.scId.ToString() );
            return false;
            //No rollbacks of previously inserted creation/transfer ccout in current tx here
        }

        ScInfo scInfo;
        scInfo.creationBlockHash = block.GetHash();
        scInfo.creationBlockHeight = blockHeight;
        scInfo.creationTxHash = txHash;
        scInfo.lastReceivedCertificateEpoch = EPOCH_NULL;
        scInfo.creationData.withdrawalEpochLength = cr.withdrawalEpochLength;

        mUpdatedOrNewScInfoList[cr.scId] = scInfo;

        LogPrint("sc", "%s():%d - scId[%s] added in scView\n", __func__, __LINE__, cr.scId.ToString() );
    }

    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = blockHeight + SC_COIN_MATURITY;

    // forward transfer ccout
    BOOST_FOREACH(auto& ft, tx.vft_ccout)
    {
        ScInfo targetScInfo;
        if (!getScInfo(ft.scId, targetScInfo))
        {
            // should not happen
            LogPrint("sc", "%s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, ft.scId.ToString() );
            return false;
            //No rollbacks of previously inserted creation/transfer ccout in current tx here
        }

        // add a new immature balance entry in sc info or increment it if already there
        targetScInfo.mImmatureAmounts[maturityHeight] += ft.nValue;
        mUpdatedOrNewScInfoList[ft.scId] = targetScInfo;

        LogPrint("sc", "%s():%d - immature balance added in scView (h=%d, amount=%s) %s\n",
            __func__, __LINE__, maturityHeight, FormatMoney(ft.nValue), ft.scId.ToString());
    }

    return true;
}

bool ScCoinsViewCache::UpdateScInfo(const CScCertificate& cert, CBlockUndo& blockundo)
{
    const uint256& certHash = cert.GetHash();
    LogPrint("cert", "%s():%d - cert=%s\n", __func__, __LINE__, certHash.ToString() );
    
    const uint256& scId = cert.scId;
    const CAmount& totalAmount = cert.totalAmount;
 
    ScInfo targetScInfo;
    if (!getScInfo(scId, targetScInfo))
    {
        // should not happen
        LogPrint("cert", "%s():%d - Can not update balance, could not find scId=%s\n",
            __func__, __LINE__, scId.ToString() );
        return false;
    }

    // add a new immature balance entry in sc info or increment it if already there
    if (targetScInfo.balance < totalAmount)
    {
        LogPrint("cert", "%s():%d - Can not update balance %s with amount[%s] for scId=%s, would be negative\n",
            __func__, __LINE__, FormatMoney(targetScInfo.balance), FormatMoney(totalAmount), scId.ToString() );
        return false;
    }

    // if there is no entry in blockundo for this scId, create it and store current cert epoch
    if (blockundo.msc_iaundo.count(scId) )
    {
        LogPrint("sc", "%s():%d - scId=%s epoch before: %d\n",
            __func__, __LINE__, scId.ToString(), blockundo.msc_iaundo[scId].certEpoch);

        // entry already exists, update only cert epoch with current value
        blockundo.msc_iaundo[scId].certEpoch = targetScInfo.lastReceivedCertificateEpoch;
    }
    else
    {
        LogPrint("sc", "%s():%d - scId=%s new entry in blockundo\n",
            __func__, __LINE__, scId.ToString());

        // new entry, initialize amount to 0
        ScUndoData data;
        data.immAmount = 0;
        data.certEpoch = targetScInfo.lastReceivedCertificateEpoch;
        blockundo.msc_iaundo[scId] = data;
    }

    LogPrint("sc", "%s():%d - scId=%s epoch after: %d\n",
        __func__, __LINE__, scId.ToString(), blockundo.msc_iaundo[scId].certEpoch);

    targetScInfo.balance -= totalAmount;
    targetScInfo.lastReceivedCertificateEpoch = cert.epochNumber;
    mUpdatedOrNewScInfoList[scId] = targetScInfo;

    LogPrint("cert", "%s():%d - amount removed from scView (amount=%s, resulting bal=%s) %s\n",
        __func__, __LINE__, FormatMoney(totalAmount), FormatMoney(targetScInfo.balance), scId.ToString());

    return true;
}

bool ScCoinsViewCache::RevertTxOutputs(const CTransaction& tx, int nHeight)
{
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = nHeight + SC_COIN_MATURITY;

    // revert forward transfers
    BOOST_FOREACH(const auto& entry, tx.vft_ccout)
    {
        const uint256& scId = entry.scId;

        LogPrint("sc", "%s():%d - removing fwt for scId=%s\n", __func__, __LINE__, scId.ToString());

        ScInfo targetScInfo;
        if (!getScInfo(scId, targetScInfo))
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
        mUpdatedOrNewScInfoList[scId] = targetScInfo;

        LogPrint("sc", "%s():%d - immature amount after: %s\n",
            __func__, __LINE__, FormatMoney(iaMap[maturityHeight]));

        if (iaMap[maturityHeight] == 0)
        {
            iaMap.erase(maturityHeight);
            mUpdatedOrNewScInfoList[scId] = targetScInfo;
            LogPrint("sc", "%s():%d - removed entry height=%d from immature amounts in memory\n",
                __func__, __LINE__, maturityHeight );
        }
    }

    // remove sidechain if the case
    BOOST_FOREACH(const auto& entry, tx.vsc_ccout)
    {
        const uint256& scId = entry.scId;

        LogPrint("sc", "%s():%d - removing scId=%s\n", __func__, __LINE__, scId.ToString());

        ScInfo targetScInfo;
        if (!getScInfo(scId, targetScInfo))
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

        mUpdatedOrNewScInfoList.erase(scId);
        sDeletedScList.insert(scId);

        LogPrint("sc", "%s():%d - scId=%s removed from scView\n", __func__, __LINE__, scId.ToString() );
    }
    return true;
}

bool ScCoinsViewCache::RevertCertOutputs(const CScCertificate& cert, int nHeight)
{
    const uint256& scId = cert.scId;
    const CAmount& totalAmount = cert.totalAmount;

    LogPrint("cert", "%s():%d - removing cert for scId=%s\n", __func__, __LINE__, scId.ToString());

    ScInfo targetScInfo;
    if (!getScInfo(scId, targetScInfo))
    {
        // should not happen
        LogPrint("cert", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
        return false;
    }

    targetScInfo.balance += totalAmount;
    mUpdatedOrNewScInfoList[scId] = targetScInfo;

    LogPrint("cert", "%s():%d - amount restored to scView (amount=%s, resulting bal=%s) %s\n",
        __func__, __LINE__, FormatMoney(totalAmount), FormatMoney(targetScInfo.balance), scId.ToString());

    return true;
}

bool ScCoinsViewCache::ApplyMatureBalances(int blockHeight, CBlockUndo& blockundo)
{
    LogPrint("sc", "%s():%d - blockHeight=%d, msc_iaundo size=%d\n",
        __func__, __LINE__, blockHeight,  blockundo.msc_iaundo.size() );

    std::set<uint256> allKnowScIds = getScIdSet();
    for(auto it_set = allKnowScIds.begin(); it_set != allKnowScIds.end(); ++it_set)
    {
        const uint256& scId = *it_set;
        const std::string& scIdString = scId.ToString();

        ScInfo targetScInfo;
        assert(getScInfo(scId, targetScInfo));

        auto it_ia_map = targetScInfo.mImmatureAmounts.begin();

        while (it_ia_map != targetScInfo.mImmatureAmounts.end() )
        {
            int maturityHeight = it_ia_map->first;
            CAmount a = it_ia_map->second;

            if (maturityHeight == blockHeight)
            {
                LogPrint("sc", "%s():%d - scId=%s balance before: %s\n",
                    __func__, __LINE__, scIdString, FormatMoney(targetScInfo.balance));

                // if maturity has been reached apply it to balance in scview
                targetScInfo.balance += a;

                LogPrint("sc", "%s():%d - scId=%s balance after: %s\n",
                    __func__, __LINE__, scIdString, FormatMoney(targetScInfo.balance));

                // scview balance has been updated, remove the entry in scview immature map
                it_ia_map = targetScInfo.mImmatureAmounts.erase(it_ia_map);
                mUpdatedOrNewScInfoList[scId] = targetScInfo;

                LogPrint("sc", "%s():%d - adding immature amount %s for scId=%s in blockundo\n",
                    __func__, __LINE__, FormatMoney(a), scIdString);
          
                // store immature balances into the blockundo obj
                blockundo.msc_iaundo[scId].immAmount = a;
            }
            else
            if (maturityHeight < blockHeight)
            {
                // should not happen
                LogPrint("sc", "ERROR: %s():%d - scId=%s maturuty(%d) < blockHeight(%d)\n",
                    __func__, __LINE__, scIdString, maturityHeight, blockHeight);
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

bool ScCoinsViewCache::RestoreImmatureBalances(int blockHeight, const CBlockUndo& blockundo)
{
    LogPrint("sc", "%s():%d - blockHeight=%d, msc_iaundo size=%d\n",
        __func__, __LINE__, blockHeight,  blockundo.msc_iaundo.size() );

    auto it_ia_undo_map = blockundo.msc_iaundo.begin();

    // loop in the map of the blockundo and process each sidechain id
    while (it_ia_undo_map != blockundo.msc_iaundo.end() )
    {
        const uint256& scId           = it_ia_undo_map->first;
        const std::string& scIdString = scId.ToString();

        ScInfo targetScInfo;
        if (!getScInfo(scId, targetScInfo))
        {
            // should not happen
            LogPrint("sc", "ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        CAmount a = it_ia_undo_map->second.immAmount;
        int e = it_ia_undo_map->second.certEpoch;

        if (a > 0)
        {
            LogPrint("sc", "%s():%d - adding immature amount %s into sc view for scId=%s\n",
                __func__, __LINE__, FormatMoney(a), scIdString);

            targetScInfo.mImmatureAmounts[blockHeight] += a;
 
            if (targetScInfo.balance < a)
            {
                LogPrint("sc", "%s():%d - Can not update balance with amount[%s] for scId=%s, would be negative\n",
                    __func__, __LINE__, FormatMoney(a), scId.ToString() );
                return false;
            }
 
            LogPrint("sc", "%s():%d - scId=%s balance before: %s\n", __func__, __LINE__, scIdString, FormatMoney(targetScInfo.balance));
            targetScInfo.balance -= a;
            LogPrint("sc", "%s():%d - scId=%s balance after: %s\n", __func__, __LINE__, scIdString, FormatMoney(targetScInfo.balance));
        }

        if (e > 0 || e == EPOCH_NULL) // allow -1 which is the default value at sc creation
        {
            LogPrint("sc", "%s():%d - scId=%s epoch before: %d\n", __func__, __LINE__, scIdString, targetScInfo.lastReceivedCertificateEpoch);
            targetScInfo.lastReceivedCertificateEpoch = it_ia_undo_map->second.certEpoch;
            LogPrint("sc", "%s():%d - scId=%s epoch after: %d\n", __func__, __LINE__, scIdString, targetScInfo.lastReceivedCertificateEpoch);
        }

        mUpdatedOrNewScInfoList[scId] = targetScInfo;

        ++it_ia_undo_map;
    }
    return true;
}

bool ScCoinsViewCache::Flush()
{
    LogPrint("sc", "%s():%d - called\n", __func__, __LINE__);

    // 1. update the entries that have been added or modified
    BOOST_FOREACH(const auto& entry, mUpdatedOrNewScInfoList)
    {
        if (!persistedView.persist(entry.first, entry.second) )
        {
            return false;
        }
    }

    // 2. process the entries to be erased
    BOOST_FOREACH(const auto& entry, sDeletedScList)
    {
        if (!persistedView.erase(entry))
            return false;
    }

    return true;
}

/**************************** ScMgr IMPLEMENTATION ***************************/
ScMgr& ScMgr::instance()
{
    static ScMgr _instance;
    return _instance;
}

bool ScMgr::initPersistence(size_t cacheSize, bool fWipe)
{
    if (pLayer != nullptr)
    {
        return error("%s():%d - could not init persistence more than once!", __func__, __LINE__);
    }

    pLayer = new DbPersistance(GetDataDir() / "sidechains", cacheSize, false, fWipe);

    return loadInitialData();
}

bool ScMgr::initPersistence(PersistenceLayer * pTestLayer)
{
    if (pLayer != nullptr)
    {
        return error("%s():%d - could not init persistence more than once!", __func__, __LINE__);
    }

    pLayer = pTestLayer;

    return loadInitialData();
}

bool ScMgr::loadInitialData()
{
    LOCK(sc_lock);
    try
    {
        bool res = pLayer->loadPersistedDataInto(mManagerScInfoMap);
        if (!res)
        {
            return error("%s():%d - error occurred during db scan", __func__, __LINE__);
        }
    }
    catch (const std::exception& e)
    {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    return true;
}

void ScMgr::reset()
{
    delete pLayer;
    pLayer = nullptr;

    mManagerScInfoMap.clear();
}

bool ScMgr::persist(const uint256& scId, const ScInfo& info)
{
    LOCK(sc_lock);
    if (pLayer == nullptr)
    {
        LogPrintf("%s():%d - Error: sc persistence layer not initialized\n", __func__, __LINE__);
        return false;
    }

    if (!pLayer->persist(scId, info))
        return false;

    mManagerScInfoMap[scId] = info;
    LogPrint("sc", "%s():%d - persisted scId=%s\n", __func__, __LINE__, scId.ToString() );
    return true;
}

bool ScMgr::erase(const uint256& scId)
{
    LOCK(sc_lock);
    if (pLayer == nullptr)
    {
        LogPrintf("%s():%d - Error: sc persistence layer not initialized\n", __func__, __LINE__);
        return false;
    }

    if (!mManagerScInfoMap.erase(scId))
    {
        LogPrint("sc", "ERROR: %s():%d - scId=%s not in map\n", __func__, __LINE__, scId.ToString() );
        return false;
    }

    LogPrint("sc", "%s():%d - erased scId=%s from memory\n", __func__, __LINE__, scId.ToString() );
    return pLayer->erase(scId);
}

bool ScMgr::sidechainExists(const uint256& scId) const
{
    LOCK(sc_lock);
    return mManagerScInfoMap.count(scId);
}

int ScMgr::getCertificateMaxIncomingHeight(const uint256& scId, int epochNumber)
{
    ScInfo info;
    if (!instance().getScInfo(scId, info) )
    {
        LogPrint("cert", "%s():%d - scId[%s] not found, returning -1\n", __func__, __LINE__, scId.ToString() );
        return -1;
    }

    // the safety margin from the end of referred epoch is computed as 20% of epoch length + 1
    // TODO move this in consensus params
    int val = info.creationBlockHeight + (epochNumber * info.creationData.withdrawalEpochLength) +
        (int)(info.creationData.withdrawalEpochLength/5) + 1;

    LogPrint("cert", "%s():%d - returning %d\n", __func__, __LINE__, val);
    return val;
}

bool ScMgr::epochAlreadyHasCertificate(const uint256& scId, int epochNumber, const CTxMemPool& pool, uint256& certHash)
{
    certHash = uint256();

    // check the mempool first
    for (auto it = pool.mapCertificate.begin(); it != pool.mapCertificate.end(); ++it)
    {
        const CScCertificate& mpCert = it->second.GetCertificate();

        if ((mpCert.scId == scId) && (mpCert.epochNumber == epochNumber))
        {
            LogPrint("sc", "%s():%d - cert[%s] in mempool already refers to scId[%s] and epoch[%d]\n",
                __func__, __LINE__, mpCert.GetHash().ToString(), scId.ToString(), epochNumber);
            certHash = mpCert.GetHash();
            return true;
        }
    }

    // check db
    ScInfo info;
    if (!instance().getScInfo(scId, info) )
    {
        LogPrint("cert", "%s():%d - scId[%s] not found\n", __func__, __LINE__, scId.ToString() );
        return false;
    }

    if (info.lastReceivedCertificateEpoch == epochNumber)
    {
        LogPrint("sc", "%s():%d - a cert for scId[%s] and epoch[%d] has already been received\n",
            __func__, __LINE__, scId.ToString(), epochNumber);
        return true;
    }

    return false;
}

bool ScMgr::isLegalEpoch(const uint256& scId, int epochNumber, const uint256& endEpochBlockHash)
{
    if (epochNumber <= 0)
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
    ScInfo info;
    if (!instance().getScInfo(scId, info) )
    {
        // should not happen
        LogPrint("sc", "%s():%d - scId[%s] not found\n",
            __func__, __LINE__, scId.ToString() );
        return false;
    }

    int endEpochHeight = info.creationBlockHeight + (epochNumber * info.creationData.withdrawalEpochLength);
    pblockindex = chainActive[endEpochHeight];

    if (!pblockindex)
    {
        LogPrint("sc", "%s():%d - calculated height %d (createHeight=%d/epochNum=%d/epochLen=%d) is out of active chain\n",
            __func__, __LINE__, endEpochHeight, info.creationBlockHeight, epochNumber, info.creationData.withdrawalEpochLength);
        return false;
    }

    const uint256& hash = pblockindex->GetBlockHash();

    bool ret = (hash == endEpochBlockHash);
    if (!ret)
    {
        LogPrint("sc", "%s():%d - bock hash mismatch: endEpochBlockHash[%s] / calculated[%s]\n", 
            __func__, __LINE__, endEpochBlockHash.ToString(), hash.ToString());
    }

    return ret;
}

bool ScMgr::getScInfo(const uint256& scId, ScInfo& info) const
{
    LOCK(sc_lock);
    const auto it = mManagerScInfoMap.find(scId);
    if (it == mManagerScInfoMap.end() )
    {
        return false;
    }

    // create a copy
    info = ScInfo(it->second);
    LogPrint("sc", "scId[%s]: %s", scId.ToString(), info.ToString() );
    return true;
}

std::set<uint256> ScMgr::getScIdSet() const
{
    std::set<uint256> sScIds;
    LOCK(sc_lock);
    BOOST_FOREACH(const auto& entry, mManagerScInfoMap)
    {
        sScIds.insert(entry.first);
    }

    return sScIds;
}

CAmount ScMgr::getSidechainBalance(const uint256& scId) const
{
    LOCK(sc_lock);
    ScInfoMap::const_iterator it = mManagerScInfoMap.find(scId);
    if (it == mManagerScInfoMap.end() )
    {
        // caller should have checked it
        return -1;
    }
        
    return it->second.balance;
}

CAmount ScMgr::getSidechainBalanceImmature(const uint256& scId) const
{
    LOCK(sc_lock);
    ScInfoMap::const_iterator it = mManagerScInfoMap.find(scId);
    if (it == mManagerScInfoMap.end() )
    {
        // caller should have checked it
        return -1;
    }

    CAmount tot = 0;
    BOOST_FOREACH(const auto& entry, it->second.mImmatureAmounts)
    {
        tot += entry.second;
    }
    return tot;
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
    LogPrint("sc", "  lastReceivedCertEpoch[%d]\n", info.lastReceivedCertificateEpoch);
    LogPrint("sc", "  balance[%s]\n", FormatMoney(info.balance));
    LogPrint("sc", "  ----- creation data:\n");
    LogPrint("sc", "      withdrawalEpochLength[%d]\n", info.creationData.withdrawalEpochLength);
    LogPrint("sc", "  immature amounts size[%d]\n", info.mImmatureAmounts.size());
// TODO    LogPrint("sc", "      ...more to come...\n");

    return true;
}

void ScMgr::dump_info()
{
    LogPrint("sc", "-- number of side chains found [%d] ------------------------\n", mManagerScInfoMap.size());
    BOOST_FOREACH(const auto& entry, mManagerScInfoMap)
    {
        dump_info(entry.first);
    }

    if (pLayer == nullptr)
    {
        return;
    }

    return pLayer->dump_info();
}

/********************** PERSISTENCE LAYER IMPLEMENTATION *********************/
DbPersistance::DbPersistance(const boost::filesystem::path& path, size_t nCacheSize, bool fMemory, bool fWipe)
{
    _db = new CLevelDBWrapper(GetDataDir() / "sidechains", nCacheSize, fMemory, fWipe);
}

DbPersistance::~DbPersistance() { delete _db; _db = nullptr; };

bool DbPersistance::loadPersistedDataInto(ScInfoMap & _mapToFill)
{
    boost::scoped_ptr<leveldb::Iterator> it(_db->NewIterator());
    for (it->SeekToFirst(); it->Valid(); it->Next())
    {
        boost::this_thread::interruption_point();

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

            _mapToFill[keyScId] = info;
            LogPrint("sc", "%s():%d - scId[%s] added in map\n", __func__, __LINE__, keyScId.ToString() );
        }
        else
        {
            // should never happen
            LogPrintf("%s():%d - Error: could not read from db, invalid record type %c\n", __func__, __LINE__, chType);
            return false;
        }
    }

    return it->status().ok();
}

bool DbPersistance::persist(const uint256& scId, const ScInfo& info)
{
    CLevelDBBatch batch;
    bool ret = true;

    try {
        batch.Write(std::make_pair(DB_SC_INFO, scId), info );
        // do it synchronously (true)
        ret = _db->WriteBatch(batch, true);
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

bool DbPersistance::erase(const uint256& scId)
{
    // erase from level db
    CLevelDBBatch batch;
    bool ret = false;

    try {
        batch.Erase(std::make_pair(DB_SC_INFO, scId));
        ret = _db->WriteBatch(batch, true);
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
    return ret;
}

void DbPersistance::dump_info()
{
    // dump leveldb contents on stdout
    boost::scoped_ptr<leveldb::Iterator> it(_db->NewIterator());

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
                << "  last recv cert epoch: " << info.lastReceivedCertificateEpoch << std::endl
                // creation parameters
                << "  withdrawalEpochLength: " << info.creationData.withdrawalEpochLength << std::endl;
        }
        else
        {
            std::cout << "unknown type " << chType << std::endl;
        }
    }
}

// TEMP TODO
std::string CBlockUndo::ToString() const
{
    std::string str;
    str += "______CBlockUndo(\n";
    BOOST_FOREACH(const auto& obj, msc_iaundo)
    {
        str += strprintf("   scId=%s -> amount=%s, certEpoch=%d\n",
            obj.first.ToString(), FormatMoney(obj.second.immAmount), obj.second.certEpoch);
    }
    str += ")_____";
    return str;
}
