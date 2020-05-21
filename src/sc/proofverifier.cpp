#include "sc/proofverifier.h"
#include "sc/TEMP_zendooError.h"
#include "primitives/certificate.h"

#include "main.h"

#include "util.h"
#include "sync.h"
#include "tinyformat.h"

#include <fstream>

namespace libzendoomc{

    bool SaveScVkToFile(const boost::filesystem::path& vkPath, const ScVk& scVk) {

        try
        {
            std::ofstream os (vkPath.string(), std::ios_base::out|std::ios::binary);
            os.exceptions(std::ios_base::failbit | std::ios_base::badbit);

            std::copy(scVk.cbegin(), scVk.cend(), std::ostream_iterator<unsigned char>(os));

            os.flush();
            os.close();
        } catch (std::ios_base::failure& e) 
        {
            return error(strprintf("SaveScVkToFile(): error writing to file: %s", e.what()).data());
        }

        return true;
    }

    bool LoadScVkFromFile(const boost::filesystem::path& vkPath, ScVk& scVk){

        try
        {
            std::ifstream is (vkPath.string(), std::ifstream::binary);
            is.exceptions(std::ios_base::failbit | std::ios_base::badbit);
            std::copy(std::istream_iterator<unsigned char>(is), std::istream_iterator<unsigned char>(), std::back_inserter(scVk));
            is.close();
        } catch (std::ios_base::failure& e) 
        {
            return error(strprintf("LoadScVkFromFile(): error reading from file: %s", e.what()).data());
        }

        return true;
    }
          
    bool CScWCertProofVerificationParameters::createParameters() {
        //Deserialize constant
        auto constant_bytes = scInfo.creationData.constant;
        if (constant_bytes.size() == 0){ //Constant can be optional
           
            constant = nullptr;
       
        } else if (constant_bytes.size() != zendoo_get_field_size_in_bytes()){ //For now constant must be just a single field element
            
            LogPrint("zendoo_mc_cryptolib",
                    "%s():%d - failed to deserialize \"constant\": expected vector of size: %d, found vector of size %d instead \n", 
                    __func__, __LINE__, constant_bytes.size(), zendoo_get_field_size_in_bytes());
            return false;

        } else { 
            
            constant = deserialize_field(constant_bytes.data()); 
            
            if (constant == nullptr) {
                
                LogPrint("zendoo_mc_cryptolib",
                        "%s():%d - failed to deserialize \"constant\" \n", 
                        __func__, __LINE__);
                print_error("Failed to deserialize \"constant\"");
                return false;
            }
        }

        //Initialize quality and proofdata
        quality = scCert.quality;
        proofdata = nullptr; //Note: For now proofdata is not present in WCert

        //Deserialize proof
        auto sc_proof_bytes = scCert.scProof;
        if (sc_proof_bytes.size() != zendoo_get_sc_proof_size()){

            LogPrint("zendoo_mc_cryptolib",
                "%s():%d - failed to deserialize \"sc_proof\": expected vector of size: %d, found vector of size %d instead \n", 
                __func__, __LINE__, sc_proof_bytes.size(), zendoo_get_sc_proof_size());
            return false;

        } else {

            sc_proof = deserialize_sc_proof(sc_proof_bytes.data());

            if(sc_proof == nullptr) {

                LogPrint("zendoo_mc_cryptolib",
                    "%s():%d - failed to deserialize \"sc_proof\" \n", 
                    __func__, __LINE__);
                print_error("Failed to deserialize \"sc_proof\"");
                return false;

            }
        }

        //Deserialize sc_vk
        {
            auto wCertVkPath = scInfo.vksPaths.wCertVkPath;
            sc_vk = deserialize_sc_vk_from_file(reinterpret_cast<const path_char_t*>(wCertVkPath.c_str()), wCertVkPath.size());

            if (sc_vk == nullptr){

                LogPrint("zendoo_mc_cryptolib",
                    "%s():%d - failed to deserialize \"sc_vk\" \n", 
                    __func__, __LINE__);
                print_error("Failed to deserialize \"sc_vk\"");
                return false;
            }
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
                "%s():%d - failed to verify \"sc_proof\" \n", 
                __func__, __LINE__);
                print_error("Failed to verify sc_proof");
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
}