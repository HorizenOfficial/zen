#include "sc/proofverifier.h"
#include "primitives/certificate.h"

#include "main.h"

#include "util.h"

namespace libzendoomc{
          
    bool CScWCertProofVerificationParameters::createParameters() {
        //Deserialize constant
        auto constant_bytes = scInfo.creationData.customData;
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
                return false;

            }
        }

        //Deserialize sc_vk
        //TODO: Insert correct data after having built logic to handle vks
        sc_vk = deserialize_sc_vk_from_file((path_char_t*)"", 0);

        if (sc_vk == nullptr){

            LogPrint("zendoo_mc_cryptolib",
                "%s():%d - failed to deserialize \"sc_vk\" \n", 
                __func__, __LINE__);
            return false;
        }

        //Retrieve MC block hashes
        //LOCK(cs_main); TODO: Is LOCK needed here ?
        end_epoch_mc_b_hash = scCert.endEpochBlockHash.begin();
        int targetHeight = scInfo.StartHeightForEpoch(scCert.epochNumber) - 1;
        prev_end_epoch_mc_b_hash = (chainActive[targetHeight] -> GetBlockHash()).begin();

        //Retrieve BT list
        std::vector<backward_transfer_t> btList;
        for (auto out : scCert.GetVout()){
            if (out.isFromBackwardTransfer){
                CBackwardTransferOut btout(out);
                backward_transfer bt;

                //TODO: Find a better way
                for(int i = 0; i < 20; i++)
                    bt.pk_dest[i] = btout.pubKeyHash.begin()[i];

                bt.amount = btout.nValue;

                btList.push_back(bt);
            }
        }
        bt_list = btList;
        return true;
    }

    bool CScWCertProofVerificationParameters::verifierCall() const {
        return verify_sc_proof(
            end_epoch_mc_b_hash, prev_end_epoch_mc_b_hash, bt_list.data(),
            bt_list.size(), quality, constant, proofdata, sc_proof, sc_vk
        );
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
    }
}