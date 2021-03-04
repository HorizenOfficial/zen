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

namespace libzendoomc {
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
};

class CSidechainField
{
public:
    CSidechainField();
    explicit CSidechainField(const uint256& sha256); //UPON INTEGRATION OF POSEIDON HASH STUFF, THIS MUST DISAPPER
    uint256 GetLegacyHashTO_BE_REMOVED() const;

    explicit CSidechainField(const std::vector<unsigned char>& _byteArray);
    ~CSidechainField() = default;

    void SetNull();
    bool IsNull() const;

    std::vector<unsigned char>  GetByteArray() const;
    void SetByteArray(const std::vector<unsigned char>& _byteArray);
    unsigned int size() const;

    /* Check if scFieldElement is a valid field, leveraging zendoo-mc-cryptolib' */
    static bool IsValid(const CSidechainField& scField);
    friend inline bool operator==(const CSidechainField& lhs, const CSidechainField& rhs) { return lhs.byteArray == rhs.byteArray; }
    friend inline bool operator!=(const CSidechainField& lhs, const CSidechainField& rhs) { return !(lhs == rhs); }
    friend inline bool operator<(const CSidechainField& lhs, const CSidechainField& rhs)  { return lhs.byteArray < rhs.byteArray; } // FOR STD::MAP ONLY

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        // substitute with real field element serialization
        READWRITE(byteArray);
    }

    std::string GetHex() const;
    std::string ToString() const;

    static CSidechainField ComputeHash(const CSidechainField& lhs, const CSidechainField& rhs);

private:
    base_blob<SC_FIELD_SIZE * 8> byteArray;
};

namespace libzendoomc {
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
                const CSidechainField& certDataHash,
                const ScVk& wCeasedVk,
                const CTxCeasedSidechainWithdrawalInput& csw
            ) const;

            bool verifyCBwtRequest(
                const uint256& scId,
                const CSidechainField& scUtxoId,
                const uint160& mcDestinationAddress,
                CAmount scFees,
                const libzendoomc::ScProof& scProof,
                const boost::optional<libzendoomc::ScVk>& wMbtrVk,
				const CSidechainField& certDataHash
            ) const;
    };
}

#endif // _SC_PROOF_VERIFIER_H
