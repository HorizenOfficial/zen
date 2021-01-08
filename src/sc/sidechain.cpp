#include "sc/sidechain.h"
#include "sc/proofverifier.h"
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


int CSidechain::EpochFor(int targetHeight) const
{
    if (creationBlockHeight == -1) //default value
        return CScCertificate::EPOCH_NULL;

    return (targetHeight - creationBlockHeight) / creationData.withdrawalEpochLength;
}

int CSidechain::StartHeightForEpoch(int targetEpoch) const
{
    if (creationBlockHeight == -1) //default value
        return -1;

    return creationBlockHeight + targetEpoch * creationData.withdrawalEpochLength;
}

int CSidechain::SafeguardMargin() const
{
    if ( creationData.withdrawalEpochLength == -1) //default value
        return -1;
    return creationData.withdrawalEpochLength/5;
}

int CSidechain::GetCeasingHeight() const
{
    if ( creationData.withdrawalEpochLength == -1) //default value
        return -1;
    return StartHeightForEpoch(prevBlockTopQualityCertReferencedEpoch+2) + SafeguardMargin();
}

std::string CSidechain::stateToString(State s)
{
    switch(s)
    {
        case State::UNCONFIRMED: return "UNCONFIRMED";    break;
        case State::ALIVE:       return "ALIVE";          break;
        case State::CEASED:      return "CEASED";         break;
        default:                 return "NOT_APPLICABLE"; break;
    }
}

size_t CSidechain::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(mImmatureAmounts);
}

size_t CSidechainEvents::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(maturingScs) + memusage::DynamicUsage(ceasingScs);
}

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
        if (tx.GetVjoinsplit().size() > 0)
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

    for (const auto& sc : tx.GetVscCcOut())
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

        if (!libzendoomc::IsValidScVk(sc.wCertVk))
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : invalid wCert verification key\n",
                __func__, __LINE__, txHash.ToString());
            return state.DoS(100, error("%s: wCertVk is invalid",
                __func__), REJECT_INVALID, "sidechain-sc-creation-invalid-wcert-vk");
        }

        if(sc.constant.size() != 0)
        {
            if(!libzendoomc::IsValidScConstant(sc.constant))
            {
                LogPrint("sc", "%s():%d - Invalid tx[%s] : invalid constant\n",
                    __func__, __LINE__, txHash.ToString());
                return state.DoS(100, error("%s: constant is invalid",
                    __func__), REJECT_INVALID, "sidechain-sc-creation-invalid-constant");
            }
        }

        if (sc.wCeasedVk.is_initialized() && !libzendoomc::IsValidScVk(sc.wCeasedVk.get()))
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : invalid wCeasedVk verification key\n",
                __func__, __LINE__, txHash.ToString());
            return state.DoS(100, error("%s: wCeasedVk is invalid",
                __func__), REJECT_INVALID, "sidechain-sc-creation-invalid-wceased-vk");
        }
    }
    // Note: no sence to check FT and ScCr amounts, because they were chacked before in `tx.CheckAmounts`
    for (const auto& ft : tx.GetVftCcOut())
    {
        if (!ft.CheckAmountRange(cumulatedAmount) )
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : sc creation amount is non-positive or larger than %s\n",
                __func__, __LINE__, txHash.ToString(), FormatMoney(MAX_MONEY) );
            return state.DoS(100, error("%s: sc creation amount is outside range",
                __func__), REJECT_INVALID, "sidechain-sc-creation-amount-outside-range");
        }
    }

    for(const CTxCeasedSidechainWithdrawalInput& csw : tx.GetVcswCcIn())
    {
        if (csw.nEpoch < 0)
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : CSW epoch %d is non-positive\n",
                __func__, __LINE__, txHash.ToString(), csw.nEpoch);
            return state.DoS(100, error("%s: CSW epoch is not valid",
                __func__), REJECT_INVALID, "sidechain-cswinput-epoch-not-valid");
        }

        if(!libzendoomc::IsValidScFieldElement(csw.nullifier))
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : invalid CSW nullifier\n",
                __func__, __LINE__, txHash.ToString());
            return state.DoS(100, error("%s: CSW nullifier is invalid",
                __func__), REJECT_INVALID, "sidechain-cswinput-invalid-nullifier");
        }

        if(!libzendoomc::IsValidScProof(csw.scProof))
        {
            LogPrint("sc", "%s():%d - Invalid tx[%s] : invalid CSW proof\n",
                __func__, __LINE__, txHash.ToString());
            return state.DoS(100, error("%s: CSW proof is invalid",
                __func__), REJECT_INVALID, "sidechain-cswinput-invalid-proof");
        }
    }

    return true;
}

bool Sidechain::hasScCreationOutput(const CTransaction& tx, const uint256& scId)
{
    BOOST_FOREACH(const auto& sc, tx.GetVscCcOut())
    {
        if (sc.GetScId() == scId)
        {
            return true;
        }
    }
    return false;
}

bool Sidechain::checkCertSemanticValidity(const CScCertificate& cert, CValidationState& state)
{
    const uint256& certHash = cert.GetHash();

    if (cert.quality < 0)
    {
        LogPrint("sc", "%s():%d - Invalid cert[%s] : negative quality\n",
            __func__, __LINE__, certHash.ToString() );
        return state.DoS(100, error("%s: certificate quality is negative",
            __func__), REJECT_INVALID, "bad-cert-quality-negative");
    }

    if(!libzendoomc::IsValidScProof(cert.scProof))
    {
        LogPrint("sc", "%s():%d - Invalid cert[%s] : invalid scProof\n",
            __func__, __LINE__, certHash.ToString() );
        return state.DoS(100, error("%s: certificate scProof is not valid",
            __func__), REJECT_INVALID, "bad-cert-invalid-sc-proof");
    }

    return true;
}
