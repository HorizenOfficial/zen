#include "sc/proofverifier.h"
#include "primitives/certificate.h"

// Let's define a struct to hold the inputs, with a function to free the memory Rust-side
// TO BE REMOVED AS SOON AS PROOFS AND VK ARE DULY WRAPPED IN A RAII-ed STRUCTURE
struct WCertVerifierInputs {
    std::vector<backward_transfer_t> bt_list;
    CFieldElement constant;
    CFieldElement proofdata;
    CScProof sc_proof;
    sc_vk_t* sc_vk;

    ~WCertVerifierInputs() {

        zendoo_sc_vk_free(sc_vk);
        sc_vk = nullptr;
    }
};

bool CScProofVerifier::verifyCScCertificate(
    const ScConstant& constant,
    const libzendoomc::ScVk& wCertVk,
    const uint256& prev_end_epoch_block_hash,
    const CScCertificate& cert
) const
{
    if(verificationMode == Verification::Loose)
        return true;

    WCertVerifierInputs inputs; // JUST A RAII WRAPPER, TO BE REMOVED ONCE PROOF AND VK ARE DULY WRAPPED BY THEMSELVES

    inputs.constant = constant;
    inputs.proofdata = CFieldElement{}; //Note: Currently proofdata is not present in WCert
    //Note: Currently quality not yet accounted for in proof verifier

    inputs.sc_proof = cert.scProof;

    //Deserialize sc_vk
    inputs.sc_vk = zendoo_deserialize_sc_vk(wCertVk.begin());
    assert(inputs.sc_vk != nullptr); // sc_vk must have been already validated at this point

    //Retrieve BT list
    for(int pos = cert.nFirstBwtPos; pos < cert.GetVout().size(); ++pos)
    {
        CBackwardTransferOut btout(cert.GetVout()[pos]);
        backward_transfer bt;

        std::copy(btout.pubKeyHash.begin(), btout.pubKeyHash.end(), std::begin(bt.pk_dest));
        bt.amount = btout.nValue;

        inputs.bt_list.push_back(bt);
    }

    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"end epoch hash\": %s\n",
                __func__, __LINE__, cert.endEpochBlockHash.ToString());
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"prev end epoch hash\": %s\n",
        __func__, __LINE__, prev_end_epoch_block_hash.ToString());
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"bt_list_len\": %d\n",
        __func__, __LINE__, inputs.bt_list.size());
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"quality\": %s\n",
        __func__, __LINE__, cert.quality);
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"constant\": %s\n",
        __func__, __LINE__, constant.GetHexRepr());
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"sc_proof\": %s\n",
        __func__, __LINE__, cert.scProof.GetHexRepr());
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"sc_vk\": %s\n",
        __func__, __LINE__, HexStr(wCertVk));

    // Call verifier
    if (!zendoo_verify_sc_proof(
            cert.endEpochBlockHash.begin(), prev_end_epoch_block_hash.begin(),
            inputs.bt_list.data(), inputs.bt_list.size(),
            cert.quality,
            inputs.constant.GetFieldElement().get(),
            inputs.proofdata.GetFieldElement().get(),
            inputs.sc_proof.GetProofPtr().get(),
            inputs.sc_vk))
    {
        Error err = zendoo_get_last_error();
        if (err.category == CRYPTO_ERROR){ // Proof verification returned false due to an error, we must log it
            LogPrint("zendoo_mc_cryptolib",
            "%s():%d - failed to verify \"sc_proof\": %s \n",
            __func__, __LINE__, libzendoomc::ToString(err));
            zendoo_clear_error();
        }
        return false;
    }
    return true;
}

bool CScProofVerifier::verifyCTxCeasedSidechainWithdrawalInput(
    const CFieldElement& certDataHash,
    const libzendoomc::ScVk& wCeasedVk,
    const CTxCeasedSidechainWithdrawalInput& csw
) const
{
    if(verificationMode == Verification::Loose)
        return true;
    else
        return true; // TODO: currently mocked, replace with rust implementation.
}

bool CScProofVerifier::verifyCBwtRequest(
    const uint256& scId,
    const CFieldElement& scRequestData,
    const uint160& mcDestinationAddress,
    CAmount scFees,
    const CScProof& scProof,
    const boost::optional<libzendoomc::ScVk>& wMbtrVk,
    const CFieldElement& certDataHash
) const
{
    if(verificationMode == Verification::Loose)
        return true;
    else
        return true; // TODO: currently mocked, replace with rust implementation.
}
