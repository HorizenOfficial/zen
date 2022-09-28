#include "sc/proofverifier.h"

#include "coins.h"
#include "main.h"
#include "primitives/certificate.h"

std::atomic<uint32_t> CScProofVerifier::proofIdCounter(0);

/**
 * @brief Converts a ProofVerificationResult enum to string.
 *
 * @param res The ProofVerificationResult to be converted
 *
 * @return std::string The converted string.
 */
std::string ProofVerificationResultToString(ProofVerificationResult res) {
    switch (res) {
        case ProofVerificationResult::Unknown:
            return "Unknown";
        case ProofVerificationResult::Failed:
            return "Failed";
        case ProofVerificationResult::Passed:
            return "Passed";
        default:
            return "";
    }
}

/**
 * @brief Creates the proof verifier input of a certificate for the proof verifier.
 *
 * @param certificate The certificate to be submitted to the proof verifier
 * @param scFixedParams The fixed parameters of the sidechain referred by the certificate
 * @param pfrom The node that sent the certificate
 *
 * @return CCertProofVerifierInput The structure containing all the data needed for the proof verification of the certificate.
 */
CCertProofVerifierInput CScProofVerifier::CertificateToVerifierItem(const CScCertificate& certificate,
                                                                    const Sidechain::ScFixedParameters& scFixedParams,
                                                                    CNode* pfrom) {
    CCertProofVerifierInput certData;

    certData.proofId = proofIdCounter++;
    certData.certHash = certificate.GetHash();
    certData.scId = certificate.GetScId();

    if (scFixedParams.constant.is_initialized())
        certData.constant = scFixedParams.constant.get();
    else
        certData.constant = CFieldElement{};

    certData.epochNumber = certificate.epochNumber;
    certData.quality = certificate.quality;

    for (int pos = certificate.nFirstBwtPos; pos < certificate.GetVout().size(); ++pos) {
        CBackwardTransferOut btout(certificate.GetVout().at(pos));
        certData.bt_list.push_back(backward_transfer{});
        backward_transfer& bt = certData.bt_list.back();

        std::copy(btout.pubKeyHash.begin(), btout.pubKeyHash.end(), std::begin(bt.pk_dest));
        bt.amount = btout.nValue;
    }

    for (int i = 0; i < certificate.vFieldElementCertificateField.size(); i++) {
        FieldElementCertificateField entry = certificate.vFieldElementCertificateField.at(i);
        CFieldElement fe{entry.GetFieldElement(scFixedParams.vFieldElementCertificateFieldConfig.at(i), scFixedParams.version)};
        assert(fe.IsValid());
        certData.vCustomFields.push_back(fe);
    }
    for (int i = 0; i < certificate.vBitVectorCertificateField.size(); i++) {
        BitVectorCertificateField entry = certificate.vBitVectorCertificateField.at(i);
        CFieldElement fe{entry.GetFieldElement(scFixedParams.vBitVectorCertificateFieldConfig.at(i), scFixedParams.version)};
        assert(fe.IsValid());
        certData.vCustomFields.push_back(fe);
    }

    certData.endEpochCumScTxCommTreeRoot = certificate.endEpochCumScTxCommTreeRoot;
    certData.mainchainBackwardTransferRequestScFee = certificate.mainchainBackwardTransferRequestScFee;
    certData.forwardTransferScFee = certificate.forwardTransferScFee;

    certData.proof = certificate.scProof;
    certData.verificationKey = scFixedParams.wCertVk;

    return certData;
}

/**
 * @brief Creates the proof verifier input of a CSW transaction for the proof verifier.
 *
 * @param cswInput The CSW input to be verified
 * @param cswTransaction The pointer to the transaction containing the CSW input
 * @param scFixedParams The fixed parameters of the sidechain referred by the certificate
 * @param pfrom The node that sent the certificate
 *
 * @return CCswProofVerifierInput The structure containing all the data needed for the proof verification of the CSW input.
 */
CCswProofVerifierInput CScProofVerifier::CswInputToVerifierItem(const CTxCeasedSidechainWithdrawalInput& cswInput,
                                                                const CTransaction* cswTransaction,
                                                                const Sidechain::ScFixedParameters& scFixedParams,
                                                                CNode* pfrom) {
    CCswProofVerifierInput cswData;

    cswData.proofId = proofIdCounter++;
    cswData.nValue = cswInput.nValue;
    cswData.scId = cswInput.scId;
    cswData.pubKeyHash = cswInput.pubKeyHash;
    cswData.certDataHash = cswInput.actCertDataHash;
    cswData.ceasingCumScTxCommTree = cswInput.ceasingCumScTxCommTree;
    cswData.nullifier = cswInput.nullifier;
    cswData.proof = cswInput.scProof;

    if (scFixedParams.constant.is_initialized())
        cswData.constant = scFixedParams.constant.get();
    else
        cswData.constant = CFieldElement{};

    // The ceased verification key must be initialized to allow CSW. This check is already performed inside
    // IsScTxApplicableToState().
    assert(scFixedParams.wCeasedVk.is_initialized());

    cswData.verificationKey = scFixedParams.wCeasedVk.get();

    return cswData;
}

#ifdef BITCOIN_TX
void CScProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom) {
    return;
}
void CScProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom) {
    return;
}
#else
/**
 * @brief Loads proof data of a certificate into the proof verifier.
 *
 * @param view The current coins view cache (it is needed to get Sidechain information)
 * @param scCert The certificate whose proof has to be verified
 * @param pfrom The node that sent the certificate
 */
void CScProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom) {
    if (verificationMode == Verification::Loose) {
        return;
    }

    LogPrint("cert", "%s():%d - called: cert[%s], scId[%s]\n", __func__, __LINE__, scCert.GetHash().ToString(),
             scCert.GetScId().ToString());

    CSidechain sidechain;
    assert(view.GetSidechain(scCert.GetScId(), sidechain) && "Unknown sidechain at scTx proof verification stage");

    CProofVerifierItem item;
    item.txHash = scCert.GetHash();
    item.parentPtr = std::make_shared<CScCertificate>(scCert);
    item.node = pfrom;
    item.result = ProofVerificationResult::Unknown;
    item.proofInput = CertificateToVerifierItem(scCert, sidechain.fixedParams, pfrom);
    proofQueue.insert(std::make_pair(scCert.GetHash(), item));
}

/**
 * @brief Loads proof data of a CSW transaction into the proof verifier.
 *
 * @param view The current coins view cache (it is needed to get Sidechain information)
 * @param scTx The CSW transaction whose proof has to be verified
 * @param pfrom The node that sent the transaction
 */
void CScProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom) {
    if (verificationMode == Verification::Loose) {
        return;
    }

    std::vector<CCswProofVerifierInput> cswInputProofs;

    for (CTxCeasedSidechainWithdrawalInput cswInput : scTx.GetVcswCcIn()) {
        CSidechain sidechain;
        assert(view.GetSidechain(cswInput.scId, sidechain) && "Unknown sidechain at scTx proof verification stage");

        cswInputProofs.push_back(CswInputToVerifierItem(cswInput, &scTx, sidechain.fixedParams, pfrom));
    }

    if (!cswInputProofs.empty()) {
        CProofVerifierItem item;
        item.txHash = scTx.GetHash();
        item.parentPtr = std::make_shared<CTransaction>(scTx);
        item.result = ProofVerificationResult::Unknown;
        item.node = pfrom;
        item.proofInput = cswInputProofs;
        auto pair_ret = proofQueue.insert(std::make_pair(scTx.GetHash(), item));

        if (!pair_ret.second) {
            LogPrint("sc", "%s():%d - tx [%s] csw inputs already there\n", __func__, __LINE__, scTx.GetHash().ToString());
        } else {
            LogPrint("sc", "%s():%d - tx [%s] added to queue with %d inputs\n", __func__, __LINE__, scTx.GetHash().ToString(),
                     cswInputProofs.size());
        }
    }
}
#endif

/**
 * @brief Runs the verification for currently queued proofs.
 *
 * @return true If the verification succeeded for all the proofs.
 * @return false If the verification failed for at least one proof.
 */
bool CScProofVerifier::BatchVerify() { return BatchVerifyInternal(proofQueue); }

/**
 * @brief Run the batch verification over a set of proofs.
 *
 * @param proofs The map containing all the proofs of any kind to be verified
 *
 * @return true If the verification succeeded for all the proofs.
 * @return false If the verification failed for at least one proof.
 */
bool CScProofVerifier::BatchVerifyInternal(std::map</* Cert or Tx hash */ uint256, CProofVerifierItem>& proofs) {
    if (proofs.size() == 0) {
        return true;
    }

    if (verificationMode == Verification::Loose) {
        for (auto& proof : proofs) {
            proof.second.result = ProofVerificationResult::Passed;
        }
        return true;
    }

    // The paramenter in the ctor is a boolean telling mc-crypto lib if the rust verifier executing thread
    // will be a high-priority one (default is false)
    ZendooBatchProofVerifier batchVerifier(verificationPriority == Priority::High);
    bool addFailure = false;
    std::map<uint32_t /* Proof ID */, uint256 /* Tx or Cert hash */> proofIdMap;
    CctpErrorCode code;

    LogPrint("bench", "%s():%d - starting verification\n", __func__, __LINE__);
    int64_t nTime1 = GetTimeMicros();
    for (auto& proofEntry : proofs) {
        CProofVerifierItem& item = proofEntry.second;

        assert(item.result == ProofVerificationResult::Unknown);

        if (item.proofInput.type() == typeid(std::vector<CCswProofVerifierInput>)) {
            for (auto& cswInput : boost::get<std::vector<CCswProofVerifierInput>>(item.proofInput)) {
                proofIdMap.insert(std::make_pair(cswInput.proofId, proofEntry.first));

                wrappedFieldPtr sptrScId = CFieldElement(cswInput.scId).GetFieldElement();
                field_t* scid_fe = sptrScId.get();

                const uint160& csw_pk_hash = cswInput.pubKeyHash;
                BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());

                wrappedFieldPtr sptrConst = cswInput.constant.GetFieldElement();
                wrappedFieldPtr sptrCdh = cswInput.certDataHash.GetFieldElement();
                wrappedFieldPtr sptrCum = cswInput.ceasingCumScTxCommTree.GetFieldElement();
                wrappedFieldPtr sptrNullifier = cswInput.nullifier.GetFieldElement();
                wrappedScProofPtr sptrProof = cswInput.proof.GetProofPtr();
                wrappedScVkeyPtr sptrCeasedVk = cswInput.verificationKey.GetVKeyPtr();

                bool ret = batchVerifier.add_csw_proof(cswInput.proofId, cswInput.nValue, sptrConst.get(), scid_fe,
                                                       sptrNullifier.get(), &bws_csw_pk_hash, sptrCdh.get(), sptrCum.get(),
                                                       sptrProof.get(), sptrCeasedVk.get(), &code);

                if (!ret || code != CctpErrorCode::OK) {
                    LogPrintf("ERROR: %s():%d - tx [%s] has csw proof which does not verify: ret[%d], code [0x%x]\n", __func__,
                              __LINE__, proofEntry.first.ToString(), (int)ret, code);

                    item.result = ProofVerificationResult::Failed;
                    addFailure = true;

                    // If one CSW input fails, it is possible to skip the whole transaction.
                    break;
                }
            }
        } else if (item.proofInput.type() == typeid(CCertProofVerifierInput)) {
            CCertProofVerifierInput certInput = boost::get<CCertProofVerifierInput>(item.proofInput);
            proofIdMap.insert(std::make_pair(certInput.proofId, proofEntry.first));

            int custom_fields_len = certInput.vCustomFields.size();
            std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
            int i = 0;
            std::vector<wrappedFieldPtr> vSptr;
            for (auto entry : certInput.vCustomFields) {
                wrappedFieldPtr sptrFe = entry.GetFieldElement();
                custom_fields[i] = sptrFe.get();
                vSptr.push_back(sptrFe);
                i++;
            }

            const backward_transfer_t* bt_list_ptr = certInput.bt_list.data();
            int bt_list_len = certInput.bt_list.size();

            // mc crypto lib wants a null ptr if we have no fields
            if (custom_fields_len == 0) custom_fields.reset();
            if (bt_list_len == 0) bt_list_ptr = nullptr;

            wrappedFieldPtr sptrScId = CFieldElement(certInput.scId).GetFieldElement();
            field_t* scidFe = sptrScId.get();

            wrappedFieldPtr sptrConst = certInput.constant.GetFieldElement();
            wrappedFieldPtr sptrCum = certInput.endEpochCumScTxCommTreeRoot.GetFieldElement();
            wrappedScProofPtr sptrProof = certInput.proof.GetProofPtr();
            wrappedScVkeyPtr sptrCertVk = certInput.verificationKey.GetVKeyPtr();

            bool ret = batchVerifier.add_certificate_proof(
                certInput.proofId, sptrConst.get(), scidFe, certInput.epochNumber, certInput.quality, bt_list_ptr, bt_list_len,
                custom_fields.get(), custom_fields_len, sptrCum.get(), certInput.mainchainBackwardTransferRequestScFee,
                certInput.forwardTransferScFee, sptrProof.get(), sptrCertVk.get(), &code);
            // dumpBtArr((backward_transfer_t*)bt_list_ptr, bt_list_len, "bwt list");
            // dumpFeArr((field_t**)custom_fields.get(), custom_fields_len, "custom fields");
            // dumpFe(sptrCum.get(), "cumTree");

            if (!ret || code != CctpErrorCode::OK) {
                LogPrintf("ERROR: %s():%d - cert [%s] has proof which does not verify: ret[%d], code [0x%x]\n", __func__,
                          __LINE__, certInput.certHash.ToString(), (int)ret, code);

                item.result = ProofVerificationResult::Failed;
                addFailure = true;
            }
        } else {
            // It should never happen that the proof entry is neither a certificate nor a CSW input.
            assert(false);
        }
    }

    CZendooBatchProofVerifierResult verRes(batchVerifier.batch_verify_all(&code));

    if (verRes.Result()) {
        assert(verRes.FailedProofs().size() == 0);

        for (auto& proof : proofs) {
            CProofVerifierItem& item = proof.second;

            // Set as "passed" only proofs that didn't fail during the add process.
            if (item.result == ProofVerificationResult::Unknown) {
                item.result = ProofVerificationResult::Passed;
            }
        }
    } else {
        if (verRes.FailedProofs().size() > 0) {
            // We know for sure that some proofs failed, the other ones may be valid or not (unknown).
            LogPrintf("ERROR: %s():%d - verify failed for %d proof(s), code [0x%x]\n", __func__, __LINE__,
                      verRes.FailedProofs().size(), code);

            for (uint32_t proofId : verRes.FailedProofs()) {
                uint256 txHash = proofIdMap.at(proofId);
                proofs.at(txHash).result = ProofVerificationResult::Failed;
            }
        } else {
            // We don't know which proofs made the batch process fail.
            LogPrintf("ERROR: %s():%d - verify failed without detailed information, code [0x%x]\n", __func__, __LINE__, code);
        }
    }

    int64_t nTime2 = GetTimeMicros();
    LogPrint("bench", "%s():%d - verification completed: %.2fms\n", __func__, __LINE__, (nTime2 - nTime1) * 0.001);
    return !addFailure && verRes.Result();
}

/**
 * @brief Runs the verification for a set of proofs one by one (not batched).
 * The result of the verification for each item is stored inside the
 * CProofVerifierItem structure itself.
 *
 * @param proofs The map of proofs of any kind to be verified.
 */
void CScProofVerifier::NormalVerify(std::map</* Cert or Tx hash */ uint256, CProofVerifierItem>& proofs) {
    for (auto& proof : proofs) {
        CProofVerifierItem& item = proof.second;

        if (item.proofInput.type() == typeid(std::vector<CCswProofVerifierInput>)) {
            item.result = NormalVerifyCsw(boost::get<std::vector<CCswProofVerifierInput>>(item.proofInput));
        } else if (item.proofInput.type() == typeid(CCertProofVerifierInput)) {
            item.result = NormalVerifyCertificate(boost::get<CCertProofVerifierInput>(item.proofInput));
        } else {
            // It should never happen that the proof entry is neither a certificate nor a CSW input.
            assert(false);
        }
    }
}

/**
 * @brief Runs the normal verification for a single certificate.
 *
 * @param input Data of the certificate to be verified
 *
 * @return true If the certificate proof is correctly verified.
 * @return false If the certificate proof is rejected.
 */
ProofVerificationResult CScProofVerifier::NormalVerifyCertificate(CCertProofVerifierInput input) const {
    CctpErrorCode code;

    int custom_fields_len = input.vCustomFields.size();

    std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
    int i = 0;
    std::vector<wrappedFieldPtr> vSptr;
    for (auto entry : input.vCustomFields) {
        wrappedFieldPtr sptrFe = entry.GetFieldElement();
        custom_fields[i] = sptrFe.get();
        vSptr.push_back(sptrFe);
        i++;
    }

    const backward_transfer_t* bt_list_ptr = input.bt_list.data();
    int bt_list_len = input.bt_list.size();

    // mc crypto lib wants a null ptr if we have no elements
    if (custom_fields_len == 0) custom_fields.reset();

    if (bt_list_len == 0) bt_list_ptr = nullptr;

    wrappedFieldPtr sptrScId = CFieldElement(input.scId).GetFieldElement();
    field_t* scidFe = sptrScId.get();

    wrappedFieldPtr sptrConst = input.constant.GetFieldElement();
    wrappedFieldPtr sptrCum = input.endEpochCumScTxCommTreeRoot.GetFieldElement();
    wrappedScProofPtr sptrProof = input.proof.GetProofPtr();
    wrappedScVkeyPtr sptrCertVk = input.verificationKey.GetVKeyPtr();

    bool ret = zendoo_verify_certificate_proof(sptrConst.get(), scidFe, input.epochNumber, input.quality, bt_list_ptr,
                                               bt_list_len, custom_fields.get(), custom_fields_len, sptrCum.get(),
                                               input.mainchainBackwardTransferRequestScFee, input.forwardTransferScFee,
                                               sptrProof.get(), sptrCertVk.get(), &code);

    if (!ret || code != CctpErrorCode::OK) {
        LogPrintf("ERROR: %s():%d - certificate proof with ID [%d] does not verify: code [0x%x]\n", __func__, __LINE__,
                  input.proofId, code);
    }

    return ret ? ProofVerificationResult::Passed : ProofVerificationResult::Failed;
}

/**
 * @brief Runs the normal verification for the CSW inputs of a single sidechain transaction.
 *
 * @param cswInputs The list of CSW inputs to be verified
 *
 * @return true If the CSW proof is correctly verified.
 * @return false If the CSW proof is rejected.
 */
ProofVerificationResult CScProofVerifier::NormalVerifyCsw(std::vector<CCswProofVerifierInput> cswInputs) const {
    for (CCswProofVerifierInput input : cswInputs) {
        wrappedFieldPtr sptrScId = CFieldElement(input.scId).GetFieldElement();
        field_t* scid_fe = sptrScId.get();

        const uint160& csw_pk_hash = input.pubKeyHash;
        BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());

        wrappedFieldPtr sptrConst = input.constant.GetFieldElement();
        wrappedFieldPtr sptrCdh = input.certDataHash.GetFieldElement();
        wrappedFieldPtr sptrCum = input.ceasingCumScTxCommTree.GetFieldElement();
        wrappedFieldPtr sptrNullifier = input.nullifier.GetFieldElement();
        wrappedScProofPtr sptrProof = input.proof.GetProofPtr();
        wrappedScVkeyPtr sptrCeasedVk = input.verificationKey.GetVKeyPtr();

        CctpErrorCode code;
        bool ret = zendoo_verify_csw_proof(input.nValue, sptrConst.get(), scid_fe, sptrNullifier.get(), &bws_csw_pk_hash,
                                           sptrCdh.get(), sptrCum.get(), sptrProof.get(), sptrCeasedVk.get(), &code);

        if (!ret || code != CctpErrorCode::OK) {
            LogPrintf("ERROR: %s():%d - Tx proof with ID [%d] does not verify: ret[%d], code [0x%x]\n", __func__, __LINE__,
                      input.proofId, (int)ret, code);
            return ProofVerificationResult::Failed;
        }
    }
    return ProofVerificationResult::Passed;
}
