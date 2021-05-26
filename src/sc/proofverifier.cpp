#include "sc/proofverifier.h"
#include <coins.h>
#include "primitives/certificate.h"

#include <main.h>

#ifdef BITCOIN_TX
void CScProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert) {return;}
void CScProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx) {return;}
#else
void CScProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert)
{
    if (verificationMode == Verification::Loose)
    {
        return;
    }

    LogPrint("cert", "%s():%d - called: cert[%s], scId[%s]\n",
        __func__, __LINE__, scCert.GetHash().ToString(), scCert.GetScId().ToString());

    CCertProofVerifierInput certData;

    certData.certificatePtr = std::make_shared<CScCertificate>(scCert);
    certData.certHash = scCert.GetHash();

    CSidechain sidechain;
    assert(view.GetSidechain(scCert.GetScId(), sidechain) && "Unknown sidechain at cert proof verification stage");

    if (sidechain.fixedParams.constant.is_initialized())
        certData.constant = sidechain.fixedParams.constant.get();
    else
        certData.constant = CFieldElement{};

    certData.epochNumber = scCert.epochNumber;
    certData.quality = scCert.quality;

    for(int pos = scCert.nFirstBwtPos; pos < scCert.GetVout().size(); ++pos)
    {
        CBackwardTransferOut btout(scCert.GetVout().at(pos));
        certData.bt_list.push_back(backward_transfer{});
        backward_transfer& bt = certData.bt_list.back();

        std::copy(btout.pubKeyHash.begin(), btout.pubKeyHash.end(), std::begin(bt.pk_dest));
        bt.amount = btout.nValue;
    }

    for (auto entry: scCert.vFieldElementCertificateField)
    {
        CFieldElement fe{entry.getVRawData()};
        certData.vCustomFields.push_back(fe);
    }
    for (auto entry: scCert.vBitVectorCertificateField)
    {
        CFieldElement fe{entry.getVRawData()};
        certData.vCustomFields.push_back(fe);
    }

    certData.endEpochCumScTxCommTreeRoot = scCert.endEpochCumScTxCommTreeRoot;
    certData.mainchainBackwardTransferRequestScFee = scCert.mainchainBackwardTransferRequestScFee;
    certData.forwardTransferScFee = scCert.forwardTransferScFee;

    certData.certProof = scCert.scProof;
    certData.CertVk = sidechain.fixedParams.wCertVk;

    certEnqueuedData.insert(std::make_pair(scCert.GetHash(), certData));

    return;
}

void CScProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx)
{
    if (verificationMode == Verification::Loose)
    {
        return;
    }

    std::map</*outputPos*/unsigned int, CCswProofVerifierInput> txMap;

    for(size_t idx = 0; idx < scTx.GetVcswCcIn().size(); ++idx)
    {
        CCswProofVerifierInput cswData;

        const CTxCeasedSidechainWithdrawalInput& csw = scTx.GetVcswCcIn().at(idx);

        cswData.nValue = csw.nValue;
        cswData.scId = csw.scId;
        cswData.pubKeyHash = csw.pubKeyHash;

        cswData.certDataHash = csw.actCertDataHash;
        cswData.ceasingCumScTxCommTree = csw.ceasingCumScTxCommTree;

        cswData.cswProof = csw.scProof;

        CSidechain sidechain;
        assert(view.GetSidechain(csw.scId, sidechain) && "Unknown sidechain at scTx proof verification stage");

        if (sidechain.fixedParams.wCeasedVk.is_initialized())
            cswData.ceasedVk = sidechain.fixedParams.wCeasedVk.get();
        else
            cswData.ceasedVk = CScVKey{};

        cswData.transactionPtr = std::make_shared<CTransaction>(scTx);

        txMap.insert(std::make_pair(idx, cswData));
    }

    if (!txMap.empty())
    {
        auto pair_ret = cswEnqueuedData.insert(std::make_pair(scTx.GetHash(), txMap));
        if (!pair_ret.second)
        {
            LogPrint("sc", "%s():%d - tx [%s] csw inputs already there\n",
                __func__, __LINE__, scTx.GetHash().ToString());
        }
        else
        {
            LogPrint("sc", "%s():%d - tx [%s] added to queue with %d inputs\n",
                __func__, __LINE__, scTx.GetHash().ToString(), txMap.size());
        }
    }
}
#endif

bool CScProofVerifier::BatchVerify() const
{
    if(verificationMode == Verification::Loose)
    {
        return true;
    }

    if (cswEnqueuedData.size() + certEnqueuedData.size() == 0)
    {
        return true;
    }

    CctpErrorCode code;
    ZendooBatchProofVerifier batchVerifier;
    uint32_t idx = 0;

    for (const auto& entry : cswEnqueuedData)
    {
        for (const auto& entry2 : entry.second)
        {
            const CCswProofVerifierInput& input = entry2.second;
 
            wrappedFieldPtr sptrScId = CFieldElement(input.scId).GetFieldElement();
            field_t* scid_fe = sptrScId.get();
 
            const uint160& csw_pk_hash = input.pubKeyHash;
            BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());
 
            wrappedFieldPtr   sptrCdh      = input.certDataHash.GetFieldElement();
            wrappedFieldPtr   sptrCum      = input.ceasingCumScTxCommTree.GetFieldElement();
            wrappedScProofPtr sptrProof    = input.cswProof.GetProofPtr();
            wrappedScVkeyPtr  sptrCeasedVk = input.ceasedVk.GetVKeyPtr();

            bool ret = batchVerifier.add_csw_proof(
                idx,
                input.nValue,
                scid_fe, 
                &bws_csw_pk_hash,
                sptrCdh.get(),
                sptrCum.get(),
                sptrProof.get(),
                sptrCeasedVk.get(),
                &code
            );
            idx++;

            if (!ret || code != CctpErrorCode::OK)
            {
                std::string txHash = "NULL";;
                if (input.transactionPtr.get())
                {
                    txHash = input.transactionPtr->GetHash().ToString();
                }
                LogPrintf("ERROR: %s():%d - tx [%s] has csw proof which does not verify: ret[%d], code [0x%x]\n",
                    __func__, __LINE__, txHash, (int)ret, code);
            }
        }
    }

    for (const auto& entry : certEnqueuedData)
    {
        const CCertProofVerifierInput& input = entry.second;

        int custom_fields_len = input.vCustomFields.size(); 
        std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
        int i = 0;
        std::vector<wrappedFieldPtr> vSptr;
        for (auto entry: input.vCustomFields)
        {
            wrappedFieldPtr sptrFe = entry.GetFieldElement();
            custom_fields[i] = sptrFe.get();
            vSptr.push_back(sptrFe);
            i++;
        }

        const backward_transfer_t* bt_list_ptr = input.bt_list.data();
        int bt_list_len = input.bt_list.size();

        // mc crypto lib wants a null ptr if we have no fields
        if (custom_fields_len == 0)
            custom_fields.reset();
        if (bt_list_len == 0)
            bt_list_ptr = nullptr;

        wrappedFieldPtr   sptrConst  = input.constant.GetFieldElement();
        wrappedFieldPtr   sptrCum    = input.endEpochCumScTxCommTreeRoot.GetFieldElement();
        wrappedScProofPtr sptrProof  = input.certProof.GetProofPtr();
        wrappedScVkeyPtr  sptrCertVk = input.CertVk.GetVKeyPtr();

        bool ret = batchVerifier.add_certificate_proof(
            idx,
            sptrConst.get(),
            input.epochNumber,
            input.quality,
            bt_list_ptr,
            bt_list_len,
            custom_fields.get(),
            custom_fields_len,
            sptrCum.get(),
            input.mainchainBackwardTransferRequestScFee,
            input.forwardTransferScFee,
            sptrProof.get(),
            sptrCertVk.get(),
            &code
        );
        idx++;

        if (!ret || code != CctpErrorCode::OK)
        {
            LogPrintf("ERROR: %s():%d - cert [%s] has proof which does not verify: ret[%d], code [0x%x]\n",
                __func__, __LINE__, input.certHash.ToString(), (int)ret, code);
        }
    }

    int64_t failingProof = -1;
    ZendooBatchProofVerifierResult verRes = batchVerifier.batch_verify_all(&code);
    if (!verRes.result)
    {
        failingProof = verRes.failing_proof;
 
        LogPrintf("ERROR: %s():%d - verify all failed: proofId[%lld], code [0x%x]\n",
            __func__, __LINE__, failingProof, code);
        return false; 
    }

    return true; 
}
