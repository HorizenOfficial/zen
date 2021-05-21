#include "asyncproofverifier.h"

#include "coins.h"
#include "init.h"
#include "main.h"
#include "util.h"
#include "primitives/certificate.h"
#include "sc/proofverifier.h"

#ifdef BITCOIN_TX
void CScProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom) {return;}
void CScProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom) {return;}
#else
void CScAsyncProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom)
{
    LogPrint("cert", "%s():%d - called: cert[%s], scId[%s]\n",
        __func__, __LINE__, scCert.GetHash().ToString(), scCert.GetScId().ToString());

    // TODO this is code duplicated from CScProofVerifier::LoadDataForCertVerification
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

    certData.node = pfrom;
    {
        LOCK(cs_asyncQueue);
        certEnqueuedData.insert(std::make_pair(scCert.GetHash(), certData));
    }

    return;
}

void CScAsyncProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom)
{
    LogPrint("cert", "%s():%d - Loading CSW of tx [%s] \n", __func__, __LINE__, scTx.GetHash().ToString());

    std::map</*outputPos*/unsigned int, CCswProofVerifierInput> txMap;

    for(size_t idx = 0; idx < scTx.GetVcswCcIn().size(); ++idx)
    {
        CCswProofVerifierInput cswData;

        cswData = cswEnqueuedData[scTx.GetHash()][idx]; //create or retrieve new entry

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

        cswData.node = pfrom;

        txMap.insert(std::make_pair(idx, cswData));
    }

    if (!txMap.empty())
    {
        LOCK(cs_asyncQueue);
        cswEnqueuedData[scTx.GetHash()] = txMap;
    }
}
#endif

void CScAsyncProofVerifier::RunPeriodicVerification()
{
    /**
     * The age of the queue in milliseconds.
     * This value represents the time spent in the queue by the oldest proof in the queue.
     */
    uint32_t queueAge = 0;

    while (!ShutdownRequested())
    {
        size_t currentQueueSize = certEnqueuedData.size() + cswEnqueuedData.size();

        if (currentQueueSize > 0)
        {
            queueAge += THREAD_WAKE_UP_PERIOD;

            /**
             * The batch verification can be triggered by two events:
             * 
             * 1. The queue has grown up beyond the threshold size;
             * 2. The oldest proof in the queue has waited for too long.
             */
            if (queueAge > BATCH_VERIFICATION_MAX_DELAY || currentQueueSize > BATCH_VERIFICATION_MAX_SIZE)
            {
                queueAge = 0;
                std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>> tempCswData;
                std::map</*certHash*/uint256, CCertProofVerifierInput> tempCertData;

                {
                    LOCK(cs_asyncQueue);

                    size_t cswQueueSize = cswEnqueuedData.size();
                    size_t certQueueSize = certEnqueuedData.size();

                    LogPrint("cert", "%s():%d - Async verification triggered, %d certificates and %d CSW inputs to verify \n",
                             __func__, __LINE__, certQueueSize, cswQueueSize);

                    // Move the queued proves into local maps, so that we can release the lock
                    tempCswData = std::move(cswEnqueuedData);
                    tempCertData = std::move(certEnqueuedData);

                    assert(cswEnqueuedData.size() == 0);
                    assert(certEnqueuedData.size() == 0);
                    assert(tempCswData.size() == cswQueueSize);
                    assert(tempCertData.size() == certQueueSize);
                }

                std::pair<bool, std::vector<AsyncProofVerifierOutput>> batchResult = BatchVerify(tempCswData, tempCertData);
                std::vector<AsyncProofVerifierOutput> outputs = batchResult.second;

                if (!batchResult.first)
                {
                    LogPrint("cert", "%s():%d - Batch verification failed, proceeding one by one... \n",
                             __func__, __LINE__);

                    // If the batch verification fails, check the proves one by one
                    outputs = NormalVerify(tempCswData, tempCertData);
                }

                // Post processing of proves
                for (AsyncProofVerifierOutput output : outputs)
                {
                    LogPrint("cert", "%s():%d - Post processing certificate or transaction [%s] from node [%d], result [%d] \n",
                             __func__, __LINE__, output.tx->GetHash().ToString(), output.node->GetId(), output.proofVerified);

                    CValidationState dummyState;
                    ProcessTxBaseAcceptToMemoryPool(*output.tx.get(), output.node,
                                                    output.proofVerified ? BatchVerificationStateFlag::VERIFIED : BatchVerificationStateFlag::FAILED,
                                                    dummyState);
                }
            }
        }

        MilliSleep(THREAD_WAKE_UP_PERIOD);
    }
}

/**
 * @brief Run the batch verification for all the queued CSW outputs and certificates.
 * 
 * @param cswInputs The map of CSW inputs data to be verified.
 * @param certInputs The map of certificates data to be verified.
 * @return std::pair<bool, std::vector<AsyncProofVerifierOutput>> A pair containing the total result of the whole batch verification (bool) and the list of single results.
 */
std::pair<bool, std::vector<AsyncProofVerifierOutput>> CScAsyncProofVerifier::BatchVerify(
    const std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>>& cswInputs,
    const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const
{
    bool allProvesVerified = true;
    std::vector<AsyncProofVerifierOutput> outputs;

    CctpErrorCode code;
    ZendooBatchProofVerifier batchVerifier;
    uint32_t idx = 0;
    bool ret = true;

    for (const auto& verifierInput : cswInputs)
    {
        for (const auto& entry : verifierInput.second)
        {
            const CCswProofVerifierInput& input = entry.second;

            field_t* scid_fe = (field_t*)input.scId.begin();
 
            const uint160& csw_pk_hash = input.pubKeyHash;
            BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());
 
            wrappedFieldPtr   sptrCdh      = input.certDataHash.GetFieldElement();
            wrappedFieldPtr   sptrCum      = input.ceasingCumScTxCommTree.GetFieldElement();
            wrappedScProofPtr sptrCswProof = input.cswProof.GetProofPtr();
            wrappedScVkeyPtr  sptrCeasedVk = input.ceasedVk.GetVKeyPtr();

            ret = batchVerifier.add_csw_proof(
                idx,
                input.nValue,
                scid_fe, 
                &bws_csw_pk_hash,
                sptrCdh.get(),
                sptrCum.get(),
                sptrCswProof.get(),
                sptrCeasedVk.get(),
                &code
            );
            idx++;

            if (!ret || code != CctpErrorCode::OK)
            {
                allProvesVerified = false;
 
                LogPrintf("ERROR: %s():%d - tx [%s] has csw proof which does not verify: ret[%d], code [0x%x]\n",
                    __func__, __LINE__, input.transactionPtr->GetHash().ToString(), (int)ret, code);
            }
        }
        outputs.push_back(AsyncProofVerifierOutput{ .tx = verifierInput.second.begin()->second.transactionPtr,
                                                    .node = verifierInput.second.begin()->second.node,
                                                    .proofVerified = ret });
    }

    for (const auto& verifierInput : certInputs)
    {
        const CCertProofVerifierInput& input = verifierInput.second;

        int custom_fields_len = input.vCustomFields.size(); 
        std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
        int i = 0;
        std::vector<wrappedFieldPtr> vSptr;
        for (auto entry: input.vCustomFields)
        {
            wrappedFieldPtr sptr = entry.GetFieldElement();
            custom_fields[i] = sptr.get();
            vSptr.push_back(sptr);
            i++;
        }

        wrappedFieldPtr   sptrConst  = input.constant.GetFieldElement();
        wrappedFieldPtr   sptrCum    = input.endEpochCumScTxCommTreeRoot.GetFieldElement();
        wrappedScProofPtr sptrProof  = input.certProof.GetProofPtr();
        wrappedScVkeyPtr  sptrCertVk = input.CertVk.GetVKeyPtr();

        bool ret = batchVerifier.add_certificate_proof(
            idx,
            sptrConst.get(),
            input.epochNumber,
            input.quality,
            input.bt_list.data(),
            input.bt_list.size(),
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
            allProvesVerified = false;
 
            LogPrintf("ERROR: %s():%d - cert [%s] has proof which does not verify: ret[%d], code [0x%x]\n",
                __func__, __LINE__, input.certHash.ToString(), (int)ret, code);
        }
        outputs.push_back(AsyncProofVerifierOutput{ .tx = verifierInput.second.certificatePtr,
                                                    .node = verifierInput.second.node,
                                                    .proofVerified = ret });
    }

    int64_t failingProof = -1;
    ZendooBatchProofVerifierResult verRes = batchVerifier.batch_verify_all(&code);
    if (!verRes.result)
    {
        allProvesVerified = false;
        failingProof = verRes.failing_proof;
 
        LogPrintf("ERROR: %s():%d - verify all failed: proofId[%lld], code [0x%x]\n",
            __func__, __LINE__, failingProof, code);
    }

    return std::pair<bool, std::vector<AsyncProofVerifierOutput>> { allProvesVerified, outputs };
}

/**
 * @brief Run the verification for CSW inputs and certificates one by one (not batched).
 * 
 * @param cswInputs The map of CSW inputs data to be verified.
 * @param certInputs The map of certificates data to be verified.
 * @return std::vector<AsyncProofVerifierOutput> The result of all processed proves.
 */
std::vector<AsyncProofVerifierOutput> CScAsyncProofVerifier::NormalVerify(const std::map</*scTxHash*/uint256,
                                                                          std::map</*outputPos*/unsigned int, CCswProofVerifierInput>>& cswInputs,
                                                                          const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const
{
    std::vector<AsyncProofVerifierOutput> outputs;

    for (const auto& verifierInput : cswInputs)
    {
        bool res = NormalVerifyCsw(verifierInput.first, verifierInput.second);

        outputs.push_back(AsyncProofVerifierOutput{ .tx = verifierInput.second.begin()->second.transactionPtr,
                                                    .node = verifierInput.second.begin()->second.node,
                                                    .proofVerified = res });
    }

    for (const auto& verifierInput : certInputs)
    {
        bool res = NormalVerifyCertificate(verifierInput.second);

        outputs.push_back(AsyncProofVerifierOutput{ .tx = verifierInput.second.certificatePtr,
                                                    .node = verifierInput.second.node,
                                                    .proofVerified = res });
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
bool CScAsyncProofVerifier::NormalVerifyCertificate(CCertProofVerifierInput input) const
{
    CctpErrorCode code;

    int custom_fields_len = input.vCustomFields.size(); 

    std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
    int i = 0;
    std::vector<wrappedFieldPtr> vSptr;
    for (auto entry: input.vCustomFields)
    {
        wrappedFieldPtr sptr = entry.GetFieldElement();
        custom_fields[i] = sptr.get();
        vSptr.push_back(sptr);
        i++;
    }

    wrappedFieldPtr   sptrConst  = input.constant.GetFieldElement();
    wrappedFieldPtr   sptrCum    = input.endEpochCumScTxCommTreeRoot.GetFieldElement();
    wrappedScProofPtr sptrProof  = input.certProof.GetProofPtr();
    wrappedScVkeyPtr  sptrCertVk = input.CertVk.GetVKeyPtr();

    bool ret = zendoo_verify_certificate_proof(
        sptrConst.get(),
        input.epochNumber,
        input.quality,
        input.bt_list.data(),
        input.bt_list.size(),
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

    return ret;
}

/**
 * @brief Run the normal verification for the CSW inputs of a sidechain transaction.
 * This is equivalent to running the batch verification with a single input.
 * 
 * @param txHash The sidechain transaction hash.
 * @param inputMap The map of CSW input data to be verified.
 * @return true If the CSW proof is correctly verified
 * @return false If the CSW proof is rejected
 */
bool CScAsyncProofVerifier::NormalVerifyCsw(uint256 txHash, std::map</*outputPos*/unsigned int, CCswProofVerifierInput> inputMap) const
{
    for (const auto& entry : inputMap)
    {
        const CCswProofVerifierInput& input = entry.second;
    
        field_t* scid_fe = (field_t*)input.scId.begin();
     
        const uint160& csw_pk_hash = input.pubKeyHash;
        BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());
     
        wrappedFieldPtr   sptrCdh      = input.certDataHash.GetFieldElement();
        wrappedFieldPtr   sptrCum      = input.ceasingCumScTxCommTree.GetFieldElement();
        wrappedScProofPtr sptrProof    = input.cswProof.GetProofPtr();
        wrappedScVkeyPtr  sptrCeasedVk = input.ceasedVk.GetVKeyPtr();

        CctpErrorCode code;
        bool ret = zendoo_verify_csw_proof(
                    input.nValue,
                    scid_fe, 
                    &bws_csw_pk_hash,
                    sptrCdh.get(),
                    sptrCum.get(),
                    sptrProof.get(),
                    sptrCeasedVk.get(),
                    &code);

        if (!ret || code != CctpErrorCode::OK)
        {
            LogPrintf("ERROR: %s():%d - tx [%s] has csw proof which does not verify: ret[%d], code [0x%x]\n",
                __func__, __LINE__, input.transactionPtr->GetHash().ToString(), (int)ret, code);
        }
    }
    return true;
}
