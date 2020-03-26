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


bool Sidechain::checkTxSemanticValidity(const CTransaction& tx, CValidationState& state)
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
        if (tx.getJoinsSplit().size() > 0)
        {
            return state.DoS(100,
                error("mismatch between transaction version and joinsplit presence"),
                REJECT_INVALID, "sidechain-tx-version");
        }
    }

    const uint256& txHash = tx.GetHash();

    LogPrint("sc", "%s():%d - tx=%s\n", __func__, __LINE__, txHash.ToString() );

    CAmount cumulatedAmount = 0;

    static const int SC_MIN_WITHDRAWAL_EPOCH_LENGTH = getScMinWithdrawalEpochLength();

    for (const auto& sc : tx.vsc_ccout)
    {
        if (sc.withdrawalEpochLength < SC_MIN_WITHDRAWAL_EPOCH_LENGTH)
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : sc creation withdrawalEpochLength %d is non-positive\n",
                __func__, __LINE__, txHash.ToString(), sc.withdrawalEpochLength);
            return state.DoS(100, error("%s: sc creation withdrawalEpochLength is not valid",
                __func__), REJECT_INVALID, "sidechain-sc-creation-epoch-not-valid");
        }

        if (!sc.CheckAmountRange(cumulatedAmount) )
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : sc creation amount is non-positive or larger than %s\n",
                __func__, __LINE__, txHash.ToString(), FormatMoney(MAX_MONEY) );
            return state.DoS(100, error("%s: sc creation amount is outside range",
                __func__), REJECT_INVALID, "sidechain-sc-creation-amount-outside-range");
        }
    }

    for (const auto& ft : tx.vft_ccout)
    {
        if (!ft.CheckAmountRange(cumulatedAmount) )
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : sc creation amount is non-positive or larger than %s\n",
                __func__, __LINE__, txHash.ToString(), FormatMoney(MAX_MONEY) );
            return state.DoS(100, error("%s: sc creation amount is outside range",
                __func__), REJECT_INVALID, "sidechain-sc-creation-amount-outside-range");
        }
    }

    return true;
}

bool Sidechain::anyForwardTransaction(const CTransaction& tx, const uint256& scId)
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

bool Sidechain::hasScCreationOutput(const CTransaction& tx, const uint256& scId)
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

bool Sidechain::checkCertSemanticValidity(const CScCertificate& cert, CValidationState& state)
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

    if (cert.totalAmount != cert.GetValueOut())
    {
        LogPrint("sc", "%s():%d - Invalid cert[%s] : certificate amount is different than the sum of outputs\n",
            __func__, __LINE__, certHash.ToString() );
        return state.DoS(100, error("%s: certificate amount is different than the sum of its outputs",
            __func__), REJECT_INVALID, "sidechain-bwd-transfer-amount-invalid");
    }

    CAmount minimumFee = ::minRelayTxFee.GetFee(cert.CalculateSize());
    CAmount fee = cert.fee;

    if ( fee < minimumFee)
    {
        LogPrint("sc", "%s():%d - Invalid cert[%s] : fee %s is less than minimum: %s\n",
            __func__, __LINE__, cert.GetHash().ToString(), FormatMoney(fee), FormatMoney(minimumFee) );

        return state.DoS(100, error("invalid amount or fee"), REJECT_INVALID, "bad-cert-amount-or-fee");
    }

    // TODO cert: add check on vbt_ccout whenever they have data

    return true;
}
