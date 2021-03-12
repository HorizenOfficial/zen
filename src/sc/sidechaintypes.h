#ifndef _SIDECHAIN_TYPES_H
#define _SIDECHAIN_TYPES_H

#include <vector>
#include <string>

#include "uint256.h"
#include "hash.h"
#include "script/script.h"
#include "amount.h"
#include "serialize.h"
#include <boost/unordered_map.hpp>
#include <boost/variant.hpp>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include<sc/proofverifier.h>

///////////////////////////////// Field types //////////////////////////////////
class CFieldElement
{
public:
	CFieldElement();
    ~CFieldElement() = default;

    explicit CFieldElement(const std::vector<unsigned char>& byteArrayIn);
    explicit CFieldElement(const uint256& value); // MIND RETROCOMPATIBILITY WITH RESERVED HASH BEFORE SIDECHAIN HARD FORK
    void SetByteArray(const std::vector<unsigned char>& byteArrayIn);

    CFieldElement(const CFieldElement& rhs) = default;
    CFieldElement& operator=(const CFieldElement& rhs) = default;

    void SetNull();
    bool IsNull() const;

    static constexpr unsigned int ByteSize() { return SC_FIELD_SIZE; }
    static constexpr unsigned int BitSize() { return ByteSize()*8; }
    const std::array<unsigned char, SC_FIELD_SIZE>&  GetByteArray() const; //apparently cannot call ByteSize() HERE
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
    std::array<unsigned char, SC_FIELD_SIZE> byteArray; //apparently cannot call ByteSize() HERE
};

class CPoseidonHash
{
public:
    CPoseidonHash ()  {SetNull();};
    explicit CPoseidonHash (const uint256& sha256): innerHash(sha256) {} //UPON INTEGRATION OF POSEIDON HASH STUFF, THIS MUST DISAPPER
    ~CPoseidonHash () = default;

    void SetNull() { innerHash.SetNull(); }
    friend inline bool operator==(const CPoseidonHash & lhs, const CPoseidonHash & rhs) { return lhs.innerHash == rhs.innerHash; }
    friend inline bool operator!=(const CPoseidonHash & lhs, const CPoseidonHash & rhs) { return !(lhs == rhs); }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(innerHash);
    }

    std::string GetHex() const   {return innerHash.GetHex();}
    std::string ToString() const {return innerHash.ToString();}

private:
    uint256 innerHash; //Temporary, for backward compatibility with beta
};
////////////////////////////// End of Field types //////////////////////////////

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

    static const int32_t MAX_BIT_VECTOR_SIZE_BITS = 1000192;
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
    	os << r.maxCompressedSizeBytes<<" "<<r.maxCompressedSizeBytes;
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
    mutable CFieldElement fieldElement; // memory only, lazy-initialized

public:
    CustomCertificateField(): state(VALIDATION_STATE::NOT_INITIALIZED) {};
    CustomCertificateField(const std::vector<unsigned char>& rawBytes)
        :vRawData(rawBytes), state(VALIDATION_STATE::NOT_INITIALIZED) {};
    virtual ~CustomCertificateField() = default;
    virtual const CFieldElement& GetFieldElement() {
    	if(state != VALIDATION_STATE::VALID) {
    		throw std::exception();
    	}

    	return fieldElement;
    }

    const std::vector<unsigned char>& getVRawData() const { return vRawData; }
};

class FieldElementCertificateField : public CustomCertificateField<FieldElementCertificateFieldConfig>
{
private:
	mutable FieldElementCertificateFieldConfig* pReferenceCfg; //mutable needed since IsValid is const
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
};

class BitVectorCertificateField : public CustomCertificateField<BitVectorCertificateFieldConfig>
{
private:
	mutable BitVectorCertificateFieldConfig* pReferenceCfg; //mutable needed since IsValid is const
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
    libzendoomc::ScConstant constant;
    libzendoomc::ScVk wCertVk;
    boost::optional<libzendoomc::ScVk> wMbtrVk;
    boost::optional<libzendoomc::ScVk> wCeasedVk;
    std::vector<FieldElementCertificateFieldConfig> vFieldElementCertificateFieldConfig;
    std::vector<BitVectorCertificateFieldConfig> vBitVectorCertificateFieldConfig;

    bool IsNull() const
    {
        return (
            withdrawalEpochLength == -1           &&
            customData.empty()                    &&
            constant.empty( )                     &&
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
    libzendoomc::ScFieldElement scUtxoId;
    libzendoomc::ScProof scProof;

    bool IsNull() const
    {
        return ( scFee == 0 && scUtxoId.IsNull() && scProof.IsNull());
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(scFee);
        READWRITE(scUtxoId);
        READWRITE(scProof);
    }
    ScBwtRequestParameters() :scFee(0) {}

    inline bool operator==(const ScBwtRequestParameters& rhs) const
    {
        return (scFee == rhs.scFee) &&
               (scUtxoId == rhs.scUtxoId) &&
               (scProof == rhs.scProof); 
    }
    inline bool operator!=(const ScBwtRequestParameters& rhs) const { return !(*this == rhs); }
    inline ScBwtRequestParameters& operator=(const ScBwtRequestParameters& cp)
    {
        scFee = cp.scFee;
        scUtxoId = cp.scUtxoId;
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
