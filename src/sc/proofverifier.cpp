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

    CSidechain sidechain;
    assert(view.GetSidechain(scCert.GetScId(), sidechain) && "Unknown sidechain at scTx proof verification stage");

    CCertProofVerifierInput certData = SidechainProofVerifier::CertificateToVerifierInput(scCert, sidechain.fixedParams, nullptr);

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
        const CTxCeasedSidechainWithdrawalInput& cswInput = scTx.GetVcswCcIn().at(idx);

        CSidechain sidechain;
        assert(view.GetSidechain(cswInput.scId, sidechain) && "Unknown sidechain at scTx proof verification stage");

        txMap.insert(std::make_pair(idx, SidechainProofVerifier::CswInputToVerifierInput(scTx.GetVcswCcIn().at(idx), &scTx, sidechain.fixedParams, nullptr)));
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

    int64_t nTime1 = GetTimeMicros();
    LogPrint("bench", "%s():%d - starting verification\n", __func__, __LINE__);
    for (const auto& entry : cswEnqueuedData)
    {
        for (const auto& entry2 : entry.second)
        {
            const CCswProofVerifierInput& input = entry2.second;
 
            wrappedFieldPtr sptrScId = CFieldElement(input.scId).GetFieldElement();
            field_t* scid_fe = sptrScId.get();
 
            const uint160& csw_pk_hash = input.pubKeyHash;
            BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());
 
            wrappedFieldPtr   sptrCdh       = input.certDataHash.GetFieldElement();
            wrappedFieldPtr   sptrCum       = input.ceasingCumScTxCommTree.GetFieldElement();
            wrappedFieldPtr   sptrNullifier = input.nullifier.GetFieldElement();
            wrappedScProofPtr sptrProof     = input.cswProof.GetProofPtr();
            wrappedScVkeyPtr  sptrCeasedVk  = input.ceasedVk.GetVKeyPtr();

            bool ret = batchVerifier.add_csw_proof(
                idx,
                input.nValue,
                scid_fe, 
                sptrNullifier.get(),
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

        wrappedFieldPtr sptrScId = CFieldElement(input.scId).GetFieldElement();
        field_t* scidFe = sptrScId.get();

        wrappedFieldPtr   sptrConst  = input.constant.GetFieldElement();
        wrappedFieldPtr   sptrCum    = input.endEpochCumScTxCommTreeRoot.GetFieldElement();
        wrappedScProofPtr sptrProof  = input.certProof.GetProofPtr();
        wrappedScVkeyPtr  sptrCertVk = input.CertVk.GetVKeyPtr();

        bool ret = batchVerifier.add_certificate_proof(
            idx,
            sptrConst.get(),
            scidFe,
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

        //dumpFeArr((field_t**)custom_fields.get(), custom_fields_len, "custom fields");
        //dumpFe(sptrCum.get(), "cumTree");

        if (!ret || code != CctpErrorCode::OK)
        {
            LogPrintf("ERROR: %s():%d - cert [%s] has proof which does not verify: ret[%d], code [0x%x]\n",
                __func__, __LINE__, input.certHash.ToString(), (int)ret, code);
        }
    }

    CZendooBatchProofVerifierResult verRes(batchVerifier.batch_verify_all(&code));

    if (!verRes.Result())
    { 
        LogPrintf("ERROR: %s():%d - verify failed for %d proof(s), code [0x%x]\n",
            __func__, __LINE__, verRes.FailedProofs().size(), code);
        return false; 
    }

    int64_t nTime2 = GetTimeMicros();
    LogPrint("bench", "%s():%d - verification succesful: %.2fms\n", __func__, __LINE__, (nTime2-nTime1) * 0.001);
    return true; 
}


/**
 * @brief Creates the proof verifier input of a certificate for the proof verifier.
 * 
 * @param certificate The certificate to send to the proof verifier
 * @param scFixedParams The fixed parameters of the sidechain referred by the certificate
 * @param node The node that sent the certificate
 * @return CCertProofVerifierInput The structure containing all the data needed for the proof verification of the certificate.
 */
CCertProofVerifierInput SidechainProofVerifier::CertificateToVerifierInput(const CScCertificate& certificate, const Sidechain::ScFixedParameters& scFixedParams, CNode* pfrom)
{
    // TODO this is code duplicated from CScProofVerifier::LoadDataForCertVerification
    CCertProofVerifierInput certData;

    certData.certificatePtr = std::make_shared<CScCertificate>(certificate);
    certData.certHash = certificate.GetHash();

    if (scFixedParams.constant.is_initialized())
        certData.constant = scFixedParams.constant.get();
    else
        certData.constant = CFieldElement{};

    certData.scId = certificate.GetScId();
    certData.epochNumber = certificate.epochNumber;
    certData.quality = certificate.quality;

    for(int pos = certificate.nFirstBwtPos; pos < certificate.GetVout().size(); ++pos)
    {
        CBackwardTransferOut btout(certificate.GetVout().at(pos));
        certData.bt_list.push_back(backward_transfer{});
        backward_transfer& bt = certData.bt_list.back();

        std::copy(btout.pubKeyHash.begin(), btout.pubKeyHash.end(), std::begin(bt.pk_dest));
        bt.amount = btout.nValue;
    }

    for (int i = 0; i < certificate.vFieldElementCertificateField.size(); i++)
    {
        FieldElementCertificateField entry = certificate.vFieldElementCertificateField.at(i);
        CFieldElement fe{entry.GetFieldElement(scFixedParams.vFieldElementCertificateFieldConfig.at(i))};
        assert(fe.IsValid());
        certData.vCustomFields.push_back(fe);
    }
    for (int i = 0; i < certificate.vBitVectorCertificateField.size(); i++)
    {
        BitVectorCertificateField entry = certificate.vBitVectorCertificateField.at(i);
        CFieldElement fe{entry.GetFieldElement(scFixedParams.vBitVectorCertificateFieldConfig.at(i))};
        assert(fe.IsValid());
        certData.vCustomFields.push_back(fe);
    }

    certData.endEpochCumScTxCommTreeRoot = certificate.endEpochCumScTxCommTreeRoot;
    certData.mainchainBackwardTransferRequestScFee = certificate.mainchainBackwardTransferRequestScFee;
    certData.forwardTransferScFee = certificate.forwardTransferScFee;

    certData.certProof = certificate.scProof;
    certData.CertVk = scFixedParams.wCertVk;

    certData.node = pfrom;

    return certData;
}

/**
 * @brief Creates the proof verifier input of a CSW transaction for the proof verifier.
 * 
 * @param cswInput The CSW input to be verified
 * @param cswTransaction The pointer to the transaction containing the CSW input
 * @param scFixedParams The fixed parameters of the sidechain referred by the certificate
 * @param node The node that sent the certificate
 * @return CCswProofVerifierInput The The structure containing all the data needed for the proof verification of the CSW input.
 */
CCswProofVerifierInput SidechainProofVerifier::CswInputToVerifierInput(const CTxCeasedSidechainWithdrawalInput& cswInput, const CTransaction* cswTransaction, const Sidechain::ScFixedParameters& scFixedParams, CNode* pfrom)
{
    CCswProofVerifierInput cswData;

    cswData.nValue = cswInput.nValue;
    cswData.scId = cswInput.scId;
    cswData.pubKeyHash = cswInput.pubKeyHash;
    cswData.certDataHash = cswInput.actCertDataHash;
    cswData.ceasingCumScTxCommTree = cswInput.ceasingCumScTxCommTree;
    cswData.nullifier = cswInput.nullifier;
    cswData.cswProof = cswInput.scProof;

    // The ceased verification key must be initialized to allow CSW. This check is already performed inside IsScTxApplicableToState().
    assert(scFixedParams.wCeasedVk.is_initialized());
    
    cswData.ceasedVk = scFixedParams.wCeasedVk.get();

    if (cswTransaction != nullptr)
    {
        cswData.transactionPtr = std::make_shared<CTransaction>(*cswTransaction);
    }

    cswData.node = pfrom;
    
    return cswData;
}
