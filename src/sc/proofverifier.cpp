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
        auto scProofDeserialized = zendoo_deserialize_sc_proof(scProof.begin());
        if (scProofDeserialized == nullptr)
            return false;
        zendoo_sc_proof_free(scProofDeserialized);
        return true;
    }

    bool IsValidScVk(const ScVk& scVk)
    {
        auto scVkDeserialized = zendoo_deserialize_sc_vk(scVk.begin());
        if (scVkDeserialized == nullptr)
            return false;
        zendoo_sc_vk_free(scVkDeserialized);
        return true;
    }

    bool IsValidScConstant(const ScConstant& scConstant)
    {
        auto scConstantDeserialized = zendoo_deserialize_field(scConstant.data());
        if (scConstantDeserialized == nullptr)
            return false;
        zendoo_field_free(scConstantDeserialized);
        return true;
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

#ifdef BITCOIN_TX
    bool CScWCertProofVerification::verifyScCert(
        const ScConstant& constant,
        const ScVk& wCertVk,
        const uint256& prev_end_epoch_block_hash,
        const CScCertificate& scCert) const {return true;}
#else
    bool CScWCertProofVerification::verifyScCert(                
        const ScConstant& constant,
        const ScVk& wCertVk,
        const uint256& prev_end_epoch_block_hash,
        const CScCertificate& scCert) const
    {
        // Collect verifier inputs

        WCertVerifierInputs inputs;

        //Deserialize constant
        if (constant.size() == 0) { //Constant can be optional
            inputs.constant = nullptr;
        } else {
            
            inputs.constant = deserialize_field(constant.data()); 
            
            if (inputs.constant == nullptr) {
                
                LogPrint("zendoo_mc_cryptolib",
                        "%s():%d - failed to deserialize \"constant\": %s \n", 
                        __func__, __LINE__, ToString(zendoo_get_last_error()));
                zendoo_clear_error();

                return false;
            }
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
            __func__, __LINE__, HexStr(constant));
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
#endif
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
        const CSidechainField& certDataHash,
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
        const CSidechainField& scUtxoId,
        const uint160& mcDestinationAddress,
        CAmount scFees,
        const libzendoomc::ScProof& scProof,
        const boost::optional<libzendoomc::ScVk>& wMbtrVk,
        const CSidechainField& certDataHash
    ) const
    {
        return true; //Currently mocked
    }
}


CSidechainField::CSidechainField()
{
    SetNull();
}

//UPON INTEGRATION OF POSEIDON HASH STUFF, THIS MUST DISAPPER
CSidechainField::CSidechainField(const uint256& sha256)
{
    byteArray.SetHex(sha256.GetHex());
}

uint256 CSidechainField::GetLegacyHashTO_BE_REMOVED() const
{
    std::vector<unsigned char> tmp(this->byteArray.begin(), this->byteArray.begin()+32);
    return uint256(tmp);
}

CSidechainField::CSidechainField(const std::vector<unsigned char>& _byteArray) :byteArray(_byteArray)
{
    assert(_byteArray.size() == sizeof(uint8_t)*SC_FIELD_SIZE);
}

void CSidechainField::SetNull()
{
    byteArray.SetNull();
}

bool CSidechainField::IsNull() const { return byteArray.IsNull(); }

std::vector<unsigned char>  CSidechainField::GetByteArray() const
{
    return std::vector<unsigned char>{byteArray.begin(), byteArray.end()};
}
void CSidechainField::SetByteArray(const std::vector<unsigned char>& _byteArray)
{
    *this = CSidechainField(_byteArray);
}

unsigned int CSidechainField::ByteSize() { return SC_FIELD_SIZE; }

std::string CSidechainField::GetHex() const   {return byteArray.GetHex();}
std::string CSidechainField::ToString() const {return byteArray.ToString();}


bool CSidechainField::IsValid(const CSidechainField& scField)
{
    auto scFieldElementDeserialized = zendoo_deserialize_field(scField.byteArray.begin());
    if (scFieldElementDeserialized == nullptr)
        return false;
    zendoo_field_free(scFieldElementDeserialized);
    return true;
}

#ifdef BITCOIN_TX
CSidechainField CSidechainField::ComputeHash(const CSidechainField& lhs, const CSidechainField& rhs)
{
    return CSidechainField{};
}
#else
CSidechainField CSidechainField::ComputeHash(const CSidechainField& lhs, const CSidechainField& rhs)
{
    zendoo_clear_error();

    field_t* lhsFe = zendoo_deserialize_field(lhs.byteArray.begin());
    if (lhsFe == nullptr) {
        LogPrintf("%s():%d - failed to deserialize: %s \n", __func__, __LINE__, libzendoomc::ToString(zendoo_get_last_error()));
        zendoo_clear_error();
        throw std::runtime_error("Could not compute poseidon hash");
    }

    field_t* rhsFe = zendoo_deserialize_field(rhs.byteArray.begin());
    if (rhsFe == nullptr) {
        LogPrintf("%s():%d - failed to deserialize: %s \n", __func__, __LINE__, libzendoomc::ToString(zendoo_get_last_error()));
        zendoo_clear_error();
        zendoo_field_free(lhsFe);
        throw std::runtime_error("Could not compute poseidon hash");
    }

    const field_t* inputArrayFe[] = {lhsFe, rhsFe};

    field_t* outFe = zendoo_compute_poseidon_hash(inputArrayFe, 2);

    CSidechainField res;
    zendoo_serialize_field(outFe, res.byteArray.begin());

    zendoo_field_free(lhsFe);
    zendoo_field_free(rhsFe);
    zendoo_field_free(outFe);
    return res;
}
#endif
