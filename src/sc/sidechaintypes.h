#ifndef _SIDECHAIN_TYPES_H
#define _SIDECHAIN_TYPES_H

#include <vector>
#include <string>

#include <boost/unordered_map.hpp>
#include <boost/variant.hpp>
#include <boost/optional.hpp>
#include <boost/variant.hpp>

#include <zendoo/zendoo_mc.h>

#include "uint256.h"
#include "hash.h"
#include "script/script.h"
#include "amount.h"
#include "serialize.h"

class CZendooCctpObject
{
public:
	CZendooCctpObject() = default;
    virtual ~CZendooCctpObject() = default;

    CZendooCctpObject(const std::vector<unsigned char>& byteArrayIn): byteVector(byteArrayIn) {}
    virtual void SetByteArray(const std::vector<unsigned char>& byteArrayIn) = 0; //Does custom-size check
    const std::vector<unsigned char>& GetByteArray() const;

    void SetNull();
    bool IsNull() const;

    virtual bool IsValid() const = 0;
    bool operator==(const CZendooCctpObject& rhs) const { return this->byteVector == rhs.byteVector; }
    bool operator!=(const CZendooCctpObject& rhs) { return !(*this == rhs); }

    // SERIALIZATION SECTION
    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(byteVector);
        //Allow zero-length byteVector for, e.g. serialization/hashing of empty txes/certs
        if (byteVector.size() != 0 && byteVector.size() != SerializedSize())
            throw std::ios_base::failure("non-canonical zendoo cctp object size");
    }

    std::string GetHexRepr() const;
protected:
    virtual unsigned int SerializedSize() const = 0;
    std::vector<unsigned char> byteVector;
};

///////////////////////////////// CFieldElement ////////////////////////////////
struct CFieldPtrDeleter
{ // deleter
    CFieldPtrDeleter() = default;
    void operator()(field_t* p) const {
        zendoo_field_free(p);
        p = nullptr;
    };
};
typedef std::shared_ptr<field_t> wrappedFieldPtr;

class CFieldElement : public CZendooCctpObject
{
public:
    CFieldElement() = default;
    ~CFieldElement() = default;
    explicit CFieldElement(const std::vector<unsigned char>& byteArrayIn);
    void SetByteArray(const std::vector<unsigned char>& byteArrayIn) override final;

    explicit CFieldElement(const uint256& value); //Currently for backward compability with pre-sidechain fork blockHeader. To re-evaluate its necessity
    explicit CFieldElement(const wrappedFieldPtr& wrappedField);

    static constexpr unsigned int ByteSize() { return SC_FIELD_SIZE; }
    static constexpr unsigned int BitSize() { return ByteSize()*8; }
    uint256 GetLegacyHashTO_BE_REMOVED() const;

    wrappedFieldPtr GetFieldElement() const;
    bool IsValid() const override final;
    bool operator<(const CFieldElement& rhs)  const { return this->byteVector < rhs.byteVector; } // FOR STD::MAP ONLY

    static CFieldElement ComputeHash(const CFieldElement& lhs, const CFieldElement& rhs);

protected:
    unsigned int SerializedSize() const override final { return ByteSize(); }

private:
    static CFieldPtrDeleter theFieldPtrDeleter;
};

typedef CFieldElement ScConstant;
///////////////////////////// End of CFieldElement /////////////////////////////

/////////////////////////////////// CScProof ///////////////////////////////////
struct CProofPtrDeleter
{ // deleter
    CProofPtrDeleter() = default;
    void operator()(sc_proof_t* p) const {
        zendoo_sc_proof_free(p);
        p = nullptr;
    };
};
typedef std::shared_ptr<sc_proof_t> wrappedScProofPtr;

class CScProof : public CZendooCctpObject
{
public:
    CScProof() = default;
    ~CScProof() = default;
    explicit CScProof(const std::vector<unsigned char>& byteArrayIn);
    void SetByteArray(const std::vector<unsigned char>& byteArrayIn) override final;

    static constexpr unsigned int ByteSize() { return SC_PROOF_SIZE; }
    static constexpr unsigned int BitSize() { return ByteSize()*8; }

    wrappedScProofPtr GetProofPtr() const;
    bool IsValid() const override final;

protected:
    unsigned int SerializedSize() const override final { return ByteSize(); }

private:
    static CProofPtrDeleter theProofPtrDeleter;
};
//////////////////////////////// End of CScProof ///////////////////////////////

//////////////////////////////////// CScVKey ///////////////////////////////////
struct CVKeyPtrDeleter
{ // deleter
	CVKeyPtrDeleter() = default;
    void operator()(sc_vk_t* p) const {
    	zendoo_sc_vk_free(p);
        p = nullptr;
    };
};
typedef std::shared_ptr<sc_vk_t> wrappedScVkeyPtr;

class CScVKey : public CZendooCctpObject
{
public:
	CScVKey() = default;
    ~CScVKey() = default;

    explicit CScVKey(const std::vector<unsigned char>& byteArrayIn);
    void SetByteArray(const std::vector<unsigned char>& byteArrayIn) override final;

    static constexpr unsigned int ByteSize() { return SC_VK_SIZE; }
    static constexpr unsigned int BitSize() { return ByteSize()*8; }

    wrappedScVkeyPtr GetVKeyPtr() const;
    bool IsValid() const override final;

protected:
    unsigned int SerializedSize() const override final { return ByteSize(); }

private:
    static CVKeyPtrDeleter theVkPtrDeleter;
};
//////////////////////////////// End of CScVKey ////////////////////////////////

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
    CScVKey wCertVk;
    boost::optional<CScVKey> wMbtrVk;
    boost::optional<CScVKey> wCeasedVk;
    std::vector<FieldElementCertificateFieldConfig> vFieldElementCertificateFieldConfig;
    std::vector<BitVectorCertificateFieldConfig> vBitVectorCertificateFieldConfig;

    bool IsNull() const
    {
        return (
            withdrawalEpochLength == -1           &&
            customData.empty()                    &&
            constant == boost::none     &&
            wCertVk.IsNull()                      &&
            wMbtrVk == boost::none                &&
            wCeasedVk == boost::none              &&
            vFieldElementCertificateFieldConfig.empty() &&
            vBitVectorCertificateFieldConfig.empty() );
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(withdrawalEpochLength);
        READWRITE(customData);
        READWRITE(constant);
        READWRITE(wCertVk);
        READWRITE(wMbtrVk);
        READWRITE(wCeasedVk);
        READWRITE(vFieldElementCertificateFieldConfig);
        READWRITE(vBitVectorCertificateFieldConfig);
    }
    ScCreationParameters() :withdrawalEpochLength(-1) {}

    inline bool operator==(const ScCreationParameters& rhs) const
    {
        return (withdrawalEpochLength == rhs.withdrawalEpochLength) &&
               (customData == rhs.customData) &&
               (constant == rhs.constant) &&
               (wCertVk == rhs.wCertVk)  &&
               (wMbtrVk == rhs.wMbtrVk)  &&
               (wCeasedVk == rhs.wCeasedVk) &&
               (vFieldElementCertificateFieldConfig == rhs.vFieldElementCertificateFieldConfig) &&
               (vBitVectorCertificateFieldConfig == rhs.vBitVectorCertificateFieldConfig);
    }
    inline bool operator!=(const ScCreationParameters& rhs) const { return !(*this == rhs); }
    inline ScCreationParameters& operator=(const ScCreationParameters& cp)
    {
        withdrawalEpochLength         = cp.withdrawalEpochLength;
        customData                    = cp.customData;
        constant                      = cp.constant;
        wCertVk                       = cp.wCertVk;
        wMbtrVk                       = cp.wMbtrVk;
        wCeasedVk                     = cp.wCeasedVk;
        vFieldElementCertificateFieldConfig = cp.vFieldElementCertificateFieldConfig;
        vBitVectorCertificateFieldConfig   = cp.vBitVectorCertificateFieldConfig;
        return *this;
    }
};

struct ScBwtRequestParameters
{
    CAmount scFee;
    CFieldElement scRequestData;
    CScProof scProof;

    bool IsNull() const
    {
        return ( scFee == 0 && scRequestData.IsNull() && scProof.IsNull());
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(scFee);
        READWRITE(scRequestData);
        READWRITE(scProof);
    }
    ScBwtRequestParameters() :scFee(0) {}

    inline bool operator==(const ScBwtRequestParameters& rhs) const
    {
        return (scFee == rhs.scFee) &&
               (scRequestData == rhs.scRequestData) &&
               (scProof == rhs.scProof); 
    }
    inline bool operator!=(const ScBwtRequestParameters& rhs) const { return !(*this == rhs); }
    inline ScBwtRequestParameters& operator=(const ScBwtRequestParameters& cp)
    {
        scFee = cp.scFee;
        scRequestData = cp.scRequestData;
        scProof = cp.scProof;
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
