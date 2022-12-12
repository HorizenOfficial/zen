#include "sc/sidechain.h"
#include "sc/sidechaintypes.h"
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
#include <boost/filesystem.hpp>

static const boost::filesystem::path Sidechain::GetSidechainDataDir()
{
    static const boost::filesystem::path sidechainsDataDir = GetDataDir() / "sidechains";
    return sidechainsDataDir;
}

bool Sidechain::InitDLogKeys()
{
    CctpErrorCode errorCode;

    if (!zendoo_init_dlog_keys(SEGMENT_SIZE, &errorCode))
    {
        LogPrintf("%s():%d - Error calling zendoo_init_dlog_keys: errCode[0x%x]\n", __func__, __LINE__, errorCode);
        return false;
    }

    return true;
}

bool Sidechain::InitSidechainsFolder()
{
    // Note: sidechainsDataDir cannot be global since
    // at start of the program network parameters are not initialized yet

    if (!boost::filesystem::exists(Sidechain::GetSidechainDataDir()))
    {
        boost::filesystem::create_directories(Sidechain::GetSidechainDataDir());
    }
    return true;
}

void Sidechain::ClearSidechainsFolder()
{
    LogPrintf("Removing sidechains files [CURRENTLY ALL] for -reindex. Subfolders untouched.\n");

    for (boost::filesystem::directory_iterator it(Sidechain::GetSidechainDataDir());
         it != boost::filesystem::directory_iterator(); it++)
    {
        if (is_regular_file(*it))
            remove(it->path());
    }
}

CSidechain::State CSidechain::GetState(const CCoinsViewCache& view) const
{
    if (!isCreationConfirmed())
        return State::UNCONFIRMED;

    if (view.GetHeight() >= GetScheduledCeasingHeight())
        return State::CEASED;

    return State::ALIVE;
}

const CScCertificateView& CSidechain::GetActiveCertView(const CCoinsViewCache& view) const
{
    // For SC v2 non-ceaseable, we always return the last cert view
    if (isNonCeasing())
        return lastTopQualityCertView;

    if (GetState(view) == State::CEASED)
        return pastEpochTopQualityCertView;

    if (GetState(view) == State::UNCONFIRMED)
        return lastTopQualityCertView;

    int certReferencedEpoch = EpochFor(view.GetHeight() + 1 - GetCertSubmissionWindowLength()) - 1;

    if (lastTopQualityCertReferencedEpoch == certReferencedEpoch)
        return lastTopQualityCertView;

    // We are in the submission window and no cert for EpochFor(currentHeight) epoch was recv yet
    if (lastTopQualityCertReferencedEpoch - 1 == certReferencedEpoch)
        return pastEpochTopQualityCertView;

    assert(false);
}

int CSidechain::getInitScCoinsMaturity()
{
    if (Params().NetworkIDString() == "regtest")
    {
        int val = (int)(GetArg("-sccoinsmaturity", Params().ScCoinsMaturity()));
        LogPrint("sc", "%s():%d - %s: using val %d \n", __func__, __LINE__, Params().NetworkIDString().c_str(), val);
        return val;
    }
    return Params().ScCoinsMaturity();
}

int CSidechain::getScCoinsMaturity()
{
    // gets constructed just one time
    static int retVal(getInitScCoinsMaturity());
    return isNonCeasing() ? 0 : retVal;
}

bool CSidechain::CheckQuality(const CScCertificate& cert) const
{

    if (lastTopQualityCertHash != cert.GetHash() &&
        lastTopQualityCertReferencedEpoch == cert.epochNumber &&
        lastTopQualityCertQuality >= cert.quality)
    {
        LogPrint("cert", "%s.%s():%d - NOK, cert %s q=%d : a cert q=%d for same sc/epoch is already in blockchain\n",
            __FILE__, __func__, __LINE__, cert.GetHash().ToString(), cert.quality, lastTopQualityCertQuality);
        return false;
    }

    return true;
}

bool CSidechain::CheckCertTiming(int certEpoch, int referencedHeight, const CCoinsViewCache& view) const {
    if (GetState(view) != State::ALIVE)
    {
        return error("%s():%d - ERROR: certificate cannot be accepted, sidechain not alive\n",
            __func__, __LINE__);
    }

    // Adding handling of quality, we can have also certificates for the same epoch of the last certificate.
    // The epoch number must be consistent with the sc certificate history (no old epoch allowed)
    if (isNonCeasing())
    {
        if (certEpoch != lastTopQualityCertReferencedEpoch + 1)
        {
                return error("%s():%d - ERROR: non ceasing sidechain certificate cannot be accepted, wrong epoch. Certificate Epoch %d (expected: %d)\n",
                    __func__, __LINE__, certEpoch, lastTopQualityCertReferencedEpoch + 1);
        }

        // Check that every certificate references a block whose commitment tree includes the previous certificate.
        // This also implies referencedHeight > lastReferencedHeight.
        if (referencedHeight < lastInclusionHeight)
        {
            return error("%s():%d - ERROR: certificate cannot be accepted, cert height (%d) not greater than last certificate inclusion height (%d)\n",
                __func__, __LINE__, referencedHeight, lastInclusionHeight);
        }
    }
    else
    {
        if (certEpoch != lastTopQualityCertReferencedEpoch &&
            certEpoch != lastTopQualityCertReferencedEpoch + 1)
        {
            return error("%s():%d - ERROR: sidechain certificate cannot be accepted, wrong epoch. Certificate Epoch %d (expected: %d, or %d)\n",
                __func__, __LINE__, certEpoch, lastTopQualityCertReferencedEpoch, lastTopQualityCertReferencedEpoch+1);
        }
    }

    int inclusionHeight = view.GetHeight() + 1;

    int certWindowStartHeight = isNonCeasing() ? lastInclusionHeight : GetCertSubmissionWindowStart(certEpoch);
    int certWindowEndHeight   = isNonCeasing() ? INT_MAX : GetCertSubmissionWindowEnd(certEpoch);

    if (inclusionHeight < certWindowStartHeight || inclusionHeight > certWindowEndHeight)
    {
        return error("%s():%d - ERROR: certificate cannot be accepted, cert received outside safeguard\n",
            __func__, __LINE__);
    }

    return true;
}

int CSidechain::GetCurrentEpoch(const CCoinsViewCache& view) const
{
    if (isNonCeasing())
        // Before the first certificate arrives, lastTopQualityCertReferencedEpoch is set to -1
        return lastTopQualityCertReferencedEpoch + 1;

    return (GetState(view) == State::ALIVE) ?
        EpochFor(view.GetHeight()) :
        EpochFor(GetScheduledCeasingHeight());
}

int CSidechain::EpochFor(int targetHeight) const
{
    if (!isCreationConfirmed()) //default value
        return CScCertificate::EPOCH_NULL;

    assert(!isNonCeasing());

    return (targetHeight - creationBlockHeight) / fixedParams.withdrawalEpochLength;
}

int CSidechain::GetStartHeightForEpoch(int targetEpoch) const
{
    if (!isCreationConfirmed()) //default value
        return -1;

    return creationBlockHeight + targetEpoch * fixedParams.withdrawalEpochLength;
}

int CSidechain::GetEndHeightForEpoch(int targetEpoch) const
{
    if (!isCreationConfirmed() || isNonCeasing()) //default value
        return -1;

    return GetStartHeightForEpoch(targetEpoch) + fixedParams.withdrawalEpochLength - 1;
}

int CSidechain::GetCertSubmissionWindowStart(int certEpoch) const
{
    if (!isCreationConfirmed()) //default value
        return -1;

    return GetStartHeightForEpoch(certEpoch+1);
}

int CSidechain::GetCertSubmissionWindowEnd(int certEpoch) const
{
    if (!isCreationConfirmed()) //default value
        return -1;

    return GetCertSubmissionWindowStart(certEpoch) + GetCertSubmissionWindowLength() - 1;
}

int CSidechain::GetCertSubmissionWindowLength() const
{
    if (isNonCeasing())
        return 0;

    return std::max(2,fixedParams.withdrawalEpochLength/5);
} 

int CSidechain::GetCertMaturityHeight(int certEpoch, int includingBlockHeight) const
{
    if (!isCreationConfirmed()) //default value
        return -1;

    //return GetCertSubmissionWindowEnd(certEpoch+1);
    return isNonCeasing() ? includingBlockHeight : GetCertSubmissionWindowEnd(certEpoch + 1);
}

int CSidechain::GetScheduledCeasingHeight() const
{
    return isNonCeasing() ? INT_MAX : GetCertSubmissionWindowEnd(lastTopQualityCertReferencedEpoch+1);
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

std::string CSidechain::ToString() const
{
    std::string str;
    str = strprintf("\n CSidechain(version=%d\n creationBlockHeight=%d\n"
                      " creationTxHash=%s\n pastEpochTopQualityCertView=%s\n"
                      " lastTopQualityCertView=%s\n"
                      " lastTopQualityCertHash=%s\n lastTopQualityCertReferencedEpoch=%d\n"
                      " lastTopQualityCertQuality=%d\n"
                      " lastInclusionHeight=%d\n"
                      " lastTopQualityCertBwtAmount=%s\n balance=%s\n"
                      " fixedParams=[NOT PRINTED CURRENTLY]\n mImmatureAmounts=[NOT PRINTED CURRENTLY])",
        fixedParams.version
        , creationBlockHeight
        , creationTxHash.ToString()
        , pastEpochTopQualityCertView.ToString()
        , lastTopQualityCertView.ToString()
        , lastTopQualityCertHash.ToString()
        , lastTopQualityCertReferencedEpoch
        , lastTopQualityCertQuality
        , lastInclusionHeight
        , FormatMoney(lastTopQualityCertBwtAmount)
        , FormatMoney(balance)
    );

    return str;
}

size_t CSidechain::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(mImmatureAmounts) + memusage::DynamicUsage(scFees);
}

size_t CSidechainEvents::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(maturingScs) + memusage::DynamicUsage(ceasingScs);
}

#ifdef BITCOIN_TX
bool Sidechain::checkCertSemanticValidity(const CScCertificate& cert, CValidationState& state) { return true; }
bool Sidechain::checkTxSemanticValidity(const CTransaction& tx, CValidationState& state) { return true; }
bool CSidechain::GetCeasingCumTreeHash(CFieldElement& ceasedBlockCum) const { return true; }
void CSidechain::InitScFees() {}
void CSidechain::UpdateScFees(const CScCertificateView& certView, int blockHeight) {}
#else
bool Sidechain::checkTxSemanticValidity(const CTransaction& tx, CValidationState& state)
{
    // check version consistency
    if (!tx.IsScVersion() )
    {
        if (!tx.ccIsNull() )
        {
            return state.DoS(100,
                error("mismatch between transaction version and sidechain output presence"),
                CValidationState::Code::INVALID, "sidechain-tx-version");
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
                CValidationState::Code::INVALID, "sidechain-tx-version");
        }
    }

    const uint256& txHash = tx.GetHash();

    LogPrint("sc", "%s():%d - tx=%s\n", __func__, __LINE__, txHash.ToString() );

    CAmount cumulatedAmount = 0;

    static const int SC_MIN_WITHDRAWAL_EPOCH_LENGTH = getScMinWithdrawalEpochLength();
    static const int SC_MAX_WITHDRAWAL_EPOCH_LENGTH = getScMaxWithdrawalEpochLength();

    for (const auto& sc : tx.GetVscCcOut())
    {
        if (sc.withdrawalEpochLength < SC_MIN_WITHDRAWAL_EPOCH_LENGTH)
        {
            // This handles the special case when we requested a non-ceasing sc with version != 2
            if (!CSidechain::isNonCeasingSidechain(sc.version, sc.withdrawalEpochLength)) {
                return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], sc creation withdrawalEpochLength %d is less than min value %d\n",
                    __func__, __LINE__, txHash.ToString(), sc.withdrawalEpochLength, SC_MIN_WITHDRAWAL_EPOCH_LENGTH),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-epoch-too-short");
            }
            else if (sc.version != 2 && sc.withdrawalEpochLength == 0)
            {
                return state.DoS(100,
                    error("%s():%d -  ERROR: Invalid tx[%s] : requested a non-v2 sidechain with withdrawal epoch length == 0\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-bad-version");
            }
        }

        if (sc.withdrawalEpochLength > SC_MAX_WITHDRAWAL_EPOCH_LENGTH)
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], sc creation withdrawalEpochLength %d is greater than max value %d\n",
                    __func__, __LINE__, txHash.ToString(), sc.withdrawalEpochLength, SC_MAX_WITHDRAWAL_EPOCH_LENGTH),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-epoch-too-long");
        }

        if (!sc.CheckAmountRange(cumulatedAmount) )
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], sc creation amount is non-positive or larger than %s\n",
                    __func__, __LINE__, txHash.ToString(), FormatMoney(MAX_MONEY)),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-amount-outside-range");
        }

        for(const auto& config: sc.vFieldElementCertificateFieldConfig)
        {
            if (!config.IsValid())
                return state.DoS(100,
                        error("%s():%d - ERROR: Invalid tx[%s], invalid config parameters for vFieldElementCertificateFieldConfig\n",
                        __func__, __LINE__, txHash.ToString()), CValidationState::Code::INVALID, "sidechain-sc-creation-invalid-custom-config");
        }

        for(const auto& config: sc.vBitVectorCertificateFieldConfig)
        {
            if (!config.IsValid())
                return state.DoS(100,
                        error("%s():%d - ERROR: Invalid tx[%s], invalid config parameters for vBitVectorCertificateFieldConfig\n",
                        __func__, __LINE__, txHash.ToString()), CValidationState::Code::INVALID, "sidechain-sc-creation-invalid-custom-config");
        }

        if (!sc.wCertVk.IsValid())
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], invalid wCert verification key\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-invalid-wcert-vk");
        }

        if (!Sidechain::IsValidProvingSystemType(sc.wCertVk.getProvingSystemType()))
        {
            return state.DoS(100,
                error("%s():%d - ERROR: Invalid tx[%s], invalid cert proving system\n",
                __func__, __LINE__, txHash.ToString()),
                CValidationState::Code::INVALID, "sidechain-sc-creation-invalid-wcert-provingsystype");
        }

        if(sc.constant.is_initialized() && !sc.constant->IsValid())
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], invalid constant\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-invalid-constant");
        }

        if (CSidechain::isNonCeasingSidechain(sc.version, sc.withdrawalEpochLength))
        {
            // Non ceasing sidechain
            if (sc.wCeasedVk.is_initialized())
            {
                return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], wCeasedVk should not be initialized on non-ceasing sidechains\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-wcvk-is-initialized");
            }
            if (sc.mainchainBackwardTransferRequestDataLength > 0)
            {
                return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], mainchainBackwardTransferRequestDataLength should be 0 for non-ceasing sidechains\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "bad-cert-mbtr-data-length-not-zero");
            }
        }
        else
        {
            // Ceasing sidechain
            if (sc.wCeasedVk.is_initialized())
            {
                if (!sc.wCeasedVk.get().IsValid())
                {
                    return state.DoS(100,
                        error("%s():%d - ERROR: Invalid tx[%s], invalid wCeasedVk verification key\n",
                        __func__, __LINE__, txHash.ToString()),
                        CValidationState::Code::INVALID, "sidechain-sc-creation-invalid-wcsw-vk");
                }
                if (!Sidechain::IsValidProvingSystemType(sc.wCeasedVk.get().getProvingSystemType()))
                {
                    return state.DoS(100,
                        error("%s():%d - ERROR: Invalid tx[%s], invalid csw proving system\n",
                        __func__, __LINE__, txHash.ToString()),
                        CValidationState::Code::INVALID, "sidechain-sc-creation-invalid-wcsw-provingsystype");
                }
            }
            if (sc.mainchainBackwardTransferRequestDataLength < 0 || sc.mainchainBackwardTransferRequestDataLength > MAX_SC_MBTR_DATA_LEN)
            {
                return state.DoS(100,
                        error("%s():%d - ERROR: Invalid tx[%s], mainchainBackwardTransferRequestDataLength out of range [%d, %d]\n",
                        __func__, __LINE__, txHash.ToString(), 0, MAX_SC_MBTR_DATA_LEN),
                        CValidationState::Code::INVALID, "bad-cert-mbtr-data-length-out-of-range");
            }
        }

        if (!MoneyRange(sc.forwardTransferScFee))
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], forwardTransferScFee out of range [%d, %d]\n",
                    __func__, __LINE__, txHash.ToString(), 0, MAX_MONEY),
                    CValidationState::Code::INVALID, "bad-cert-ft-fee-out-of-range");
        }

        if (!MoneyRange(sc.mainchainBackwardTransferRequestScFee))
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], mainchainBackwardTransferRequestScFee out of range [%d, %d]\n",
                    __func__, __LINE__, txHash.ToString(), 0, MAX_MONEY),
                    CValidationState::Code::INVALID, "bad-cert-mbtr-fee-out-of-range");
        }
    }

    // Note: no sence to check FT and ScCr amounts, because they were chacked before in `tx.CheckAmounts`
    for (const auto& ft : tx.GetVftCcOut())
    {
        if (!ft.CheckAmountRange(cumulatedAmount) )
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], sc fwd amount is non-positive or larger than %s\n",
                    __func__, __LINE__, txHash.ToString(), FormatMoney(MAX_MONEY)),
                    CValidationState::Code::INVALID, "sidechain-sc-fwd-amount-outside-range");
        }
    }

    for (const auto& bt : tx.GetVBwtRequestOut())
    {
        if (!bt.CheckAmountRange(cumulatedAmount) )
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], sc fee amount is non-positive or larger than %s\n",
                    __func__, __LINE__, txHash.ToString(), FormatMoney(MAX_MONEY)),
                    CValidationState::Code::INVALID, "sidechain-sc-fee-amount-outside-range");
        }

        if (bt.vScRequestData.size() == 0)
        {
            return state.DoS(100,
                error("%s():%d - ERROR: Invalid tx[%s], vScRequestData empty is not allowed\n",
                __func__, __LINE__, txHash.ToString()),
                CValidationState::Code::INVALID, "sidechain-sc-bwt-invalid-request-data");
        }

        if (bt.vScRequestData.size() > MAX_SC_MBTR_DATA_LEN)
        {
            return state.DoS(100,
                error("%s():%d - ERROR: Invalid tx[%s], vScRequestData size out of range [%d, %d]\n",
                __func__, __LINE__, txHash.ToString(), 0, MAX_SC_MBTR_DATA_LEN),
                CValidationState::Code::INVALID, "sidechain-sc-bwt-invalid-request-data");
        }

        for (const CFieldElement& fe : bt.vScRequestData)
        {
            if (!fe.IsValid())
            {
                return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], invalid bwt vScRequestData\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "sidechain-sc-bwt-invalid-request-data");
            }
        }
    }

    for(const CTxCeasedSidechainWithdrawalInput& csw : tx.GetVcswCcIn())
    {
        if (csw.nValue == 0 || !MoneyRange(csw.nValue))
        {
            return state.DoS(100,
                error("%s():%d - ERROR: Invalid tx[%s] : CSW value %d is non-positive or out of range\n",
                    __func__, __LINE__, txHash.ToString(), csw.nValue),
                CValidationState::Code::INVALID, "sidechain-cswinput-value-not-valid");
        }

        if(!csw.nullifier.IsValid())
        {
            return state.DoS(100,
                error("%s():%d - ERROR: Invalid tx[%s] : invalid CSW nullifier\n",
                    __func__, __LINE__, txHash.ToString()),
                CValidationState::Code::INVALID, "sidechain-cswinput-invalid-nullifier");
        }

        // this can be null in case a ceased sc does not have any valid cert
        if(!csw.actCertDataHash.IsValid() && !csw.actCertDataHash.IsNull())
        {
            return state.DoS(100,
                error("%s():%d - ERROR: Invalid tx[%s] : invalid CSW actCertDataHash\n",
                    __func__, __LINE__, txHash.ToString()),
                CValidationState::Code::INVALID, "sidechain-cswinput-invalid-actCertDataHash");
        }
        
        if(!csw.ceasingCumScTxCommTree.IsValid())
        {
            return state.DoS(100,
                error("%s():%d - ERROR: Invalid tx[%s] : invalid CSW ceasingCumScTxCommTree\n",
                    __func__, __LINE__, txHash.ToString()),
                CValidationState::Code::INVALID, "sidechain-cswinput-invalid-ceasingCumScTxCommTree");
        }

        if(!csw.scProof.IsValid())
        {
            return state.DoS(100,
                error("%s():%d - ERROR: Invalid tx[%s] : invalid CSW proof\n",
                    __func__, __LINE__, txHash.ToString()),
                CValidationState::Code::INVALID, "sidechain-cswinput-invalid-proof");
        }
    }

    return true;
}

bool Sidechain::checkCertSemanticValidity(const CScCertificate& cert, CValidationState& state)
{
    const uint256& certHash = cert.GetHash();

    if (cert.quality < 0)
    {
        return state.DoS(100,
                error("%s():%d - ERROR: Invalid cert[%s], negative quality\n",
                __func__, __LINE__, certHash.ToString()),
                CValidationState::Code::INVALID, "bad-cert-quality-negative");
    }

    if (cert.epochNumber < 0)
    {
        return state.DoS(100,
                error("%s():%d - ERROR: Invalid cert[%s], negative epoch number\n",
                __func__, __LINE__, certHash.ToString()),
                CValidationState::Code::INVALID, "bad-cert-invalid-epoch-data");;
    }
    
    if (!cert.endEpochCumScTxCommTreeRoot.IsValid() )
    {
        return state.DoS(100,
                error("%s():%d - ERROR: Invalid cert[%s], invalid endEpochCumScTxCommTreeRoot [%s]\n",
                __func__, __LINE__, certHash.ToString(), cert.endEpochCumScTxCommTreeRoot.GetHexRepr()),
                CValidationState::Code::INVALID, "bad-cert-invalid-cum-comm-tree");;
    }

    if (!MoneyRange(cert.forwardTransferScFee))
    {
        return state.DoS(100,
                error("%s():%d - ERROR: Invalid cert[%s], forwardTransferScFee out of range\n",
                __func__, __LINE__, certHash.ToString()),
                CValidationState::Code::INVALID, "bad-cert-ft-fee-out-of-range");;
    }

    if (!MoneyRange(cert.mainchainBackwardTransferRequestScFee))
    {
        return state.DoS(100,
                error("%s():%d - ERROR: Invalid cert[%s], mainchainBackwardTransferRequestScFee out of range\n",
                __func__, __LINE__, certHash.ToString()),
                CValidationState::Code::INVALID, "bad-cert-mbtr-fee-out-of-range");;
    }

    if(!cert.scProof.IsValid())
    {
        return state.DoS(100,
                error("%s():%d - ERROR: Invalid cert[%s], invalid scProof\n",
                __func__, __LINE__, certHash.ToString()),
                CValidationState::Code::INVALID, "bad-cert-invalid-sc-proof");
    }

    return true;
}

bool Sidechain::checkCertCustomFields(const CSidechain& sidechain, const CScCertificate& cert)
{
    const std::vector<FieldElementCertificateFieldConfig>& vCfeCfg = sidechain.fixedParams.vFieldElementCertificateFieldConfig;
    const std::vector<BitVectorCertificateFieldConfig>& vCmtCfg = sidechain.fixedParams.vBitVectorCertificateFieldConfig;

    const std::vector<FieldElementCertificateField>& vCfe = cert.vFieldElementCertificateField;
    const std::vector<BitVectorCertificateField>& vCmt = cert.vBitVectorCertificateField;

    if ( vCfeCfg.size() != vCfe.size() || vCmtCfg.size() != vCmt.size() )
    {
        LogPrint("sc", "%s():%d - invalid custom field cfg sz: %d/%d - %d/%d\n", __func__, __LINE__,
            vCfeCfg.size(), vCfe.size(), vCmtCfg.size(), vCmt.size() );
        return false;
    }

    for (int i = 0; i < vCfe.size(); i++)
    {
        const FieldElementCertificateField& fe = vCfe.at(i);
        if (!fe.IsValid(vCfeCfg.at(i), sidechain.fixedParams.version))
        {
            LogPrint("sc", "%s():%d - invalid custom field at pos %d\n", __func__, __LINE__, i);
            return false;
        }
    }

    for (int i = 0; i < vCmt.size(); i++)
    {
        const BitVectorCertificateField& cmt = vCmt.at(i);
        if (!cmt.IsValid(vCmtCfg.at(i), sidechain.fixedParams.version))
        {
            LogPrint("sc", "%s():%d - invalid compr mkl tree field at pos %d\n", __func__, __LINE__, i);
            return false;
        }
    }
    return true;
}

bool CSidechain::GetCeasingCumTreeHash(CFieldElement& ceasedBlockCum) const
{
    // block where the sc has ceased. In case the sidechain were not ceased the block index would be null
    int nCeasedHeight = GetScheduledCeasingHeight();
    CBlockIndex* ceasedBlockIndex = chainActive[nCeasedHeight];

    if (ceasedBlockIndex == nullptr)
    {
        LogPrint("sc", "%s():%d - invalid height %d for sc ceasing block: not in active chain\n",
            __func__, __LINE__, nCeasedHeight);
        return false;
    }

    ceasedBlockCum = ceasedBlockIndex->scCumTreeHash;
    return true;
}

int CSidechain::getNumBlocksForScFeeCheck()
{
    if ( (Params().NetworkIDString() == "regtest") )
    {
        int val = (int)(GetArg("-blocksforscfeecheck", Params().ScNumBlocksForScFeeCheck() ));
        if (val >= 0)
        {
            LogPrint("sc", "%s():%d - %s: using val %d \n", __func__, __LINE__, Params().NetworkIDString().c_str(), val);
            return val;
        }
        LogPrint("sc", "%s():%d - %s: val %d is negative, using default %d\n",
            __func__, __LINE__, Params().NetworkIDString().c_str(), val, Params().ScNumBlocksForScFeeCheck());
    }
    return Params().ScNumBlocksForScFeeCheck();
}

int CSidechain::getMaxSizeOfScFeesContainers()
{
    if (maxSizeOfScFeesContainers == -1) {
        const int numBlocks = getNumBlocksForScFeeCheck();
        if (!isNonCeasing()) {
            const int epochLength = fixedParams.withdrawalEpochLength;
            assert(epochLength > 0);
            maxSizeOfScFeesContainers = (numBlocks + epochLength - 1) / epochLength;
            // CSidechain::getNumBlocksForScFeeCheck() may return 0 for regtest...
            // This was managed in the same way in the old version
            maxSizeOfScFeesContainers = std::max(maxSizeOfScFeesContainers, 1);
        } else {
            // maxSizeOfScFeesContainers value is copied from the numBlocksForScFeeCheck variable defined in
            // chainparams.cpp. Currently is se at 200 for mainnet and testnet, and 10 for regtest. For regtest,
            // this value can be overridden with the command line option -blocksforscfeecheck=<n>. In this case,
            // we provide a safe guard of five slots for the scFees size.
            // This value has been chosen as it was the max possible size obtainable by the previous
            // implementation: by default, nScNumBlocksForScFeeCheck is set to 10, and withdrawal epoch
            // length ranges from 2 to 4032. This allows scFees size to range from 1 to 5, as evaluated by
            // numBlocks / epochLength.
            LogPrint("sc", "%s():%d - Warning: the value specified with -blocksforscfeecheck was too low; \
                maxSizeOfScFeesContainers has been set to 5\n", __func__, __LINE__);
            maxSizeOfScFeesContainers = std::max(numBlocks, 5);
        }
    }
    return maxSizeOfScFeesContainers;
}

void CSidechain::InitScFees()
{
    // only in the very first time, calculate the size of the buffer
    if (maxSizeOfScFeesContainers == -1)
    {
        maxSizeOfScFeesContainers = getMaxSizeOfScFeesContainers();
        LogPrint("sc", "%s():%d - maxSizeOfScFeesContainers set to %d\n", __func__, __LINE__, maxSizeOfScFeesContainers);

        assert(scFees.empty());

        if (!isNonCeasing()) {
            scFees.emplace_back(new Sidechain::ScFeeData(lastTopQualityCertView.forwardTransferScFee,
                                lastTopQualityCertView.mainchainBackwardTransferRequestScFee));
        } else {
            scFees.emplace_back(new Sidechain::ScFeeData_v2(lastTopQualityCertView.forwardTransferScFee,
                                lastTopQualityCertView.mainchainBackwardTransferRequestScFee, lastInclusionHeight));
        }
    }
}

void CSidechain::UpdateScFees(const CScCertificateView& certView, int blockHeight)
{
    // this is mostly for new UTs which need to call InitScFees() beforehand, should never happen otherwise
    assert(maxSizeOfScFeesContainers > 0);

    CAmount ftScFee   = certView.forwardTransferScFee;
    CAmount mbtrScFee = certView.mainchainBackwardTransferRequestScFee;

    LogPrint("sc", "%s():%d - pushing f=%d/m=%d into list with size %d\n",
        __func__, __LINE__, ftScFee, mbtrScFee, scFees.size());

    // Ceasable sidechain
    if (!isNonCeasing()) {
        scFees.emplace_back(new Sidechain::ScFeeData(ftScFee, mbtrScFee));

        // remove from the front as many elements are needed to be within the circular buffer size
        // --
        // usually this is just one element, but in regtest a node can set the max size via a startup option
        // therefore such size might be lesser than scFee size after a node restart
        const size_t max_size { static_cast<size_t>(maxSizeOfScFeesContainers) };
        while (scFees.size() > max_size)
        {
            const auto& entry = scFees.front();
            LogPrint("sc", "%s():%d - popping %s from list, as scFees maxsize has been reached\n",
                __func__, __LINE__, entry->ToString());
            scFees.pop_front();
        }
    }
    // Non-ceasable sidechain
    else {
        auto scFeeIt = std::find_if(scFees.begin(), scFees.end(),
            [&blockHeight](const std::shared_ptr<Sidechain::ScFeeData> & scFeeElem) {
                const std::shared_ptr<Sidechain::ScFeeData_v2> casted_entry = std::dynamic_pointer_cast<Sidechain::ScFeeData_v2>(scFeeElem);
                assert(casted_entry);
                return casted_entry->submissionHeight == blockHeight;
            });

        // We have not found a scFeeData for the current height, so we add a new one and perform all the checks
        // on the container size / element age
        if (scFeeIt == scFees.end()) {
            scFees.emplace_back(new Sidechain::ScFeeData_v2(ftScFee, mbtrScFee, blockHeight));

            // As for ceasable sidechains
            const size_t max_size { static_cast<size_t>(maxSizeOfScFeesContainers) };

            // Also purge all elements submitted within a block that now are too old to be considered
            const auto threshold{blockHeight - this->maxSizeOfScFeesContainers};
            scFees.remove_if([threshold](std::shared_ptr<Sidechain::ScFeeData> entry) {
                const std::shared_ptr<Sidechain::ScFeeData_v2> casted_entry = std::dynamic_pointer_cast<Sidechain::ScFeeData_v2>(entry);
                assert(casted_entry);
                const bool testCondition = (casted_entry->submissionHeight <= threshold);
                if (testCondition)
                    LogPrint("sc", "%s():%d - popping %s from list, as entry is too old (threshold height was %d)\n",
                        __func__, __LINE__, casted_entry->ToString(), threshold);
                return testCondition;
            });

        }
        // On the other hand, if we found a scFeeData element for the current height, we just update it with lower
        // ft and/or mbtr fee rates. All the checks on the container size and element age have been performed
        // at the moment of the element insertion, hence their are not required here
        else {
            auto scFeeEntry = scFeeIt->get();
            scFeeEntry->forwardTxScFee = std::min(scFeeEntry->forwardTxScFee, ftScFee);
            scFeeEntry->mbtrTxScFee    = std::min(scFeeEntry->mbtrTxScFee, mbtrScFee);
        }

    }
}

void CSidechain::DumpScFees() const
{
    for (const auto& entry : scFees) {
        std::cout << entry->ToString() << std::endl;
    }
}

CAmount CSidechain::GetMinFtScFee() const
{
    assert(!scFees.empty());

    CAmount minScFee = std::min_element(
        scFees.begin(), scFees.end(),
        [] (const std::shared_ptr<Sidechain::ScFeeData> &a, const std::shared_ptr<Sidechain::ScFeeData> &b) {
            return a->forwardTxScFee < b->forwardTxScFee;
        }
    )->get()->forwardTxScFee;

    LogPrint("sc", "%s():%d - returning min=%lld\n", __func__, __LINE__, minScFee);
    return minScFee;
}

CAmount CSidechain::GetMinMbtrScFee() const
{
    assert(!scFees.empty());

    CAmount minScFee = std::min_element(
        scFees.begin(), scFees.end(),
        [] (const std::shared_ptr<Sidechain::ScFeeData> &a, const std::shared_ptr<Sidechain::ScFeeData> &b) {
            return a->mbtrTxScFee < b->mbtrTxScFee;
        }
    )->get()->mbtrTxScFee;

    LogPrint("sc", "%s():%d - returning min=%lld\n", __func__, __LINE__, minScFee);
    return minScFee;
}

#endif
