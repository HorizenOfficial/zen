#ifndef _SIDECHAIN_CORE_H
#define _SIDECHAIN_CORE_H

#include "amount.h"
#include "hash.h"
#include "sc/sidechaintypes.h"
#include <primitives/certificate.h>

class CValidationState;
class CTransaction;

class CSidechainEvents {
public:
    CSidechainEvents(): sidechainEventsVersion(0), ceasingScs(), maturingScs() {};
    ~CSidechainEvents() = default;

    int32_t sidechainEventsVersion;
    std::set<uint256> ceasingScs;
    std::set<uint256> maturingScs;

    bool IsNull() const {return ceasingScs.empty() && maturingScs.empty();}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
    	READWRITE(sidechainEventsVersion);
        READWRITE(ceasingScs);
        READWRITE(maturingScs);
    }

    inline bool operator==(const CSidechainEvents& rhs) const {
        return (this->sidechainEventsVersion  == rhs.sidechainEventsVersion) &&
               (this->ceasingScs              == rhs.ceasingScs)             &&
               (this->maturingScs             == rhs.maturingScs);
    }

    inline bool operator!=(const CSidechainEvents& rhs) const { return !(*this == rhs); }

    // Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const;
};

class CSidechain {
public:
    CSidechain() : sidechainVersion(0), creationBlockHash(), creationBlockHeight(-1), creationTxHash(),
                   prevBlockTopQualityCertReferencedEpoch(CScCertificate::EPOCH_NULL),
                   prevBlockTopQualityCertHash(), prevBlockTopQualityCertQuality(CScCertificate::QUALITY_NULL),
                   prevBlockTopQualityCertBwtAmount(0), balance(0), currentState((uint8_t)State::NOT_APPLICABLE) {}

    bool IsNull() const
    {
        return (
             creationBlockHash.IsNull()                                           &&
             creationBlockHeight == -1                                            &&
             creationTxHash.IsNull()                                              &&
             prevBlockTopQualityCertReferencedEpoch == CScCertificate::EPOCH_NULL &&
             prevBlockTopQualityCertHash.IsNull()                                 &&
             prevBlockTopQualityCertQuality == CScCertificate::QUALITY_NULL       &&
             prevBlockTopQualityCertBwtAmount == 0 && balance == 0                &&
             creationData.IsNull()                                                &&
             mImmatureAmounts.empty()                                             &&
             currentState == (uint8_t)State::NOT_APPLICABLE);
    }

    int32_t sidechainVersion;

    // reference to the block containing the tx that created the side chain
    uint256 creationBlockHash;

    // We can not serialize a pointer value to block index, but can retrieve it from chainActive if we have height
    int creationBlockHeight;

    // hash of the tx who created it
    uint256 creationTxHash;

    // last epoch for which a certificate have been received
    int prevBlockTopQualityCertReferencedEpoch;

    // hash of the best quality certificate received for this sidechain
    uint256 prevBlockTopQualityCertHash;

    // quality of the certificate
    int64_t prevBlockTopQualityCertQuality;

    // total bwt amount of the certificate
    CAmount prevBlockTopQualityCertBwtAmount;

    // total amount given by sum(fw transfer)-sum(bkw transfer)
    CAmount balance;

    // creation data
    Sidechain::ScCreationParameters creationData;

    // immature amounts
    // key   = height at which amount will be considered as mature and will be part of the sc balance
    // value = the immature amount
    std::map<int, CAmount> mImmatureAmounts;

    enum class State : uint8_t {
        NOT_APPLICABLE = 0,
        MEMPOOL,
        ALIVE,
        CEASED
    };
    uint8_t currentState;

    static std::string stateToString(State s);
    static void SetVoidedCert(const uint256& certHash, bool flag, std::map<uint256, bool>* pVoidedCertsMap);

    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(sidechainVersion);
        READWRITE(creationBlockHash);
        READWRITE(creationBlockHeight);
        READWRITE(creationTxHash);
        READWRITE(prevBlockTopQualityCertReferencedEpoch);
        READWRITE(prevBlockTopQualityCertHash);
        READWRITE(prevBlockTopQualityCertQuality);
        READWRITE(prevBlockTopQualityCertBwtAmount);
        READWRITE(balance);
        READWRITE(creationData);
        READWRITE(mImmatureAmounts);
        READWRITE(currentState);
    }

    inline bool operator==(const CSidechain& rhs) const
    {
        return (this->sidechainVersion                        == rhs.sidechainVersion)                       &&
               (this->creationBlockHash                       == rhs.creationBlockHash)                      &&
               (this->creationBlockHeight                     == rhs.creationBlockHeight)                    &&
               (this->creationTxHash                          == rhs.creationTxHash)                         &&
               (this->prevBlockTopQualityCertReferencedEpoch  == rhs.prevBlockTopQualityCertReferencedEpoch) &&
               (this->prevBlockTopQualityCertHash             == rhs.prevBlockTopQualityCertHash)            &&
               (this->prevBlockTopQualityCertQuality          == rhs.prevBlockTopQualityCertQuality)         &&
               (this->prevBlockTopQualityCertBwtAmount        == rhs.prevBlockTopQualityCertBwtAmount)       &&
               (this->balance                                 == rhs.balance)                                &&
               (this->creationData                            == rhs.creationData)                           &&
               (this->mImmatureAmounts                        == rhs.mImmatureAmounts)                       &&
               (this->currentState                            == rhs.currentState);
    }
    inline bool operator!=(const CSidechain& rhs) const { return !(*this == rhs); }

    int EpochFor(int targetHeight) const;
    int StartHeightForEpoch(int targetEpoch) const;
    int SafeguardMargin() const;
    int GetCeasingHeight() const;

    // Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const;
};

namespace Sidechain {
    bool checkCertSemanticValidity(const CScCertificate& cert, CValidationState& state);
    bool checkTxSemanticValidity(const CTransaction& tx, CValidationState& state);
    bool hasScCreationOutput(const CTransaction& tx, const uint256& scId);
}; // end of namespace

#endif // _SIDECHAIN_CORE_H
