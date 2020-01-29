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
CSidechainsViewCache::CSidechainsViewCache(CCoinsView* scView): CCoinsViewCache(scView) {}

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
