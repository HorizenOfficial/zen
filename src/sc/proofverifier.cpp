#include "sc/proofverifier.h"
#include "sc/TEMP_zendooInterface.h"
#include "primitives/certificate.h"

#include "main.h"

#include "util.h"

namespace libzendoomc{

    bool CScProofVerifier::operator()(const CScCertificate& scCert) const{
        if(perform_verification){

            //Deserialize constant
            field_t* constant;
            auto constant_bytes = scInfo->creationData.customData;
            if (!constant_bytes.size() == 0){ //Constant can be optional

                constant = zendoo_deserialize_field(constant_bytes.data()); 
                if (constant_bytes.size() != zendoo_get_field_size_in_bytes() || //For now constant must be just a single field element
                    constant == nullptr) {

                    LogPrint("zendoo_mc_cryptolib",
                            "%s():%d - failed to deserialize \"constant\" from vector of size: %d \n", 
                            __func__, __LINE__, constant_bytes.size());
                    return false;
                }
            } else {
                constant = nullptr;
            }

            //Deserialize proof
            auto sc_proof_bytes = scCert.scProof;
            auto sc_proof = zendoo_deserialize_sc_proof(sc_proof_bytes.data());
            if (sc_proof_bytes.size() != zendoo_get_sc_proof_size() ||
                sc_proof == nullptr) {

                    LogPrint("zendoo_mc_cryptolib",
                        "%s():%d - failed to deserialize \"sc_proof\" from vector of size: %d \n", 
                        __func__, __LINE__, sc_proof_bytes.size());
                return false;
            }

            //Deserialize sc_vk
            //TODO: Insert correct data after having built logic to handle vks
            auto sc_vk = zendoo_deserialize_sc_vk_from_file((path_char_t*)"", 0);

            if (sc_vk == nullptr){

                LogPrint("zendoo_mc_cryptolib",
                    "%s():%d - failed to deserialize \"sc_vk\" \n", 
                    __func__, __LINE__);
                return false;
            }

            //Retrieve previous end epoch MC block hash
            //LOCK(cs_main); TODO: Is LOCK needed here ?
            int targetHeight = scInfo->StartHeightForEpoch(scCert.epochNumber) - 1; //Is this ok ? Can epochNumber be 0 or 1 ?
            auto prev_end_epoch_mc_b_hash = (chainActive[targetHeight] -> GetBlockHash()).begin();

            //Retrieve BT list
            std::vector<backward_transfer_t> btList;
            int btListLen = 0; //What if BTList is 0 in the certificate ?
            for (auto out : scCert.GetVout()){
                if (out.isFromBackwardTransfer){
                    CBackwardTransferOut btout(out);
                    backward_transfer bt;

                    //TODO: Find a better way
                    for(int i = 0; i < 20; i++)
                        bt.pk_dest[i] = btout.pubKeyHash.begin()[i];

                    bt.amount = btout.nValue;

                    btList.push_back(bt);
                    btListLen += 1;
                }
            }

            //Retrieve proofdata
            auto proofdata = nullptr; //Note: For now proofdata is not present in WCert

            return zendoo_verify_sc_proof(
                scCert.endEpochBlockHash.begin(), prev_end_epoch_mc_b_hash, btList.data(),
                btListLen, scCert.quality, constant, nullptr, sc_proof, sc_vk
            );
        } else {
            return true;
        }
    }
}