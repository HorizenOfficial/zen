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
    if (!isCreationConfirmed()) //default value
        return CScCertificate::EPOCH_NULL;

    return (targetHeight - creationBlockHeight) / creationData.withdrawalEpochLength;
}

int CSidechain::GetStartHeightForEpoch(int targetEpoch) const
{
    if (!isCreationConfirmed()) //default value
        return -1;

    return creationBlockHeight + targetEpoch * creationData.withdrawalEpochLength;
}

int CSidechain::GetEndHeightForEpoch(int targetEpoch) const
{
    if (!isCreationConfirmed()) //default value
        return -1;

    return GetStartHeightForEpoch(targetEpoch) + creationData.withdrawalEpochLength - 1;
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
    return std::max(2,creationData.withdrawalEpochLength/5);
}

int CSidechain::GetCertMaturityHeight(int certEpoch) const
{
    if (!isCreationConfirmed()) //default value
        return -1;

    return GetCertSubmissionWindowEnd(certEpoch+1);
}

int CSidechain::GetScheduledCeasingHeight() const
{
    return GetCertSubmissionWindowEnd(lastTopQualityCertReferencedEpoch+1);
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
                      " creationTxHash=%s\n pastEpochTopQualityCertDataHash=%s\n"
                      " lastTopQualityCertDataHash=%s\n"
                      " lastTopQualityCertHash=%s\n lastTopQualityCertReferencedEpoch=%d\n"
                      " lastTopQualityCertQuality=%d\n lastTopQualityCertBwtAmount=%s\n balance=%s\n"
                      " creationData=[NOT PRINTED CURRENTLY]\n mImmatureAmounts=[NOT PRINTED CURRENTLY])",
        sidechainVersion
        , creationBlockHeight
        , creationTxHash.ToString()
        , pastEpochTopQualityCertDataHash.GetHexRepr()
        , lastTopQualityCertDataHash.GetHexRepr()
        , lastTopQualityCertHash.ToString()
        , lastTopQualityCertReferencedEpoch
        , lastTopQualityCertQuality
        , FormatMoney(lastTopQualityCertBwtAmount)
        , FormatMoney(balance)
    );

    return str;
}

size_t CSidechain::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(mImmatureAmounts);
}

size_t CSidechainEvents::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(maturingScs) + memusage::DynamicUsage(ceasingScs);
}

#ifdef BITCOIN_TX
bool Sidechain::checkCertSemanticValidity(const CScCertificate& cert, CValidationState& state) { return true; }
bool Sidechain::checkTxSemanticValidity(const CTransaction& tx, CValidationState& state) { return true; }
bool CSidechain::GetCeasedCumTreeHashes(
    CFieldElement& lastEpochEndBlockCum, CFieldElement& ceasedBlockCum) const { return true; }
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

    for (const auto& sc : tx.GetVscCcOut())
    {
        if (sc.withdrawalEpochLength < SC_MIN_WITHDRAWAL_EPOCH_LENGTH)
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], sc creation withdrawalEpochLength %d is non-positive\n",
                    __func__, __LINE__, txHash.ToString(), sc.withdrawalEpochLength),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-epoch-not-valid");
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

        if (!libzendoomc::IsValidScVk(sc.wCertVk))
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], invalid wCert verification key\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-invalid-wcert-vk");
        }

        if(sc.constant.is_initialized() && !sc.constant->IsValid())
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], invalid constant\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-invalid-constant");
        }

        if (sc.wMbtrVk.is_initialized() && !libzendoomc::IsValidScVk(sc.wMbtrVk.get()))
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], invalid wMbtrVk verification key\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-invalid-w-mbtr-vk");
        }

        if (sc.wCeasedVk.is_initialized() && !libzendoomc::IsValidScVk(sc.wCeasedVk.get()))
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], invalid wCeasedVk verification key\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "sidechain-sc-creation-invalid-wceased-vk");
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

        if (!libzendoomc::IsValidScProof(bt.scProof))
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], invalid bwt scProof\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "sidechain-sc-bwt-invalid-sc-proof");
        }

        if (!bt.scRequestData.IsValid())
        {
            return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s], invalid bwt scRequestData\n",
                    __func__, __LINE__, txHash.ToString()),
                    CValidationState::Code::INVALID, "sidechain-sc-bwt-invalid-sc-utxo-id");
        }
    }

    if (!tx.GetVcswCcIn().empty() && tx.GetVActCertData().size() == 0)
    {
        return state.DoS(100,
            error("%s():%d - ERROR: Invalid tx[%s] : Missing active cert data for some csw inputs\n",
                __func__, __LINE__, txHash.ToString()),
            CValidationState::Code::INVALID, "sidechain-cswinput-empty-act-cert-data-vec");
    }

    if (tx.GetVActCertData().size() > tx.GetVcswCcIn().size())
    {
        return state.DoS(100,
            error("%s():%d - ERROR: Invalid tx[%s] : vector of active cert data hash has to many entries\n",
                __func__, __LINE__, txHash.ToString()),
            CValidationState::Code::INVALID, "sidechain-cswinput-too-big-act-cert-data-vec");
    }

    std::set<int32_t> sActCertDataIdxs;

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
        
        int32_t idx = csw.actCertDataIdx;
        if(idx < 0 || idx > (tx.GetVActCertData().size()-1))
        {
            return state.DoS(100,
                error("%s():%d - ERROR: Invalid tx[%s] : invalid CSW actCertDataIdx[%d] (vActCertDataSize=%d)\n",
                    __func__, __LINE__, txHash.ToString(), idx, tx.GetVActCertData().size()),
                CValidationState::Code::INVALID, "sidechain-cswinput-invalid-act-cert-data-idx");
        }

        if (!sActCertDataIdxs.count(idx))
        {
            sActCertDataIdxs.insert(idx);

            if(!tx.GetVActCertData()[idx].IsValid())
            {
                return state.DoS(100,
                    error("%s():%d - ERROR: Invalid tx[%s] : invalid CSW active cert data hash at idx[%d]\n",
                        __func__, __LINE__, txHash.ToString(), idx),
                    CValidationState::Code::INVALID, "sidechain-cswinput-invalid-act-cert-data");
            }
        }

        if(!libzendoomc::IsValidScProof(csw.scProof))
        {
            return state.DoS(100,
                error("%s():%d - ERROR: Invalid tx[%s] : invalid CSW proof\n",
                    __func__, __LINE__, txHash.ToString()),
                CValidationState::Code::INVALID, "sidechain-cswinput-invalid-proof");
        }
    }

    // check we processed all and only relevant cert data hashes
    if (sActCertDataIdxs.size() != tx.GetVActCertData().size())
    {
        return state.DoS(100,
            error("%s():%d - ERROR: Invalid tx[%s] : invalid vector for active cert data (size=%d, processed=%d)\n",
                __func__, __LINE__, txHash.ToString(), tx.GetVActCertData().size(), sActCertDataIdxs.size()),
            CValidationState::Code::INVALID, "sidechain-cswinput-invalid-act-cert-data-vec");
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

    if (cert.epochNumber < 0 || cert.endEpochBlockHash.IsNull())
    {
        return state.DoS(100,
                error("%s():%d - ERROR: Invalid cert[%s], negative epoch number or null endEpochBlockHash\n",
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

    if(!libzendoomc::IsValidScProof(cert.scProof))
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
    const std::vector<FieldElementCertificateFieldConfig>& vCfeCfg = sidechain.creationData.vFieldElementCertificateFieldConfig;
    const std::vector<BitVectorCertificateFieldConfig>& vCmtCfg = sidechain.creationData.vBitVectorCertificateFieldConfig;

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
        if (!fe.IsValid(vCfeCfg.at(i)))
        {
            LogPrint("sc", "%s():%d - invalid custom field at pos %d\n", __func__, __LINE__, i);
            return false;
        }
    }

    for (int i = 0; i < vCmt.size(); i++)
    {
        const BitVectorCertificateField& cmt = vCmt.at(i);
        if (!cmt.IsValid(vCmtCfg.at(i)))
        {
            LogPrint("sc", "%s():%d - invalid compr mkl tree field at pos %d\n", __func__, __LINE__, i);
            return false;
        }
    }
    return true;
}

bool CSidechain::GetCeasedCumTreeHashes(CFieldElement& lastEpochEndBlockCum, CFieldElement& ceasedBlockCum) const
{
    // last block of the last valid epoch
    int lastEpoch          = lastTopQualityCertReferencedEpoch;
    int lastEpochEndHeight = GetEndHeightForEpoch(lastEpoch);
    CBlockIndex* lastEpochEndBlockIndex = chainActive[lastEpochEndHeight];

    if (lastEpochEndBlockIndex == nullptr)
    {
        LogPrint("sc", "%s():%d - invalid height %d for last end epoch: not in active chain\n",
            __func__, __LINE__, lastEpochEndHeight);
        return false;
    }

    lastEpochEndBlockCum = lastEpochEndBlockIndex->scCumTreeHash;

    // this checks the extreme case where the block is the first one of the SC fork
    if (BOOST_UNLIKELY(lastEpochEndBlockCum.IsNull()))
        lastEpochEndBlockCum = CFieldElement::GetPhantomHash();

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

#endif
