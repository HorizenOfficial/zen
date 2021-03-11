#ifndef _SC_PROOF_VERIFIER_H
#define _SC_PROOF_VERIFIER_H

#include <zendoo/error.h>
#include <zendoo/zendoo_mc.h>
#include "uint256.h"

#include <string>
#include <boost/variant.hpp>
#include <boost/filesystem.hpp>
#include <amount.h>

class CSidechain;
class CScCertificate;
class CTxCeasedSidechainWithdrawalInput;

class CFieldElement
{
public:
	CFieldElement();
    ~CFieldElement() = default;

    explicit CFieldElement(const std::vector<unsigned char>& byteArrayIn);
    explicit CFieldElement(const uint256& value);
    void SetByteArray(const std::vector<unsigned char>& byteArrayIn);

    CFieldElement(const CFieldElement& rhs) = default;
    CFieldElement& operator=(const CFieldElement& rhs) = default;

    void SetNull();
    bool IsNull() const;

    static constexpr unsigned int ByteSize() { return SC_FIELD_SIZE; }
    static constexpr unsigned int BitSize() { return ByteSize()*8; }
    const std::vector<unsigned char>&  GetByteArray() const;
    uint256 GetLegacyHashTO_BE_REMOVED() const;

    const field_t* const GetFieldElement() const;

    bool IsValid() const;
    // equality is not tested on deserializedField attribute since it is a ptr to memory specific per instance
    friend inline bool operator==(const CFieldElement& lhs, const CFieldElement& rhs) { return lhs.byteArray == rhs.byteArray; }
    friend inline bool operator!=(const CFieldElement& lhs, const CFieldElement& rhs) { return !(lhs == rhs); }
    friend inline bool operator<(const CFieldElement& lhs, const CFieldElement& rhs)  { return lhs.byteArray < rhs.byteArray; } // FOR STD::MAP ONLY

    // SERIALIZATION SECTION
    size_t GetSerializeSize(int nType, int nVersion) const //ADAPTED FROM SERIALIZED.H
    {
        return CFieldElement::ByteSize(); //byteArray content (each element a single byte)
    };

    template<typename Stream>
    void Serialize(Stream& os, int nType, int nVersion) const //ADAPTED FROM SERIALIZE.H
    {
		os.write((char*)&byteArray[0], CFieldElement::ByteSize());
    }

    template<typename Stream> //ADAPTED FROM SERIALIZED.H
    void Unserialize(Stream& is, int nType, int nVersion) //ADAPTED FROM SERIALIZE.H
    {
        is.read((char*)&byteArray[0], CFieldElement::ByteSize());
    }

    std::string GetHexRepr() const;
    static CFieldElement ComputeHash(const CFieldElement& lhs, const CFieldElement& rhs);

private:
    std::array<unsigned char, CFieldElement::ByteSize()> byteArray;
};

namespace libzendoomc{

	typedef base_blob<SC_FIELD_SIZE * 8> ScFieldElement;
    /* Check if scFieldElement is a valid field, leveraging zendoo-mc-cryptolib' */
    bool IsValidScFieldElement(const ScFieldElement& scFieldElement);

    typedef base_blob<SC_PROOF_SIZE * 8> ScProof;

    /* Check if scProof is a valid zendoo-mc-cryptolib's sc_proof */
    bool IsValidScProof(const ScProof& scProof);

    typedef base_blob<SC_VK_SIZE * 8> ScVk;
    
    /* Check if scVk is a valid zendoo-mc-cryptolib's sc_vk */
    bool IsValidScVk(const ScVk& scVk);

    typedef std::vector<unsigned char> ScConstant;
    
    /* Check if scConstant is a valid zendoo-mc-cryptolib's field */
    bool IsValidScConstant(const ScConstant& scConstant);

    /* Convert to std::string a zendoo-mc-cryptolib Error. Useful for logging */
    std::string ToString(Error err);

    /* Write scVk to file in vkPath. Returns true if operation succeeds, false otherwise. */
    bool SaveScVkToFile(const boost::filesystem::path& vkPath, const ScVk& scVk);

    /* Support class for WCert SNARK proof verification. */
    class CScWCertProofVerification {
        public:
            CScWCertProofVerification() = default;
            virtual ~CScWCertProofVerification() = default;

            // Returns false if proof verification has failed or deserialization of certificate's elements
            // into libzendoomc's elements has failed.
            bool verifyScCert(
                const ScConstant& constant,
                const ScVk& wCertVk,
                const uint256& prev_end_epoch_block_hash,
                const CScCertificate& scCert
            ) const;

        protected:
        
            /* 
             * Wrappers for function calls to zendoo-mc-cryptolib. Useful for testing purposes,
             * enabling to mock the behaviour of each function.
             */
            virtual field_t* deserialize_field(const unsigned char* field_bytes) const {
                return zendoo_deserialize_field(field_bytes);
            }

            virtual sc_proof_t* deserialize_sc_proof(const unsigned char* sc_proof_bytes) const {
                return zendoo_deserialize_sc_proof(sc_proof_bytes);
            }

            virtual sc_vk_t* deserialize_sc_vk(const unsigned char* sc_vk_bytes) const {
                return zendoo_deserialize_sc_vk(sc_vk_bytes);
            }

            virtual bool verify_sc_proof(
                const unsigned char* end_epoch_mc_b_hash,
                const unsigned char* prev_end_epoch_mc_b_hash,
                const backward_transfer_t* bt_list,
                size_t bt_list_len,
                int64_t quality,
                const field_t* constant,
                const field_t* proofdata,
                const sc_proof_t* sc_proof,
                const sc_vk_t* sc_vk
            ) const
            {
                return zendoo_verify_sc_proof(
                    end_epoch_mc_b_hash, prev_end_epoch_mc_b_hash, bt_list,
                    bt_list_len, quality, constant, proofdata, sc_proof, sc_vk
                );
            }
    };

    /* Class for instantiating a verifier able to verify different kind of ScProof for different kind of ScProof(s) */
    class CScProofVerifier {
        protected:
            bool perform_verification;

            CScProofVerifier(bool perform_verification): perform_verification(perform_verification) {}

        public:
            // CScProofVerifier should never be copied
            CScProofVerifier(const CScProofVerifier&) = delete;
            CScProofVerifier& operator=(const CScProofVerifier&) = delete;
            CScProofVerifier(CScProofVerifier&&);
            CScProofVerifier& operator=(CScProofVerifier&&);

            // Creates a verification context that strictly verifies all proofs using zendoo-mc-cryptolib's API.
            static CScProofVerifier Strict(){ return CScProofVerifier(true); }

            // Creates a verifier that performs no verification, used when avoiding duplicate effort
            // such as during reindexing.
            static CScProofVerifier Disabled() { return CScProofVerifier(false); }

            // Returns false if proof verification has failed or deserialization of certificate's elements
            // into libzendoomc's elements has failed.
            bool verifyCScCertificate(
                const ScConstant& constant,
                const ScVk& wCertVk,
                const uint256& prev_end_epoch_block_hash,
                const CScCertificate& scCert
            ) const;

            // Returns false if proof verification has failed or deserialization of CSW's elements
            // into libzendoomc's elements has failed.
            bool verifyCTxCeasedSidechainWithdrawalInput(
                const ScFieldElement& certDataHash,
                const ScVk& wCeasedVk,
                const CTxCeasedSidechainWithdrawalInput& csw
            ) const;

            bool verifyCBwtRequest(
                const uint256& scId,
                const libzendoomc::ScFieldElement& scUtxoId,
                const uint160& mcDestinationAddress,
                CAmount scFees,
                const libzendoomc::ScProof& scProof,
                const boost::optional<libzendoomc::ScVk>& wMbtrVk,
				const libzendoomc::ScFieldElement& certDataHash
            ) const;
    };
}

#endif // _SC_PROOF_VERIFIER_H
