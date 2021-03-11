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
    static const int32_t MAX_BIT_VECTOR_SIZE_BITS = 1000192;
    static const int32_t MAX_COMPRESSED_SIZE_BYTES = MAX_BIT_VECTOR_SIZE_BITS / 8;
public:
    BitVectorCertificateFieldConfig(int32_t bitVectorSizeBits, int32_t maxCompressedSizeBytes);
    ~BitVectorCertificateFieldConfig() = default;

    //for serialization only, which requires the default ctor. No checkValid call here
    BitVectorCertificateFieldConfig(): CustomCertificateFieldConfig(), bitVectorSizeBits(-1), maxCompressedSizeBytes(-1) {}

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
    enum VALIDATION_STATE {NOT_INITIALIZED, INVALID, VALID};
    VALIDATION_STATE state;
    const libzendoomc::FieldElementWrapper fieldElement; // memory only, lazy-initialized by GetFieldElement call

public:
    CustomCertificateField(): state(NOT_INITIALIZED) {};
    CustomCertificateField(const std::vector<unsigned char>& rawBytes)
        :vRawData(rawBytes), state(NOT_INITIALIZED) {};
    virtual ~CustomCertificateField() = default;
    virtual const libzendoomc::FieldElementWrapper& GetFieldElement() {
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
	FieldElementCertificateFieldConfig* pReferenceCfg;
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
	BitVectorCertificateFieldConfig* pReferenceCfg;
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
