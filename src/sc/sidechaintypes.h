#ifndef _SIDECHAIN_TYPES_H
#define _SIDECHAIN_TYPES_H

#include <vector>
#include <string>
#include <mutex>

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
#include "tinyformat.h"
#include "sync.h"

namespace Sidechain
{
    /**
     * The enumeration of available proving systems
     */
    enum class ProvingSystemType : uint8_t
    {
        Undefined,
        Darlin,
        CoboundaryMarlin
    };

    static const int MAX_SC_CUSTOM_DATA_LEN     = 1024;     /**< Maximum data length for custom data (optional attribute for sidechain creation) in bytes. */
    static const int MAX_SC_MBTR_DATA_LEN       = 16;       /**< Maximum number of field elements contained in a mainchain backward transfer request (optional attribute for sidechain creation). */

    static_assert(MAX_SC_MBTR_DATA_LEN < UINT8_MAX, "MAX_SC_MBTR_DATA_LEN must be lower than max uint8_t size!");

    static const int SC_FE_SIZE_IN_BYTES        = 32;
    static const int MAX_PROOF_PLUS_VK_SIZE     = 9*1024;
    static const int MAX_SC_PROOF_SIZE_IN_BYTES = MAX_PROOF_PLUS_VK_SIZE;
    static const int MAX_SC_VK_SIZE_IN_BYTES    = MAX_PROOF_PLUS_VK_SIZE;

    static const int SEGMENT_SIZE = 1 << 18;
}

class CZendooLowPrioThreadGuard
{
private:
    const bool _pause;
public:
    CZendooLowPrioThreadGuard(bool pauseThreads);
    ~CZendooLowPrioThreadGuard();

    CZendooLowPrioThreadGuard(const CZendooLowPrioThreadGuard&) = delete;
    CZendooLowPrioThreadGuard& operator=(const CZendooLowPrioThreadGuard&) = delete;
};

///////////////////////////////// CZendooBatchProofVerifierResult ////////////////////////////////
struct CZendooBatchProofVerifierResultPtrDeleter
{ // deleter
    CZendooBatchProofVerifierResultPtrDeleter() = default;
    void operator()(ZendooBatchProofVerifierResult* p) const;
};

class CZendooBatchProofVerifierResult
{
public:
    CZendooBatchProofVerifierResult() = default;
    explicit CZendooBatchProofVerifierResult(ZendooBatchProofVerifierResult* result);

    bool Result() const;
    std::vector<uint32_t> FailedProofs() const;

private:
    const std::unique_ptr<ZendooBatchProofVerifierResult, CZendooBatchProofVerifierResultPtrDeleter> resultPtr;
};
///////////////////////////// End of CZendooBatchProofVerifierResult /////////////////////////////

class CZendooCctpLibraryChecker
{
    public:
    // assert the size of exported types are as expected by comparing static const declarations in header
    // file and lib rust getters values
    static void CheckTypeSizes();
};

class CZendooCctpObject
{
public:
    CZendooCctpObject() = default;
    CZendooCctpObject& operator=(const CZendooCctpObject& obj);
    CZendooCctpObject(const CZendooCctpObject&);

    virtual ~CZendooCctpObject() = default;

    CZendooCctpObject(const std::vector<unsigned char>& byteArrayIn): byteVector(byteArrayIn) {}
    virtual void SetByteArray(const std::vector<unsigned char>& byteArrayIn) = 0; //Does custom-size check
    const std::vector<unsigned char>& GetByteArray() const;
    const unsigned char* const GetDataBuffer() const;
    int GetDataSize() const;

    void SetNull();
    bool IsNull() const;

    virtual bool IsValid() const = 0;

    std::string GetHexRepr() const;

protected:
    bool isBaseEqual(const CZendooCctpObject& rhs) const { return this->byteVector == rhs.byteVector; }

    mutable std::mutex _mutex;

    std::vector<unsigned char> byteVector;
};

///////////////////////////////// CFieldElement ////////////////////////////////
struct CFieldPtrDeleter
{ // deleter
    CFieldPtrDeleter() = default;
    void operator()(field_t* p) const;
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

    static constexpr unsigned int ByteSize() { return Sidechain::SC_FE_SIZE_IN_BYTES; }
    static constexpr unsigned int BitSize()  { return ByteSize()*8; }
    uint256 GetLegacyHash() const;

    wrappedFieldPtr GetFieldElement() const;
    bool IsValid() const override final;
    bool operator<(const CFieldElement& rhs)  const { return this->byteVector < rhs.byteVector; } // FOR STD::MAP ONLY

    // do not check wrapped ptr
    bool operator==(const CFieldElement& rhs) const { return isBaseEqual(rhs); }
    bool operator!=(const CFieldElement& rhs) const { return !(*this == rhs); }

    static CFieldElement ComputeHash(const CFieldElement& lhs, const CFieldElement& rhs);
    static const CFieldElement& GetPhantomHash();

    // SERIALIZATION SECTION
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(byteVector);
    }

    mutable wrappedFieldPtr fieldData;

    // shared_ptr reference count, mainly for UT
    long getUseCount() const { return fieldData.use_count(); }

private:
    static CFieldPtrDeleter theFieldPtrDeleter;
};

typedef CFieldElement ScConstant;
///////////////////////////// End of CFieldElement /////////////////////////////

/////////////////////////////////// CScProof ///////////////////////////////////
struct CProofPtrDeleter
{ // deleter
    CProofPtrDeleter() = default;
    void operator()(sc_proof_t* p) const;
};
typedef std::shared_ptr<sc_proof_t> wrappedScProofPtr;

class CScProof : public CZendooCctpObject
{
public:
    CScProof() = default;
    ~CScProof() = default;

    /**< The type of proving system.*/
    Sidechain::ProvingSystemType getProvingSystemType() const;

    explicit CScProof(const std::vector<unsigned char>& byteArrayIn);
    void SetByteArray(const std::vector<unsigned char>& byteArrayIn) override final;

    static constexpr unsigned int MaxByteSize() { return Sidechain::MAX_SC_PROOF_SIZE_IN_BYTES; }

    wrappedScProofPtr GetProofPtr() const;
    bool IsValid() const override final;

    // do not check wrapped ptr
    bool operator==(const CScProof& rhs) const { return isBaseEqual(rhs); }
    bool operator!=(const CScProof& rhs) const { return !(*this == rhs); }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(byteVector);
    }

    mutable wrappedScProofPtr proofData;

    // shared_ptr reference count, mainly for UT
    long getUseCount() const { return proofData.use_count(); }

private:
    static CProofPtrDeleter theProofPtrDeleter;
};
//////////////////////////////// End of CScProof ///////////////////////////////

//////////////////////////////////// CScVKey ///////////////////////////////////
struct CVKeyPtrDeleter
{ // deleter
    CVKeyPtrDeleter() = default;
    void operator()(sc_vk_t* p) const;
};
typedef std::shared_ptr<sc_vk_t> wrappedScVkeyPtr;

class CScVKey : public CZendooCctpObject
{
public:
    CScVKey() = default;
    ~CScVKey() = default;

    /**< The type of proving system used for verifying proof.*/
    Sidechain::ProvingSystemType getProvingSystemType() const;

    CScVKey(const std::vector<unsigned char>& byteArrayIn);
    void SetByteArray(const std::vector<unsigned char>& byteArrayIn) override final;

    static constexpr unsigned int MaxByteSize() { return Sidechain::MAX_SC_VK_SIZE_IN_BYTES; }

    wrappedScVkeyPtr GetVKeyPtr() const;
    bool IsValid() const override final;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(byteVector);
    }

    // do not check wrapped ptr
    bool operator==(const CScVKey& rhs) const { return isBaseEqual(rhs) && getProvingSystemType() == rhs.getProvingSystemType(); }
    bool operator!=(const CScVKey& rhs) const { return !(*this == rhs); }

    mutable wrappedScVkeyPtr vkData;

    // shared_ptr reference count, mainly for UT
    long getUseCount() const { return vkData.use_count(); }
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
    uint8_t nBits;

public:
    FieldElementCertificateFieldConfig(uint8_t nBitsIn);
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

    uint8_t getBitSize() const;

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

    static const int32_t MAX_BIT_VECTOR_SIZE_BITS;
    static const int32_t MAX_COMPRESSED_SIZE_BYTES;
    static const int32_t SPARSE_VECTOR_COMPRESSION_OVERHEAD;

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
    mutable CFieldElement fieldElement; // memory only, lazy-initialized
    virtual const CFieldElement& GetFieldElement(const T& cfg, uint8_t sidechainVersion) const = 0;

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

    const CFieldElement& GetFieldElement(const FieldElementCertificateFieldConfig& cfg, uint8_t sidechainVersion) const override;
    bool IsValid(const FieldElementCertificateFieldConfig& cfg, uint8_t sidechainVersion) const;
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

    const CFieldElement& GetFieldElement(const BitVectorCertificateFieldConfig& cfg, uint8_t sidechainVersion) const override;
    bool IsValid(const BitVectorCertificateFieldConfig& cfg, uint8_t sidechainVersion) const;
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

// useful in checking SC fees
enum class ScFeeCheckFlag {
    LATEST_VALUE,
    MINIMUM_IN_A_RANGE
};

typedef struct sScFeeData_tag
{
    CAmount forwardTxScFee;
    CAmount mbtrTxScFee;
    sScFeeData_tag(): forwardTxScFee(0), mbtrTxScFee(0) {}
    sScFeeData_tag(CAmount f, CAmount m): forwardTxScFee(f), mbtrTxScFee(m) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(forwardTxScFee);
        READWRITE(mbtrTxScFee);
    }

    inline bool operator==(const sScFeeData_tag& rhs) const
    {
        return (forwardTxScFee == rhs.forwardTxScFee && mbtrTxScFee == rhs.mbtrTxScFee);
    }

} ScFeeData;

static const std::string PROVING_SYS_TYPE_COBOUNDARY_MARLIN = "CoboundaryMarlin";
static const std::string PROVING_SYS_TYPE_DARLIN            = "Darlin";
static const std::string PROVING_SYS_TYPE_UNDEFINED         = "Undefined";

std::string ProvingSystemTypeHelp();
bool IsValidProvingSystemType(uint8_t val);
bool IsValidProvingSystemType(ProvingSystemType val);
std::string ProvingSystemTypeToString(ProvingSystemType val);
ProvingSystemType StringToProvingSystemType(const std::string& str);
bool IsUndefinedProvingSystemType(const std::string& str);

struct ScFixedParameters
{
    uint8_t version;
    int withdrawalEpochLength;
    // all creation data follows...
    std::vector<unsigned char> customData;
    boost::optional<CFieldElement> constant;
    CScVKey wCertVk;
    boost::optional<CScVKey> wCeasedVk;
    std::vector<FieldElementCertificateFieldConfig> vFieldElementCertificateFieldConfig;
    std::vector<BitVectorCertificateFieldConfig> vBitVectorCertificateFieldConfig;
    uint8_t mainchainBackwardTransferRequestDataLength;             /**< The mandatory size of the field element included in MBTR transaction outputs (0 to disable the MBTR). */


    bool IsNull() const
    {
        return (
            version == 0xff                                           &&
            withdrawalEpochLength == -1                               &&
            customData.empty()                                        &&
            constant == boost::none                                   &&
            wCertVk.IsNull()                                          &&
            wCeasedVk == boost::none                                  &&
            vFieldElementCertificateFieldConfig.empty()               &&
            vBitVectorCertificateFieldConfig.empty()                  &&
            mainchainBackwardTransferRequestDataLength == 0
            );
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {

        // See CTxScCreationOut for details about the serialization of "version" field.
        if (ser_action.ForRead())
        {
            int withdrawalEpochLengthAndVersion;
            READWRITE(withdrawalEpochLengthAndVersion);
            
            // Get the most significant byte
            version = withdrawalEpochLengthAndVersion >> 24;

            // Get the least significant 3 bytes
            withdrawalEpochLength = withdrawalEpochLengthAndVersion & 0x00FFFFFF;
        }
        else
        {
            // Check any possible inconsistency that would overwrite the "version" byte
            assert(withdrawalEpochLength <= 0x00FFFFFF);

            int withdrawalEpochLengthAndVersion = (version << 24) | withdrawalEpochLength;
            READWRITE(withdrawalEpochLengthAndVersion);
        }

        READWRITE(customData);
        READWRITE(constant);
        READWRITE(wCertVk);
        READWRITE(wCeasedVk);
        READWRITE(vFieldElementCertificateFieldConfig);
        READWRITE(vBitVectorCertificateFieldConfig);
        READWRITE(mainchainBackwardTransferRequestDataLength);
    }

    ScFixedParameters(): version(0xff), withdrawalEpochLength(-1), mainchainBackwardTransferRequestDataLength(0)
    {}

    inline bool operator==(const ScFixedParameters& rhs) const
    {
        return (version == rhs.version)                                                                          &&
               (withdrawalEpochLength == rhs.withdrawalEpochLength)                                             &&
               (customData == rhs.customData)                                                                   &&
               (constant == rhs.constant)                                                                       &&
               (wCertVk == rhs.wCertVk)                                                                         &&
               (wCeasedVk == rhs.wCeasedVk)                                                                     &&
               (vFieldElementCertificateFieldConfig == rhs.vFieldElementCertificateFieldConfig)                 &&
               (vBitVectorCertificateFieldConfig == rhs.vBitVectorCertificateFieldConfig)                       &&
               (mainchainBackwardTransferRequestDataLength == rhs.mainchainBackwardTransferRequestDataLength);
    }
    inline bool operator!=(const ScFixedParameters& rhs) const { return !(*this == rhs); }
    inline ScFixedParameters& operator=(const ScFixedParameters& cp)
    {
        version                                     = cp.version;
        withdrawalEpochLength                       = cp.withdrawalEpochLength;
        customData                                  = cp.customData;
        constant                                    = cp.constant;
        wCertVk                                     = cp.wCertVk;
        wCeasedVk                                   = cp.wCeasedVk;
        vFieldElementCertificateFieldConfig         = cp.vFieldElementCertificateFieldConfig;
        vBitVectorCertificateFieldConfig            = cp.vBitVectorCertificateFieldConfig;
        mainchainBackwardTransferRequestDataLength  = cp.mainchainBackwardTransferRequestDataLength;
        return *this;
    }
};

struct ScBwtRequestParameters
{
    CAmount scFee;
    std::vector<CFieldElement> vScRequestData;

    bool IsNull() const
    {
        return ( scFee == 0 && vScRequestData.empty());
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(scFee);
        READWRITE(vScRequestData);
    }
    ScBwtRequestParameters() :scFee(0) {}

    inline bool operator==(const ScBwtRequestParameters& rhs) const
    {
        return (scFee == rhs.scFee) &&
               (vScRequestData == rhs.vScRequestData);
    }
    inline bool operator!=(const ScBwtRequestParameters& rhs) const { return !(*this == rhs); }
    inline ScBwtRequestParameters& operator=(const ScBwtRequestParameters& cp)
    {
        scFee = cp.scFee;
        vScRequestData = cp.vScRequestData;
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
    ScFixedParameters fixedParams;     /**< Fixed creation parameters */
    CAmount ftScFee;                    /**< Forward transfer sidechain fee */
    CAmount mbtrScFee;                  /**< Mainchain backward transfer request fee */

    CRecipientScCreation():
        ftScFee(-1),
        mbtrScFee(-1)
    {}
};

struct CRecipientForwardTransfer : public CRecipientCrossChainBase
{
    uint256 scId;
    uint160 mcReturnAddress;
};

struct CRecipientBwtRequest
{
    uint256 scId;
    uint160 mcDestinationAddress;
    ScBwtRequestParameters bwtRequestData;
    CRecipientBwtRequest(): bwtRequestData() {}
    CAmount GetScValue() const { return bwtRequestData.scFee; }
};

}; // end of namespace

void dumpBuffer(BufferWithSize* buf, const std::string& name);
void dumpBvCfg(BitVectorElementsConfig* buf, size_t len, const std::string& name);
void dumpFe(field_t* fe, const std::string& name);
void dumpFeArr(field_t** feArr, size_t len, const std::string& name);
void dumpBt(const backward_transfer_t& bt, const std::string& name);
void dumpBtArr(backward_transfer_t* buf, size_t len, const std::string& name);


#endif // _SIDECHAIN_TYPES_H
