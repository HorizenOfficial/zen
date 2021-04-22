#include "sc/proofverifier.h"
#include <coins.h>
#include "primitives/certificate.h"

#include <zendoo/error.h>
#include <main.h>

#ifdef BITCOIN_TX
void CScProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert) {return;}
#else
void CScProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert)
{
    const uint256& certHash = scCert.GetHash();

    LogPrint("cert", "%s():%d - called: cert[%s], scId[%s]\n",
        __func__, __LINE__, certHash.ToString(), scCert.GetScId().ToString());

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

    certVerifierInputsList& newItem = certEnqueuedData[scCert.GetHash()]; //create or retrieve new entry
    newItem.endEpochBlockHash = scCert.endEpochBlockHash;
    newItem.prevEndEpochBlockHash = prev_end_epoch_block_index->GetBlockHash();

    for(int pos = scCert.nFirstBwtPos; pos < scCert.GetVout().size(); ++pos)
    {
        CBackwardTransferOut btout(scCert.GetVout().at(pos));
        newItem.bt_list.push_back(backward_transfer{});
        backward_transfer& bt = newItem.bt_list.back();

        std::copy(btout.pubKeyHash.begin(), btout.pubKeyHash.end(), std::begin(bt.pk_dest));
        bt.amount = btout.nValue;
    }

    newItem.quality = scCert.quality; //Currently quality not yet accounted for in proof verifier
    if (sidechain.fixedParams.constant.is_initialized())
        newItem.constant = sidechain.fixedParams.constant.get();
    else
        newItem.constant = CFieldElement{};

    newItem.proofdata = CFieldElement{}; //Note: Currently proofdata is not present in WCert
    newItem.certProof = scCert.scProof;
    newItem.CertVk = sidechain.fixedParams.wCertVk;

    return;
}
#endif

std::map<uint256,bool> CScProofVerifier::batchVerifyCerts() const
{
#ifdef MC_CRYPTO_LIB_MOCKED
    std::map<uint256,bool> res;
	for(const auto& pair: certEnqueuedData)
		res[pair.first] = true;

	return res;
#else
    std::map<uint256,bool> res;
    if(verificationMode == Verification::Loose)
    {
        for(const auto& pair: certEnqueuedData)
            res[pair.first] = true;

        return res;
    }

    //currently call verifier for each single certificate
    for(const auto& pair: certEnqueuedData)
    {
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"end epoch hash\": %s\n",
                    __func__, __LINE__, pair.second.endEpochBlockHash.ToString());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"prev end epoch hash\": %s\n",
            __func__, __LINE__, pair.second.prevEndEpochBlockHash.ToString());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"bt_list_len\": %d\n",
            __func__, __LINE__, pair.second.bt_list.size());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"quality\": %s\n",
            __func__, __LINE__, pair.second.quality);
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"constant\": %s\n",
            __func__, __LINE__, pair.second.constant.GetHexRepr());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"sc_proof\": %s\n",
            __func__, __LINE__, pair.second.certProof.GetHexRepr());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"sc_vk\": %s\n",
            __func__, __LINE__, pair.second.CertVk.GetHexRepr());

        res[pair.first] = zendoo_verify_sc_proof(
                pair.second.endEpochBlockHash.begin(), pair.second.prevEndEpochBlockHash.begin(),
                pair.second.bt_list.data(), pair.second.bt_list.size(),
                pair.second.quality,
                pair.second.constant.GetFieldElement().get(),
                pair.second.proofdata.GetFieldElement().get(),
                pair.second.certProof.GetProofPtr().get(),
                pair.second.CertVk.GetVKeyPtr().get());

        if (!res.at(pair.first))
        {
            Error err = zendoo_get_last_error();
            if (err.category == CRYPTO_ERROR)
            {
                std::string errorStr = strprintf( "%s: [%d - %s]\n",
                    err.msg, err.category,
                    zendoo_get_category_name(err.category));

                LogPrintf("ERROR: %s():%d - cert [%s] has proof which does not verify, with error [%s]\n",
                    __func__, __LINE__, pair.first.ToString(), errorStr);
                zendoo_clear_error();
            }
        }
    }

    return res;
#endif
}

#ifdef BITCOIN_TX
void CScProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx) {return;}
#else
void CScProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx)
{
    for(size_t idx = 0; idx < scTx.GetVcswCcIn().size(); ++idx)
    {
        const CTxCeasedSidechainWithdrawalInput& csw = scTx.GetVcswCcIn().at(idx);

        CSidechain sidechain;
        assert(view.GetSidechain(csw.scId, sidechain) && "Unknown sidechain at scTx proof verification stage");

        cswVerifierInputsList& newItem = cswEnqueuedData[scTx.GetHash()][idx]; //create or retrieve new entry
        newItem.certDataHash = view.GetActiveCertView(csw.scId).certDataHash;
//        //TODO: Unlock when we'll handle recovery of fwt of last epoch
//        if (certDataHash.IsNull())
//            return error("%s():%d - ERROR: Tx[%s] CSW input [%s] has missing active cert data hash for required scId[%s]\n",
//                            __func__, __LINE__, tx.ToString(), csw.ToString(), csw.scId.ToString());

        if (sidechain.fixedParams.wCeasedVk.is_initialized())
            newItem.ceasedVk = sidechain.fixedParams.wCeasedVk.get();
        else
            newItem.ceasedVk = CScVKey{};

        newItem.cswOut = csw;
    }
}
#endif

std::map<uint256,bool> CScProofVerifier::batchVerifyCsws() const
{
    std::map</*scTxHash*/uint256, bool> res;

    // all mocked currently
    for(const auto& txHashItem: cswEnqueuedData)
    {
        res[txHashItem.first] = true;
    }

    return res;
}
