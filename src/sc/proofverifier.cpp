#include "sc/proofverifier.h"
#include "primitives/certificate.h"
#include <zendoo/error.h>

bool CScProofVerifier::verifyCScCertificate(
    const ScConstant& constant,
    const CScVKey& wCertVk,
    const uint256& prev_end_epoch_block_hash,
    const CScCertificate& cert
) const
{
    if(verificationMode == Verification::Loose)
        return true;

     CFieldElement proofdata{}; //Note: Currently proofdata is not present in WCert
    //Note: Currently quality not yet accounted for in proof verifier

    //Retrieve BT list
     std::vector<backward_transfer_t> bt_list;
    for(int pos = cert.nFirstBwtPos; pos < cert.GetVout().size(); ++pos)
    {
        CBackwardTransferOut btout(cert.GetVout()[pos]);
        backward_transfer bt;

        std::copy(btout.pubKeyHash.begin(), btout.pubKeyHash.end(), std::begin(bt.pk_dest));
        bt.amount = btout.nValue;

        bt_list.push_back(bt);
    }

    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"end epoch hash\": %s\n",
                __func__, __LINE__, cert.endEpochBlockHash.ToString());
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"prev end epoch hash\": %s\n",
        __func__, __LINE__, prev_end_epoch_block_hash.ToString());
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"bt_list_len\": %d\n",
        __func__, __LINE__, bt_list.size());
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"quality\": %s\n",
        __func__, __LINE__, cert.quality);
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"constant\": %s\n",
        __func__, __LINE__, constant.GetHexRepr());
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"sc_proof\": %s\n",
        __func__, __LINE__, cert.scProof.GetHexRepr());
    LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"sc_vk\": %s\n",
        __func__, __LINE__, wCertVk.GetHexRepr());

    // Call verifier
    if (!zendoo_verify_sc_proof(
            cert.endEpochBlockHash.begin(), prev_end_epoch_block_hash.begin(),
            bt_list.data(), bt_list.size(),
            cert.quality,
            constant.GetFieldElement().get(),
            proofdata.GetFieldElement().get(),
			cert.scProof.GetProofPtr().get(),
			wCertVk.GetVKeyPtr().get()))
    {
        Error err = zendoo_get_last_error();
        if (err.category == CRYPTO_ERROR){ // Proof verification returned false due to an error, we must log it
        	std::string errorStr = strprintf(
                    "%s: [%d - %s]\n",
                    err.msg,
                    err.category,
                    zendoo_get_category_name(err.category));
            LogPrint("zendoo_mc_cryptolib",
            "%s():%d - failed to verify \"sc_proof\": %s \n",
            __func__, __LINE__, errorStr);
            zendoo_clear_error();
        }
        return false;
    }
    return true;
}

bool CScProofVerifier::verifyCTxCeasedSidechainWithdrawalInput(
    const CFieldElement& certDataHash,
    const CScVKey& wCeasedVk,
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
    const boost::optional<CScVKey>& wMbtrVk,
    const CFieldElement& certDataHash
) const
{
    if(verificationMode == Verification::Loose)
        return true;
    else
        return true; // TODO: currently mocked, replace with rust implementation.
}
