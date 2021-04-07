#ifndef _SIDECHAIN_TYPES_H
#define _SIDECHAIN_TYPES_H

#include <vector>
#include <string>

#include <boost/unordered_map.hpp>
#include <boost/variant.hpp>
#include <boost/optional.hpp>
#include <boost/variant.hpp>

#include <zendoo/zendoo_mc.h>
#include <zendoo/error.h>

#include "uint256.h"
#include "hash.h"
#include "script/script.h"
#include "amount.h"
#include "serialize.h"
///////////////////////////////// Field types //////////////////////////////////

struct CFieldPtrDeleter
{ // deleter
    CFieldPtrDeleter() = default;
    void operator()(field_t* p) const {
        zendoo_field_free(p);
        p = nullptr;
    };
};
typedef std::shared_ptr<field_t> wrappedFieldPtr;

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

    wrappedFieldPtr GetFieldElement() const;

    bool IsValid() const;
    // equality is not tested on deserializedField attribute since it is a ptr to memory specific per instance
    friend inline bool operator==(const CFieldElement& lhs, const CFieldElement& rhs) { return lhs.byteVector == rhs.byteVector; }
    friend inline bool operator!=(const CFieldElement& lhs, const CFieldElement& rhs) { return !(lhs == rhs); }
    friend inline bool operator<(const CFieldElement& lhs, const CFieldElement& rhs)  { return lhs.byteVector < rhs.byteVector; } // FOR STD::MAP ONLY

    // SERIALIZATION SECTION
    size_t GetSerializeSize(int nType, int nVersion) const //ADAPTED FROM SERIALIZED.H
    {
        return 1 + CFieldElement::ByteSize(); //byte for size + byteArray content (each element a single byte)
    };

    template<typename Stream>
    void Serialize(Stream& os, int nType, int nVersion) const //ADAPTED FROM SERIALIZE.H
    {
        char tmp = static_cast<char>(byteVector.size());
           os.write(&tmp, 1);
        if (!byteVector.empty())
            os.write((char*)&byteVector[0], byteVector.size());
    }

    template<typename Stream> //ADAPTED FROM SERIALIZED.H
    void Unserialize(Stream& is, int nType, int nVersion) //ADAPTED FROM SERIALIZE.H
    {
        byteVector.clear();

        char tmp {0};
        is.read(&tmp, 1);
        unsigned int nSize = static_cast<unsigned int>(tmp);
        if (nSize != 0 && nSize != CFieldElement::ByteSize())
            throw std::ios_base::failure("non-canonical CSidechainField size");

        if (nSize != 0)
        {
            byteVector.resize(nSize);
            is.read((char*)&byteVector[0], nSize);
        }
    }

    std::string GetHexRepr() const;
    static CFieldElement ComputeHash(const CFieldElement& lhs, const CFieldElement& rhs);
    static const CFieldElement& GetPhantomHash();

private:
    std::vector<unsigned char> byteVector;
    static CFieldPtrDeleter theFieldPtrDeleter;
};

typedef CFieldElement ScConstant;
////////////////////////////// End of Field types //////////////////////////////

/////////////////////// libzendoomc namespace definitions //////////////////////
namespace libzendoomc {
    typedef base_blob<SC_PROOF_SIZE * 8> ScProof;

    /* Check if scProof is a valid zendoo-mc-cryptolib's sc_proof */
    bool IsValidScProof(const ScProof& scProof);

    typedef base_blob<SC_VK_SIZE * 8> ScVk;

    /* Check if scVk is a valid zendoo-mc-cryptolib's sc_vk */
    bool IsValidScVk(const ScVk& scVk);

    /* Convert to std::string a zendoo-mc-cryptolib Error. Useful for logging */
    std::string ToString(Error err);
}
/////////////////// end of libzendoomc namespace definitions ///////////////////

////////////////////////////// Custom Config types //////////////////////////////
class CustomCertificateFieldConfig
{
public:
    CustomCertificateFieldConfig() = default;
    virtual ~CustomCertificateFieldConfig() = default;
    virtual bool IsValid() const = 0;
};

class FieldElementCertificateFieldConfig : public CustomCertificateFieldConfig
{
private:
    int32_t nBits;

public:
    FieldElementCertificateFieldConfig(int32_t nBitsIn);
    FieldElementCertificateFieldConfig(const FieldElementCertificateFieldConfig& rhs) = default;
    ~FieldElementCertificateFieldConfig() = default;

    //For serialization only, which requires the default ctor. No checkValid call here
    FieldElementCertificateFieldConfig(): CustomCertificateFieldConfig(), nBits(0) {}

    bool IsValid() const override final;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBits);
    }

    int32_t getBitSize() const;

    bool operator==(const FieldElementCertificateFieldConfig& rhs) const {
        return (this->nBits == rhs.nBits);
    }

    bool operator!=(const FieldElementCertificateFieldConfig& rhs) const {
        return !(*this == rhs);
    }

    friend std::ostream& operator<<(std::ostream& os, const FieldElementCertificateFieldConfig& r) {
        os << r.nBits;
        return os;
    }
};

class BitVectorCertificateFieldConfig : public CustomCertificateFieldConfig
{
private:
    int32_t bitVectorSizeBits;
    int32_t maxCompressedSizeBytes;

public:
    BitVectorCertificateFieldConfig(int32_t bitVectorSizeBits, int32_t maxCompressedSizeBytes);
    ~BitVectorCertificateFieldConfig() = default;

    //for serialization only, which requires the default ctor. No checkValid call here
    BitVectorCertificateFieldConfig(): CustomCertificateFieldConfig(), bitVectorSizeBits(-1), maxCompressedSizeBytes(-1) {}

    static const int32_t MAX_BIT_VECTOR_SIZE_BITS = 1000760;
    static const int32_t MAX_COMPRESSED_SIZE_BYTES = MAX_BIT_VECTOR_SIZE_BITS / 8;

    bool IsValid() const override final;

    int32_t getBitVectorSizeBits() const {
        return bitVectorSizeBits;
    }

    int32_t getMaxCompressedSizeBytes() const {
        return maxCompressedSizeBytes;
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(bitVectorSizeBits);
        READWRITE(maxCompressedSizeBytes);
    }

    bool operator==(const BitVectorCertificateFieldConfig& rhs) const {
        return (this->bitVectorSizeBits == rhs.bitVectorSizeBits) &&
               (this->maxCompressedSizeBytes == rhs.maxCompressedSizeBytes);
    }

    bool operator!=(const BitVectorCertificateFieldConfig& rhs) const {
        return !(*this == rhs);
    }

    friend std::ostream& operator<<(std::ostream& os, const BitVectorCertificateFieldConfig& r) {
        os << "["<<r.bitVectorSizeBits<<","<<r.maxCompressedSizeBytes<<"],";
        return os;
    }
};
////////////////////////// End of Custom Config types //////////////////////////

////////////////////////////// Custom Field types //////////////////////////////
template <typename T> //TODO: T should extend CustomCertificateFieldConfig
class CustomCertificateField
{
protected:
    const std::vector<unsigned char> vRawData;
    enum class VALIDATION_STATE {NOT_INITIALIZED, INVALID, VALID};
    mutable VALIDATION_STATE state;

public:
    CustomCertificateField(): state(VALIDATION_STATE::NOT_INITIALIZED) {};
    CustomCertificateField(const std::vector<unsigned char>& rawBytes)
        :vRawData(rawBytes), state(VALIDATION_STATE::NOT_INITIALIZED) {};
    virtual ~CustomCertificateField() = default;

    const std::vector<unsigned char>& getVRawData() const { return vRawData; }
};

class FieldElementCertificateField : public CustomCertificateField<FieldElementCertificateFieldConfig>
{
private:
    mutable FieldElementCertificateFieldConfig* pReferenceCfg; //mutable needed since IsValid is const
    mutable CFieldElement fieldElement; // memory only, lazy-initialized
public:
    FieldElementCertificateField(): pReferenceCfg{nullptr} {};
    FieldElementCertificateField(const std::vector<unsigned char>& rawBytes);
    FieldElementCertificateField(const FieldElementCertificateField& rhs);
    FieldElementCertificateField& operator=(const FieldElementCertificateField& rhs);
    ~FieldElementCertificateField() {delete pReferenceCfg; pReferenceCfg = nullptr; };

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*const_cast<std::vector<unsigned char>*>(&vRawData));
    }

    bool IsValid(const FieldElementCertificateFieldConfig& cfg) const;
    const CFieldElement& GetFieldElement(const FieldElementCertificateFieldConfig& cfg) const;
};

class BitVectorCertificateField : public CustomCertificateField<BitVectorCertificateFieldConfig>
{
private:
    mutable BitVectorCertificateFieldConfig* pReferenceCfg; //mutable needed since IsValid is const
    mutable CFieldElement fieldElement; // memory only, lazy-initialized
public:
    BitVectorCertificateField(): pReferenceCfg{nullptr} {};
    BitVectorCertificateField(const std::vector<unsigned char>& rawBytes);
    BitVectorCertificateField(const BitVectorCertificateField& rhs);
    BitVectorCertificateField& operator=(const BitVectorCertificateField& rhs);
    ~BitVectorCertificateField() {delete pReferenceCfg; pReferenceCfg = nullptr; };

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*const_cast<std::vector<unsigned char>*>(&vRawData));
    }

    bool IsValid(const BitVectorCertificateFieldConfig& cfg) const;
    const CFieldElement& GetFieldElement(const BitVectorCertificateFieldConfig& cfg) const;
};
////////////////////////// End of Custom Field types ///////////////////////////

namespace Sidechain
{
typedef boost::unordered_map<uint256, CAmount, ObjectHasher> ScAmountMap;

// useful in sc rpc command for getting genesis info
typedef struct sPowRelatedData_tag
{
    uint32_t a;
    uint32_t b;
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(a);
        READWRITE(b);
    }
} ScPowRelatedData;

struct ScCreationParameters
{
    int withdrawalEpochLength;
    // all creation data follows...
    std::vector<unsigned char> customData;
    boost::optional<CFieldElement> constant;
    libzendoomc::ScVk wCertVk;
    boost::optional<libzendoomc::ScVk> wCeasedVk;
    std::vector<FieldElementCertificateFieldConfig> vFieldElementCertificateFieldConfig;
    std::vector<BitVectorCertificateFieldConfig> vBitVectorCertificateFieldConfig;
    CAmount forwardTransferScFee;
    CAmount mainchainBackwardTransferRequestScFee;
    size_t mainchainBackwardTransferRequestDataLength;

    bool IsNull() const
    {
        return (
            withdrawalEpochLength == -1           &&
            customData.empty()                    &&
            constant == boost::none     &&
            wCertVk.IsNull()                      &&
            wCeasedVk == boost::none              &&
            vFieldElementCertificateFieldConfig.empty() &&
            vBitVectorCertificateFieldConfig.empty() &&
            forwardTransferScFee == 0                  &&
            mainchainBackwardTransferRequestScFee == 0 &&
            mainchainBackwardTransferRequestDataLength == 0);
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(withdrawalEpochLength);
        READWRITE(customData);
        READWRITE(constant);
        READWRITE(wCertVk);
        READWRITE(wCeasedVk);
        READWRITE(vFieldElementCertificateFieldConfig);
        READWRITE(vBitVectorCertificateFieldConfig);
        READWRITE(forwardTransferScFee);
        READWRITE(mainchainBackwardTransferRequestScFee);
        READWRITE(mainchainBackwardTransferRequestDataLength);
    }
    ScCreationParameters() :withdrawalEpochLength(-1), forwardTransferScFee(0),
                            mainchainBackwardTransferRequestScFee(0),
                            mainchainBackwardTransferRequestDataLength(0) {}

    inline bool operator==(const ScCreationParameters& rhs) const
    {
        return (withdrawalEpochLength == rhs.withdrawalEpochLength) &&
               (customData == rhs.customData) &&
               (constant == rhs.constant) &&
               (wCertVk == rhs.wCertVk)  &&
               (wCeasedVk == rhs.wCeasedVk) &&
               (vFieldElementCertificateFieldConfig == rhs.vFieldElementCertificateFieldConfig) &&
               (vBitVectorCertificateFieldConfig == rhs.vBitVectorCertificateFieldConfig) &&
               (forwardTransferScFee == rhs.forwardTransferScFee) &&
               (mainchainBackwardTransferRequestScFee == rhs.mainchainBackwardTransferRequestScFee) &&
               (mainchainBackwardTransferRequestDataLength == rhs.mainchainBackwardTransferRequestDataLength);
    }
    inline bool operator!=(const ScCreationParameters& rhs) const { return !(*this == rhs); }
    inline ScCreationParameters& operator=(const ScCreationParameters& cp)
    {
        withdrawalEpochLength         = cp.withdrawalEpochLength;
        customData                    = cp.customData;
        constant                      = cp.constant;
        wCertVk                       = cp.wCertVk;
        wCeasedVk                     = cp.wCeasedVk;
        vFieldElementCertificateFieldConfig = cp.vFieldElementCertificateFieldConfig;
        vBitVectorCertificateFieldConfig   = cp.vBitVectorCertificateFieldConfig;
        forwardTransferScFee          = cp.forwardTransferScFee;
        mainchainBackwardTransferRequestScFee = cp.mainchainBackwardTransferRequestScFee;
        mainchainBackwardTransferRequestDataLength = cp.mainchainBackwardTransferRequestDataLength;
        return *this;
    }
};

struct ScBwtRequestParameters
{
    CAmount scFee;
    std::vector<CFieldElement> scRequestData;

    bool IsNull() const
    {
        return ( scFee == 0 && scRequestData.empty());
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(scFee);
        READWRITE(scRequestData);
    }
    ScBwtRequestParameters() :scFee(0) {}

    inline bool operator==(const ScBwtRequestParameters& rhs) const
    {
        return (scFee == rhs.scFee) &&
               (scRequestData == rhs.scRequestData); 
    }
    inline bool operator!=(const ScBwtRequestParameters& rhs) const { return !(*this == rhs); }
    inline ScBwtRequestParameters& operator=(const ScBwtRequestParameters& cp)
    {
        scFee = cp.scFee;
        scRequestData = cp.scRequestData;
        return *this;
    }
};

struct CRecipientCrossChainBase
{
    uint256 address;
    CAmount nValue;

    CRecipientCrossChainBase(): nValue(0) {};
    virtual ~CRecipientCrossChainBase() {}
    CAmount GetScValue() const { return nValue; }
};

struct CRecipientScCreation : public CRecipientCrossChainBase
{
    ScCreationParameters creationData;
};

struct CRecipientForwardTransfer : public CRecipientCrossChainBase
{
    uint256 scId;
};

struct CRecipientBwtRequest
{
    uint256 scId;
    uint160 mcDestinationAddress;
    ScBwtRequestParameters bwtRequestData;
    CRecipientBwtRequest(): bwtRequestData() {}
    CAmount GetScValue() const { return bwtRequestData.scFee; }
};

static const int MAX_SC_DATA_LEN = 1024;

}; // end of namespace

#endif // _SIDECHAIN_TYPES_H
