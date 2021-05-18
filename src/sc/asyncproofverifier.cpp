#include "asyncproofverifier.h"

#include "coins.h"
#include "init.h"
#include "main.h"
#include "util.h"
#include "primitives/certificate.h"
#include "sc/proofverifier.h"
#include "zendoo/error.h"

#ifdef BITCOIN_TX
void CScProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom) {return;}
void CScProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom) {return;}
#else
void CScAsyncProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom)
{
    CCertProofVerifierInput certData;

    certData.certificatePtr = std::make_shared<CScCertificate>(scCert);
    certData.certHash = scCert.GetHash();

    LogPrint("cert", "%s():%d - Loading certificate [%s] data for sidechain [%s]\n",
        __func__, __LINE__, certData.certHash.ToString(), scCert.GetScId().ToString());

    CSidechain sidechain;
    assert(view.GetSidechain(scCert.GetScId(), sidechain) && "Unknown sidechain at cert proof verification stage");

    // Retrieve current and previous end epoch block info for certificate proof verification
    int curr_end_epoch_block_height = sidechain.GetEndHeightForEpoch(scCert.epochNumber);
    int prev_end_epoch_block_height = curr_end_epoch_block_height - sidechain.fixedParams.withdrawalEpochLength;

    CBlockIndex* prev_end_epoch_block_index = chainActive[prev_end_epoch_block_height];
    CBlockIndex* curr_end_epoch_block_index = chainActive[curr_end_epoch_block_height];

    assert(prev_end_epoch_block_index);
    assert(curr_end_epoch_block_index);

    const CFieldElement& scCumTreeHash_start = prev_end_epoch_block_index->scCumTreeHash;
    const CFieldElement& scCumTreeHash_end   = curr_end_epoch_block_index->scCumTreeHash;

    // TODO Remove prev_end_epoch_block_hash after changing of verification circuit.
    const uint256& prev_end_epoch_block_hash = prev_end_epoch_block_index->GetBlockHash();

    certData.endEpochBlockHash = scCert.endEpochBlockHash;
    certData.prevEndEpochBlockHash = prev_end_epoch_block_index->GetBlockHash();

    for(int pos = scCert.nFirstBwtPos; pos < scCert.GetVout().size(); ++pos)
    {
        CBackwardTransferOut btout(scCert.GetVout().at(pos));
        certData.bt_list.push_back(backward_transfer{});
        backward_transfer& bt = certData.bt_list.back();

        std::copy(btout.pubKeyHash.begin(), btout.pubKeyHash.end(), std::begin(bt.pk_dest));
        bt.amount = btout.nValue;
    }

    certData.quality = scCert.quality; //Currently quality not yet accounted for in proof verifier
    if (sidechain.fixedParams.constant.is_initialized())
        certData.constant = sidechain.fixedParams.constant.get();
    else
        certData.constant = CFieldElement{};

    certData.proofdata = CFieldElement{}; //Note: Currently proofdata is not present in WCert
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
    LogPrint("cert", "%s():%d - Loading CSW of tx [%s] \n",
        __func__, __LINE__, scTx.GetHash().ToString());

    std::map</*outputPos*/unsigned int, CCswProofVerifierInput> txMap;

    for(size_t idx = 0; idx < scTx.GetVcswCcIn().size(); ++idx)
    {
        CCswProofVerifierInput cswData;

        const CTxCeasedSidechainWithdrawalInput& csw = scTx.GetVcswCcIn().at(idx);

        CSidechain sidechain;
        assert(view.GetSidechain(csw.scId, sidechain) && "Unknown sidechain at scTx proof verification stage");

        cswData = cswEnqueuedData[scTx.GetHash()][idx]; //create or retrieve new entry

        cswData.transactionPtr = std::make_shared<CTransaction>(scTx);
        cswData.certDataHash = view.GetActiveCertView(csw.scId).certDataHash;
//        //TODO: Unlock when we'll handle recovery of fwt of last epoch
//        if (certDataHash.IsNull())
//            return error("%s():%d - ERROR: Tx[%s] CSW input [%s] has missing active cert data hash for required scId[%s]\n",
//                            __func__, __LINE__, tx.ToString(), csw.ToString(), csw.scId.ToString());

        if (sidechain.fixedParams.wCeasedVk.is_initialized())
            cswData.ceasedVk = sidechain.fixedParams.wCeasedVk.get();
        else
            cswData.ceasedVk = CScVKey{};

        cswData.cswInput = csw;
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

                    // CODE USED FOR UNIT TEST ONLY [Start]
                    if (BOOST_UNLIKELY(Params().NetworkIDString() == "regtest"))
                    {
                        UpdateStatistics(output); // Update the statistics

                        // Check if the AcceptToMemoryPool has to be skipped.
                        if (skipAcceptToMemoryPool)
                        {
                            continue;
                        }
                    }
                    // CODE USED FOR UNIT TEST ONLY [End]

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
std::pair<bool, std::vector<AsyncProofVerifierOutput>> CScAsyncProofVerifier::BatchVerify(const std::map</*scTxHash*/uint256,
                                                                                          std::map</*outputPos*/unsigned int, CCswProofVerifierInput>>& cswInputs,
                                                                                          const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const
{
    for (const auto& verifierInput : cswInputs)
    {
        // TODO: load all CSW proves to RUST verifier.
    }

    for (const auto& verifierInput : certInputs)
    {
        // TODO: load all certificate proves to RUST verifier.
    }

    // TODO: call the bacth verification on RUST verifier instead of this temporary internal implementation.
    return _batchVerifyInternal(cswInputs, certInputs);
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
    bool res = zendoo_verify_sc_proof(
                input.endEpochBlockHash.begin(), input.prevEndEpochBlockHash.begin(),
                input.bt_list.data(), input.bt_list.size(),
                input.quality,
                input.constant.GetFieldElement().get(),
                input.proofdata.GetFieldElement().get(),
                input.certProof.GetProofPtr().get(),
                input.CertVk.GetVKeyPtr().get());

    if (!res)
    {
        Error err = zendoo_get_last_error();

        if (err.category == CRYPTO_ERROR)
        {
            std::string errorStr = strprintf( "%s: [%d - %s]\n",
                err.msg, err.category,
                zendoo_get_category_name(err.category));

            LogPrintf("ERROR: %s():%d - cert [%s] has proof which does not verify, with error [%s]\n",
                __func__, __LINE__, input.certHash.ToString(), errorStr);
            zendoo_clear_error();
        }

        return false;
    }

    return true;
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
    // TODO: call RUST implementation
    return true;
}

/**
 * @brief Update the statistics of the proof verifier.
 * It must be used in regression test mode only.
 * @param output The result of the proof verification that has been performed.
 */
void CScAsyncProofVerifier::UpdateStatistics(const AsyncProofVerifierOutput& output)
{
    assert(Params().NetworkIDString() == "regtest");

    if (output.tx->IsCertificate())
    {
        if (output.proofVerified)
        {
            stats.okCertCounter++;
        }
        else
        {
            stats.failedCertCounter++;
        }
    }
    else
    {
        if (output.proofVerified)
        {
            stats.okCswCounter++;
        }
        else
        {
            stats.failedCswCounter++;
        }
    }
}

std::pair<bool, std::vector<AsyncProofVerifierOutput>> CScAsyncProofVerifier::_batchVerifyInternal(const std::map</*scTxHash*/uint256, std::map</*outputPos*/unsigned int, CCswProofVerifierInput>>& cswTxInputs,
                                                                                                   const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const 
{
    bool allProvesVerified = true;
    std::vector<AsyncProofVerifierOutput> outputs;

    for (const auto& certInput : certInputs)
    {
        const CCertProofVerifierInput& input = certInput.second;

        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"end epoch hash\": %s\n",
                __func__, __LINE__, input.endEpochBlockHash.ToString());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"prev end epoch hash\": %s\n",
            __func__, __LINE__, input.prevEndEpochBlockHash.ToString());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"bt_list_len\": %d\n",
            __func__, __LINE__, input.bt_list.size());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"quality\": %s\n",
            __func__, __LINE__, input.quality);
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"constant\": %s\n",
            __func__, __LINE__, input.constant.GetHexRepr());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"sc_proof\": %s\n",
            __func__, __LINE__, input.certProof.GetHexRepr());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"sc_vk\": %s\n",
            __func__, __LINE__, input.CertVk.GetHexRepr());

        // bool res = zendoo_verify_sc_proof(
        //         input.endEpochBlockHash.begin(), input.prevEndEpochBlockHash.begin(),
        //         input.bt_list.data(), input.bt_list.size(),
        //         input.quality,
        //         input.constant.GetFieldElement().get(),
        //         input.proofdata.GetFieldElement().get(),
        //         input.certProof.GetProofPtr().get(),
        //         input.CertVk.GetVKeyPtr().get());

        // Since the proof verification system has not been completely integrated yet, let's mock the implementation.

        bool res = !input.certProof.IsNull();

        // if (!res)
        // {
        //     allProvesVerified = false;
        //     Error err = zendoo_get_last_error();

        //     if (err.category == CRYPTO_ERROR)
        //     {
        //         std::string errorStr = strprintf( "%s: [%d - %s]\n",
        //             err.msg, err.category,
        //             zendoo_get_category_name(err.category));

        //         LogPrintf("ERROR: %s():%d - cert [%s] has proof which does not verify, with error [%s]\n",
        //             __func__, __LINE__, input.certHash.ToString(), errorStr);
        //         zendoo_clear_error();
        //     }
        // }

        outputs.push_back(AsyncProofVerifierOutput{ .tx = input.certificatePtr,
                                                    .node = input.node,
                                                    .proofVerified = res });
    }

    for (const auto& cswTxInput : cswTxInputs)
    {
        bool proofVerified = true;

        for (const auto& cswInput : cswTxInput.second)
        {
            if (cswInput.second.cswInput.scProof.IsNull())
            {
                proofVerified = false;
                break;
            }
        }

        outputs.push_back(AsyncProofVerifierOutput{ .tx = cswTxInput.second.begin()->second.transactionPtr,
                                                    .node = cswTxInput.second.begin()->second.node,
                                                    .proofVerified = proofVerified });
    }

    return std::pair<bool, std::vector<AsyncProofVerifierOutput>> { allProvesVerified, outputs };
}
