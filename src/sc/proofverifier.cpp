#include "sc/proofverifier.h"
#include "primitives/certificate.h"

#include "main.h"

#include "util.h"
#include "sync.h"
#include "tinyformat.h"

#include <fstream>

namespace libzendoomc{

    bool IsValidScProof(const ScProof& scProof)
    {
        bool result = true;
        auto scProofDeserialized = zendoo_deserialize_sc_proof(scProof.begin());
        if (scProofDeserialized == nullptr)
            result = false;
        zendoo_sc_proof_free(scProofDeserialized);
        return result;
    }

    bool IsValidScVk(const ScVk& scVk)
    {
        bool result = true;
        auto scVkDeserialized = zendoo_deserialize_sc_vk(scVk.begin());
        if (scVkDeserialized == nullptr)
            result = false;
        zendoo_sc_vk_free(scVkDeserialized);
        return result;
    }

    bool IsValidScConstant(const ScConstant& scConstant)
    {
        bool result = true;
        auto scConstantDeserialized = zendoo_deserialize_field(scConstant.begin());
        if (scConstantDeserialized == nullptr)
            result = false;
        zendoo_field_free(scConstantDeserialized);
        return result;
    }

    std::string ToString(Error err){
        return strprintf(
            "%s: [%d - %s]\n",
            err.msg,
            err.category,
            zendoo_get_category_name(err.category));
    }

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
          
    bool CScWCertProofVerificationParameters::createParameters() {
        //Deserialize constant
        auto constant_bytes = scInfo.creationData.constant;
        if (constant_bytes.IsNull()){ //Constant can be optional
            constant = nullptr;
       
        } else {
            
            constant = deserialize_field(constant_bytes.begin()); 
            
            if (constant == nullptr) {
                
                LogPrint("zendoo_mc_cryptolib",
                        "%s():%d - failed to deserialize \"constant\": %s \n", 
                        __func__, __LINE__, ToString(zendoo_get_last_error()));
                return false;
            }
        }

        //Initialize quality and proofdata
        quality = scCert.quality;
        proofdata = nullptr; //Note: For now proofdata is not present in WCert

        //Deserialize proof
        auto sc_proof_bytes = scCert.scProof;

        sc_proof = deserialize_sc_proof(sc_proof_bytes.begin());

        if(sc_proof == nullptr) {

            LogPrint("zendoo_mc_cryptolib",
                "%s():%d - failed to deserialize \"sc_proof\": %s \n", 
                __func__, __LINE__, ToString(zendoo_get_last_error()));
            return false;

        }

        //Deserialize sc_vk
        auto wCertVkBytes = scInfo.creationData.wCertVk;
        sc_vk = deserialize_sc_vk(wCertVkBytes.begin());

        if (sc_vk == nullptr){

            LogPrint("zendoo_mc_cryptolib",
                "%s():%d - failed to deserialize \"wCertVk\": %s \n", 
                __func__, __LINE__, ToString(zendoo_get_last_error()));
            return false;
        }

        //Retrieve MC block hashes
        {
            //LOCK(cs_main); TODO: Is LOCK needed here ?
            end_epoch_mc_b_hash = scCert.endEpochBlockHash.begin();
            int targetHeight = scInfo.StartHeightForEpoch(scCert.epochNumber) - 1;
            prev_end_epoch_mc_b_hash = (chainActive[targetHeight] -> GetBlockHash()).begin();
        }

        //Retrieve BT list
        std::vector<backward_transfer_t> btList;
        for (auto out : scCert.GetVout()){
            if (out.isFromBackwardTransfer){
                CBackwardTransferOut btout(out);
                backward_transfer bt;

                std::copy(btout.pubKeyHash.begin(), btout.pubKeyHash.end(), std::begin(bt.pk_dest));
                bt.amount = btout.nValue;

                btList.push_back(bt);
            }
        }
        bt_list = btList;
        return true;
    }

    bool CScWCertProofVerificationParameters::verifierCall() const {
        if (!verify_sc_proof(end_epoch_mc_b_hash, prev_end_epoch_mc_b_hash, bt_list.data(),
                             bt_list.size(), quality, constant, proofdata, sc_proof, sc_vk))
        {
            Error err = zendoo_get_last_error();
            if (err.category == CRYPTO_ERROR){ // Proof verification returned false due to an error, we must log it
                LogPrint("zendoo_mc_cryptolib",
                "%s():%d - failed to verify \"sc_proof\": %s \n", 
                __func__, __LINE__, ToString(err));
            }
            return false;
        }
        return true;
    }

    void CScWCertProofVerificationParameters::freeParameters() {
        
        end_epoch_mc_b_hash = nullptr;
        prev_end_epoch_mc_b_hash = nullptr;

        zendoo_field_free(constant);
        constant = nullptr;

        zendoo_field_free(proofdata);
        proofdata = nullptr;

        zendoo_sc_proof_free(sc_proof);
        sc_proof = nullptr;

        zendoo_sc_vk_free(sc_vk);
        sc_vk = nullptr;

        zendoo_clear_error();
    }

    bool CScProofVerifier::verifyCScCertificate(const CSidechain& scInfo, const CScCertificate& scCert) const {
        return CScWCertProofVerificationParameters(scInfo, scCert).run(perform_verification);
    }
}