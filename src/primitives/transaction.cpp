// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/transaction.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "librustzcash.h"
#include <boost/foreach.hpp>

// static global check methods, now called by CTransaction instances
#include "main.h"
#include "sc/sidechain.h"
#include "sc/sidechainrpc.h"
#include "consensus/validation.h"
#include "validationinterface.h"
#include "undo.h"
#include "core_io.h"
#include "miner.h"
#include "utilmoneystr.h"
#include <univalue.h>
#include <limits.h>

extern UniValue TxJoinSplitToJSON(const CTransaction& tx);

JSDescription JSDescription::getNewInstance(bool useGroth) {
	JSDescription js;

	if(useGroth) {
		js.proof = libzcash::GrothProof();
	} else {
		js.proof = libzcash::PHGRProof();
	}

	return js;
}

JSDescription::JSDescription(
    bool makeGrothProof,
    ZCJoinSplit& params,
    const uint256& joinSplitPubKey,
    const uint256& anchor,
    const std::array<libzcash::JSInput, ZC_NUM_JS_INPUTS>& inputs,
    const std::array<libzcash::JSOutput, ZC_NUM_JS_OUTPUTS>& outputs,
    CAmount vpub_old,
    CAmount vpub_new,
    bool computeProof,
    uint256 *esk // payment disclosure
) : vpub_old(vpub_old), vpub_new(vpub_new), anchor(anchor)
{
    std::array<libzcash::Note, ZC_NUM_JS_OUTPUTS> notes;

    proof = params.prove(
        makeGrothProof,
        inputs,
        outputs,
        notes,
        ciphertexts,
        ephemeralKey,
        joinSplitPubKey,
        randomSeed,
        macs,
        nullifiers,
        commitments,
        vpub_old,
        vpub_new,
        anchor,
        computeProof,
        esk // payment disclosure
    );
}

JSDescription JSDescription::Randomized(
    bool makeGrothProof,
    ZCJoinSplit& params,
    const uint256& joinSplitPubKey,
    const uint256& anchor,
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
    bool computeProof,
    uint256 *esk, // payment disclosure
    std::function<int(int)> gen
)
{
    // Randomize the order of the inputs and outputs
    inputMap = {0, 1};
    outputMap = {0, 1};

    assert(gen);

    MappedShuffle(inputs.begin(), inputMap.begin(), ZC_NUM_JS_INPUTS, gen);
    MappedShuffle(outputs.begin(), outputMap.begin(), ZC_NUM_JS_OUTPUTS, gen);

    return JSDescription(
        makeGrothProof,
        params, joinSplitPubKey, anchor, inputs, outputs,
        vpub_old, vpub_new, computeProof,
        esk // payment disclosure
    );
}

class SproutProofVerifier : public boost::static_visitor<bool>
{
    ZCJoinSplit& params;
    libzcash::ProofVerifier& verifier;
    const uint256& joinSplitPubKey;
    const JSDescription& jsdesc;

public:
    SproutProofVerifier(
        ZCJoinSplit& params,
        libzcash::ProofVerifier& verifier,
        const uint256& joinSplitPubKey,
        const JSDescription& jsdesc
        ) : params(params), verifier(verifier), joinSplitPubKey(joinSplitPubKey), jsdesc(jsdesc) {}

    bool operator()(const libzcash::PHGRProof& proof) const
    {
        return params.verify(
            proof,
            verifier,
            joinSplitPubKey,
            jsdesc.randomSeed,
            jsdesc.macs,
            jsdesc.nullifiers,
            jsdesc.commitments,
            jsdesc.vpub_old,
            jsdesc.vpub_new,
            jsdesc.anchor
        );
    }

    bool operator()(const libzcash::GrothProof& proof) const
    {
        uint256 h_sig = params.h_sig(jsdesc.randomSeed, jsdesc.nullifiers, joinSplitPubKey);

        return librustzcash_sprout_verify(
            proof.begin(),
            jsdesc.anchor.begin(),
            h_sig.begin(),
            jsdesc.macs[0].begin(),
            jsdesc.macs[1].begin(),
            jsdesc.nullifiers[0].begin(),
            jsdesc.nullifiers[1].begin(),
            jsdesc.commitments[0].begin(),
            jsdesc.commitments[1].begin(),
            jsdesc.vpub_old,
            jsdesc.vpub_new
        );
    }
};

bool JSDescription::Verify(
    ZCJoinSplit& params,
    libzcash::ProofVerifier& verifier,
    const uint256& joinSplitPubKey
) const {
    auto pv = SproutProofVerifier(params, verifier, joinSplitPubKey, *this);
    return boost::apply_visitor(pv, proof);
}

uint256 JSDescription::h_sig(ZCJoinSplit& params, const uint256& joinSplitPubKey) const
{
    return params.h_sig(randomSeed, nullifiers, joinSplitPubKey);
}

std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hash.ToString().substr(0,10), n);
}

CTxIn::CTxIn(const COutPoint& prevoutIn, const CScript& scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(const uint256& hashPrevTx, const uint32_t& nOut, const CScript& scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
    if (nSequence != std::numeric_limits<unsigned int>::max())
        str += strprintf(", nSequence=%u", nSequence);
    str += ")";
    return str;
}

CTxOut::CTxOut(const CBackwardTransferOut& btout) : nValue(btout.nValue)
{
    scriptPubKey.clear();
    std::vector<unsigned char> pkh(btout.pubKeyHash.begin(), btout.pubKeyHash.end());
    scriptPubKey << OP_DUP << OP_HASH160 << pkh << OP_EQUALVERIFY << OP_CHECKSIG;
}

uint256 CTxOut::GetHash() const
{
    return SerializeHash(*this);
}

std::string CTxOut::ToString() const
{
    return strprintf("CTxOut(nValue=%d.%08d, scriptPubKey=%s)",
        nValue / COIN, nValue % COIN, HexStr(scriptPubKey).substr(0, 30));
}

//----------------------------------------------------------------------------
uint256 CTxForwardTransferOut::GetHash() const
{
    return SerializeHash(*this);
}

std::string CTxForwardTransferOut::ToString() const
{
    return strprintf("CTxForwardTransferOut(nValue=%d.%08d, address=%s, scId=%s)",
        nValue / COIN, nValue % COIN, HexStr(address).substr(0, 30), scId.ToString() );
}

//----------------------------------------------------------------------------
bool CTxCrosschainOut::CheckAmountRange(CAmount& cumulatedAmount) const
{
    if (nValue == CAmount(0) || !MoneyRange(nValue))
    {
        LogPrint("sc", "%s():%d - ERROR: invalid nValue %lld\n", __func__, __LINE__, nValue);
        return false;
    }

    cumulatedAmount += nValue;

    if (!MoneyRange(cumulatedAmount))
    {
        LogPrint("sc", "%s():%d - ERROR: invalid cumulated value %lld\n", __func__, __LINE__, cumulatedAmount);
        return false;
    }

    return true;
}

CTxScCreationOut::CTxScCreationOut(
    const CAmount& nValueIn, const uint256& addressIn,
    const Sidechain::ScCreationParameters& paramsIn)
    :CTxCrosschainOut(nValueIn, addressIn), generatedScId(),
     withdrawalEpochLength(paramsIn.withdrawalEpochLength), customData(paramsIn.customData),
     constant(paramsIn.constant), wCertVk(paramsIn.wCertVk) {}

uint256 CTxScCreationOut::GetHash() const
{
    return SerializeHash(*this);
}

std::string CTxScCreationOut::ToString() const
{
    return strprintf("CTxScCreationOut(scId=%s, withdrawalEpochLength=%d, nValue=%d.%08d, address=%s, customData=[%s], constant=[%s], wCertVk=[%s]",
        generatedScId.ToString(), withdrawalEpochLength, nValue / COIN, nValue % COIN, HexStr(address).substr(0, 30), HexStr(customData), HexStr(constant), HexStr(wCertVk) );
}

void CTxScCreationOut::GenerateScId(const uint256& txHash, unsigned int pos) const
{
    const uint256& scid = Hash(
            BEGIN(txHash),    END(txHash),
            BEGIN(pos),       END(pos) );

    LogPrint("sc", "%s():%d - updating scid=%s - tx[%s], pos[%u]\n",
        __func__, __LINE__, scid.ToString(), txHash.ToString(), pos);

    *const_cast<uint256*>(&generatedScId) = scid;
}

CTxScCreationOut& CTxScCreationOut::operator=(const CTxScCreationOut &ccout) {
    CTxCrosschainOut::operator=(ccout);
    *const_cast<uint256*>(&generatedScId) = ccout.generatedScId;
    withdrawalEpochLength = ccout.withdrawalEpochLength;
    customData = ccout.customData;
    constant = ccout.constant;
    wCertVk = ccout.wCertVk;
    return *this;
}


CMutableTransactionBase::CMutableTransactionBase():
    nVersion(TRANSPARENT_TX_VERSION), vin(), vout() {}

CMutableTransaction::CMutableTransaction() : CMutableTransactionBase(),
    vsc_ccout(), vft_ccout(), nLockTime(0), vjoinsplit(), joinSplitPubKey(), joinSplitSig() {}

CMutableTransaction::CMutableTransaction(const CTransaction& tx): CMutableTransactionBase(),
    vsc_ccout(tx.GetVscCcOut()), vft_ccout(tx.GetVftCcOut()), nLockTime(tx.GetLockTime()),
    vjoinsplit(tx.GetVjoinsplit()), joinSplitPubKey(tx.joinSplitPubKey), joinSplitSig(tx.joinSplitSig)
{
    nVersion = tx.nVersion;
    vin = tx.GetVin();
    vout = tx.GetVout();
}
    
uint256 CMutableTransaction::GetHash() const
{
    return SerializeHash(*this);
}

void CMutableTransaction::insertAtPos(unsigned int pos, const CTxOut& out) { vout.insert(vout.begin() + pos, out);}
void CMutableTransaction::eraseAtPos(unsigned int pos) { vout.erase(vout.begin() + pos); }
void CMutableTransaction::resizeOut(unsigned int newSize) { vout.resize(newSize); }
bool CMutableTransaction::addOut(const CTxOut& out) { vout.push_back(out); return true;}
bool CMutableTransaction::addBwt(const CTxOut& out) { return false; }
bool CMutableTransaction::add(const CTxScCreationOut& out)  { vsc_ccout.push_back(out); return true; }
bool CMutableTransaction::add(const CTxForwardTransferOut& out) { vft_ccout.push_back(out); return true; }

//--------------------------------------------------------------------------------------------------------
CTransactionBase::CTransactionBase(int nVersionIn):
    nVersion(nVersionIn), vin(), vout(), hash() {}

CTransactionBase::CTransactionBase(const CTransactionBase &tx):
    nVersion(tx.nVersion), vin(tx.vin), vout(tx.vout), hash(tx.hash) {}

CTransactionBase& CTransactionBase::operator=(const CTransactionBase &tx) {
    *const_cast<uint256*>(&hash)             = tx.hash;
    *const_cast<int*>(&nVersion)             = tx.nVersion;
    *const_cast<std::vector<CTxIn>*>(&vin)   = tx.vin;
    *const_cast<std::vector<CTxOut>*>(&vout) = tx.vout;
    return *this;
}

CTransactionBase::CTransactionBase(const CMutableTransactionBase& mutTxBase):
    nVersion(mutTxBase.nVersion), vin(mutTxBase.vin), vout(mutTxBase.getVout()), hash(mutTxBase.GetHash()) {}

CAmount CTransactionBase::GetValueOut() const
{
    CAmount nValueOut = 0;
    for(const CTxOut& out: vout) {
        nValueOut += out.nValue;
        if (!MoneyRange(out.nValue) || !MoneyRange(nValueOut))
            throw std::runtime_error("CTransactionBase::GetValueOut(): value out of range");
    }

    return nValueOut;
}

CAmount CTransactionBase::GetJoinSplitValueIn() const
{
    CAmount nCumulatedValue = 0;
    for(const JSDescription& js : GetVjoinsplit())
    {
        // NB: vpub_new "gives" money to the value pool just as inputs do
        nCumulatedValue += js.vpub_new;

        if (!MoneyRange(js.vpub_new) || !MoneyRange(nCumulatedValue))
            throw std::runtime_error("CTransaction::GetJoinSplitValueIn(): value out of range");
    }

    return nCumulatedValue;
}

bool CTransaction::CheckAmounts(CValidationState &state) const
{
    // Check for negative or overflow output values
    CAmount nCumulatedValueOut = 0;
    for(const CTxOut& txout: vout)
    {
        if (txout.nValue < 0)
            return state.DoS(100, error("CheckAmounts(): txout.nValue negative"),
                             REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, error("CheckAmounts(): txout.nValue too high"),
                             REJECT_INVALID, "bad-txns-vout-toolarge");
        nCumulatedValueOut += txout.nValue;
        if (!MoneyRange(nCumulatedValueOut))
            return state.DoS(100, error("CheckAmounts(): txout total out of range"),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Ensure that joinsplit values are well-formed
    for(const JSDescription& joinsplit: vjoinsplit)
    {
        if (joinsplit.vpub_old < 0) {
            return state.DoS(100, error("CheckAmounts(): joinsplit.vpub_old negative"),
                             REJECT_INVALID, "bad-txns-vpub_old-negative");
        }

        if (joinsplit.vpub_new < 0) {
            return state.DoS(100, error("CheckAmounts(): joinsplit.vpub_new negative"),
                             REJECT_INVALID, "bad-txns-vpub_new-negative");
        }

        if (joinsplit.vpub_old > MAX_MONEY) {
            return state.DoS(100, error("CheckAmounts(): joinsplit.vpub_old too high"),
                             REJECT_INVALID, "bad-txns-vpub_old-toolarge");
        }

        if (joinsplit.vpub_new > MAX_MONEY) {
            return state.DoS(100, error("CheckAmounts(): joinsplit.vpub_new too high"),
                             REJECT_INVALID, "bad-txns-vpub_new-toolarge");
        }

        if (joinsplit.vpub_new != 0 && joinsplit.vpub_old != 0) {
            return state.DoS(100, error("CheckAmounts(): joinsplit.vpub_new and joinsplit.vpub_old both nonzero"),
                             REJECT_INVALID, "bad-txns-vpubs-both-nonzero");
        }

        nCumulatedValueOut += joinsplit.vpub_old;
        if (!MoneyRange(nCumulatedValueOut)) {
            return state.DoS(100, error("CheckAmounts(): txout total out of range"),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
        }
    }

    for(const CTxScCreationOut& scOut: vsc_ccout)
    {
        if (scOut.nValue < 0)
            return state.DoS(100, error("CheckAmounts(): scOut.nValue negative"),
                             REJECT_INVALID, "bad-txns-vout-negative");
        if (scOut.nValue > MAX_MONEY)
            return state.DoS(100, error("CheckAmounts(): scOut.nValue too high"),
                             REJECT_INVALID, "bad-txns-vout-toolarge");
        nCumulatedValueOut += scOut.nValue;
        if (!MoneyRange(nCumulatedValueOut))
            return state.DoS(100, error("CheckAmounts(): txout total out of range"),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    for(const CTxForwardTransferOut& fwdOut: vft_ccout)
    {
        if (fwdOut.nValue < 0)
            return state.DoS(100, error("CheckAmounts(): fwdOut.nValue negative"),
                             REJECT_INVALID, "bad-txns-vout-negative");
        if (fwdOut.nValue > MAX_MONEY)
            return state.DoS(100, error("CheckAmounts(): fwdOut.nValue too high"),
                             REJECT_INVALID, "bad-txns-vout-toolarge");
        nCumulatedValueOut += fwdOut.nValue;
        if (!MoneyRange(nCumulatedValueOut))
            return state.DoS(100, error("CheckAmounts(): txout total out of range"),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Ensure input values do not exceed MAX_MONEY
    // We have not resolved the txin values at this stage, but we do know what the joinsplits claim to add
    // to the value pool.
    CAmount nCumulatedValueIn = 0;
    for(const JSDescription& joinsplit: GetVjoinsplit())
    {
        nCumulatedValueIn += joinsplit.vpub_new;

        if (!MoneyRange(joinsplit.vpub_new) || !MoneyRange(nCumulatedValueIn)) {
            return state.DoS(100, error("CheckAmounts(): txin total out of range"),
                             REJECT_INVALID, "bad-txns-txintotal-toolarge");
        }
    }

    return true;
}

bool CTransactionBase::CheckInputsDuplication(CValidationState &state) const
{
    // Check for duplicate inputs
    std::set<COutPoint> vInOutPoints;
    for(const CTxIn& txin: GetVin())
    {
        if (vInOutPoints.count(txin.prevout))
            return state.DoS(100, error("CheckInputsDuplications(): duplicate inputs"),
                             REJECT_INVALID, "bad-txns-inputs-duplicate");
        vInOutPoints.insert(txin.prevout);
    }

    // Check for duplicate joinsplit nullifiers in this transaction
    std::set<uint256> vJoinSplitNullifiers;
    for(const JSDescription& joinsplit: GetVjoinsplit())
    {
        for(const uint256& nf: joinsplit.nullifiers)
        {
            if (vJoinSplitNullifiers.count(nf))
                return state.DoS(100, error("CheckInputsDuplications(): duplicate nullifiers"),
                             REJECT_INVALID, "bad-joinsplits-nullifiers-duplicate");

            vJoinSplitNullifiers.insert(nf);
        }
    }

    return true;
}

bool CTransaction::CheckInputsInteraction(CValidationState &state) const
{
    if (IsCoinBase())
    {
        // There should be no joinsplits in a coinbase transaction
        if (vjoinsplit.size() > 0)
            return state.DoS(100, error("CheckInputsInteraction(): coinbase has joinsplits"),
                             REJECT_INVALID, "bad-cb-has-joinsplits");

        if (vin[0].scriptSig.size() < 2 || vin[0].scriptSig.size() > 100)
            return state.DoS(100, error("CheckInputsInteraction(): coinbase script size"),
                             REJECT_INVALID, "bad-cb-length");

        if (vsc_ccout.size() != 0 || vft_ccout.size() != 0)
            return state.DoS(100, error("CheckInputsInteraction(): coinbase destined to sidechains"),
                                         REJECT_INVALID, "bad-cb-destination");
    }
    else
    {
        for(const CTxIn& txin: vin)
            if (txin.prevout.IsNull())
                return state.DoS(10, error("CheckInputsInteraction(): prevout is null"),
                                 REJECT_INVALID, "bad-txns-prevout-null");
    }

    return true;
}

CTransaction::CTransaction(int nVersionIn): CTransactionBase(nVersionIn),
    vjoinsplit(), nLockTime(0), vsc_ccout(), vft_ccout(),
    joinSplitPubKey(), joinSplitSig() {}

CTransaction::CTransaction(const CTransaction &tx) : CTransactionBase(tx),
    vjoinsplit(tx.vjoinsplit), nLockTime(tx.nLockTime),
    vsc_ccout(tx.vsc_ccout), vft_ccout(tx.vft_ccout),
    joinSplitPubKey(tx.joinSplitPubKey), joinSplitSig(tx.joinSplitSig) {}

CTransaction& CTransaction::operator=(const CTransaction &tx) {
    CTransactionBase::operator=(tx);
    *const_cast<std::vector<JSDescription>*>(&vjoinsplit)        = tx.vjoinsplit;
    *const_cast<uint32_t*>(&nLockTime)                           = tx.nLockTime;
    *const_cast<std::vector<CTxScCreationOut>*>(&vsc_ccout)      = tx.vsc_ccout;
    *const_cast<std::vector<CTxForwardTransferOut>*>(&vft_ccout) = tx.vft_ccout;
    *const_cast<uint256*>(&joinSplitPubKey)                      = tx.joinSplitPubKey;
    *const_cast<joinsplit_sig_t*>(&joinSplitSig)                 = tx.joinSplitSig;
    return *this;
}

void CTransaction::UpdateHash() const
{
    *const_cast<uint256*>(&hash) = SerializeHash(*this);
    // if any sidechain creation is taking place within this transaction, we generate the sidechain id
    for(unsigned int pos = 0; pos < vsc_ccout.size(); pos++)
        vsc_ccout[pos].GenerateScId(hash, pos);
}

CTransaction::CTransaction(const CMutableTransaction &tx): CTransactionBase(tx),
    vjoinsplit(tx.vjoinsplit), nLockTime(tx.nLockTime), vsc_ccout(tx.vsc_ccout),
    vft_ccout(tx.vft_ccout), joinSplitPubKey(tx.joinSplitPubKey), joinSplitSig(tx.joinSplitSig)
{
    UpdateHash();
}

unsigned int CTransactionBase::CalculateModifiedSize(unsigned int nTxSize) const
{
    // In order to avoid disincentivizing cleaning up the UTXO set we don't count
    // the constant overhead for each txin and up to 110 bytes of scriptSig (which
    // is enough to cover a compressed pubkey p2sh redemption) for priority.
    // Providing any more cleanup incentive than making additional inputs free would
    // risk encouraging people to create junk outputs to redeem later.
    if (nTxSize == 0)
    {
        // polymorphic call
        nTxSize = GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
    }
    for (std::vector<CTxIn>::const_iterator it(vin.begin()); it != vin.end(); ++it)
    {
        unsigned int offset = 41U + std::min(110U, (unsigned int)it->scriptSig.size());
        if (nTxSize > offset)
            nTxSize -= offset;
    }
    return nTxSize;
}

double CTransactionBase::ComputePriority(double dPriorityInputs, unsigned int nTxSize) const
{
    nTxSize = CalculateModifiedSize(nTxSize);
    if (nTxSize == 0) return 0.0;

    return dPriorityInputs / nTxSize;
}

const uint256& CTransaction::GetScIdFromScCcOut(int pos) const
{
    static const uint256 nullHash;
    if (pos < 0 ||pos >= GetVscCcOut().size())
    {
        LogPrint("sc", "%s():%d - tx[%s] pos %d out of range (vsc_ccout size %d)\n",
            __func__, __LINE__, GetHash().ToString(), pos, GetVscCcOut().size());
        return nullHash;
    }

    const uint256& scid = GetVscCcOut().at(pos).GetScId();

    LogPrint("sc", "%s():%d - tx[%s] has scid[%s] for ccout[%s] (pos[%d])\n",
        __func__, __LINE__, GetHash().ToString(), scid.ToString(), GetVscCcOut().at(pos).GetHash().ToString(), pos);

    return scid;
}

bool CTransaction::IsValidVersion(CValidationState &state) const
{
    // Basic checks that don't depend on any context
    // Check transaction version
    if (nVersion < MIN_OLD_TX_VERSION && nVersion != GROTH_TX_VERSION && !IsScVersion() )
    {
        return state.DoS(100, error("BasicVersionCheck(): version too low"),
                         REJECT_INVALID, "bad-txns-version-too-low");
    }

    return true;
}

bool CTransaction::CheckNonEmpty(CValidationState &state) const
{
    // Transactions can contain empty `vin` and `vout` so long as
    // `vjoinsplit` is non-empty.
    if (GetVin().empty() && GetVjoinsplit().empty())
    {
        LogPrint("sc", "%s():%d - Error: tx[%s]\n", __func__, __LINE__, GetHash().ToString() );
        return state.DoS(10, error("CheckNonEmpty(): vin empty"),
                         REJECT_INVALID, "bad-txns-vin-empty");
    }

    // Allow the case when crosschain outputs are not empty. In that case there might be no vout at all
    // when utxo reminder is only dust, which is added to fee leaving no change for the sender
    if (GetVout().empty() && GetVjoinsplit().empty() && ccIsNull())
    {
        return state.DoS(10, error("CheckNonEmpty(): vout empty"),
                         REJECT_INVALID, "bad-txns-vout-empty");
    }

    return true;
}

bool CTransactionBase::CheckSerializedSize(CValidationState &state) const
{
    BOOST_STATIC_ASSERT(MAX_BLOCK_SIZE > MAX_TX_SIZE); // sanity
    if (GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION) > MAX_TX_SIZE)
        return state.DoS(100, error("checkSerializedSizeLimits(): size limits failed"),
                         REJECT_INVALID, "bad-txns-oversize");

    return true;
}

bool CTransaction::CheckFeeAmount(const CAmount& totalVinAmount, CValidationState& state) const {
    if (!MoneyRange(totalVinAmount))
        return state.DoS(100, error("CheckFeeAmount(): total input amount out of range"),
                         REJECT_INVALID, "bad-txns-inputvalues-outofrange");

    if (!CheckAmounts(state))
        return false;

    if (totalVinAmount < GetValueOut() )
        return state.DoS(100, error("CheckInputs(): %s value in (%s) < value out (%s)",
                                    GetHash().ToString(),
                                    FormatMoney(totalVinAmount), FormatMoney(GetValueOut()) ),
                         REJECT_INVALID, "bad-txns-in-belowout");

    CAmount nTxFee = totalVinAmount - GetValueOut();
    if (nTxFee < 0)
        return state.DoS(100, error("CheckFeeAmount(): %s nTxFee < 0", GetHash().ToString()),
                         REJECT_INVALID, "bad-txns-fee-negative");

    if (!MoneyRange(nTxFee))
        return state.DoS(100, error("CheckFeeAmount(): nTxFee out of range"),
                         REJECT_INVALID, "bad-txns-fee-outofrange");

    return true;
}

CAmount CTransaction::GetValueOut() const
{
    // vout
    CAmount nValueOut = CTransactionBase::GetValueOut();

    for (std::vector<JSDescription>::const_iterator it(vjoinsplit.begin()); it != vjoinsplit.end(); ++it)
    {
        // NB: vpub_old "takes" money from the value pool just as outputs do
        nValueOut += it->vpub_old;

        if (!MoneyRange(it->vpub_old) || !MoneyRange(nValueOut))
            throw std::runtime_error("CTransaction::GetValueOut(): value out of range");
    }

    nValueOut += (GetValueCcOut(vsc_ccout) + GetValueCcOut(vft_ccout));
    if (!MoneyRange(nValueOut))
        throw std::runtime_error("CTransaction::GetValueOut(): value out of range");

    return nValueOut;
}

std::string CTransaction::ToString() const
{
    std::string str;

    if (IsScVersion())
    {
        str += strprintf("CTransaction(hash=%s, ver=%d, vin.size=%u, vout.size=%u, vsc_ccout.size=%u, vft_ccout.size=%u, nLockTime=%u)\n",
            GetHash().ToString().substr(0,10),
            nVersion,
            vin.size(),
            vout.size(),
            vsc_ccout.size(),
            vft_ccout.size(),
            nLockTime);

        for (unsigned int i = 0; i < vin.size(); i++)
            str += "    " + vin[i].ToString() + "\n";
        for (unsigned int i = 0; i < vout.size(); i++)
            str += "    " + vout[i].ToString() + "\n";
        for (unsigned int i = 0; i < vsc_ccout.size(); i++)
            str += "    " + vsc_ccout[i].ToString() + "\n";
        for (unsigned int i = 0; i < vft_ccout.size(); i++)
            str += "    " + vft_ccout[i].ToString() + "\n";
    }
    else
    {
        str += strprintf("CTransaction(hash=%s, ver=%d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
            GetHash().ToString().substr(0,10),
            nVersion,
            vin.size(),
            vout.size(),
            nLockTime);
        for (unsigned int i = 0; i < vin.size(); i++)
            str += "    " + vin[i].ToString() + "\n";
        for (unsigned int i = 0; i < vout.size(); i++)
            str += "    " + vout[i].ToString() + "\n";
    }
    return str;
}

//--------------------------------------------------------------------------------------------
// binaries other than zend that are produced in the build, do not call these members and therefore do not
// need linking all of the related symbols. We use this macro as it is already defined with a similar purpose
// in zen-tx binary build configuration
#ifdef BITCOIN_TX
bool CTransactionBase::CheckBlockAtHeight(CValidationState& state, int unused, int dosLevel) const { return true; }
bool CTransaction::IsVersionStandard(int nHeight) const {return true;}

void CTransaction::AddToBlock(CBlock* pblock) const { return; }
void CTransaction::AddToBlockTemplate(CBlockTemplate* pblocktemplate, CAmount fee, unsigned int sigops) const {return; }
bool CTransaction::ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const { return true; }
void CTransaction::AddJoinSplitToJSON(UniValue& entry) const { return; }
void CTransaction::AddSidechainOutsToJSON(UniValue& entry) const { return; }
bool CTransaction::ContextualCheckInputs(CValidationState &state, const CCoinsViewCache &view, bool fScriptChecks,
          const CChain& chain, unsigned int flags, bool cacheStore, const Consensus::Params& consensusParams,
          std::vector<CScriptCheck> *pvChecks) const { return true;}
std::string CTransaction::EncodeHex() const { return ""; }
std::shared_ptr<BaseSignatureChecker> CTransaction::MakeSignatureChecker(unsigned int nIn, const CChain* chain, bool cacheStore) const
{
    return std::shared_ptr<BaseSignatureChecker>();
}
void CTransaction::Relay() const {}
std::shared_ptr<const CTransactionBase> CTransaction::MakeShared() const
{
    return std::shared_ptr<const CTransactionBase>();
}

#else
//----- 
bool CTransactionBase::CheckBlockAtHeight(CValidationState& state, int nHeight, int dosLevel) const
{
    // Check for vout's without OP_CHECKBLOCKATHEIGHT opcode
    //BOOST_FOREACH(const CTxOut& txout, vout)
    for(int pos = 0; pos < GetVout().size(); ++pos)
    {
        // if the output comes from a backward transfer (when we are a certificate), skip this check
        // but go on if the certificate txout is an ordinary one
        if (IsBackwardTransfer(pos))
            continue;

        const CTxOut& txout = GetVout()[pos];
        txnouttype whichType;
        ::IsStandard(txout.scriptPubKey, whichType);

        // provide temporary replay protection for two minerconf windows during chainsplit
        if (!IsCoinBase() && !ForkManager::getInstance().isTransactionTypeAllowedAtHeight(nHeight, whichType))
        {
            return state.DoS(dosLevel, error("%s: %s: %s is not activated at this block height %d. Transaction rejected. Tx id: %s",
                    __FILE__, __func__, ::GetTxnOutputType(whichType), nHeight, GetHash().ToString()),
                REJECT_CHECKBLOCKATHEIGHT_NOT_FOUND, "op-checkblockatheight-needed");
        }
    }

    return true;
}

bool CTransaction::IsVersionStandard(int nHeight) const {
    // sidechain fork (happens after groth fork)
    int sidechainVersion = 0;
    bool areSidechainsSupported = ForkManager::getInstance().areSidechainsSupported(nHeight);
    if (areSidechainsSupported)
    {
        sidechainVersion = ForkManager::getInstance().getSidechainTxVersion(nHeight);
    }

    // groth fork
    const int shieldedTxVersion = ForkManager::getInstance().getShieldedTxVersion(nHeight);
    bool isGROTHActive = (shieldedTxVersion == GROTH_TX_VERSION);

    if(!isGROTHActive)
    {
        // sidechain fork is after groth one
        assert(!areSidechainsSupported);

        if (nVersion > CTransaction::MAX_OLD_VERSION || nVersion < CTransaction::MIN_OLD_VERSION)
        {
            return false;
        }
    }
    else
    {
        if (nVersion != TRANSPARENT_TX_VERSION && nVersion != GROTH_TX_VERSION)
        {
            // check sidechain tx
            if ( !(areSidechainsSupported && (nVersion == sidechainVersion)) )
            {
                return false;
            }
        }
    }

    return true;
}

bool CTransactionBase::CheckInputsLimit() const {
    // Node operator can choose to reject tx by number of transparent inputs
    static_assert(std::numeric_limits<size_t>::max() >= std::numeric_limits<int64_t>::max(), "size_t too small");
    size_t limit = (size_t) GetArg("-mempooltxinputlimit", 0);
    if (limit > 0) {
        size_t n = GetVin().size();
        if (n > limit) {
            LogPrint("mempool", "Dropping txid %s : too many transparent inputs %zu > limit %zu\n",
                    GetHash().ToString(), n, limit );
            return false;
        }
    }
    return true;
}

void CTransaction::AddToBlock(CBlock* pblock) const 
{
    LogPrint("sc", "%s():%d - adding to block tx %s\n", __func__, __LINE__, GetHash().ToString());
    pblock->vtx.push_back(*this);
}

void CTransaction::AddToBlockTemplate(CBlockTemplate* pblocktemplate, CAmount fee, unsigned int sigops) const
{
    LogPrint("sc", "%s():%d - adding to block templ tx %s, fee=%s, sigops=%u\n", __func__, __LINE__,
        GetHash().ToString(), FormatMoney(fee), sigops);
    pblocktemplate->vTxFees.push_back(fee);
    pblocktemplate->vTxSigOps.push_back(sigops);
}

bool CTransaction::ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const
{
    if (!CheckBlockAtHeight(state, nHeight, dosLevel))
        return false;

    //Valid txs are:
    // at any height
    // at height < groth_fork v>=1 txs with PHGR proofs
    // at height >= groth_fork v=-3 shielded with GROTH proofs and v=1 transparent with joinsplit empty
    // at height >= sidechain_fork same as above but also v=-4 with joinsplit empty

    // sidechain fork (happens after groth fork)
    int sidechainVersion = 0; 
    bool areSidechainsSupported = ForkManager::getInstance().areSidechainsSupported(nHeight);
    if (areSidechainsSupported)
    {
        sidechainVersion = ForkManager::getInstance().getSidechainTxVersion(nHeight);
    }

    // groth fork
    const int shieldedTxVersion = ForkManager::getInstance().getShieldedTxVersion(nHeight);
    bool isGROTHActive = (shieldedTxVersion == GROTH_TX_VERSION);

    if(isGROTHActive)
    {
        //verify if transaction is transparent or related to sidechain...
        if (nVersion == TRANSPARENT_TX_VERSION  ||
            (areSidechainsSupported && (nVersion == sidechainVersion) ) )
        {
            //enforce empty joinsplit for transparent txs and sidechain tx
            if(!GetVjoinsplit().empty()) {
                return state.DoS(dosLevel, error("ContextualCheck(): transparent or sc tx but vjoinsplit not empty"),
                                     REJECT_INVALID, "bad-txns-transparent-jsnotempty");
            }
            return true;
        }

        // ... or the actual shielded version
        if(nVersion != GROTH_TX_VERSION)
        {
            LogPrintf("ContextualCheck(): rejecting (ver=%d) transaction at block height %d - groth_active[%d], sidechain_active[%d]\n",
                nVersion, nHeight, (int)isGROTHActive, (int)areSidechainsSupported);
            return state.DoS(dosLevel,
                             error("ContextualCheck(): unexpected tx version"),
                             REJECT_INVALID, "bad-tx-version-unexpected");
        }
        return true;
    }
    else
    {
        // sidechain fork is after groth one
        assert(!areSidechainsSupported);

        if(nVersion < TRANSPARENT_TX_VERSION)
        {
            LogPrintf("ContextualCheck(): rejecting (ver=%d) transaction at block height %d - groth_active[%d], sidechain_active[%d]\n",
                nVersion, nHeight, (int)isGROTHActive, (int)areSidechainsSupported);
            return state.DoS(0,
                             error("ContextualCheck(): unexpected tx version"),
                             REJECT_INVALID, "bad-tx-version-unexpected");
        }
        return true;
    }

    return true;
}

void CTransaction::AddJoinSplitToJSON(UniValue& entry) const
{
    entry.push_back(Pair("vjoinsplit", TxJoinSplitToJSON(*this)));
}

void CTransaction::AddSidechainOutsToJSON(UniValue& entry) const
{
    Sidechain::AddSidechainOutsToJSON(*this, entry);
}

bool CTransactionBase::VerifyScript(
        const CScript& scriptPubKey, unsigned int nFlags, unsigned int nIn, const CChain* chain,
        bool cacheStore, ScriptError* serror) const
{
    if (nIn >= GetVin().size() )
        return ::error("%s:%d can not verify Signature: nIn too large for vin size %d",
                                       GetHash().ToString(), nIn, GetVin().size());

    const CScript &scriptSig = GetVin()[nIn].scriptSig;

    if (!::VerifyScript(scriptSig, scriptPubKey, nFlags,
                      //CachingTransactionSignatureChecker(this, nIn, chain, cacheStore),
                      *MakeSignatureChecker(nIn, chain, cacheStore),
                      serror))
    {
        return ::error("%s:%d VerifySignature failed: %s", GetHash().ToString(), nIn, ScriptErrorString(*serror));
    }

    return true;
}

std::shared_ptr<BaseSignatureChecker> CTransaction::MakeSignatureChecker(unsigned int nIn, const CChain* chain, bool cacheStore) const
{
    return std::shared_ptr<BaseSignatureChecker>(new CachingTransactionSignatureChecker(this, nIn, chain, cacheStore));
}

bool CTransaction::ContextualCheckInputs(CValidationState &state, const CCoinsViewCache &view, bool fScriptChecks,
          const CChain& chain, unsigned int flags, bool cacheStore, const Consensus::Params& consensusParams,
          std::vector<CScriptCheck> *pvChecks) const
{
    return ::ContextualCheckInputs(*this, state, view, fScriptChecks, chain, flags, cacheStore, consensusParams, pvChecks);
}

std::string CTransaction::EncodeHex() const
{
    return EncodeHexTx(*this);
}

void CTransaction::Relay() const { ::Relay(*this); }

std::shared_ptr<const CTransactionBase>
CTransaction::MakeShared() const {
    return std::shared_ptr<const CTransactionBase>(new CTransaction(*this));
}

#endif // BITCOIN_TX
