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
    const int epochNumber;
    const uint256 endEpochBlockHash;
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
        READWRITE(*const_cast<int*>(&epochNumber));
        READWRITE(*const_cast<uint256*>(&endEpochBlockHash));
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
            epochNumber == 0 &&
            endEpochBlockHash == uint256() &&
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

    void getCrosschainOutputs(std::map<uint256, std::vector<uint256> >& map) const override;

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

    bool UpdateScInfo(Sidechain::ScCoinsViewCache& view, const CBlock& block, int nHeight, CBlockUndo& bu) const override;

    double GetPriority(const CCoinsViewCache &view, int nHeight) const override;
    unsigned int GetLegacySigOpCount() const override;

    bool IsCoinCertified() const override { return true; }

    // return true if the block marking the end of the withdrawal epoch of this certificate is in the main chain
    bool epochIsInMainchain() const;

    bool checkEpochBlockHash() const;

 private:
    template <typename T>
    inline void fillCrosschainOutput(const uint256& scid, const T& vOuts, unsigned int& nIdx, std::map<uint256, std::vector<uint256> >& map) const
    {
        uint256 certHash = GetHash();
 
        // if the mapped value exists, vec is a reference to it. If it does not, vec is
        // a reference to the new element inserted in the map with the scid as a key
        std::vector<uint256>& vec = map[scid];

        LogPrint("sc", "%s():%d - processing scId[%s], vec size = %d\n",
            __func__, __LINE__, scId.ToString(), vec.size());
 
        for(const auto& vout : vOuts)
        {
            uint256 ccoutHash = vout.GetHash();
            unsigned int n = nIdx;
 
            LogPrint("sc", "%s():%d -Inputs: h1[%s], h2[%s], n[%d]\n",
                __func__, __LINE__, ccoutHash.ToString(), certHash.ToString(), n);

            uint256 entry = Hash(
                BEGIN(ccoutHash), END(ccoutHash),
                BEGIN(certHash),  END(certHash),
                BEGIN(n),         END(n) );

#ifdef DEBUG_SC_HASH
            CDataStream ss2(SER_NETWORK, PROTOCOL_VERSION);
            ss2 << ccoutHash;
            ss2 << certHash;
            ss2 << n;
            std::string ser2( HexStr(ss2.begin(), ss2.end()));
            uint256 entry2 = Hash(ss2.begin(), ss2.begin() + (unsigned int)ss2.in_avail() );

            CHashWriter ss3(SER_GETHASH, PROTOCOL_VERSION);
            ss3 << ccoutHash;
            ss3 << certHash;
            ss3 << n;
            uint256 entry3 = ss3.GetHash();

            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss << vout;
            std::string ser( HexStr(ss.begin(), ss.end()));
         
            std::cout << __func__ << " -------------------------------------------" << std::endl;
            std::cout << "                         ccout: " << ser << std::endl;
            std::cout << "-------------------------------------------" << std::endl;
            std::cout << "                   Hash(ccout): " << ccoutHash.ToString() << std::endl;
            std::cout << "                        certid: " << certHash.ToString() << std::endl;
            std::cout << "                             n: " << std::hex << n << std::dec << std::endl;
            std::cout << "-------------------------------------------" << std::endl;
            std::cout << "    Hash(Hash(ccout)|certid|n): " << entry.ToString() << std::endl;
            std::cout << "-------------------------------------------" << std::endl;
            std::cout << "concat = Hash(ccout)|certid| n: " << ser2 << std::endl;
            std::cout << "                  Hash(concat): " << entry2.ToString() << std::endl;
#endif

            vec.push_back(entry);

            LogPrint("sc", "%s():%d -Output: entry[%s]\n", __func__, __LINE__, entry.ToString());
 
            nIdx++;
        }
    }
};

/** A mutable version of CScCertificate. */
struct CMutableScCertificate : public CMutableTransactionBase
{
    uint256 scId;
    int epochNumber;
    uint256 endEpochBlockHash;
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
        READWRITE(epochNumber);
        READWRITE(endEpochBlockHash);
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
