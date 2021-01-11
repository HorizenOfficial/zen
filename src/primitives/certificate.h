#ifndef _CERTIFICATE_H
#define _CERTIFICATE_H

#include "transaction.h"
#include "sc/proofverifier.h"
#include "policy/fees.h"

struct CMutableScCertificate;

class CBackwardTransferOut
{
public:
    CAmount nValue;
    uint160 pubKeyHash;

    CBackwardTransferOut(): nValue(-1), pubKeyHash() {};
    explicit CBackwardTransferOut(const CTxOut& txout);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nValue);
        READWRITE(pubKeyHash);
    }

    void SetNull()
    {
        nValue = -1;
        pubKeyHash.SetNull();
    }

    bool IsNull() const { return (nValue == -1);  }
};

class CScCertificate : virtual public CTransactionBase
{
    /** Memory only. */
    void UpdateHash() const override;

public:
    static const int32_t INT_NULL = -1;
    static const int32_t INT_NOT_INITIALIZED = -2;

    static const int32_t EPOCH_NULL = INT_NULL;
    static const int32_t EPOCH_NOT_INITIALIZED = INT_NOT_INITIALIZED;

    static const int64_t QUALITY_NULL = INT_NULL;
    static const int64_t QUALITY_NOT_INITIALIZED = INT_NOT_INITIALIZED;

private:
    const uint256 scId;

public:
    const int32_t epochNumber;
    const int64_t quality;
    const uint256 endEpochBlockHash;
    const libzendoomc::ScProof scProof;
    const int nFirstBwtPos;

    /** Construct a CScCertificate that qualifies as IsNull() */
    CScCertificate(int versionIn = SC_CERT_VERSION);
    CScCertificate(const CScCertificate& tx);
    CScCertificate& operator=(const CScCertificate& tx);
    ~CScCertificate() = default;

    /** Convert a CMutableScCertificate into a CScCertificate.  */
    CScCertificate(const CMutableScCertificate &tx);

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
        READWRITE(*const_cast<int64_t*>(&quality));
        READWRITE(*const_cast<uint256*>(&endEpochBlockHash));
        READWRITE(*const_cast<libzendoomc::ScProof*>(&scProof));
        READWRITE(*const_cast<std::vector<CTxIn>*>(&vin));

        if (ser_action.ForRead())
        {
            // reading from data stream to memory
            READWRITE(*const_cast<std::vector<CTxOut>*>(&vout));
            *const_cast<int*>(&nFirstBwtPos) = vout.size();

            std::vector<CBackwardTransferOut> vbt_ccout_ser;
            READWRITE(*const_cast<std::vector<CBackwardTransferOut>*>(&vbt_ccout_ser));
            for (auto& btout : vbt_ccout_ser)
                (*const_cast<std::vector<CTxOut>*>(&vout)).push_back(CTxOut(btout));
        }
        else
        {
            // reading from memory and writing to data stream
            // we must not modify vout
            std::vector<CTxOut> vout_ser;
            for(int pos = 0; pos < nFirstBwtPos; ++pos)
                vout_ser.push_back(vout[pos]);

            READWRITE(*const_cast<std::vector<CTxOut>*>(&vout_ser));

            std::vector<CBackwardTransferOut> vbt_ccout_ser;
            for(int pos = nFirstBwtPos; pos < vout.size(); ++pos)
                vbt_ccout_ser.push_back(CBackwardTransferOut(vout[pos]));

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
    const uint256&                            GetJoinSplitPubKey() const override { static const uint256 nullKey; return nullKey;}
    const std::vector<JSDescription>&         GetVjoinsplit() const override {static const std::vector<JSDescription> noJs; return noJs;};
    const uint256&                            GetScId()       const          {return scId;};
    const uint32_t&                           GetLockTime()   const override {static const uint32_t noLockTime(0); return noLockTime;};
    //END OF GETTERS

    bool IsBackwardTransfer(int pos) const override final;

    //CHECK FUNCTIONS
    bool IsValidVersion   (CValidationState &state) const override;
    bool IsVersionStandard(int nHeight) const override;
    bool CheckAmounts     (CValidationState &state) const override;
    bool CheckFeeAmount(const CAmount& totalVinAmount, CValidationState& state) const override;
    bool CheckInputsInteraction(CValidationState &state) const override;
    bool CheckInputsLimit() const override;

    //END OF CHECK FUNCTIONS

    void Relay() const override;
    std::shared_ptr<const CTransactionBase> MakeShared() const override;

    bool IsCoinBase()    const override final { return false; }
    bool IsCertificate() const override final { return true; }

    bool IsNull() const override {
        return (
            scId.IsNull() &&
            epochNumber == EPOCH_NULL &&
            quality == QUALITY_NULL &&
            endEpochBlockHash.IsNull() &&
            scProof.IsNull() &&
            vin.empty() &&
            vout.empty() );
    }

    CAmount GetFeeAmount(const CAmount& valueIn) const override;

    std::string EncodeHex() const override;
    std::string ToString() const override;

    CAmount GetValueOfBackwardTransfers() const;
    CAmount GetValueOfChange() const;

    void AddToBlock(CBlock* pblock) const override; 
    void AddToBlockTemplate(CBlockTemplate* pblocktemplate, CAmount fee, unsigned int /* not used sigops */) const override;

    bool ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const override;
    bool ContextualCheckInputs(CValidationState &state, const CCoinsViewCache &view, bool fScriptChecks,
                           const CChain& chain, unsigned int flags, bool cacheStore, const Consensus::Params& consensusParams,
                           std::vector<CScriptCheck> *pvChecks = NULL) const override;

    bool VerifyScript(
            const CScript& scriptPubKey, unsigned int nFlags, unsigned int nIn, const CChain* chain,
            bool cacheStore, ScriptError* serror) const override;
};

libzendoomc::ScFieldElement calculateCertDataHash(const CScCertificate& cert);
libzendoomc::ScFieldElement calculateCumulativeCertDataHash(const libzendoomc::ScFieldElement& prevCumulativeHash, const libzendoomc::ScFieldElement& prevCertHash);

/** A mutable version of CScCertificate. */
struct CMutableScCertificate : public CMutableTransactionBase
{
    uint256 scId;
    int32_t epochNumber;
    int64_t quality;
    uint256 endEpochBlockHash;
    libzendoomc::ScProof scProof;
    const int nFirstBwtPos;

    CMutableScCertificate();
    CMutableScCertificate(const CScCertificate& tx);
    operator CScCertificate() { return CScCertificate(*this); }
    CMutableScCertificate& operator=(const CMutableScCertificate& tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        READWRITE(scId);
        READWRITE(epochNumber);
        READWRITE(quality);
        READWRITE(endEpochBlockHash);
        READWRITE(scProof);
        READWRITE(vin);

        if (ser_action.ForRead())
        {
            // reading from data stream to memory
            READWRITE(vout);
            *const_cast<int*>(&nFirstBwtPos) = vout.size();

            std::vector<CBackwardTransferOut> vbt_ccout_ser;
            READWRITE(vbt_ccout_ser);
            for (auto& btout : vbt_ccout_ser)
                vout.push_back(CTxOut(btout));
        }
        else
        {
            // reading from memory and writing to data stream
            // we must not modify vout
            std::vector<CTxOut> vout_ser;
            for(int pos = 0; pos < nFirstBwtPos; ++pos)
                vout_ser.push_back(vout[pos]);

            READWRITE(vout_ser);

            std::vector<CBackwardTransferOut> vbt_ccout_ser;
            for(int pos = nFirstBwtPos; pos < vout.size(); ++pos)
                vbt_ccout_ser.push_back(CBackwardTransferOut(vout[pos]));

            READWRITE(vbt_ccout_ser);
        }
    }

    template <typename Stream>
    CMutableScCertificate(deserialize_type, Stream& s) :
    scId(), epochNumber(CScCertificate::EPOCH_NULL), quality(CScCertificate::QUALITY_NULL), endEpochBlockHash(), scProof() {
        Unserialize(s);
    }

    /** Compute the hash of this CMutableScCertificate. This is computed on the
     * fly, as opposed to GetHash() in CScCertificate, which uses a cached result.
     */
    uint256 GetHash() const override;

    void insertAtPos(unsigned int pos, const CTxOut& out) override final;
    void eraseAtPos(unsigned int pos)                     override final;
    void resizeOut(unsigned int newSize)                  override final;
    void resizeBwt(unsigned int newSize)                  override final;
    bool addOut(const CTxOut& out)                        override final;
    bool addBwt(const CTxOut& out)                        override final;
    bool add(const CTxScCreationOut& out)                 override final;
    bool add(const CTxForwardTransferOut& out)            override final;

    std::string ToString() const;
};

struct CScCertificateStatusUpdateInfo
{
    uint256  scId;
    uint256  certHash;
    uint32_t certEpoch;
    int64_t  certQuality;
    enum  BwtState : uint8_t {
        BWT_UNKNOW,
        BWT_ON,
        BWT_OFF
    };
    uint8_t bwtState;

    CScCertificateStatusUpdateInfo(): scId(), certHash(), certEpoch(CScCertificate::EPOCH_NOT_INITIALIZED),
                                      certQuality(CScCertificate::QUALITY_NOT_INITIALIZED), bwtState(BwtState::BWT_UNKNOW) {};
    CScCertificateStatusUpdateInfo(const uint256& _scId, const uint256& _certHash, uint32_t _certEpoch, int64_t _certQuality, BwtState _bwtState):
        scId(_scId), certHash(_certHash), certEpoch(_certEpoch), certQuality(_certQuality), bwtState(_bwtState) {};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        //Note: ScId is not serialized here, as it is used as key in wallet where this object is stored
        READWRITE(this->certHash);
        READWRITE(this->certEpoch);
        READWRITE(this->certQuality);
        READWRITE(this->bwtState);
    };

    std::string ToString() const
    {
        std::string str;
        str += strprintf("CScCertificateStatusUpdateInfo(scId=%s, certHash=%s, certEpoch=%d, bwtState=%d)",
                         this->scId.ToString(), this->certHash.ToString(), this->certEpoch, this->bwtState);
        return str;
    }
};

#endif // _CERTIFICATE_H
