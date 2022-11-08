#ifndef _CERTIFICATE_H
#define _CERTIFICATE_H

#include "transaction.h"
#include "sc/sidechaintypes.h"

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

class CScCertificate : public CTransactionBase
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
    const CFieldElement endEpochCumScTxCommTreeRoot;
    const CScProof scProof;
    std::vector<FieldElementCertificateField> vFieldElementCertificateField;
    std::vector<BitVectorCertificateField> vBitVectorCertificateField;
    const CAmount forwardTransferScFee;
    const CAmount mainchainBackwardTransferRequestScFee;

    // memory only
    const int nFirstBwtPos;

    /** Construct a CScCertificate that qualifies as IsNull() */
    CScCertificate(int versionIn = SC_CERT_VERSION);
    CScCertificate(const CScCertificate& tx);
    CScCertificate& operator=(const CScCertificate& tx);
    ~CScCertificate() = default;

    /** Convert a CMutableScCertificate into a CScCertificate. */
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
        READWRITE(*const_cast<CFieldElement*>(&endEpochCumScTxCommTreeRoot));
        READWRITE(*const_cast<CScProof*>(&scProof));
        READWRITE(*const_cast<std::vector<FieldElementCertificateField>*>(&vFieldElementCertificateField));
        READWRITE(*const_cast<std::vector<BitVectorCertificateField>*>(&vBitVectorCertificateField));
        READWRITE(*const_cast<CAmount*>(&forwardTransferScFee));
        READWRITE(*const_cast<CAmount*>(&mainchainBackwardTransferRequestScFee));

        READWRITE(*const_cast<std::vector<CTxIn>*>(&vin));

        //  - in the in-memory representation, ordinary outputs and backward transfer outputs are contained
        //    in the same vout vector which is parted in two segments.
        //    The first segment contains ordinary outputs (if any) and nFirstBwtPos is a positional index
        //    in vout pointing at the first backward transfer entry (if any).
        //
        //  - when serializing a certificate, ordinary outputs and backward transfer outputs are splitted
        //    in two separate vectors:
        //       vout_ser      - ordinary outputs, CTxOut objects
        //       vbt_ccout_ser - backward transfer outputs, CBackwardTransferOut objects 
        //    
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
    const uint256&                     GetJoinSplitPubKey() const override { static const uint256 nullKey; return nullKey;}
    const std::vector<JSDescription>&  GetVjoinsplit() const override {static const std::vector<JSDescription> noJs; return noJs;};
    const uint256&                     GetScId()       const          {return scId;};
    const uint32_t&                    GetLockTime()   const override {static const uint32_t noLockTime(0); return noLockTime;};
    CFieldElement                      GetDataHash(const Sidechain::ScFixedParameters& scFixedParams) const;
    //END OF GETTERS

    bool IsBackwardTransfer(int pos) const override final;

    //CHECK FUNCTIONS
    bool IsValidVersion   (CValidationState &state) const override;
    bool IsVersionStandard(int nHeight) const override;
    bool CheckSerializedSize(CValidationState &state) const override;
    bool CheckAmounts     (CValidationState &state) const override;
    bool CheckInputsOutputsNonEmpty(CValidationState &state) const override;
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
            endEpochCumScTxCommTreeRoot.IsNull() &&
            scProof.IsNull() &&
            vFieldElementCertificateField.empty() &&
            vBitVectorCertificateField.empty() &&
            vin.empty() &&
            vout.empty() );
    }

    CAmount GetFeeAmount(const CAmount& valueIn) const override;

    std::string EncodeHex() const override;
    std::string ToString() const override;

    CAmount GetValueOfBackwardTransfers() const;
    CAmount GetValueOfChange() const;

    bool ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const override;

    bool VerifyScript(
            const CScript& scriptPubKey, unsigned int nFlags, unsigned int nIn, const CChain* chain,
            bool cacheStore, ScriptError* serror) const override;
    void AddJoinSplitToJSON(UniValue& entry) const override;
};

/** A mutable version of CScCertificate. */
struct CMutableScCertificate : public CMutableTransactionBase
{
    uint256 scId;
    int32_t epochNumber;
    int64_t quality;
    CFieldElement endEpochCumScTxCommTreeRoot;
    CScProof scProof;
    std::vector<FieldElementCertificateField> vFieldElementCertificateField;
    std::vector<BitVectorCertificateField> vBitVectorCertificateField;
    CAmount forwardTransferScFee;
    CAmount mainchainBackwardTransferRequestScFee;

    // memory only
    const int nFirstBwtPos;

    CMutableScCertificate();
    CMutableScCertificate(const CScCertificate& tx);
    CMutableScCertificate(const CMutableScCertificate& tx) = default;
    operator CScCertificate() { return CScCertificate(static_cast<const CMutableScCertificate&>(*this)); }
    CMutableScCertificate& operator=(const CMutableScCertificate& tx);
    ~CMutableScCertificate() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        READWRITE(scId);
        READWRITE(epochNumber);
        READWRITE(quality);
        READWRITE(endEpochCumScTxCommTreeRoot);
        READWRITE(scProof);
        READWRITE(vFieldElementCertificateField);
        READWRITE(vBitVectorCertificateField);
        READWRITE(forwardTransferScFee);
        READWRITE(mainchainBackwardTransferRequestScFee);
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
        scId(), epochNumber(CScCertificate::EPOCH_NULL),
        quality(CScCertificate::QUALITY_NULL), endEpochCumScTxCommTreeRoot(), scProof(),
        vFieldElementCertificateField(), vBitVectorCertificateField()
    {
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
        str += strprintf("CScCertificateStatusUpdateInfo(scId=%s, certHash=%s, certEpoch=%d, certQuality=%d, bwtState=%d)",
                         scId.ToString(), certHash.ToString(), certEpoch, certQuality, bwtState);
        return str;
    }
};

/**
 * A structure containing a subset of the sidechain certificate data.
 */
struct CScCertificateView
{
    CFieldElement certDataHash;
    CAmount forwardTransferScFee;
    CAmount mainchainBackwardTransferRequestScFee;

    CScCertificateView(): certDataHash(), forwardTransferScFee(CScCertificate::INT_NULL), mainchainBackwardTransferRequestScFee(CScCertificate::INT_NULL) {};
    CScCertificateView(const uint256& certDataHash, CAmount ftFee, CAmount mbtrFee):
        certDataHash(certDataHash), forwardTransferScFee(ftFee), mainchainBackwardTransferRequestScFee(mbtrFee) {};
    CScCertificateView(const CScCertificate& certificate, const Sidechain::ScFixedParameters& scFixedParams):
        certDataHash(certificate.GetDataHash(scFixedParams)), forwardTransferScFee(certificate.forwardTransferScFee),
        mainchainBackwardTransferRequestScFee(certificate.mainchainBackwardTransferRequestScFee) {};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(certDataHash);
        READWRITE(forwardTransferScFee);
        READWRITE(mainchainBackwardTransferRequestScFee);
    }

    inline bool operator==(const CScCertificateView& rhs) const
    {
        return (this->certDataHash == rhs.certDataHash) &&
               (this->forwardTransferScFee == rhs.forwardTransferScFee) &&
               (this->mainchainBackwardTransferRequestScFee == rhs.mainchainBackwardTransferRequestScFee);
    }
    inline bool operator!=(const CScCertificateView& rhs) const { return !(*this == rhs); }

    std::string ToString() const
    {
        return strprintf("{ certDataHash=%s, ftScFee=%d, mbtrScFee=%d }", certDataHash.GetHexRepr(), forwardTransferScFee, mainchainBackwardTransferRequestScFee);
    }

    bool IsNull() const
    {
        return (
            certDataHash.IsNull() &&
            forwardTransferScFee == CScCertificate::INT_NULL &&
            mainchainBackwardTransferRequestScFee == CScCertificate::INT_NULL
        );
    }
};

#endif // _CERTIFICATE_H
