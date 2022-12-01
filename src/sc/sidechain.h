#ifndef _SIDECHAIN_CORE_H
#define _SIDECHAIN_CORE_H

#include "amount.h"
#include "sc/sidechaintypes.h"
#include <primitives/certificate.h>

class CValidationState;
class CTransaction;
class CCoinsViewCache;

namespace Sidechain
{
    static const boost::filesystem::path GetSidechainDataDir();
    bool InitDLogKeys();
    bool InitSidechainsFolder();
    void ClearSidechainsFolder();
    void LoadCumulativeProofsParameters();
};

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
private:
    int getInitScCoinsMaturity();
    int EpochFor(int targetHeight) const;

public:
    CSidechain():
        creationBlockHeight(-1), creationTxHash(),
        pastEpochTopQualityCertView(), lastTopQualityCertView(), lastTopQualityCertHash(),
        lastTopQualityCertReferencedEpoch(CScCertificate::EPOCH_NULL),
        lastTopQualityCertQuality(CScCertificate::QUALITY_NULL), lastTopQualityCertBwtAmount(0),
        balance(0), maxSizeOfScFeesContainers(-1),
        lastReferencedHeight(-1), lastInclusionHeight(-1) {}

    bool IsNull() const
    {
        return (
             creationBlockHeight == -1                                        &&
             creationTxHash.IsNull()                                          &&
             pastEpochTopQualityCertView.IsNull()                             &&
             lastTopQualityCertView.IsNull()                                  &&
             lastTopQualityCertHash.IsNull()                                  &&
             lastTopQualityCertReferencedEpoch == CScCertificate::EPOCH_NULL  &&
             lastTopQualityCertQuality == CScCertificate::QUALITY_NULL        &&
             lastTopQualityCertBwtAmount == 0                                 &&
             balance == 0                                                     &&
             fixedParams.IsNull()                                             &&
             mImmatureAmounts.empty()                                         &&
             scFees.empty());
    }

    // We can not serialize a pointer value to block index, but can retrieve it from chainActive if we have height
    int creationBlockHeight;

    // hash of the tx who created it
    uint256 creationTxHash;

    // Certificate view section
    CScCertificateView pastEpochTopQualityCertView;
    CScCertificateView lastTopQualityCertView;

    // Data for latest top quality cert confirmed in blockchain
    uint256 lastTopQualityCertHash;
    int32_t lastTopQualityCertReferencedEpoch;
    int64_t lastTopQualityCertQuality;
    CAmount lastTopQualityCertBwtAmount;

    int lastReferencedHeight;
    int lastInclusionHeight;

    // total amount given by sum(fw transfer)-sum(bkw transfer)
    CAmount balance;

    // creation data
    Sidechain::ScFixedParameters fixedParams;

    // immature amounts
    // key   = height at which amount will be considered as mature and will be part of the sc balance
    // value = the immature amount
    std::map<int, CAmount> mImmatureAmounts;

    // memory only
    int maxSizeOfScFeesContainers;
    // the last ftScFee and mbtrScFee values, as set by the active certificates
    // it behaves like a circular buffer once the max size is reached
    std::list<std::shared_ptr<Sidechain::ScFeeData>> scFees;

    // compute the max size of the sc fee list
    int getMaxSizeOfScFeesContainers();

    // returns the chain param value with the number of blocks to consider for sc fee check logic
    int getNumBlocksForScFeeCheck();

    enum class State : uint8_t {
        NOT_APPLICABLE = 0,
        UNCONFIRMED,
        ALIVE,
        CEASED
    };

    State GetState(const CCoinsViewCache& view) const;
    int getScCoinsMaturity();
    const CScCertificateView& GetActiveCertView(const CCoinsViewCache& view) const;
    int GetCurrentEpoch(const CCoinsViewCache& view) const;
    bool CheckQuality(const CScCertificate& cert) const;
    bool CheckCertTiming(int certEpoch, int referencedHeight, const CCoinsViewCache& view) const;

    static std::string stateToString(State s);

    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        /*
            This is some reserved space that at beginning was intended to be used for storing the sidechain version.
            However, since Zendoo was released without handling the version (in particular in CTxScCreationOut), such field
            has been introduced later with fork 9 and "extracted" from the first byte of the withdrawalEpochLength inside
            ScFixedParameters.
            The initial "sidechainVersion" variable has then been removed to avoid confusion and inconsistencies and its
            space in the serialization is held by this dummy/reserved variable.
        */
        uint32_t reserved = 0;

        READWRITE(reserved);
        READWRITE(VARINT(creationBlockHeight));
        READWRITE(creationTxHash);
        READWRITE(pastEpochTopQualityCertView);
        READWRITE(lastTopQualityCertView);
        READWRITE(lastTopQualityCertHash);
        READWRITE(lastTopQualityCertReferencedEpoch);
        READWRITE(lastTopQualityCertQuality);
        READWRITE(lastTopQualityCertBwtAmount);
        READWRITE(balance);
        READWRITE(fixedParams);
        READWRITE(mImmatureAmounts);

        if (ser_action.ForRead())
        {
            maxSizeOfScFeesContainers = getMaxSizeOfScFeesContainers();
        }

        if (isNonCeasing()) {
            READWRITE_POLYMORPHIC(scFees, Sidechain::ScFeeData, Sidechain::ScFeeData_v2);
            READWRITE(VARINT(lastInclusionHeight)); // not really needed for ceasing sc
            READWRITE(VARINT(lastReferencedHeight));
        }
        else {
            READWRITE_POLYMORPHIC(scFees, Sidechain::ScFeeData, Sidechain::ScFeeData);
        }
    }

    inline bool operator==(const CSidechain& rhs) const
    {
        return (this->creationBlockHeight                        == rhs.creationBlockHeight)               &&
               (this->creationTxHash                             == rhs.creationTxHash)                    &&
               (this->pastEpochTopQualityCertView                == rhs.pastEpochTopQualityCertView)       &&
               (this->lastTopQualityCertView                     == rhs.lastTopQualityCertView)            &&
               (this->lastTopQualityCertHash                     == rhs.lastTopQualityCertHash)            &&
               (this->lastTopQualityCertReferencedEpoch          == rhs.lastTopQualityCertReferencedEpoch) &&
               (this->lastTopQualityCertQuality                  == rhs.lastTopQualityCertQuality)         &&
               (this->lastTopQualityCertBwtAmount                == rhs.lastTopQualityCertBwtAmount)       &&
               (this->balance                                    == rhs.balance)                           &&
               (this->fixedParams                                == rhs.fixedParams)                       &&
               (this->mImmatureAmounts                           == rhs.mImmatureAmounts)                  &&
               (this->scFees                                     == rhs.scFees);
    }
    inline bool operator!=(const CSidechain& rhs) const { return !(*this == rhs); }

    int GetStartHeightForEpoch(int targetEpoch) const;
    int GetEndHeightForEpoch(int targetEpoch) const;
    int GetCertSubmissionWindowStart(int certEpoch) const;
    int GetCertSubmissionWindowEnd(int certEpoch) const;
    int GetCertSubmissionWindowLength() const;
    int GetCertMaturityHeight(int certEpoch, int includingBlockHeight) const;
    int GetScheduledCeasingHeight() const;
    bool GetCeasingCumTreeHash(CFieldElement& ceasedBlockCum) const;

    bool isCreationConfirmed() const {
        return creationBlockHeight != -1;
    }

    static bool isNonCeasingSidechain(int version, int withdrawalEpochLength) {
        // SC v2 can be either ceasing (as v0 and v1), or non-ceasing.
        // Non ceasing sidechains are identified by fixedParams.withdrawalEpochLength == 0, which is not allowed for
        // other sidechain versions.
        return version == 2 && withdrawalEpochLength == 0;
    }

    bool isNonCeasing() const {
        return isNonCeasingSidechain(fixedParams.version, fixedParams.withdrawalEpochLength);
    }

    void InitScFees();
    void UpdateScFees(const CScCertificateView& certView, int blockHeight);
    void DumpScFees() const;

    CAmount GetMinFtScFee() const;
    CAmount GetMinMbtrScFee() const;

    // Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const;
};

namespace Sidechain {
    bool checkCertCustomFields(const CSidechain& sidechain, const CScCertificate& cert);
    bool checkCertSemanticValidity(const CScCertificate& cert, CValidationState& state);
    bool checkTxSemanticValidity(const CTransaction& tx, CValidationState& state);
}; // end of namespace

#endif // _SIDECHAIN_CORE_H

