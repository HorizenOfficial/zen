#ifndef _CERTIFICATE_H
#define _CERTIFICATE_H

#include "transaction.h"
#include "policy/fees.h"

struct CMutableScCertificate;
class CTxBackwardTransferCrosschainOut;

class CScCertificate : virtual public CTransactionBase
{
    /** Memory only. */
    void UpdateHash() const override;

public:
    const uint256 scId;
    const CAmount totalAmount;
    const std::vector<CTxBackwardTransferCrosschainOut> vbt_ccout;

    const uint256 nonce;

    /** Construct a CScCertificate that qualifies as IsNull() */
    CScCertificate();

    /** Convert a CMutableScCertificate into a CScCertificate.  */
    CScCertificate(const CMutableScCertificate &tx);

    CScCertificate& operator=(const CScCertificate& tx);
    CScCertificate(const CScCertificate& tx);

    friend bool operator==(const CScCertificate& a, const CScCertificate& b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CScCertificate& a, const CScCertificate& b)
    {
        return a.hash != b.hash;
    }

    const uint256& GetHash() const { return hash; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*const_cast<int32_t*>(&this->nVersion));
        READWRITE(*const_cast<uint256*>(&scId));
        READWRITE(*const_cast<CAmount*>(&totalAmount));
        READWRITE(*const_cast<std::vector<CTxOut>*>(&vout));
        READWRITE(*const_cast<std::vector<CTxBackwardTransferCrosschainOut>*>(&vbt_ccout));
        READWRITE(*const_cast<uint256*>(&nonce));
        if (ser_action.ForRead())
            UpdateHash();
    }

    template <typename Stream>
    CScCertificate(deserialize_type, Stream& s) : CScCertificate(CMutableScCertificate(deserialize, s)) {}

    bool IsNull() const override {
        return (
            scId == uint256() &&
            totalAmount == 0 &&
            vout.empty() &&
            vbt_ccout.empty() &&
            nonce == uint256() );
    }

    CAmount GetFeeAmount(CAmount valueIn) const override;

    unsigned int CalculateSize() const override;
    unsigned int CalculateModifiedSize(unsigned int /* unused nTxSize*/) const override;

    std::string EncodeHex() const override;
    std::string ToString() const override;

    void RemoveFromMemPool(CTxMemPool* pool) const override; 

    bool AddUncheckedToMemPool(CTxMemPool* pool,
        const CAmount& nFee, int64_t nTime, double dPriority, int nHeight, bool poolHasNoInputsOf, bool fCurrentEstimate
    ) const override;

    void AddToBlock(CBlock* pblock) const override; 
    void AddToBlockTemplate(CBlockTemplate* pblocktemplate, CAmount fee, unsigned int /* not used sigops */) const override;

    bool Check(CValidationState& state, libzcash::ProofVerifier& verifier) const override;
    bool ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const override;
    bool CheckFinal(int flags) const override;
    bool IsApplicableToState() const override;

    bool IsStandard(std::string& reason, int nHeight) const override;
    bool IsAllowedInMempool(CValidationState& state, const CTxMemPool& pool) const override;
    
    void SyncWithWallets(const CBlock* pblock = NULL) const override;
    void UpdateCoins(CValidationState &state, CCoinsViewCache& view, int nHeight) const override;
    void UpdateCoins(CValidationState &state, CCoinsViewCache& view, CBlockUndo& txundo, int nHeight) const override;

    bool UpdateScInfo(Sidechain::ScCoinsViewCache& view, const CBlock& block, int nHeight) const override;
    bool RevertOutputs(Sidechain::ScCoinsViewCache& view, int nHeight) const override;

    double GetPriority(const CCoinsViewCache &view, int nHeight) const override;
    unsigned int GetLegacySigOpCount() const override;

    bool IsCoinCertified() const override { return true; }
};

/** A mutable version of CScCertificate. */
struct CMutableScCertificate : public CMutableTransactionBase
{
    uint256 scId;
    CAmount totalAmount;
    std::vector<CTxBackwardTransferCrosschainOut> vbt_ccout;
    uint256 nonce;

    CMutableScCertificate();
    CMutableScCertificate(const CScCertificate& tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        READWRITE(scId);
        READWRITE(totalAmount);
        READWRITE(vout);
        READWRITE(vbt_ccout);
        READWRITE(nonce);
    }

    template <typename Stream>
    CMutableScCertificate(deserialize_type, Stream& s) : totalAmount(0) {
        Unserialize(s);
    }

    /** Compute the hash of this CMutableScCertificate. This is computed on the
     * fly, as opposed to GetHash() in CScCertificate, which uses a cached result.
     */
    uint256 GetHash() const override;
};

// for the time being, this class is an empty place holder: attributes will be added in future as soon as they are designed
class CTxBackwardTransferCrosschainOut
{
public:

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        // any attributes go here
        //...
    }

    virtual uint256 GetHash() const;
    virtual std::string ToString() const;
};

#endif // _CERTIFICATE_H
