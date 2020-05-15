#include "sc/proofverifier.h"
#include "primitives/certificate.h"

#include "main.h"

#include "util.h"

namespace libzendoomc{
          
    bool CScWCertProofVerificationParameters::createParameters() {
        //Deserialize constant
        auto constant_bytes = scInfo.creationData.customData;
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

        //Initialize quality and proofdata
        quality = scCert.quality;
        proofdata = nullptr; //Note: For now proofdata is not present in WCert

        //Deserialize proof
        auto sc_proof_bytes = scCert.scProof;
        sc_proof = zendoo_deserialize_sc_proof(sc_proof_bytes.data());
        if (sc_proof_bytes.size() != zendoo_get_sc_proof_size() ||
            sc_proof == nullptr) {

                LogPrint("zendoo_mc_cryptolib",
                    "%s():%d - failed to deserialize \"sc_proof\" from vector of size: %d \n", 
                    __func__, __LINE__, sc_proof_bytes.size());
            return false;
        }

        //Deserialize sc_vk
        //TODO: Insert correct data after having built logic to handle vks
        sc_vk = zendoo_deserialize_sc_vk_from_file((path_char_t*)"", 0);

        if (sc_vk == nullptr){

            LogPrint("zendoo_mc_cryptolib",
                "%s():%d - failed to deserialize \"sc_vk\" \n", 
                __func__, __LINE__);
            return false;
        }

        //Retrieve MC block hashes
        //LOCK(cs_main); TODO: Is LOCK needed here ?
        end_epoch_mc_b_hash = scCert.endEpochBlockHash.begin();
        int targetHeight = scInfo.StartHeightForEpoch(scCert.epochNumber) - 1; //Is this ok ? Can epochNumber be 0 or 1 ?
        prev_end_epoch_mc_b_hash = (chainActive[targetHeight] -> GetBlockHash()).begin();

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
        bt_list = btList.data();
        bt_list_len = btListLen;
        return true;
    }

    bool CScWCertProofVerificationParameters::verifierCall() const {
        return zendoo_verify_sc_proof(
            end_epoch_mc_b_hash, prev_end_epoch_mc_b_hash, bt_list,
            bt_list_len, quality, constant, proofdata, sc_proof, sc_vk
        );
    }

    void CScWCertProofVerificationParameters::freeParameters() {
        free(const_cast<unsigned char*>(this->end_epoch_mc_b_hash));
        end_epoch_mc_b_hash = nullptr;

        free(const_cast<unsigned char*>(this->prev_end_epoch_mc_b_hash));
        prev_end_epoch_mc_b_hash = nullptr;

        free(const_cast<backward_transfer_t*>(this->bt_list));
        bt_list = nullptr;

        zendoo_field_free(const_cast<field_t*>(this->constant));
        constant = nullptr;

        zendoo_field_free(const_cast<field_t*>(this->proofdata));
        proofdata = nullptr;

        zendoo_sc_proof_free(const_cast<sc_proof_t*>(this->sc_proof));
        sc_proof = nullptr;

        zendoo_sc_vk_free(const_cast<sc_vk_t*>(this->sc_vk));
        sc_vk = nullptr;
    }
}