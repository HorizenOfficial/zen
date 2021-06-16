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
CCertProofVerifierInput CScProofVerifier::CertificateToVerifierItem(const CScCertificate& certificate, const Sidechain::ScFixedParameters& scFixedParams, CNode* pfrom)
{
    CCertProofVerifierInput certData;

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
 * @return CCswProofVerifierInput The The structure containing all the data needed for the proof verification of the CSW input.
 */
CCswProofVerifierInput CScProofVerifier::CswInputToVerifierItem(const CTxCeasedSidechainWithdrawalInput& cswInput, const CTransaction* cswTransaction, const Sidechain::ScFixedParameters& scFixedParams, CNode* pfrom)
{
    CCswProofVerifierInput cswData;

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

    if (cswTransaction != nullptr)
    {
        cswData.parentPtr = std::make_shared<CTransaction>(*cswTransaction);
    }

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

    CProofVerifierItem item;
    item.txHash = scCert.GetHash();
    item.parentPtr = std::make_shared<CScCertificate>(scCert);
    item.node = pfrom;
    item.result = ProofVerificationResult::Unknown;
    item.certInput = CertificateToVerifierItem(scCert, sidechain.fixedParams, pfrom);
    //item.cswInputs = boost::none;
    assert(!item.cswInputs.is_initialized());
    assert(item.cswInputs == boost::none);
    assert(!item.cswInputs);
    proofQueue.insert(std::make_pair(scCert.GetHash(), item));
}

void CScProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom)
{
    if (verificationMode == Verification::Loose)
    {
        return;
    }

    std::vector<CCswProofVerifierInput> cswInputProofs;

    for(CTxCeasedSidechainWithdrawalInput cswInput : scTx.GetVcswCcIn())
    {
        CSidechain sidechain;
        assert(view.GetSidechain(cswInput.scId, sidechain) && "Unknown sidechain at scTx proof verification stage");
        
        cswInputProofs.push_back(CswInputToVerifierItem(cswInput, &scTx, sidechain.fixedParams, pfrom));
    }

    if (!cswInputProofs.empty())
    {
        CProofVerifierItem item;
        item.txHash = scTx.GetHash();
        item.parentPtr = cswInputProofs.begin()->parentPtr;
        item.result = ProofVerificationResult::Unknown;
        item.node = pfrom;
        item.cswInputs = cswInputProofs;
        assert(!item.certInput.is_initialized());
        assert(item.certInput == boost::none);
        assert(!item.certInput);
        auto pair_ret = proofQueue.insert(std::make_pair(scTx.GetHash(), item));

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
    return BatchVerifyInternal(proofQueue).first;
}

/**
 * @brief Run the batch verification for all the queued CSW outputs and certificates.
 * 
 * @param cswInputs The map of CSW inputs data to be verified.
 * @param certInputs The map of certificates data to be verified.
 * @return std::pair<bool, std::vector<uint32_t>> A pair containing the total result of the whole batch verification (bool) and the list of failed proofs.
 */
std::pair<bool, std::map<uint256, ProofVerifierOutput>> CScProofVerifier::BatchVerifyInternal(const std::map</* Cert or Tx hash */ uint256, CProofVerifierItem>& proofs) const
{
    if (verificationMode == Verification::Loose)
    {
        return std::make_pair(true, GenerateVerifierResults(proofs, ProofVerificationResult::Passed));
    }

    if (proofs.size() == 0)
    {
        return std::make_pair(true, GenerateVerifierResults(proofs, ProofVerificationResult::Passed));
    }

    CctpErrorCode code;
    ZendooBatchProofVerifier batchVerifier;

    std::map<uint256, ProofVerifierOutput> addFailures;   /**< The list of Tx/Cert that failed during the load operation. */
    std::map<uint32_t /* Proof ID */, uint256 /* Tx or Cert hash */> proofIdMap;

    int64_t nTime1 = GetTimeMicros();
    LogPrint("bench", "%s():%d - starting verification\n", __func__, __LINE__);

    for (auto& proofEntry : proofs)
    {
        const CProofVerifierItem& item = proofEntry.second;

        if (item.cswInputs)
        {
            for (auto& item : *item.cswInputs)
            {
                proofIdMap.insert(std::make_pair(item.proofId, proofEntry.first));

                wrappedFieldPtr sptrScId = CFieldElement(item.scId).GetFieldElement();
                field_t* scid_fe = sptrScId.get();
    
                const uint160& csw_pk_hash = item.pubKeyHash;
                BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());
    
                wrappedFieldPtr   sptrCdh       = item.certDataHash.GetFieldElement();
                wrappedFieldPtr   sptrCum       = item.ceasingCumScTxCommTree.GetFieldElement();
                wrappedFieldPtr   sptrNullifier = item.nullifier.GetFieldElement();
                wrappedScProofPtr sptrProof     = item.proof.GetProofPtr();
                wrappedScVkeyPtr  sptrCeasedVk  = item.verificationKey.GetVKeyPtr();

                bool ret = batchVerifier.add_csw_proof(
                    item.proofId,
                    item.nValue,
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
                        __func__, __LINE__, proofEntry.first.ToString(), (int)ret, code);

                    addFailures.insert(std::make_pair(proofEntry.first, ProofVerifierOutput {.tx = item.parentPtr,
                                                                                        .node = item.node,
                                                                                        .proofResult = ProofVerificationResult::Failed}));

                    // If one CSW input fails, it is possible to skip the whole transaction.
                    break;
                }
            }
        }
        else if (item.certInput)
        {
            proofIdMap.insert(std::make_pair(item.certInput->proofId, proofEntry.first));

            int custom_fields_len = item.certInput->vCustomFields.size(); 
            std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
            int i = 0;
            std::vector<wrappedFieldPtr> vSptr;
            for (auto entry: item.certInput->vCustomFields)
            {
                wrappedFieldPtr sptrFe = entry.GetFieldElement();
                custom_fields[i] = sptrFe.get();
                vSptr.push_back(sptrFe);
                i++;
            }

            const backward_transfer_t* bt_list_ptr = item.certInput->bt_list.data();
            int bt_list_len = item.certInput->bt_list.size();

            // mc crypto lib wants a null ptr if we have no fields
            if (custom_fields_len == 0)
                custom_fields.reset();
            if (bt_list_len == 0)
                bt_list_ptr = nullptr;

            wrappedFieldPtr   sptrConst  = item.certInput->constant.GetFieldElement();
            wrappedFieldPtr   sptrCum    = item.certInput->endEpochCumScTxCommTreeRoot.GetFieldElement();
            wrappedScProofPtr sptrProof  = item.certInput->proof.GetProofPtr();
            wrappedScVkeyPtr  sptrCertVk = item.certInput->verificationKey.GetVKeyPtr();

            bool ret = batchVerifier.add_certificate_proof(
                item.certInput->proofId,
                sptrConst.get(),
                item.certInput->epochNumber,
                item.certInput->quality,
                bt_list_ptr,
                bt_list_len,
                custom_fields.get(),
                custom_fields_len,
                sptrCum.get(),
                item.certInput->mainchainBackwardTransferRequestScFee,
                item.certInput->forwardTransferScFee,
                sptrProof.get(),
                sptrCertVk.get(),
                &code
            );

            //dumpFeArr((field_t**)custom_fields.get(), custom_fields_len, "custom fields");
            //dumpFe(sptrCum.get(), "cumTree");

            if (!ret || code != CctpErrorCode::OK)
            {
                LogPrintf("ERROR: %s():%d - cert [%s] has proof which does not verify: ret[%d], code [0x%x]\n",
                    __func__, __LINE__, item.certInput->certHash.ToString(), (int)ret, code);

                addFailures.insert(std::make_pair(proofEntry.first, ProofVerifierOutput {.tx = item.certInput->parentPtr,
                                                                                        .node = item.certInput->node,
                                                                                        .proofResult = ProofVerificationResult::Failed}));
            }
        }
        else
        {
            // It should never happen that the proof entry is neither a certificate nor a CSW input.
            assert(false);
        }
    }

    CZendooBatchProofVerifierResult verRes(batchVerifier.batch_verify_all(&code));
    bool totalResult = verRes.Result();
    std::map<uint256, ProofVerifierOutput> results;

    if (totalResult)
    {
        assert(verRes.FailedProofs().size() == 0);
        results = GenerateVerifierResults(proofs, ProofVerificationResult::Passed);
    }
    else
    { 
        results = GenerateVerifierResults(proofs, ProofVerificationResult::Unknown);

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

    // Set the failures that happened during the load process.
    if (addFailures.size() > 0)
    {
        for (auto failure : addFailures)
        {
            results[failure.first] = failure.second;
        }

        totalResult = false;
    }

    int64_t nTime2 = GetTimeMicros();
    LogPrint("bench", "%s():%d - verification succesful: %.2fms\n", __func__, __LINE__, (nTime2-nTime1) * 0.001);
    return std::make_pair(totalResult, results);
}

/**
 * @brief Run the verification for CSW inputs and certificates one by one (not batched).
 * 
 * @param cswInputs The map of CSW inputs data to be verified.
 * @param certInputs The map of certificates data to be verified.
 * @return std::vector<AsyncProofVerifierOutput> The result of all processed proofs.
 */
std::map<uint256, ProofVerifierOutput> CScProofVerifier::NormalVerify(const std::map</* Cert or Tx hash */ uint256, CProofVerifierItem>& proofs) const
{
    std::map<uint256, ProofVerifierOutput> outputs;
    
    for (const auto& verifierInput : proofs)
    {
        ProofVerificationResult res;

        if (verifierInput.second.cswInputs)
        {
            res = NormalVerifyCsw(*verifierInput.second.cswInputs);
        }
        else if (verifierInput.second.certInput)
        {
            res = NormalVerifyCertificate(*verifierInput.second.certInput);
        }
        else
        {
            // It should never happen that the proof entry is neither a certificate nor a CSW input.
            assert(false);
        }

        outputs.insert(std::make_pair(verifierInput.first, ProofVerifierOutput{ .tx = verifierInput.second.parentPtr,
                                                                                .node = verifierInput.second.node,
                                                                                .proofResult = res }));
    }

    return outputs;
}

/**
 * @brief Run the normal verification for a certificate.
 * This is equivalent to running the batch verification with a single input.
 * 
 * @param input Data of the certificate to be verified.
 * @return true If the certificate proof is correctly verified
 * @return false If the certificate proof is rejected
 */
ProofVerificationResult CScProofVerifier::NormalVerifyCertificate(CCertProofVerifierInput input) const
{
    CctpErrorCode code;

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

    // mc crypto lib wants a null ptr if we have no elements
    if (custom_fields_len == 0)
        custom_fields.reset();

    if (bt_list_len == 0)
        bt_list_ptr = nullptr;

    wrappedFieldPtr   sptrConst  = input.constant.GetFieldElement();
    wrappedFieldPtr   sptrCum    = input.endEpochCumScTxCommTreeRoot.GetFieldElement();
    wrappedScProofPtr sptrProof  = input.proof.GetProofPtr();
    wrappedScVkeyPtr  sptrCertVk = input.verificationKey.GetVKeyPtr();

    bool ret = zendoo_verify_certificate_proof(
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

    if (!ret || code != CctpErrorCode::OK)
    {
        LogPrintf("ERROR: %s():%d - cert [%s] has proof which does not verify: code [0x%x]\n",
            __func__, __LINE__, input.certHash.ToString(), code);
    }

    return ret ? ProofVerificationResult::Passed : ProofVerificationResult::Failed;
}

/**
 * @brief Run the normal verification for the CSW inputs of a sidechain transaction.
 * This is equivalent to running the batch verification with a single input.
 * 
 * @param inputMap The map of CSW input data to be verified.
 * @return true If the CSW proof is correctly verified
 * @return false If the CSW proof is rejected
 */
ProofVerificationResult CScProofVerifier::NormalVerifyCsw(std::vector<CCswProofVerifierInput> cswInputs) const
{
    for (CCswProofVerifierInput input : cswInputs)
    {
        wrappedFieldPtr sptrScId = CFieldElement(input.scId).GetFieldElement();
        field_t* scid_fe = sptrScId.get();
 
        const uint160& csw_pk_hash = input.pubKeyHash;
        BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());
     
        wrappedFieldPtr   sptrCdh       = input.certDataHash.GetFieldElement();
        wrappedFieldPtr   sptrCum       = input.ceasingCumScTxCommTree.GetFieldElement();
        wrappedFieldPtr   sptrNullifier = input.nullifier.GetFieldElement();
        wrappedScProofPtr sptrProof     = input.proof.GetProofPtr();
        wrappedScVkeyPtr  sptrCeasedVk  = input.verificationKey.GetVKeyPtr();

        CctpErrorCode code;
        bool ret = zendoo_verify_csw_proof(
                    input.nValue,
                    scid_fe, 
                    sptrNullifier.get(),
                    &bws_csw_pk_hash,
                    sptrCdh.get(),
                    sptrCum.get(),
                    sptrProof.get(),
                    sptrCeasedVk.get(),
                    &code);

        if (!ret || code != CctpErrorCode::OK)
        {
            LogPrintf("ERROR: %s():%d - tx [%s] has csw proof which does not verify: ret[%d], code [0x%x]\n",
                __func__, __LINE__, input.parentPtr->GetHash().ToString(), (int)ret, code);
            return ProofVerificationResult::Failed;
        }
    }
    return ProofVerificationResult::Passed;
}

std::map<uint256, ProofVerifierOutput> CScProofVerifier::GenerateVerifierResults(const std::map</* Cert or Tx hash */ uint256, CProofVerifierItem>& proofs,
                                                                                 ProofVerificationResult defaultResult) const
{
    std::map<uint256, ProofVerifierOutput> results;

    for (auto proofEntry : proofs)
    {
        results.insert(std::make_pair(proofEntry.first, ProofVerifierOutput {.tx = proofEntry.second.parentPtr,
                                                                                .node = proofEntry.second.node,
                                                                                .proofResult = defaultResult}));
    }

    return results;
}
