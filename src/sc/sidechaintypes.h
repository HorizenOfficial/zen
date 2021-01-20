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

class CTxForwardTransferOut;

class FieldElementConfig
{
public:
    int32_t nBits;

    FieldElementConfig(int32_t nBitsIn):nBits(nBitsIn) {}
    FieldElementConfig():nBits(-1) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBits);
    }

    friend bool operator==(const FieldElementConfig& a, const FieldElementConfig& b)
    {
        return (a.nBits == b.nBits);
    }

    friend bool operator!=(const FieldElementConfig& a, const FieldElementConfig& b)
    {
        return !(a == b);
    }

    friend std::ostream& operator<<(std::ostream& os, const FieldElementConfig& r)  
    {  
        os << r.nBits;  
        return os;  
    }
};

class CompressedMerkleTreeConfig
{
public:
    int32_t treeHeight;

    CompressedMerkleTreeConfig(int32_t treeHeightIn):treeHeight(treeHeightIn) {}
    CompressedMerkleTreeConfig():treeHeight(-1) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(treeHeight);
    }

    friend bool operator==(const CompressedMerkleTreeConfig& a, const CompressedMerkleTreeConfig& b)
    {
        return (a.treeHeight == b.treeHeight);
    }

    friend bool operator!=(const CompressedMerkleTreeConfig& a, const CompressedMerkleTreeConfig& b)
    {
        return !(a == b);
    }

    friend std::ostream& operator<<(std::ostream& os, const CompressedMerkleTreeConfig& r)  
    {  
        os << r.treeHeight;  
        return os;  
    }
};

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
