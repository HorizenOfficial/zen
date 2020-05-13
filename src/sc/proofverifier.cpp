#include "sc/proofverifier.h"
#include "primitives/certificate.h"
#include "main.h"

namespace libzendoomc{

    bool CScProofVerifier::operator()(const CScCertificate& scCert) const{
        if(perform_verification){

            //Deserialize needed field elements, proof and vk.
            auto constant = zendoo_deserialize_field(scInfo->creationData.customData.data());
            auto sc_proof = zendoo_deserialize_sc_proof(scCert.scProof.data());
            //TODO: Logic for saving vk on file
            auto sc_vk = zendoo_deserialize_sc_vk_from_file((path_char_t*)"", 0);

            if (constant == nullptr || sc_proof == nullptr || sc_vk == nullptr)
                return false;

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

            return zendoo_verify_sc_proof(
                scCert.endEpochBlockHash.begin(), prev_end_epoch_mc_b_hash, btList.data(),
                btListLen, scCert.quality, constant, nullptr, sc_proof, sc_vk
            );
        } else {
            return true;
        }
    }
}