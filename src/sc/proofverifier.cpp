#include "sc/proofverifier.h"
#include <coins.h>
#include "primitives/certificate.h"

#include <main.h>

std::atomic<uint32_t> CScProofVerifier::proofIdCounter(0);

/**
 * @brief Creates the proof verifier input of a certificate for the proof verifier.
 * 
 * @param certificate The certificate to send to the proof verifier
 * @param scFixedParams The fixed parameters of the sidechain referred by the certificate
 * @param node The node that sent the certificate
 * @return CCertProofVerifierInput The structure containing all the data needed for the proof verification of the certificate.
 */
CCertProofVerifierItem CScProofVerifier::CertificateToVerifierItem(const CScCertificate& certificate, const Sidechain::ScFixedParameters& scFixedParams, CNode* pfrom)
{
    CCertProofVerifierItem certData;

    certData.proofId = proofIdCounter++;
    certData.parentPtr = std::make_shared<CScCertificate>(certificate);
    certData.certHash = certificate.GetHash();

    if (scFixedParams.constant.is_initialized())
        certData.constant = scFixedParams.constant.get();
    else
        certData.constant = CFieldElement{};

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

    certData.proof = certificate.scProof;
    certData.verificationKey = scFixedParams.wCertVk;

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
 * @return CCswProofVerifierItem The The structure containing all the data needed for the proof verification of the CSW input.
 */
CCswProofVerifierItem CScProofVerifier::CswInputToVerifierItem(const CTxCeasedSidechainWithdrawalInput& cswInput, const CTransaction* cswTransaction, const Sidechain::ScFixedParameters& scFixedParams, CNode* pfrom)
{
    CCswProofVerifierItem cswData;

    cswData.proofId = proofIdCounter++;
    cswData.nValue = cswInput.nValue;
    cswData.scId = cswInput.scId;
    cswData.pubKeyHash = cswInput.pubKeyHash;
    cswData.certDataHash = cswInput.actCertDataHash;
    cswData.ceasingCumScTxCommTree = cswInput.ceasingCumScTxCommTree;
    cswData.nullifier = cswInput.nullifier;
    cswData.proof = cswInput.scProof;

    // The ceased verification key must be initialized to allow CSW. This check is already performed inside IsScTxApplicableToState().
    assert(scFixedParams.wCeasedVk.is_initialized());
    
    cswData.verificationKey = scFixedParams.wCeasedVk.get();

    //if (cswTransaction != nullptr)
    //{
    assert(cswTransaction != nullptr);
    cswData.parentPtr = std::make_shared<CTransaction>(*cswTransaction);
    //}

    cswData.node = pfrom;
    
    return cswData;
}

#ifdef BITCOIN_TX
void CScProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom) {return;}
void CScProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom) {return;}
#else
void CScProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom)
{
    if (verificationMode == Verification::Loose)
    {
        return;
    }

    LogPrint("cert", "%s():%d - called: cert[%s], scId[%s]\n",
        __func__, __LINE__, scCert.GetHash().ToString(), scCert.GetScId().ToString());

    CSidechain sidechain;
    assert(view.GetSidechain(scCert.GetScId(), sidechain) && "Unknown sidechain at scTx proof verification stage");

    CCertProofVerifierItem certData = CertificateToVerifierItem(scCert, sidechain.fixedParams, pfrom);

    certEnqueuedData.insert(std::make_pair(scCert.GetHash(), std::vector<CCertProofVerifierItem>{certData}));
}

void CScProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom)
{
    if (verificationMode == Verification::Loose)
    {
        return;
    }

    std::vector<CCswProofVerifierItem> cswInputProofs;

    for(CTxCeasedSidechainWithdrawalInput cswInput : scTx.GetVcswCcIn())
    {
        CSidechain sidechain;
        assert(view.GetSidechain(cswInput.scId, sidechain) && "Unknown sidechain at scTx proof verification stage");
        
        CCswProofVerifierItem verifierInput = CswInputToVerifierItem(cswInput, &scTx, sidechain.fixedParams, pfrom);
        cswInputProofs.push_back(verifierInput);
    }

    if (!cswInputProofs.empty())
    {
        auto pair_ret = cswEnqueuedData.insert(std::make_pair(scTx.GetHash(), cswInputProofs));
        if (!pair_ret.second)
        {
            LogPrint("sc", "%s():%d - tx [%s] csw inputs already there\n",
                __func__, __LINE__, scTx.GetHash().ToString());
        }
        else
        {
            LogPrint("sc", "%s():%d - tx [%s] added to queue with %d inputs\n",
                __func__, __LINE__, scTx.GetHash().ToString(), cswInputProofs.size());
        }
    }
}
#endif

bool CScProofVerifier::BatchVerify() const
{
    return BatchVerifyInternal(cswEnqueuedData, certEnqueuedData).first;
}

/**
 * @brief Run the batch verification for all the queued CSW outputs and certificates.
 * 
 * @param cswInputs The map of CSW inputs data to be verified.
 * @param certInputs The map of certificates data to be verified.
 * @return std::pair<bool, std::vector<uint32_t>> A pair containing the total result of the whole batch verification (bool) and the list of failed proofs.
 */
std::pair<bool, std::map<uint256, ProofVerifierOutput>> CScProofVerifier::BatchVerifyInternal(
                const std::map</* Tx hash */ uint256, std::vector<CCswProofVerifierItem>>& cswProofs,
                const std::map</* Cert hash */ uint256, std::vector<CCertProofVerifierItem>>& certProofs) const
{
    if(verificationMode == Verification::Loose)
    {
        return std::make_pair(true, GenerateVerifierResults(cswProofs, certProofs, ProofVerificationResult::Passed));
    }

    if (cswEnqueuedData.size() + certEnqueuedData.size() == 0)
    {
        return std::make_pair(true, GenerateVerifierResults(cswProofs, certProofs, ProofVerificationResult::Passed));
    }

    CctpErrorCode code;
    ZendooBatchProofVerifier batchVerifier;

    std::map<uint256, ProofVerifierOutput> addFailures;   /**< The list of Tx/Cert that failed during the load operation. */
    std::map<uint32_t /* Proof ID */, uint256 /* Tx or Cert hash */> proofIdMap;

    int64_t nTime1 = GetTimeMicros();
    LogPrint("bench", "%s():%d - starting verification\n", __func__, __LINE__);
    for (const auto& cswEntry : cswEnqueuedData)
    {
        for (const auto& proof : cswEntry.second)
        {
            proofIdMap.insert(std::make_pair(proof.proofId, cswEntry.first));

            wrappedFieldPtr sptrScId = CFieldElement(proof.scId).GetFieldElement();
            field_t* scid_fe = sptrScId.get();
 
            const uint160& csw_pk_hash = proof.pubKeyHash;
            BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());
 
            wrappedFieldPtr   sptrCdh       = proof.certDataHash.GetFieldElement();
            wrappedFieldPtr   sptrCum       = proof.ceasingCumScTxCommTree.GetFieldElement();
            wrappedFieldPtr   sptrNullifier = proof.nullifier.GetFieldElement();
            wrappedScProofPtr sptrProof     = proof.proof.GetProofPtr();
            wrappedScVkeyPtr  sptrCeasedVk  = proof.verificationKey.GetVKeyPtr();

            bool ret = batchVerifier.add_csw_proof(
                proof.proofId,
                proof.nValue,
                scid_fe, 
                sptrNullifier.get(),
                &bws_csw_pk_hash,
                sptrCdh.get(),
                sptrCum.get(),
                sptrProof.get(),
                sptrCeasedVk.get(),
                &code
            );

            if (!ret || code != CctpErrorCode::OK)
            {
                LogPrintf("ERROR: %s():%d - tx [%s] has csw proof which does not verify: ret[%d], code [0x%x]\n",
                    __func__, __LINE__, cswEntry.first.ToString(), (int)ret, code);

                addFailures.insert(std::make_pair(cswEntry.first, ProofVerifierOutput {.tx = proof.parentPtr,
                                                                                       .node = proof.node,
                                                                                       .proofResult = ProofVerificationResult::Failed}));

                // If one CSW input fails, it is possible to skip the whole transaction.
                break;
            }
        }
    }

    for (const auto& certEntry : certEnqueuedData)
    {
        for (const auto& proof : certEntry.second)
        {
            proofIdMap.insert(std::make_pair(proof.proofId, certEntry.first));

            int custom_fields_len = proof.vCustomFields.size(); 
            std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
            int i = 0;
            std::vector<wrappedFieldPtr> vSptr;
            for (auto entry: proof.vCustomFields)
            {
                wrappedFieldPtr sptrFe = entry.GetFieldElement();
                custom_fields[i] = sptrFe.get();
                vSptr.push_back(sptrFe);
                i++;
            }

            const backward_transfer_t* bt_list_ptr = proof.bt_list.data();
            int bt_list_len = proof.bt_list.size();

            // mc crypto lib wants a null ptr if we have no fields
            if (custom_fields_len == 0)
                custom_fields.reset();
            if (bt_list_len == 0)
                bt_list_ptr = nullptr;

            wrappedFieldPtr   sptrConst  = proof.constant.GetFieldElement();
            wrappedFieldPtr   sptrCum    = proof.endEpochCumScTxCommTreeRoot.GetFieldElement();
            wrappedScProofPtr sptrProof  = proof.proof.GetProofPtr();
            wrappedScVkeyPtr  sptrCertVk = proof.verificationKey.GetVKeyPtr();

            bool ret = batchVerifier.add_certificate_proof(
                proof.proofId,
                sptrConst.get(),
                proof.epochNumber,
                proof.quality,
                bt_list_ptr,
                bt_list_len,
                custom_fields.get(),
                custom_fields_len,
                sptrCum.get(),
                proof.mainchainBackwardTransferRequestScFee,
                proof.forwardTransferScFee,
                sptrProof.get(),
                sptrCertVk.get(),
                &code
            );

            //dumpFeArr((field_t**)custom_fields.get(), custom_fields_len, "custom fields");
            //dumpFe(sptrCum.get(), "cumTree");

            if (!ret || code != CctpErrorCode::OK)
            {
                LogPrintf("ERROR: %s():%d - cert [%s] has proof which does not verify: ret[%d], code [0x%x]\n",
                    __func__, __LINE__, proof.certHash.ToString(), (int)ret, code);

                addFailures.insert(std::make_pair(certEntry.first, ProofVerifierOutput {.tx = proof.parentPtr,
                                                                                        .node = proof.node,
                                                                                        .proofResult = ProofVerificationResult::Failed}));
            }
        }
    }

    std::map<uint256, ProofVerifierOutput> results;
    CZendooBatchProofVerifierResult verRes(batchVerifier.batch_verify_all(&code));

    if (verRes.Result())
    {
        assert(verRes.FailedProofs().size() == 0);
        results = GenerateVerifierResults(cswProofs, certProofs, ProofVerificationResult::Passed);
    }
    else
    { 
        results = GenerateVerifierResults(cswProofs, certProofs, ProofVerificationResult::Unknown);

        if (verRes.FailedProofs().size() > 0)
        {
            // We know for sure that some proofs failed, the other ones may be valid or not.
            LogPrintf("ERROR: %s():%d - verify failed for %d proof(s), code [0x%x]\n",
            __func__, __LINE__, verRes.FailedProofs().size(), code);

            for (uint32_t proofId : verRes.FailedProofs())
            {
                uint256 txHash = proofIdMap.at(proofId);
                results.at(txHash).proofResult = ProofVerificationResult::Failed;
            }
        }
        else
        {
            // We don't know which proofs made the batch process fail.
            LogPrintf("ERROR: %s():%d - verify failed without detailed information\n", __func__, __LINE__);
        }
    }

    // Add the failures that happened during the load process.
    results.insert(addFailures.begin(), addFailures.end());

    int64_t nTime2 = GetTimeMicros();
    LogPrint("bench", "%s():%d - verification succesful: %.2fms\n", __func__, __LINE__, (nTime2-nTime1) * 0.001);
    return std::make_pair(verRes.Result(), results);
}

std::map<uint256, ProofVerifierOutput> CScProofVerifier::GenerateVerifierResults(const std::map</* Tx hash */ uint256, std::vector<CCswProofVerifierItem>>& cswProofs,
                                                                                     const std::map</* Cert hash */ uint256, std::vector<CCertProofVerifierItem>>& certProofs,
                                                                                     ProofVerificationResult defaultResult) const
{
    std::map<uint256, ProofVerifierOutput> results;

    for (auto proofEntry : cswProofs)
    {
        assert(proofEntry.second.size() > 0);
        results.insert(std::make_pair(proofEntry.first, ProofVerifierOutput {.tx = proofEntry.second.at(0).parentPtr,
                                                                                .node = proofEntry.second.at(0).node,
                                                                                .proofResult = defaultResult}));
    }

    return results;
}
