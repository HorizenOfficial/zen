#include "sc/proofverifier.h"
#include "primitives/certificate.h"
#include "main.h"

namespace libzendoomc{

    void CProofVerificationContext::setNull(){
        free(const_cast<unsigned char*>(this->end_epoch_mc_b_hash));
        end_epoch_mc_b_hash = nullptr;

        free(const_cast<unsigned char*>(this->prev_end_epoch_mc_b_hash));
        prev_end_epoch_mc_b_hash = nullptr;

        free(const_cast<backward_transfer_t*>(this->bt_list));
        bt_list = nullptr;
        bt_list_len = -1;
        
        quality = 0;

        zendoo_field_free(const_cast<field_t*>(this->constant));
        constant = nullptr;

        zendoo_field_free(const_cast<field_t*>(this->proofdata));
        proofdata = nullptr;

        zendoo_sc_proof_free(const_cast<sc_proof_t*>(this->sc_proof));
        sc_proof = nullptr;

        zendoo_sc_vk_free(const_cast<sc_vk_t*>(this->sc_vk));
        sc_vk = nullptr;
    }

    bool CProofVerificationContext::isNull() const {
        return (end_epoch_mc_b_hash == nullptr &&
        prev_end_epoch_mc_b_hash == nullptr &&
        bt_list == nullptr &&
        bt_list_len == -1 &&
        quality == 0 &&
        constant == nullptr &&
        proofdata == nullptr &&
        sc_proof == nullptr &&
        sc_vk == nullptr );
    }

    bool CWCertVerifyFunction::checkInputsParameters(const CProofVerificationContext& params) const {
        return (params.end_epoch_mc_b_hash != nullptr &&
        params.prev_end_epoch_mc_b_hash != nullptr &&
        params.bt_list != nullptr &&
        params.bt_list_len >= 0 && //TODO: Is it possible to have a certificate with 0 BTs ?
        params.quality >= 0 && //TODO: gt or ge ?
        params.sc_proof != nullptr &&
        params.sc_vk != nullptr );
    }

    bool CProofVerifier::loadData(const CSidechain& scInfo, const CScCertificate& cert) {
        //Reset current parameters
        ctx.setNull();

        //Deserialize needed field elements, proof and vk.
        auto constant = zendoo_deserialize_field(scInfo.creationData.customData.data());
        auto sc_proof = zendoo_deserialize_sc_proof(cert.scProof.data());
        auto sc_vk = zendoo_deserialize_sc_vk_from_file((path_char_t*)"", 0);

        //if(constant == nullptr || sc_proof == nullptr || sc_vk == nullptr)
        if(constant == nullptr || sc_proof == nullptr)
            return false;

        //Retrieve MC block hashes
        uint256 currentEndEpochBlockHash = cert.endEpochBlockHash;
        ctx.end_epoch_mc_b_hash = currentEndEpochBlockHash.begin();
        
        //LOCK(cs_main); TODO: Is LOCK needed here ?
        int targetHeight = scInfo.StartHeightForEpoch(cert.epochNumber) - 1; 
        ctx.prev_end_epoch_mc_b_hash = (chainActive[targetHeight] -> GetBlockHash()).begin();

        //Retrieve BT list
        std::vector<backward_transfer_t> btList;
        int btListLen = 0;
        for (auto out : cert.GetVout()){
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

        ctx.bt_list = btList.data();
        ctx.bt_list_len = btListLen;
            
        // Set quality, constant and proof data
        ctx.quality = cert.quality;
        ctx.constant = constant;
        ctx.proofdata = nullptr;

        //Set proof and vk
        ctx.sc_proof = sc_proof;
        //ctx.sc_vk = sc_vk;

        return true;
    }
}