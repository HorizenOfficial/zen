#ifndef _SIDECHAIN_CORE_H
#define _SIDECHAIN_CORE_H

#include "amount.h"
#include "chain.h"
#include "hash.h"
#include <boost/unordered_map.hpp>
#include "sync.h"

#include "sc/sidechaintypes.h"

//------------------------------------------------------------------------------------
class CTxMemPool;
class CBlockUndo;
class UniValue;
class CValidationState;
class CLevelDBWrapper;

class CCeasingSidechains {
public:
    CCeasingSidechains() = default;
    ~CCeasingSidechains() = default;

    std::set<uint256> ceasingScs;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(ceasingScs);
    }

    inline bool operator==(const CCeasingSidechains& rhs) const {
        return (this->ceasingScs                == rhs.ceasingScs);
    }

    inline bool operator!=(const CCeasingSidechains& rhs) const { return !(*this == rhs); }
};

class CSidechain {
public:
    CSidechain() : creationBlockHash(), creationBlockHeight(-1), creationTxHash(),
                   lastEpochReferencedByCertificate(CScCertificate::EPOCH_NULL),
                   lastCertificateHash(), balance(0) {}

    // reference to the block containing the tx that created the side chain
    uint256 creationBlockHash;

    // We can not serialize a pointer value to block index, but can retrieve it from chainActive if we have height
    int creationBlockHeight;

    // hash of the tx who created it
    uint256 creationTxHash;

    // last epoch for which a certificate have been received
    int lastEpochReferencedByCertificate;

    // hash of the last certificate received for this sidechain
    uint256 lastCertificateHash;

    // total amount given by sum(fw transfer)-sum(bkw transfer)
    CAmount balance;

    // creation data
    Sidechain::ScCreationParameters creationData;

    // immature amounts
    // key   = height at which amount will be considered as mature and will be part of the sc balance
    // value = the immature amount
    std::map<int, CAmount> mImmatureAmounts;

    enum class state {
        NOT_APPLICABLE = 0,
        ALIVE,
        CEASED
    };

    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(creationBlockHash);
        READWRITE(creationBlockHeight);
        READWRITE(creationTxHash);
        READWRITE(lastEpochReferencedByCertificate);
        READWRITE(lastCertificateHash);
        READWRITE(balance);
        READWRITE(creationData);
        READWRITE(mImmatureAmounts);
    }

    inline bool operator==(const CSidechain& rhs) const
    {
        return (this->creationBlockHash                == rhs.creationBlockHash)                &&
               (this->creationBlockHeight              == rhs.creationBlockHeight)              &&
               (this->creationTxHash                   == rhs.creationTxHash)                   &&
               (this->lastEpochReferencedByCertificate == rhs.lastEpochReferencedByCertificate) &&
               (this->lastCertificateHash              == rhs.lastCertificateHash)              &&
               (this->creationData                     == rhs.creationData)                     &&
               (this->mImmatureAmounts                 == rhs.mImmatureAmounts);
    }
    inline bool operator!=(const CSidechain& rhs) const { return !(*this == rhs); }

    int EpochFor(int targetHeight) const;
    int StartHeightForEpoch(int targetEpoch) const;
    int SafeguardMargin() const;
};

namespace Sidechain {
    bool checkTxSemanticValidity(const CTransaction& tx, CValidationState& state);
    bool anyForwardTransaction(const CTransaction& tx, const uint256& scId);
    bool hasScCreationOutput(const CTransaction& tx, const uint256& scId);

    bool checkCertSemanticValidity(const CScCertificate& cert, CValidationState& state);
}; // end of namespace

#endif // _SIDECHAIN_CORE_H
