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
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include<sc/proofverifier.h>


////////////////////////////// Custom Config types //////////////////////////////
class CustomFieldConfig
{
public:
    CustomFieldConfig() = default;
    virtual ~CustomFieldConfig() = default;
    virtual int32_t getBitSize() const = 0;
};

class FieldElementConfig : public CustomFieldConfig
{
private:
    int32_t nBits;

public:
    FieldElementConfig(int32_t nBitsIn);

    FieldElementConfig(): CustomFieldConfig(), nBits(0) {} //for serialization only, which requires the default ctor
    ~FieldElementConfig() = default;

    int32_t getBitSize() const override;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBits);

        if (nBits <= 0)
            throw std::invalid_argument("FieldElementConfig size must be strictly positive");
    }

    bool operator==(const FieldElementConfig& rhs) const {
        return (this->nBits == rhs.nBits);
    }

    bool operator!=(const FieldElementConfig& rhs) const {
        return !(*this == rhs);
    }

    friend std::ostream& operator<<(std::ostream& os, const FieldElementConfig& r) {
        os << r.nBits;
        return os;
    }
};

class CompressedMerkleTreeConfig : public CustomFieldConfig
{
private:
    int32_t treeHeight;

    static const int32_t MAX_TREE_HEIGHT = log2(std::numeric_limits<int32_t>::max()/2);

public:
    CompressedMerkleTreeConfig(int32_t treeHeightIn);
    CompressedMerkleTreeConfig(): CustomFieldConfig(), treeHeight(-1) {} //for serialization only, which requires the default ctor
    ~CompressedMerkleTreeConfig() = default;

    int32_t getBitSize() const override;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(treeHeight);

        if (treeHeight <= 0)
            throw std::invalid_argument(
                std::string("CompressedMerkleTreeConfig height must be strictly positive: ") + std::to_string(treeHeight));

        if (treeHeight >= MAX_TREE_HEIGHT)
            throw std::invalid_argument(
                std::string("CompressedMerkleTreeConfig height too large: ") + std::to_string(treeHeight) +
                std::string(", Max: ") + std::to_string(MAX_TREE_HEIGHT));
    }

    bool operator==(const CompressedMerkleTreeConfig& rhs) const {
        return (this->treeHeight == rhs.treeHeight);
    }

    bool operator!=(const CompressedMerkleTreeConfig& rhs) const {
        return !(*this == rhs);
    }

    friend std::ostream& operator<<(std::ostream& os, const CompressedMerkleTreeConfig& r) {
        os << r.treeHeight;
        return os;
    }
};
////////////////////////// End of Custom Config types //////////////////////////

////////////////////////////// Custom Field types //////////////////////////////
class CustomField
{
protected:
    const std::vector<unsigned char> vRawField;

public:
    CustomField(const CustomFieldConfig& cfg): vRawField(cfg.getBitSize()) {};
    CustomField(const std::vector<unsigned char>& rawBytes): vRawField(rawBytes) {};
    virtual ~CustomField() = default;
    virtual void InitFieldElement() const = 0;
    virtual const libzendoomc::ScFieldElement& GetFieldElement() const = 0;

    virtual bool IsValid() const = 0;
    virtual bool checkCfg(const CustomFieldConfig& cfg) const = 0;
};

class FieldElement : public CustomField
{
private:
    libzendoomc::ScFieldElement scFieldElement; // memory only, lazy-initialized by GetFieldElement call

public:
    FieldElement(const FieldElementConfig& cfg = FieldElementConfig()); //default ctor for serialization and containers only
    FieldElement(const std::vector<unsigned char>& rawBytes);
    FieldElement(const FieldElement& rhs) = default;
    FieldElement& operator=(const FieldElement& rhs);
    ~FieldElement() = default;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*const_cast<std::vector<unsigned char>*>(&vRawField));
    }

    void InitFieldElement() const override;
    const libzendoomc::ScFieldElement& GetFieldElement() const override;
    bool IsValid() const override;
    bool checkCfg(const CustomFieldConfig& cfg) const override;
};

class CompressedMerkleTree : public CustomField
{
private:
    libzendoomc::ScFieldElement merkleRoot; // memory only, lazy-initialized by GetFieldElement call

public:
    CompressedMerkleTree(const CompressedMerkleTreeConfig& cfg = CompressedMerkleTreeConfig()); //default ctor for serialization and containers only
    CompressedMerkleTree(const std::vector<unsigned char>& rawBytes);
    ~CompressedMerkleTree() = default;
    CompressedMerkleTree(const CompressedMerkleTree& rhs) = default;
    CompressedMerkleTree& operator=(const CompressedMerkleTree& rhs);

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*const_cast<std::vector<unsigned char>*>(&vRawField));
    }

    void InitFieldElement() const override;
    const libzendoomc::ScFieldElement& GetFieldElement() const override;
    bool IsValid() const override;
    bool checkCfg(const CustomFieldConfig& cfg) const override;
};
////////////////////////// End of Custom Field types ///////////////////////////


class CFakePoseidonHash
{
public:
    CFakePoseidonHash()  {SetNull();};
    explicit CFakePoseidonHash(const uint256& sha256): innerHash(sha256) {} //UPON INTEGRATION OF POSEIDON HASH STUFF, THIS MUST DISAPPER
    ~CFakePoseidonHash() = default;

    void SetNull() { innerHash.SetNull(); }
    friend inline bool operator==(const CFakePoseidonHash& lhs, const CFakePoseidonHash& rhs) { return lhs.innerHash == rhs.innerHash; }
    friend inline bool operator!=(const CFakePoseidonHash& lhs, const CFakePoseidonHash& rhs) { return !(lhs == rhs); }

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
    std::vector<FieldElementConfig> vFieldElementConfig;
    std::vector<CompressedMerkleTreeConfig> vCompressedMerkleTreeConfig;

    bool IsNull() const
    {
        return (
            withdrawalEpochLength == -1        &&
            customData.empty()                 &&
            constant.empty( )                  &&
            wCertVk.IsNull()                   &&
            wMbtrVk == boost::none             &&
            vFieldElementConfig.empty()        &&
            vCompressedMerkleTreeConfig.empty() );
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(withdrawalEpochLength);
        READWRITE(customData);
        READWRITE(constant);
        READWRITE(wCertVk);
        READWRITE(wMbtrVk);
        READWRITE(vFieldElementConfig);
        READWRITE(vCompressedMerkleTreeConfig);
    }
    ScCreationParameters() :withdrawalEpochLength(-1) {}

    inline bool operator==(const ScCreationParameters& rhs) const
    {
        return (withdrawalEpochLength == rhs.withdrawalEpochLength) &&
               (customData == rhs.customData) &&
               (constant == rhs.constant) &&
               (wCertVk == rhs.wCertVk)  &&
               (wMbtrVk == rhs.wMbtrVk)  &&
               (vFieldElementConfig == rhs.vFieldElementConfig) &&
               (vCompressedMerkleTreeConfig == rhs.vCompressedMerkleTreeConfig);
    }
    inline bool operator!=(const ScCreationParameters& rhs) const { return !(*this == rhs); }
    inline ScCreationParameters& operator=(const ScCreationParameters& cp)
    {
        withdrawalEpochLength       = cp.withdrawalEpochLength;
        customData                  = cp.customData;
        constant                    = cp.constant;
        wCertVk                     = cp.wCertVk;
        wMbtrVk                     = cp.wMbtrVk;
        vFieldElementConfig         = cp.vFieldElementConfig;
        vCompressedMerkleTreeConfig = cp.vCompressedMerkleTreeConfig;
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
