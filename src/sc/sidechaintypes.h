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
namespace Sidechain
{
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

    inline bool operator==(const sCreationParameters_tag& rhs) const
    {
        return (this->withdrawalEpochLength == rhs.withdrawalEpochLength);
    }
    inline bool operator!=(const sCreationParameters_tag& rhs) const { return !(*this == rhs); }
} ScCreationParameters;

class ScInfo
{
public:
    ScInfo() : creationBlockHash(), creationBlockHeight(-1), creationTxHash(), balance(0) {}

    // reference to the block containing the tx that created the side chain
    uint256 creationBlockHash;

    // We can not serialize a pointer value to block index, but can retrieve it from chainActive if we have height
    int creationBlockHeight;

    // hash of the tx who created it
    uint256 creationTxHash;

    // total amount given by sum(fw transfer)-sum(bkw transfer)
    CAmount balance;

    // creation data
    ScCreationParameters creationData;

    // immature amounts
    // key   = height at which amount will be considered as mature and will be part of the sc balance
    // value = the immature amount
    std::map<int, CAmount> mImmatureAmounts;

    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(creationBlockHash);
        READWRITE(creationBlockHeight);
        READWRITE(creationTxHash);
        READWRITE(balance);
        READWRITE(creationData);
        READWRITE(mImmatureAmounts);
    }

    inline bool operator==(const ScInfo& rhs) const
    {
        return (this->creationBlockHash   == rhs.creationBlockHash)   &&
               (this->creationBlockHeight == rhs.creationBlockHeight) &&
               (this->creationTxHash      == rhs.creationTxHash)      &&
               (this->creationData        == rhs.creationData)        &&
               (this->mImmatureAmounts    == rhs.mImmatureAmounts);
    }
    inline bool operator!=(const ScInfo& rhs) const { return !(*this == rhs); }
};

struct CSidechainsCacheEntry
{
    ScInfo scInfo; // The actual cached data.

    enum class Flags {
        DEFAULT = 0,
        DIRTY   = (1 << 0), // This cache entry is potentially different from the version in the parent view.
        FRESH   = (1 << 1), // The parent view does not have this entry
        ERASED  = (1 << 2), // Flag in sidechain only, to be removed to conform what happens in CCoinsCacheEntry
    } flag;

    CSidechainsCacheEntry() : scInfo(), flag(Flags::DEFAULT) {}
    CSidechainsCacheEntry(const ScInfo & _scInfo, Flags _flag) : scInfo(_scInfo), flag(_flag) {}
};

typedef boost::unordered_map<uint256, CSidechainsCacheEntry, ObjectHasher> CSidechainsMap;

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
