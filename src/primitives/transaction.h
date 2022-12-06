// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_TRANSACTION_H
#define BITCOIN_PRIMITIVES_TRANSACTION_H

#include <array>
#include <optional>

#include "amount.h"
#include "random.h"
#include "script/script.h"
#include "serialize.h"
#include "streams.h"
#include "uint256.h"
#include "consensus/consensus.h"
#include "util.h"
#include "net.h"

#include <boost/variant.hpp>

#include "zcash/NoteEncryption.hpp"
#include "zcash/Zcash.h"
#include "zcash/JoinSplit.hpp"
#include "zcash/Proof.hpp"
#include "utilstrencodings.h"
#include "hash.h"

#include "consensus/params.h"
#include <sc/sidechaintypes.h>
#include <script/script_error.h>

class UniValue;
class CBackwardTransferOut;
class CValidationState;
class CChain;
class CMutableTransactionBase;
struct CMutableTransaction;

static const int32_t SC_CERT_VERSION = 0xFFFFFFFB; // -5
static const int32_t SC_TX_VERSION = 0xFFFFFFFC; // -4
static const int32_t GROTH_TX_VERSION = 0xFFFFFFFD; // -3
static const int32_t PHGR_TX_VERSION = 2;
static const int32_t TRANSPARENT_TX_VERSION = 1;
static_assert(GROTH_TX_VERSION < MIN_OLD_TX_VERSION,
    "Groth tx version must be lower than minimum");

static_assert(PHGR_TX_VERSION >= MIN_OLD_TX_VERSION,
    "PHGR tx version must not be lower than minimum");

static_assert(TRANSPARENT_TX_VERSION >= MIN_OLD_TX_VERSION,
    "TRANSPARENT tx version must not be lower than minimum");

//Many static casts to int * of Tx nVersion (int32_t *) are performed. Verify at compile time that they are equivalent.
static_assert(sizeof(int32_t) == sizeof(int), "int size differs from 4 bytes. This may lead to unexpected behaviors on static casts");

template <typename Stream>
class SproutProofSerializer : public boost::static_visitor<>
{
    Stream& s;
    bool useGroth;
    int nType;
    int nVersion;
public:
    SproutProofSerializer(Stream& s, bool useGroth, int nType, int nVersion) : s(s), useGroth(useGroth), nType(nType), nVersion(nVersion) {}

    void operator()(const libzcash::PHGRProof& proof) const
    {
        if (useGroth) {
            throw std::ios_base::failure("Invalid Sprout proof for transaction format (expected GrothProof, found PHGRProof)");
        }
        ::Serialize(s, proof, nType, nVersion);
    }

    void operator()(const libzcash::GrothProof& proof) const
    {
        if (!useGroth) {
            throw std::ios_base::failure("Invalid Sprout proof for transaction format (expected PHGRProof, found GrothProof)");
        }
        ::Serialize(s, proof, nType, nVersion);
    }
};

template<typename Stream, typename T>
inline void SerReadWriteSproutProof(Stream& s, const T& proof, bool useGroth, CSerActionSerialize ser_action, int nType, int nVersion)
{
    auto ps = SproutProofSerializer<Stream>(s, useGroth, nType, nVersion);
    boost::apply_visitor(ps, proof);
}

template<typename Stream, typename T>
inline void SerReadWriteSproutProof(Stream& s, T& proof, bool useGroth, CSerActionUnserialize ser_action, int nType, int nVersion)
{
    if (useGroth) {
        libzcash::GrothProof grothProof;
        ::Unserialize(s, grothProof, nType, nVersion);
        proof = grothProof;
    } else {
        libzcash::PHGRProof pghrProof;
        ::Unserialize(s, pghrProof, nType, nVersion);
        proof = pghrProof;
    }
}

class JSDescription
{

public:
    // These values 'enter from' and 'exit to' the value
    // pool, respectively.
    CAmount vpub_old;
    CAmount vpub_new;

    // JoinSplits are always anchored to a root in the note
    // commitment tree at some point in the blockchain
    // history or in the history of the current
    // transaction.
    uint256 anchor;

    // Nullifiers are used to prevent double-spends. They
    // are derived from the secrets placed in the note
    // and the secret spend-authority key known by the
    // spender.
    std::array<uint256, ZC_NUM_JS_INPUTS> nullifiers;

    // Note commitments are introduced into the commitment
    // tree, blinding the public about the values and
    // destinations involved in the JoinSplit. The presence of
    // a commitment in the note commitment tree is required
    // to spend it.
    std::array<uint256, ZC_NUM_JS_OUTPUTS> commitments;

    // Ephemeral key
    uint256 ephemeralKey;

    // Ciphertexts
    // These contain trapdoors, values and other information
    // that the recipient needs, including a memo field. It
    // is encrypted using the scheme implemented in crypto/NoteEncryption.cpp
    std::array<ZCNoteEncryption::Ciphertext, ZC_NUM_JS_OUTPUTS> ciphertexts = {{ {{0}} }};

    // Random seed
    uint256 randomSeed;

    // MACs
    // The verification of the JoinSplit requires these MACs
    // to be provided as an input.
    std::array<uint256, ZC_NUM_JS_INPUTS> macs;

    // JoinSplit proof
    // This is a zk-SNARK which ensures that this JoinSplit is valid.
    libzcash::SproutProof proof;

    JSDescription(): vpub_old(0), vpub_new(0) { }

    static JSDescription getNewInstance(bool useGroth);

    JSDescription(
            bool makeGrothProof,
            ZCJoinSplit& params,
            const uint256& joinSplitPubKey,
            const uint256& rt,
            const std::array<libzcash::JSInput, ZC_NUM_JS_INPUTS>& inputs,
            const std::array<libzcash::JSOutput, ZC_NUM_JS_OUTPUTS>& outputs,
            CAmount vpub_old,
            CAmount vpub_new,
            bool computeProof = true, // Set to false in some tests
            uint256 *esk = nullptr // payment disclosure
    );

    static JSDescription Randomized(
            bool makeGrothProof,
            ZCJoinSplit& params,
            const uint256& joinSplitPubKey,
            const uint256& rt,
            std::array<libzcash::JSInput, ZC_NUM_JS_INPUTS>& inputs,
            std::array<libzcash::JSOutput, ZC_NUM_JS_OUTPUTS>& outputs,
            #ifdef __LP64__ // required to build on MacOS due to size_t ambiguity errors
            std::array<uint64_t, ZC_NUM_JS_INPUTS>& inputMap,
            std::array<uint64_t, ZC_NUM_JS_OUTPUTS>& outputMap,
            #else
            std::array<size_t, ZC_NUM_JS_INPUTS>& inputMap,
            std::array<size_t, ZC_NUM_JS_OUTPUTS>& outputMap,
            #endif
            CAmount vpub_old,
            CAmount vpub_new,
            bool computeProof = true, // Set to false in some tests
            uint256 *esk = nullptr, // payment disclosure
            std::function<int(int)> gen = GetRandInt
    );

    // Verifies that the JoinSplit proof is correct.
    bool Verify(
        ZCJoinSplit& params,
        libzcash::ProofVerifier& verifier,
        const uint256& joinSplitPubKey
    ) const;

    // Returns the calculated h_sig
    uint256 h_sig(ZCJoinSplit& params, const uint256& joinSplitPubKey) const;

    size_t GetSerializeSize(int nType, int nVersion, int nTxVersion) const {
        CSizeComputer s(nType, nVersion);
        auto os = WithTxVersion(&s, nTxVersion);
        NCONST_PTR(this)->SerializationOp(os, CSerActionSerialize(), nType, nVersion);
        return s.size();
    }

    template<typename OverrideStreamTx>
    void Serialize(OverrideStreamTx& s, int nType, int nVersion) const {
        NCONST_PTR(this)->SerializationOp(s, CSerActionSerialize(), nType, nVersion);
    }

    template<typename OverrideStreamTx>
    void Unserialize(OverrideStreamTx& s, int nType, int nVersion) {
        SerializationOp(s, CSerActionUnserialize(), nType, nVersion);
    }

    template <typename OverrideStreamTx, typename Operation>
    inline void SerializationOp(OverrideStreamTx& s, Operation ser_action, int nType, int nVersion) {
        // Stream version (that is transaction version) is set by CTransaction and CMutableTransaction to
        //  tx.nVersion
        const int txVersion = s.GetTxVersion();

        if( !(txVersion >= TRANSPARENT_TX_VERSION) && txVersion != GROTH_TX_VERSION) {
            LogPrintf("============== JsDescription GetTxVersion: Invalid shielded tx version %d \n", txVersion);
            throw std::ios_base::failure("Invalid shielded tx version (expected >=1 for PHGRProof or -3 for GrothProof)");
        }
        bool useGroth = (txVersion == GROTH_TX_VERSION);
        READWRITE(vpub_old);
        READWRITE(vpub_new);
        READWRITE(anchor);
        READWRITE(nullifiers);
        READWRITE(commitments);
        READWRITE(ephemeralKey);
        READWRITE(randomSeed);
        READWRITE(macs);
        ::SerReadWriteSproutProof(s, proof, useGroth, ser_action, nType, nVersion);
        READWRITE(ciphertexts);
    }

    friend bool operator==(const JSDescription& a, const JSDescription& b)
    {
        return (
            a.vpub_old == b.vpub_old &&
            a.vpub_new == b.vpub_new &&
            a.anchor == b.anchor &&
            a.nullifiers == b.nullifiers &&
            a.commitments == b.commitments &&
            a.ephemeralKey == b.ephemeralKey &&
            a.ciphertexts == b.ciphertexts &&
            a.randomSeed == b.randomSeed &&
            a.macs == b.macs &&
            a.proof == b.proof
            );
    }

    friend bool operator!=(const JSDescription& a, const JSDescription& b)
    {
        return !(a == b);
    }
};

class BaseOutPoint
{
public:
    uint256 hash;
    uint32_t n;

    BaseOutPoint() { SetNull(); }
    BaseOutPoint(const uint256& hashIn, uint32_t nIn): hash(hashIn), n(nIn) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(hash);
        READWRITE(n);
    }

    void SetNull() { hash.SetNull(); n = (uint32_t) -1; }
    bool IsNull() const { return (hash.IsNull() && n == (uint32_t) -1); }

    friend bool operator<(const BaseOutPoint& a, const BaseOutPoint& b)
    {
        return (a.hash < b.hash || (a.hash == b.hash && a.n < b.n));
    }

    friend bool operator==(const BaseOutPoint& a, const BaseOutPoint& b)
    {
        return (a.hash == b.hash && a.n == b.n);
    }

    friend bool operator!=(const BaseOutPoint& a, const BaseOutPoint& b)
    {
        return !(a == b);
    }
};

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint : public BaseOutPoint
{
public:
    COutPoint() : BaseOutPoint() {};
    COutPoint(const uint256& hashIn, uint32_t nIn) : BaseOutPoint(hashIn, nIn) {};
    std::string ToString() const;
};



/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn
{
public:
    COutPoint prevout;
    CScript scriptSig;
    uint32_t nSequence;

    CTxIn()
    {
        nSequence = std::numeric_limits<unsigned int>::max();
    }

    explicit CTxIn(const COutPoint& prevoutIn, const CScript& scriptSigIn=CScript(), uint32_t nSequenceIn=std::numeric_limits<unsigned int>::max());
    CTxIn(const uint256& hashPrevTx, const uint32_t& nOut, const CScript& scriptSigIn=CScript(), uint32_t nSequenceIn=std::numeric_limits<uint32_t>::max());

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(prevout);
        READWRITE(scriptSig);
        READWRITE(nSequence);
    }

    bool IsFinal() const
    {
        return (nSequence == std::numeric_limits<uint32_t>::max());
    }

    friend bool operator==(const CTxIn& a, const CTxIn& b)
    {
        return (a.prevout   == b.prevout &&
                a.scriptSig == b.scriptSig &&
                a.nSequence == b.nSequence);
    }

    friend bool operator!=(const CTxIn& a, const CTxIn& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxCeasedSidechainWithdrawalInput
{
public:
    CAmount nValue;
    uint256 scId;
    CFieldElement nullifier;
    uint160 pubKeyHash;
    CScProof scProof;
    CFieldElement actCertDataHash; 
    CFieldElement ceasingCumScTxCommTree; 
    CScript redeemScript;

    CTxCeasedSidechainWithdrawalInput();

    explicit CTxCeasedSidechainWithdrawalInput(const CAmount& nValueIn, const uint256& scIdIn,
                                               const CFieldElement& nullifierIn, const uint160& pubKeyHashIn,
                                               const CScProof& scProofIn, const CFieldElement& actCertDataHashIn,
                                               const CFieldElement& ceasingCumScTxCommTreeIn, const CScript& redeemScriptIn
                                               );

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nValue);
        READWRITE(scId);
        READWRITE(nullifier);
        READWRITE(pubKeyHash);
        READWRITE(scProof);
        READWRITE(actCertDataHash); // it is valid having actCertDataHash FE backed by an empty vector
        READWRITE(ceasingCumScTxCommTree);
        READWRITE(redeemScript);
    }

    friend bool operator==(const CTxCeasedSidechainWithdrawalInput& a, const CTxCeasedSidechainWithdrawalInput& b)
    {
        return (a.nValue                 == b.nValue &&
                a.scId                   == b.scId &&
                a.nullifier              == b.nullifier &&
                a.pubKeyHash             == b.pubKeyHash &&
                a.scProof                == b.scProof &&
                a.actCertDataHash            == b.actCertDataHash &&
                a.ceasingCumScTxCommTree == b.ceasingCumScTxCommTree &&
                a.redeemScript           == b.redeemScript);
    }

    friend bool operator!=(const CTxCeasedSidechainWithdrawalInput& a, const CTxCeasedSidechainWithdrawalInput& b)
    {
        return !(a == b);
    }

    std::string ToString() const;

    CScript scriptPubKey() const;
};

/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut
{
public:
    CAmount nValue;
    CScript scriptPubKey;

    CTxOut(): nValue(-1), scriptPubKey() {}

    CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn) :
        nValue(nValueIn), scriptPubKey(scriptPubKeyIn) {}

    explicit CTxOut(const CBackwardTransferOut& btdata);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nValue);
        READWRITE(scriptPubKey);
    }

    void SetNull()
    {
        nValue = -1;
        scriptPubKey.clear();
    }

    bool IsNull() const { return (nValue == -1);  }

    uint256 GetHash() const;

    CAmount GetDustThreshold(const CFeeRate &minRelayTxFee) const
    {
        // "Dust" is defined in terms of CTransaction::minRelayTxFee,
        // which has units satoshis-per-kilobyte.
        // If you'd pay more than 1/3 in fees
        // to spend something, then we consider it dust.
        // A typical spendable txout is 34 bytes big (or ~75 bytes big if we have the replay protection extension)
        // and will need a CTxIn of at least 148 bytes to spend:
        // so dust is a spendable txout less than 54 satoshis (or ~63)
        // with default minRelayTxFee.
        // For certificate backward transfers (which do not have the replay protection extension) we
        // consider the 148 bytes too, even if they do not have a corresponding input in the certificate.
        // This is because is better to avoid having UTXOs under the dust threshold anyway. 
        if (scriptPubKey.IsUnspendable())
            return 0;

        size_t nSize = GetSerializeSize(SER_DISK,0)+148u;
        return 3*minRelayTxFee.GetFee(nSize);
    }

    bool IsDust(const CFeeRate &minRelayTxFee) const
    {
        return (nValue < GetDustThreshold(minRelayTxFee));
    }

    friend bool operator==(const CTxOut& a, const CTxOut& b)
    {
        return (a.nValue                 == b.nValue &&
                a.scriptPubKey           == b.scriptPubKey);
    }

    friend bool operator!=(const CTxOut& a, const CTxOut& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

/** An output of a transaction related to SideChain only.
 */
class CTxCrosschainOutBase
{
public:
    virtual ~CTxCrosschainOutBase() {};

    bool IsNull() const {return this->GetScValue() == -1; };
    virtual CAmount GetScValue() const = 0;
    virtual bool AllowedZeroScValue() const = 0;

    bool CheckAmountRange(CAmount& cumulatedAmount) const;

    CAmount GetDustThreshold(const CFeeRate &minRelayTxFee) const
    {
        size_t nSize = GetSerializeSize(SER_DISK,0)+148u;
        return 3*minRelayTxFee.GetFee(nSize);
    }

    virtual const uint256& GetScId() const = 0; 

    virtual std::string ToString() const = 0;
};

class CTxCrosschainOut : public CTxCrosschainOutBase
{
public:
    CAmount nValue;
    uint256 address;

    CTxCrosschainOut(const CAmount& nValueIn, const uint256& addressIn)
        : nValue(nValueIn), address(addressIn) { }

    ~CTxCrosschainOut() {};

    CTxCrosschainOut():nValue(-1), address() {}

    CAmount GetScValue()      const override { return nValue; }
    bool AllowedZeroScValue() const override { return false; }

    virtual uint256 GetHash() const = 0;
protected:
    static bool isBaseEqual(const CTxCrosschainOut& a, const CTxCrosschainOut& b)
    {
        return ( a.nValue  == b.nValue && a.address == b.address);
    }

};

class CTxForwardTransferOut : public CTxCrosschainOut
{
public:
    uint256 scId;
    uint160 mcReturnAddress;

    CTxForwardTransferOut() {}

    CTxForwardTransferOut(const uint256& scIdIn, const CAmount& nValueIn, const uint256& addressIn, const uint160& mcReturnAddressIn):
        CTxCrosschainOut(nValueIn, addressIn), scId(scIdIn), mcReturnAddress(mcReturnAddressIn) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nValue);
        READWRITE(address);
        READWRITE(scId);
        READWRITE(mcReturnAddress);
    }

    const uint256& GetScId() const override final { return scId;};
    uint256 GetHash() const override final;
    std::string ToString() const override final;

    friend bool operator==(const CTxForwardTransferOut& a, const CTxForwardTransferOut& b)
    {
        return (isBaseEqual(a, b) && a.scId == b.scId && a.mcReturnAddress == b.mcReturnAddress);
    }

    friend bool operator!=(const CTxForwardTransferOut& a, const CTxForwardTransferOut& b)
    {
        return !(a == b);
    }
};

class CTxScCreationOut : public CTxCrosschainOut
{
friend class CTransaction;
private:
    /** Memory only. */
    const uint256 generatedScId;

    void GenerateScId(const uint256& txHash, unsigned int pos) const;

public:
    /**
     * @brief  Version of the sidechain.
     * The sidechain version has been introduced with hard fork 9 to increase the flexibility of the sidechain mechanism.
     * Since the Sidechain Creation Output was at the beginning designed without the version, a single byte for such field
     * has been taken from the WithdrawalEpochLength (before hard fork 9 its size was 4 bytes, after hard fork 9 it is 3 bytes).
     * Due to the constraints of the consensus logic, we are sure that the most significant byte of the WithdrawalEpochLength was
     * always zero before hard fork 9.
     * 
     * Version 0: due to a bug in the validation of the certificate custom fields, they had to be treated in a special way
     *            (wrong endianness, see FieldElementCertificateField::GetFieldElement function for further details).
     * 
     * Version 1: the validation of custom fields has been fixed.
     */
    uint8_t version;
    int withdrawalEpochLength; 
    std::vector<unsigned char> customData;
    std::optional<CFieldElement> constant;
    CScVKey wCertVk;
    std::optional<CScVKey> wCeasedVk;
    std::vector<FieldElementCertificateFieldConfig> vFieldElementCertificateFieldConfig;
    std::vector<BitVectorCertificateFieldConfig> vBitVectorCertificateFieldConfig;
    CAmount forwardTransferScFee;
    CAmount mainchainBackwardTransferRequestScFee;
    uint8_t mainchainBackwardTransferRequestDataLength;

    CTxScCreationOut(): version(0xff),
                        withdrawalEpochLength(-1),
                        forwardTransferScFee(-1),
                        mainchainBackwardTransferRequestScFee(-1),
                        mainchainBackwardTransferRequestDataLength(0) { }

    CTxScCreationOut(const CAmount& nValueIn, const uint256& addressIn,
                     const CAmount& ftScFee, const CAmount& mbtrScFee,
                     const Sidechain::ScFixedParameters& params);
    CTxScCreationOut& operator=(const CTxScCreationOut &ccout);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {

        // The first serialization field is a 4 bytes element containing the SC Creation version in the first byte
        // and the withdrawalEpochLenght in the remaining 3 bytes. The first implementation didn't have the version
        // field, but only the withdrawalEpochLenght, so the version was implicitly set to 0 anyway.
        if (ser_action.ForRead())
        {
            int withdrawalEpochLengthAndVersion;
            READWRITE(withdrawalEpochLengthAndVersion);
            
            // Get the most significant byte
            version = withdrawalEpochLengthAndVersion >> 24;

            // Get the least significant 3 bytes
            withdrawalEpochLength = withdrawalEpochLengthAndVersion & 0x00FFFFFF;
        }
        else
        {
            // Check any possible inconsistency that would overwrite the "version" byte
            assert(withdrawalEpochLength <= 0x00FFFFFF);

            int withdrawalEpochLengthAndVersion = (version << 24) | withdrawalEpochLength;
            READWRITE(withdrawalEpochLengthAndVersion);
        }

        READWRITE(nValue);
        READWRITE(address);
        READWRITE(customData);
        READWRITE(constant);
        READWRITE(wCertVk);
        READWRITE(wCeasedVk);
        READWRITE(vFieldElementCertificateFieldConfig);
        READWRITE(vBitVectorCertificateFieldConfig);
        READWRITE(forwardTransferScFee);
        READWRITE(mainchainBackwardTransferRequestScFee);
        READWRITE(mainchainBackwardTransferRequestDataLength);
    }

    const uint256& GetScId() const override final { return generatedScId;}; 

    uint256 GetHash() const override final;
    std::string ToString() const override final;

    friend bool operator==(const CTxScCreationOut& a, const CTxScCreationOut& b)
    {
        return isBaseEqual(a, b) &&
                 a.generatedScId == b.generatedScId &&
                 a.withdrawalEpochLength == b.withdrawalEpochLength &&
                 a.customData == b.customData &&
                 a.constant == b.constant &&
                 a.wCertVk == b.wCertVk &&
                 a.wCeasedVk == b.wCeasedVk &&
                 a.vFieldElementCertificateFieldConfig == b.vFieldElementCertificateFieldConfig &&
                 a.vBitVectorCertificateFieldConfig == b.vBitVectorCertificateFieldConfig &&
                 a.forwardTransferScFee == b.forwardTransferScFee &&
                 a.mainchainBackwardTransferRequestScFee == b.mainchainBackwardTransferRequestScFee &&
                 a.mainchainBackwardTransferRequestDataLength == b.mainchainBackwardTransferRequestDataLength;
    }

    friend bool operator!=(const CTxScCreationOut& a, const CTxScCreationOut& b)
    {
        return !(a == b);
    }
};

class CBwtRequestOut : public CTxCrosschainOutBase
{
  public:
    uint256 scId;
    std::vector<CFieldElement> vScRequestData;
    uint160 mcDestinationAddress;
    CAmount scFee;

    CBwtRequestOut():scFee(0) {}

    CBwtRequestOut(const uint256& scIdIn, const uint160& pkhIn, const Sidechain::ScBwtRequestParameters& params);
    CBwtRequestOut& operator=(const CBwtRequestOut &ccout);

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(scId);
        READWRITE(vScRequestData);
        READWRITE(mcDestinationAddress);
        READWRITE(scFee);
    }

    CAmount GetScValue() const override { return scFee; };
    bool AllowedZeroScValue() const override { return true; }

    friend bool operator==(const CBwtRequestOut& a, const CBwtRequestOut& b)
    {
        return ( a.scId                 == b.scId                 &&
                 a.vScRequestData       == b.vScRequestData       &&
                 a.mcDestinationAddress == b.mcDestinationAddress &&
                 a.scFee                == b.scFee
                );
    }

    friend bool operator!=(const CBwtRequestOut& a, const CBwtRequestOut& b)
    {
        return !(a == b);
    }

    const uint256& GetScId() const override { return scId;}; 

    std::string ToString() const override;
};

// abstract interface for CTransaction and CScCertificate
class CTransactionBase
{
public:
    const int32_t nVersion;
protected:
    const std::vector<CTxIn> vin;
    const std::vector<CTxOut> vout;

    /** Memory only. */
    const uint256 hash;

    virtual void UpdateHash() const = 0;
public:

    virtual size_t GetSerializeSize(int nType, int nVersion) const = 0;

    CTransactionBase(int versionIn);
    CTransactionBase(const CTransactionBase& tx);
    CTransactionBase& operator=(const CTransactionBase& tx);

    explicit CTransactionBase(const CMutableTransactionBase& mutTxBase);
    virtual ~CTransactionBase() = default;

    template <typename Stream>
    CTransactionBase(deserialize_type, Stream& s): CTransactionBase(CMutableTransactionBase(deserialize, s)) {}

    const uint256& GetHash() const {
        return hash;
    }

    friend bool operator==(const CTransactionBase& a, const CTransactionBase& b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CTransactionBase& a, const CTransactionBase& b)
    {
        return !(a==b);
    }

    //GETTERS
    const std::vector<CTxIn>&         GetVin()        const {return vin;};
    const std::vector<CTxOut>&        GetVout()       const {return vout;};

    virtual const std::vector<JSDescription>&         GetVjoinsplit() const = 0;
    virtual const uint32_t&                           GetLockTime()   const = 0;
    //END OF GETTERS

    virtual bool IsBackwardTransfer(int pos) const = 0;

    //CHECK FUNCTIONS
    virtual bool IsValidVersion   (CValidationState &state) const = 0;
    virtual bool IsVersionStandard(int nHeight) const = 0;
    virtual bool ContextualCheck  (CValidationState& state, int nHeight, int dosLevel) const = 0;

    virtual bool CheckSerializedSize (CValidationState &state) const = 0;
    virtual bool CheckAmounts(CValidationState &state) const = 0;

    virtual bool CheckInputsDuplication(CValidationState &state) const;
    virtual bool CheckInputsOutputsNonEmpty(CValidationState &state) const = 0;
    virtual bool CheckInputsInteraction(CValidationState &state) const = 0;

    bool CheckBlockAtHeight(CValidationState& state, int nHeight, int dosLevel) const;

    virtual bool CheckInputsLimit() const = 0;
    //END OF CHECK FUNCTIONS

    // Return sum of txouts.
    virtual CAmount GetValueOut() const;

    virtual int GetComplexity() const;

    // Compute modified tx size for priority calculation (optionally given tx size)
    unsigned int CalculateModifiedSize(unsigned int nTxSize=0) const;

    // Compute priority, given priority of inputs and (optionally) tx size
    double ComputePriority(double dPriorityInputs, unsigned int nTxSize=0) const;

    // Return sum of JoinSplit vpub_new if supported
    virtual CAmount GetJoinSplitValueIn() const;

    virtual CAmount GetCSWValueIn() const { return 0; }

    //-----------------
    // pure virtual interfaces 
    virtual void Relay() const = 0;
    virtual std::shared_ptr<const CTransactionBase> MakeShared() const = 0;

    virtual bool CheckFeeAmount(const CAmount& totalVinAmount, CValidationState& state) const = 0;

    virtual bool IsNull() const = 0;

    // return fee amount
    virtual CAmount GetFeeAmount(const CAmount& valueIn) const = 0;

    virtual std::string EncodeHex() const = 0;
    virtual std::string ToString() const = 0;

    virtual bool VerifyScript(
        const CScript& scriptPubKey, unsigned int flags, unsigned int nIn, const CChain* chain,
        bool cacheStore, ScriptError* serror) const = 0;

    //-----------------
    // default values for derived classes which do not support specific data structures

    // return false when meaningful only in a block context. As of now only tx coin base returns false

    virtual bool IsCoinBase()    const = 0;
    virtual bool IsCertificate() const = 0;

    virtual void AddJoinSplitToJSON(UniValue& entry) const { return; }
    virtual void AddCeasedSidechainWithdrawalInputsToJSON(UniValue& entry) const { return; }
    virtual void AddSidechainOutsToJSON(UniValue& entry) const { return; }

    virtual const uint256& GetJoinSplitPubKey() const = 0;

    static bool IsCertificate(int nVersion) {
        return (nVersion == SC_CERT_VERSION);
    }

    static bool IsTransaction(int nVersion) {
        return !IsCertificate(nVersion);
    }
};

/** The basic transaction that is broadcasted on the network and contained in
 * blocks.  A transaction can contain multiple inputs and outputs.
 */
class CTransaction : public CTransactionBase
{
protected:
    void UpdateHash() const override;

public:
    typedef boost::array<unsigned char, 64> joinsplit_sig_t;

    // Transactions that include a list of JoinSplits are version 2.
    static const int32_t MIN_OLD_VERSION = 1;
    static const int32_t MAX_OLD_VERSION = PHGR_TX_VERSION;

    static_assert(MIN_OLD_VERSION >= MIN_OLD_TX_VERSION,
                  "standard rule for tx version should be consistent with network rule");

    // The local variables are made const to prevent unintended modification
    // without updating the cached hash value. However, CTransaction is not
    // actually immutable; deserialization and assignment are implemented,
    // and bypass the constness. This is safe, as they update the entire
    // structure, including the hash.
private:
    const std::vector<JSDescription>                     vjoinsplit;
    const uint32_t                                       nLockTime;
    const std::vector<CTxCeasedSidechainWithdrawalInput> vcsw_ccin;
    const std::vector<CTxScCreationOut>                  vsc_ccout;
    const std::vector<CTxForwardTransferOut>             vft_ccout;
    const std::vector<CBwtRequestOut>                    vmbtr_out;
public:
    const uint256 joinSplitPubKey;
    const joinsplit_sig_t joinSplitSig = {{0}};

    /** Construct a CTransaction that qualifies as IsNull() */
    CTransaction(int nVersionIn = TRANSPARENT_TX_VERSION);
    CTransaction& operator=(const CTransaction& tx);
    CTransaction(const CTransaction& tx);
    ~CTransaction() = default;

    /** Convert a CMutableTransaction into a CTransaction. */
    CTransaction(const CMutableTransaction &tx);

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
        READWRITE(*const_cast<std::vector<CTxIn>*>(&vin));
        READWRITE(*const_cast<std::vector<CTxOut>*>(&vout));
        if (this->IsScVersion())
        {
            READWRITE(*const_cast<std::vector<CTxCeasedSidechainWithdrawalInput>*>(&vcsw_ccin));
            READWRITE(*const_cast<std::vector<CTxScCreationOut>*>(&vsc_ccout));
            READWRITE(*const_cast<std::vector<CTxForwardTransferOut>*>(&vft_ccout));
            READWRITE(*const_cast<std::vector<CBwtRequestOut>*>(&vmbtr_out));
        }
        READWRITE(*const_cast<uint32_t*>(&nLockTime));
        if (nVersion >= PHGR_TX_VERSION || nVersion == GROTH_TX_VERSION) {
            auto os = WithTxVersion(&s, static_cast<int>(nVersion));
            ::SerReadWrite(os, *const_cast<std::vector<JSDescription>*>(&vjoinsplit), nType, nVersion, ser_action);
            if (vjoinsplit.size() > 0) {
                READWRITE(*const_cast<uint256*>(&joinSplitPubKey));
                READWRITE(*const_cast<joinsplit_sig_t*>(&joinSplitSig));
            }
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
    CTransaction(deserialize_type, Stream& s) : CTransaction(CMutableTransaction(deserialize, s)) {}
    
    std::string EncodeHex() const override;

    bool IsCoinBase()    const override final { return GetVin().size() == 1 && GetVin()[0].prevout.IsNull(); }
    bool IsCertificate() const override final { return false; }

    bool IsNull() const override
    {
        bool ret = vin.empty() && vout.empty();
        if (IsScVersion())
            ret = ret && ccIsNull();

        return ret;
    }

    bool ccIsNull() const {
        return vcsw_ccin.empty() && ccOutIsNull();
    }

    bool ccOutIsNull() const {
        return vsc_ccout.empty() && vft_ccout.empty() && vmbtr_out.empty();
    }
    
    bool IsScVersion() const 
    {
        // so far just one version
        return (nVersion == SC_TX_VERSION);
    }

    //GETTERS
    const std::vector<CTxCeasedSidechainWithdrawalInput>&   GetVcswCcIn()   const { return vcsw_ccin; }
    const std::vector<CTxScCreationOut>&                    GetVscCcOut()   const { return vsc_ccout; }
    const std::vector<CTxForwardTransferOut>&               GetVftCcOut()   const { return vft_ccout; }
    const std::vector<CBwtRequestOut>&                      GetVBwtRequestOut() const { return vmbtr_out; }
    const std::vector<JSDescription>&                       GetVjoinsplit() const override { return vjoinsplit; }
    const uint32_t&                                         GetLockTime()   const override { return nLockTime; }
    const uint256&                                          GetScIdFromScCcOut(int pos) const;
    //END OF GETTERS

    bool IsBackwardTransfer(int pos) const override final { return false; };

    //CHECK FUNCTIONS
    bool IsValidVersion   (CValidationState &state) const override;
    bool IsVersionStandard(int nHeight) const override;
    bool CheckSerializedSize (CValidationState &state) const override;
    bool CheckAmounts     (CValidationState &state) const override;
    bool CheckInputsOutputsNonEmpty    (CValidationState &state) const override;
    bool CheckFeeAmount(const CAmount& totalVinAmount, CValidationState& state) const override;
    bool CheckInputsDuplication(CValidationState &state) const override;
    bool CheckInputsInteraction(CValidationState &state) const override;
    bool CheckInputsLimit() const override;
    //END OF CHECK FUNCTIONS

    void Relay() const override;
    std::shared_ptr<const CTransactionBase> MakeShared() const override;

    // Return sum of txouts.
    CAmount GetValueOut() const override;

    // Return Tx complexity considering both regular and CSW inputs
    int GetComplexity() const override;

    // Return sum of CSW inputs
    CAmount GetCSWValueIn() const override;

    // value in should be computed via the method above using a proper coin view
    CAmount GetFeeAmount(const CAmount& valueIn) const override { return (valueIn - GetValueOut() ); }

    const uint256& GetJoinSplitPubKey() const override { return joinSplitPubKey; }

    std::string ToString() const override;

 private:
    template <typename T>
    inline CAmount GetValueCcOut(const T& ccout) const
    {
        CAmount nValueOut = 0;
        for (const auto& it : ccout)
        {
            nValueOut += it.GetScValue();
            if (!MoneyRange(it.GetScValue()) || !MoneyRange(nValueOut))
                throw std::runtime_error("CTransaction::GetValueCcOut(): value out of range");
        }
        return nValueOut;
    }

 public:
    bool ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const override;
    void AddJoinSplitToJSON(UniValue& entry) const override;
    void AddCeasedSidechainWithdrawalInputsToJSON(UniValue& entry) const override;
    void AddSidechainOutsToJSON(UniValue& entry) const override;

    bool VerifyScript(
            const CScript& scriptPubKey, unsigned int flags, unsigned int nIn, const CChain* chain,
            bool cacheStore, ScriptError* serror) const override;
};

/** A mutable hierarchy version of CTransaction. */
struct CMutableTransactionBase
{
    int32_t nVersion;
    std::vector<CTxIn> vin;

protected:
    std::vector<CTxOut> vout;
public:
    CMutableTransactionBase();
    virtual ~CMutableTransactionBase() = default;

    /** Compute the hash of this CMutableTransaction. This is computed on the
     * fly, as opposed to GetHash() in CTransaction, which uses a cached result.
     */
    virtual uint256 GetHash() const = 0;

    const std::vector<CTxOut>& getVout() const                { return vout; }
                       CTxOut& getOut(unsigned int pos)       { return vout[pos]; }
                 const CTxOut& getOut(unsigned int pos) const { return vout[pos]; }

    virtual void insertAtPos(unsigned int pos, const CTxOut& out) = 0;
    virtual void eraseAtPos(unsigned int pos)                     = 0;
    virtual void resizeOut(unsigned int newSize)                  = 0;
    virtual void resizeBwt(unsigned int newSize)                  = 0;
    virtual bool addOut(const CTxOut& out)                        = 0;
    virtual bool addBwt(const CTxOut& out)                        = 0;
};


struct CMutableTransaction : public CMutableTransactionBase
{
    std::vector<CTxCeasedSidechainWithdrawalInput> vcsw_ccin;
    std::vector<CTxScCreationOut>                  vsc_ccout;
    std::vector<CTxForwardTransferOut>             vft_ccout;
    std::vector<CBwtRequestOut>                    vmbtr_out;
    uint32_t nLockTime;
    std::vector<JSDescription> vjoinsplit;
    uint256 joinSplitPubKey;
    CTransaction::joinsplit_sig_t joinSplitSig = {{0}};

    CMutableTransaction();
    CMutableTransaction(const CTransaction& tx);
    operator CTransaction() { return CTransaction(static_cast<const CMutableTransaction&>(*this)); }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(vin);
        READWRITE(vout);
        if (this->IsScVersion())
        {
            READWRITE(vcsw_ccin);
            READWRITE(vsc_ccout);
            READWRITE(vft_ccout);
            READWRITE(vmbtr_out);
        }
        READWRITE(nLockTime);
        if (nVersion >= PHGR_TX_VERSION || nVersion == GROTH_TX_VERSION) {
            auto os = WithTxVersion(&s, static_cast<int>(this->nVersion));
            ::SerReadWrite(os, vjoinsplit, nType, nVersion, ser_action);
            if (vjoinsplit.size() > 0) {
                READWRITE(joinSplitPubKey);
                READWRITE(joinSplitSig);
            }
        }
    }

    template <typename Stream>
    CMutableTransaction(deserialize_type, Stream& s):nLockTime(0) {
        Unserialize(s);
    }

    /** Compute the hash of this CMutableTransaction. This is computed on the
     * fly, as opposed to GetHash() in CTransaction, which uses a cached result.
     */
    uint256 GetHash() const override;

    bool IsScVersion() const
    {
        // so far just one version
        return (nVersion == SC_TX_VERSION);
    }

    void insertAtPos(unsigned int pos, const CTxOut& out) override final;
    void eraseAtPos(unsigned int pos)                     override final;
    void resizeOut(unsigned int newSize)                  override final;
    void resizeBwt(unsigned int newSize)                  override final;
    bool addOut(const CTxOut& out)                        override final;
    bool addBwt(const CTxOut& out)                        override final;
    bool add(const CTxScCreationOut& out);
    bool add(const CTxForwardTransferOut& out);
    bool add(const CBwtRequestOut& out);
    bool add(const CFieldElement& acd);
};

//! Struct for storing transaction subsections size info
struct CBaseTransactionSizeInfo
{
    size_t overheadSize = 0;
    size_t baseInputsSize = 0;
    size_t baseOutputsNoChangeSize = 0;
    size_t baseOutputChangeOnlySize = 0;
};

struct CTransactionSizeInfo : CBaseTransactionSizeInfo
{
    size_t joinsplitsSize = 0;
    size_t sidechainInputsSize = 0;
    size_t sidechainOutputsSize = 0;
};

#endif // BITCOIN_PRIMITIVES_TRANSACTION_H
