#ifndef _SIDECHAIN_TYPES_H
#define _SIDECHAIN_TYPES_H

#include <vector>

#include "uint256.h"
#include "hash.h"
#include "script/script.h"
#include "amount.h"
#include "serialize.h"
#include <boost/unordered_map.hpp>
#include <boost/variant.hpp>

//------------------------------------------------------------------------------------
class CTxForwardTransferOut;

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

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(withdrawalEpochLength);
        READWRITE(customData);
    }
    ScCreationParameters() :withdrawalEpochLength(-1) {}

    inline bool operator==(const ScCreationParameters& rhs) const
    {
        return (withdrawalEpochLength == rhs.withdrawalEpochLength) &&
               (customData == rhs.customData);
    }
    inline bool operator!=(const ScCreationParameters& rhs) const { return !(*this == rhs); }
    inline ScCreationParameters& operator=(const ScCreationParameters& cp)
    {
        withdrawalEpochLength = cp.withdrawalEpochLength;
        customData = cp.customData;
        return *this;
    }
};

struct CRecipientCrossChainBase
{
    uint256 scId;
    uint256 address;
    CAmount nValue;

    CRecipientCrossChainBase(): nValue(0) {};
    virtual ~CRecipientCrossChainBase() {}
};

struct CRecipientScCreation : public CRecipientCrossChainBase
{
    ScCreationParameters creationData;
};

struct CRecipientCertLock : public CRecipientCrossChainBase
{
    int64_t epoch;
    CRecipientCertLock() : epoch(-1) {}
};

typedef CRecipientCrossChainBase CRecipientForwardTransfer;

struct CRecipientBackwardTransfer
{
    CScript scriptPubKey;
    CAmount nValue;

    CRecipientBackwardTransfer(): nValue(0) {};
};

typedef boost::variant<
        CRecipientScCreation,
        CRecipientCertLock,
        CRecipientForwardTransfer,
        CRecipientBackwardTransfer
    > CcRecipientVariant;

static const int MAX_CUSTOM_DATA_LEN = 1024;
static const int MAX_CUSTOM_DATA_BITS = MAX_CUSTOM_DATA_LEN*8;

}; // end of namespace

#endif // _SIDECHAIN_TYPES_H
