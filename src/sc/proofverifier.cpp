#include "sc/proofverifier.h"
#include "primitives/certificate.h"
#include "main.h"

namespace libzendoomc{

    bool CScProofVerifier::verify(const CWCertProofVerificationContext& wCertCtx) const {
        if (perform_verification)
            return zendoo_verify_sc_proof(
                wCertCtx.params.end_epoch_mc_b_hash, wCertCtx.params.prev_end_epoch_mc_b_hash, 
                wCertCtx.params.bt_list, wCertCtx.params.bt_list_len, wCertCtx.params.quality, wCertCtx.params.constant,
                wCertCtx.params.proofdata, wCertCtx.params.sc_proof, wCertCtx.params.sc_vk
            );
        else 
            return true;
    }

    void CWCertProofVerificationContext::setNull(){
        params.end_epoch_mc_b_hash = nullptr;
        params.prev_end_epoch_mc_b_hash = nullptr;
        params.bt_list = nullptr;
        params.bt_list_len = -1;
        params.quality = CScCertificate::QUALITY_NULL;
        params.constant = nullptr;
        params.proofdata = nullptr;
        params.sc_proof = nullptr;
        params.sc_vk = nullptr;
    }

    void CWCertProofVerificationContext::reset() {
        free(const_cast<unsigned char*>(this->params.end_epoch_mc_b_hash));
        free(const_cast<unsigned char*>(this->params.prev_end_epoch_mc_b_hash));
        free(const_cast<backward_transfer_t*>(this->params.bt_list));
        zendoo_field_free(const_cast<field_t*>(this->params.constant));
        zendoo_field_free(const_cast<field_t*>(this->params.proofdata));
        zendoo_sc_proof_free(const_cast<sc_proof_t*>(this->params.sc_proof));
        zendoo_sc_vk_free(const_cast<sc_vk_t*>(this->params.sc_vk));
    }

    CWCertProofVerificationContext::~CWCertProofVerificationContext(){
        reset();
        setNull();
    }

    bool CWCertProofVerificationContext::checkParameters() const {
        return (params.end_epoch_mc_b_hash != nullptr &&
        params.prev_end_epoch_mc_b_hash != nullptr &&
        params.bt_list != nullptr &&
        params.bt_list_len >= 0 && //TODO: Is it possible to have a certificate with 0 BTs ?
        params.quality >= 0 &&
        params.sc_proof != nullptr &&
        params.sc_vk != nullptr );
    }

    bool CWCertProofVerificationContext::updateParameters(CSidechain& scInfo, CScCertificate& scCert) {
        //Reset current parameters
        reset();

        //Deserialize needed field elements, proof and vk.
        auto constant = zendoo_deserialize_field(scInfo.creationData.customData.data());
        auto sc_proof = zendoo_deserialize_sc_proof(scCert.scProof.data());
        //TODO: Logic for saving vk on file
        auto sc_vk = zendoo_deserialize_sc_vk_from_file((path_char_t*)"", 0);

        //Retrieve MC block hashes
        uint256 currentEndEpochBlockHash = scCert.endEpochBlockHash;
        params.end_epoch_mc_b_hash = currentEndEpochBlockHash.begin();
        
        //LOCK(cs_main); TODO: Is LOCK needed here ?
        int targetHeight = scInfo.StartHeightForEpoch(scCert.epochNumber) - 1; 
        params.prev_end_epoch_mc_b_hash = (chainActive[targetHeight] -> GetBlockHash()).begin();

        //Retrieve BT list
        std::vector<backward_transfer_t> btList;
        int btListLen = 0;
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

        params.bt_list = btList.data();
        params.bt_list_len = btListLen;
            
        // Set quality, constant and proof data
        params.quality = scCert.quality;
        params.constant = constant;
        params.proofdata = nullptr;

        //Set proof and vk
        params.sc_proof = sc_proof;
        params.sc_vk = sc_vk;

        return true;
    }

    bool CWCertProofVerificationContext::verify(const CScProofVerifier& verifier) const { 
        return verifier.verify(*this);
    }
}