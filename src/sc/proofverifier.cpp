#include "sc/proofverifier.h"
#include "primitives/certificate.h"

#include "main.h"
#include "util.h"
#include "sync.h"
#include "tinyformat.h"
#include <fstream>

namespace libzendoomc
{
    bool SaveScVkToFile(const boost::filesystem::path& vkPath, const ScVk& scVk) {

        try
        {
            std::ofstream os (vkPath.string(), std::ios_base::out|std::ios::binary);
            os.exceptions(std::ios_base::failbit | std::ios_base::badbit);

            std::copy(scVk.begin(), scVk.end(), std::ostream_iterator<unsigned char>(os));

            os.flush();
            os.close();
        } catch (std::ios_base::failure& e) 
        {
            return error(strprintf("SaveScVkToFile(): error writing to file: %s", e.what()).data());
        }

        return true;
    }

    // Let's define a struct to hold the inputs, with a function to free the memory Rust-side
    struct WCertVerifierInputs {
        std::vector<backward_transfer_t> bt_list;
        field_t* constant;
        field_t* proofdata;
        sc_proof_t* sc_proof;
        sc_vk_t* sc_vk;

        ~WCertVerifierInputs(){

            zendoo_field_free(constant);
            constant = nullptr;

            zendoo_field_free(proofdata);
            proofdata = nullptr;

            zendoo_sc_proof_free(sc_proof);
            sc_proof = nullptr;

            zendoo_sc_vk_free(sc_vk);
            sc_vk = nullptr;
        }
    };

    bool CScWCertProofVerification::verifyScCert(                
        const ScConstant& constant,
        const ScVk& wCertVk,
        const uint256& prev_end_epoch_block_hash,
        const CScCertificate& scCert) const
    {
        // Collect verifier inputs

        WCertVerifierInputs inputs;

        //Deserialize constant
        if (constant.IsNull()) { //Constant can be optional
            inputs.constant = nullptr;
        } else
        {
            inputs.constant = deserialize_field(&constant.GetByteArray()[0]);
        }

        //Initialize quality and proofdata
        inputs.proofdata = nullptr; //Note: For now proofdata is not present in WCert

        //Deserialize proof
        auto sc_proof_bytes = scCert.scProof;

        inputs.sc_proof = deserialize_sc_proof(sc_proof_bytes.begin());

        if(inputs.sc_proof == nullptr) {

            LogPrint("zendoo_mc_cryptolib",
                "%s():%d - failed to deserialize \"sc_proof\": %s \n", 
                __func__, __LINE__, ToString(zendoo_get_last_error()));
            zendoo_clear_error();
            
            return false;
        }

        //Deserialize sc_vk
        inputs.sc_vk = deserialize_sc_vk(wCertVk.begin());

        if (inputs.sc_vk == nullptr){

            LogPrint("zendoo_mc_cryptolib",
                "%s():%d - failed to deserialize \"wCertVk\": %s \n", 
                __func__, __LINE__, ToString(zendoo_get_last_error()));
            zendoo_clear_error();

            return false;
        }

        //Retrieve BT list
        for(int pos = scCert.nFirstBwtPos; pos < scCert.GetVout().size(); ++pos)
        {
            CBackwardTransferOut btout(scCert.GetVout()[pos]);
            backward_transfer bt;

            std::copy(btout.pubKeyHash.begin(), btout.pubKeyHash.end(), std::begin(bt.pk_dest));
            bt.amount = btout.nValue;

            inputs.bt_list.push_back(bt);
        }

        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"end epoch hash\": %s\n",
                    __func__, __LINE__, scCert.endEpochBlockHash.ToString());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"prev end epoch hash\": %s\n",
            __func__, __LINE__, prev_end_epoch_block_hash.ToString());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"bt_list_len\": %d\n",
            __func__, __LINE__, inputs.bt_list.size());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"quality\": %s\n",
            __func__, __LINE__, scCert.quality);
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"constant\": %s\n",
            __func__, __LINE__, constant.GetHexRepr());
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"sc_proof\": %s\n",
            __func__, __LINE__, HexStr(scCert.scProof));
        LogPrint("zendoo_mc_cryptolib", "%s():%d - verified proof \"sc_vk\": %s\n",
            __func__, __LINE__, HexStr(wCertVk));

        // Call verifier
        if (!verify_sc_proof(scCert.endEpochBlockHash.begin(), prev_end_epoch_block_hash.begin(),
                            inputs.bt_list.data(), inputs.bt_list.size(), scCert.quality,
                            inputs.constant, inputs.proofdata, inputs.sc_proof, inputs.sc_vk))
        {
            Error err = zendoo_get_last_error();
            if (err.category == CRYPTO_ERROR){ // Proof verification returned false due to an error, we must log it
                LogPrint("zendoo_mc_cryptolib",
                "%s():%d - failed to verify \"sc_proof\": %s \n", 
                __func__, __LINE__, ToString(err));
                zendoo_clear_error();
            }
            return false;
        }
        return true;
    }

    bool CScProofVerifier::verifyCScCertificate(
        const ScConstant& constant,
        const ScVk& wCertVk,
        const uint256& prev_end_epoch_block_hash,
        const CScCertificate& cert
    ) const 
    {
        if(!perform_verification)
            return true;
        else
            return CScWCertProofVerification().verifyScCert(constant, wCertVk, prev_end_epoch_block_hash, cert);
    }

    bool CScProofVerifier::verifyCTxCeasedSidechainWithdrawalInput(
        const CFieldElement& certDataHash,
        const ScVk& wCeasedVk,
        const CTxCeasedSidechainWithdrawalInput& csw
    ) const
    {
        if(!perform_verification)
            return true;
        else // TODO: emit rust implementation.
            return true;// CswProofVerification().verifyCsw(certDataHash, wCeasedVk, csw);
    }

    bool CScProofVerifier::verifyCBwtRequest(
        const uint256& scId,
        const CFieldElement& scUtxoId,
        const uint160& mcDestinationAddress,
        CAmount scFees,
        const libzendoomc::ScProof& scProof,
        const boost::optional<libzendoomc::ScVk>& wMbtrVk,
        const CFieldElement& certDataHash
    ) const
    {
        return true; //Currently mocked
    }
}
