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

typedef struct sCreationParameters_tag
{
    int withdrawalEpochLength; 
    // all creation data follows...
    // TODO

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(withdrawalEpochLength);
    }
    sCreationParameters_tag() :withdrawalEpochLength(-1) {}
} ScCreationParameters;

struct CRecipientCrossChainBase
{
    uint256 scId;

    virtual ~CRecipientCrossChainBase() {}
};

struct CRecipientScCreation : public CRecipientCrossChainBase
{
    ScCreationParameters creationData;
};

struct CRecipientCertLock : public CRecipientCrossChainBase
{
    uint256 address;
    CAmount nValue;
    int64_t epoch;
    CRecipientCertLock() : nValue(0), epoch(-1) {}
};

struct CRecipientForwardTransfer : public CRecipientCrossChainBase
{
    uint256 address;
    CAmount nValue;
    explicit CRecipientForwardTransfer(const CTxForwardTransferOut&);
    CRecipientForwardTransfer(): nValue(0) {};
};

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

}; // end of namespace

#endif // _SIDECHAIN_TYPES_H
