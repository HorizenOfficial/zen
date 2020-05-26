#ifndef _CERTIFICATE_H
#define _CERTIFICATE_H

#include "transaction.h"
#include "policy/fees.h"

struct CMutableScCertificate;

class CBackwardTransferOut
{
public:
    CAmount nValue;
    uint160 pubKeyHash;

    CBackwardTransferOut(): nValue(0), pubKeyHash() {};
    explicit CBackwardTransferOut(const CTxOut& txout);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nValue);
        READWRITE(pubKeyHash);
    }
};

class CScCertificate : virtual public CTransactionBase
{
    /** Memory only. */
    void UpdateHash() const override;

public:
    static const int32_t EPOCH_NULL = -1;
    static const int32_t EPOCH_NOT_INITIALIZED = -2;

private:
    const uint256 scId;

public:
    const int32_t epochNumber;
    const uint256 endEpochBlockHash;

    /** Construct a CScCertificate that qualifies as IsNull() */
    CScCertificate(int versionIn = SC_CERT_VERSION);

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

    size_t GetSerializeSize(int nType, int nVersion) const override {
        CSizeComputer s(nType, nVersion);
        NCONST_PTR(this)->SerializationOp(s, CSerActionSerialize(), nType, nVersion);
        return s.size();
    };

    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        NCONST_PTR(this)->SerializationOp(s, CSerActionSerialize(), nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        SerializationOp(s, CSerActionUnserialize(), nType, nVersion);
    }

    template <typename Stream, typename Operation>
    inline void SerializationOpInternal(Stream& s, Operation ser_action, int nType, int unused) {
        READWRITE(*const_cast<uint256*>(&scId));
        READWRITE(*const_cast<int32_t*>(&epochNumber));
        READWRITE(*const_cast<uint256*>(&endEpochBlockHash));
        READWRITE(*const_cast<std::vector<CTxIn>*>(&vin));

        if (ser_action.ForRead())
        {
            std::vector<CBackwardTransferOut> vbt_ccout_ser;

            // reading from data stream to memory
            READWRITE(*const_cast<std::vector<CTxOut>*>(&vout));
            READWRITE(*const_cast<std::vector<CBackwardTransferOut>*>(&vbt_ccout_ser));

            for (auto& btout : vbt_ccout_ser)
            {
                CTxOut out(btout);
                (*const_cast<std::vector<CTxOut>*>(&vout)).push_back(out);
            }
        }
        else
        {
            std::vector<CBackwardTransferOut> vbt_ccout_ser;
            // we must not modify vout
            std::vector<CTxOut> vout_ser;

            // reading from memory and writing to data stream
            for (auto it = vout.begin(); it != vout.end(); ++it)
            {
                if ((*it).isFromBackwardTransfer)
                {
                    CBackwardTransferOut btout((*it));
                    vbt_ccout_ser.push_back(btout);
                }
                else
                {
                    vout_ser.push_back(*it);
                }
            }
            READWRITE(*const_cast<std::vector<CTxOut>*>(&vout_ser));
            READWRITE(*const_cast<std::vector<CBackwardTransferOut>*>(&vbt_ccout_ser));
        }

        if (ser_action.ForRead())
            UpdateHash();
    }

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int unused) {
        READWRITE(*const_cast<int32_t*>(&nVersion));
        SerializationOpInternal(s, ser_action, nType, unused);
    }

    template <typename Stream>
    CScCertificate(deserialize_type, Stream& s) : CScCertificate(CMutableScCertificate(deserialize, s)) {}

    //GETTERS
    const std::vector<JSDescription>&         GetVjoinsplit() const override {static const std::vector<JSDescription> noJs; return noJs;};
    const uint256&                            GetScId()       const          {return scId;};
    const uint32_t&                           GetLockTime()   const override {static const uint32_t noLockTime(0); return noLockTime;};
    //END OF GETTERS

    //CHECK FUNCTIONS
    bool IsValidVersion   (CValidationState &state) const override;
    bool CheckVersionIsStandard   (std::string& reason, int nHeight) const override;
    bool CheckAmounts     (CValidationState &state) const override;
    bool CheckFeeAmount(const CAmount& totalVinAmount, CValidationState& state) const override;
    //END OF CHECK FUNCTIONS

    void Relay() const override;
    std::shared_ptr<const CTransactionBase> MakeShared() const override;

    bool IsCoinBase()    const override final { return false; }
    bool IsCertificate() const override final { return true; }

    bool IsNull() const override {
        return (
            scId.IsNull() &&
            epochNumber == EPOCH_NULL &&
            endEpochBlockHash.IsNull() &&
            vin.empty() &&
            vout.empty() );
    }

    CAmount GetFeeAmount(const CAmount& valueIn) const override;

    std::string EncodeHex() const override;
    std::string ToString() const override;

    void addToScCommitment(std::map<uint256, uint256>& mLeaves, std::set<uint256>& sScIds) const;
    CAmount GetValueOfBackwardTransfers() const;
    int GetNumbOfBackwardTransfers() const;
    CAmount GetValueOfChange() const { return (GetValueOut() - GetValueOfBackwardTransfers()); }

    void AddToBlock(CBlock* pblock) const override; 
    void AddToBlockTemplate(CBlockTemplate* pblocktemplate, CAmount fee, unsigned int /* not used sigops */) const override;

    bool ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const override;
    bool CheckFinal(int flags) const override;

    bool TryPushToMempool(bool fLimitFree, bool fRejectAbsurdFee) const override final;

    std::shared_ptr<BaseSignatureChecker> MakeSignatureChecker(
        unsigned int nIn, const CChain* chain, bool cacheStore) const override;
};

/** A mutable version of CScCertificate. */
struct CMutableScCertificate : public CMutableTransactionBase
{
    uint256 scId;
    int32_t epochNumber;
    uint256 endEpochBlockHash;

    CMutableScCertificate();
    CMutableScCertificate(const CScCertificate& tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        READWRITE(scId);
        READWRITE(epochNumber);
        READWRITE(endEpochBlockHash);
        READWRITE(vin);

        if (ser_action.ForRead())
        {
            std::vector<CBackwardTransferOut> vbt_ccout_ser;

            // reading from data stream to memory
            READWRITE(vout);
            READWRITE(vbt_ccout_ser);

            for (auto& btout : vbt_ccout_ser)
            {
                CTxOut out(btout);
                vout.push_back(out);
            }
        }
        else
        {
            std::vector<CBackwardTransferOut> vbt_ccout_ser;
            // we must not modify vout
            std::vector<CTxOut> vout_ser;

            // reading from memory and writing to data stream
            for (auto it = vout.begin(); it != vout.end(); ++it)
            {
                if ((*it).isFromBackwardTransfer)
                {
                    CBackwardTransferOut btout((*it));
                    vbt_ccout_ser.push_back(btout);
                }
                else
                {
                    vout_ser.push_back(*it);
                }
            }
            READWRITE(vout_ser);
            READWRITE(vbt_ccout_ser);
        }
    }

    template <typename Stream>
    CMutableScCertificate(deserialize_type, Stream& s) :
    scId(), epochNumber(CScCertificate::EPOCH_NULL), endEpochBlockHash() {
        Unserialize(s);
    }

    /** Compute the hash of this CMutableScCertificate. This is computed on the
     * fly, as opposed to GetHash() in CScCertificate, which uses a cached result.
     */
    uint256 GetHash() const override;

    bool add(const CTxOut& out)
    { 
        vout.push_back(out);
        return true;
    }

    std::string ToString() const;
};

#endif // _CERTIFICATE_H
