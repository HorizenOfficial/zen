#include "primitives/certificate.h"
#include "primitives/block.h"
#include "undo.h"
#include "coins.h"
#include "validationinterface.h"
#include "coins.h"
#include "core_io.h"
#include "miner.h"
#include "utilmoneystr.h"
#include "consensus/validation.h"
#include "sc/sidechain.h"
#include "txmempool.h"
#include "zen/forkmanager.h"
#include "script/interpreter.h"

#include "main.h"

bool CScCertificate::IsScEquivalent(const CScCertificate& a, const CScCertificate& b)
{
    // first of all check simple attributes
    bool attrAreEqual = (
        a.scId              == b.scId              &&
        a.epochNumber       == b.epochNumber       &&
        a.quality           == b.quality           &&
        a.endEpochBlockHash == b.endEpochBlockHash &&
        // a.scProof           == b.scProof       && // this is most probably different anyway
        a.nFirstBwtPos      == b.nFirstBwtPos
    );

    if (!attrAreEqual)
        return false;

    // check backward transfers contributions only
    for(int pos = a.nFirstBwtPos; pos < a.vout.size(); ++pos)
        if (a.vout[pos] != b.vout[pos])
            return false;

    // all concerned fields are the same

    // TODO - remove, just for test
    assert(a.GetScAttributesHash() == b.GetScAttributesHash());

    return true;
}

uint256 CScCertificate::GetScAttributesHash() const
{
    CHashWriter ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << scId;
    ss << epochNumber;
    ss << quality;
    ss << endEpochBlockHash;
    ss << nFirstBwtPos;

    for(int pos = nFirstBwtPos; pos < vout.size(); ++pos)
        ss << vout[pos];

    return ss.GetHash();
}

CBackwardTransferOut::CBackwardTransferOut(const CTxOut& txout): nValue(txout.nValue), pubKeyHash()
{
    if (!txout.IsNull())
    {
        auto it = std::find(txout.scriptPubKey.begin(), txout.scriptPubKey.end(), OP_HASH160);
        assert(it != txout.scriptPubKey.end());
        ++it;
        assert(*it == sizeof(uint160));
        ++it;
        std::vector<unsigned char>  pubKeyV(it, (it + sizeof(uint160)));
        pubKeyHash = uint160(pubKeyV);
    }
}

CScCertificate::CScCertificate(int versionIn): CTransactionBase(versionIn),
    scId(), epochNumber(EPOCH_NOT_INITIALIZED), quality(QUALITY_NULL),
    endEpochBlockHash(), scProof(), nFirstBwtPos(0) {}

CScCertificate::CScCertificate(const CScCertificate &cert): CTransactionBase(cert),
    scId(cert.scId), epochNumber(cert.epochNumber), quality(cert.quality),
    endEpochBlockHash(cert.endEpochBlockHash), scProof(cert.scProof), nFirstBwtPos(cert.nFirstBwtPos) {}

CScCertificate& CScCertificate::operator=(const CScCertificate &cert)
{
    CTransactionBase::operator=(cert);
    *const_cast<uint256*>(&scId) = cert.scId;
    *const_cast<int32_t*>(&epochNumber) = cert.epochNumber;
    *const_cast<int64_t*>(&quality) = cert.quality;
    *const_cast<uint256*>(&endEpochBlockHash) = cert.endEpochBlockHash;
    *const_cast<libzendoomc::ScProof*>(&scProof) = cert.scProof;
    *const_cast<int*>(&nFirstBwtPos) = cert.nFirstBwtPos;
    return *this;
}

CScCertificate::CScCertificate(const CMutableScCertificate &cert): CTransactionBase(cert),
    scId(cert.scId), epochNumber(cert.epochNumber), quality(cert.quality),
    endEpochBlockHash(cert.endEpochBlockHash), scProof(cert.scProof), nFirstBwtPos(cert.nFirstBwtPos)
{
    UpdateHash();
}

void CScCertificate::UpdateHash() const
{
    *const_cast<uint256*>(&hash) = SerializeHash(*this);
}

bool CScCertificate::IsBackwardTransfer(int pos) const
{
    assert(pos >= 0 && pos < vout.size());
    return pos >= nFirstBwtPos;
}

bool CScCertificate::IsValidVersion(CValidationState &state) const
{
    if (nVersion != SC_CERT_VERSION )
    {
        LogPrint("sc", "%s():%d - Invalid cert[%s] : certificate bad version %d\n",
            __func__, __LINE__, GetHash().ToString(), nVersion );
        return state.DoS(100, error("cert version"), REJECT_INVALID, "bad-cert-version");
    }

    return true;
}

bool CScCertificate::IsVersionStandard(int nHeight) const
{
    if (!zen::ForkManager::getInstance().areSidechainsSupported(nHeight))
        return false;

    return true;
}

bool CScCertificate::CheckAmounts(CValidationState &state) const
{
    // Check for negative or overflow output values
    CAmount nCumulatedValueOut = 0;
    for(unsigned int pos = 0; pos < vout.size(); ++pos)
    {
        const CTxOut & txout = vout[pos];
        if (txout.nValue < 0)
            return state.DoS(100, error("CheckAmounts(): txout.nValue negative"),
                             REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, error("CheckAmounts(): txout.nValue too high"),
                             REJECT_INVALID, "bad-txns-vout-toolarge");

        if (pos >= nFirstBwtPos && txout.nValue == 0)
            return state.DoS(100, error("CheckAmounts(): backward transfer has zero amount"),
                             REJECT_INVALID, "bad-txns-bwd-vout-zero");
        nCumulatedValueOut += txout.nValue;
        if (!MoneyRange(nCumulatedValueOut))
            return state.DoS(100, error("CheckAmounts(): txout total out of range"),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    if (!MoneyRange(GetValueOfBackwardTransfers()))
    {
        LogPrint("sc", "%s():%d - Invalid cert[%s] : certificate amount is outside range\n",
            __func__, __LINE__, GetHash().ToString() );
        return state.DoS(100, error("%s: certificate amount is outside range",
            __func__), REJECT_INVALID, "bwd-transfer-amount-outside-range");
    }

    return true;
}

bool CScCertificate::CheckFeeAmount(const CAmount& totalVinAmount, CValidationState& state) const
{
    if (!MoneyRange(totalVinAmount))
        return state.DoS(100, error("CheckFeeAmount(): total input amount out of range"),
                         REJECT_INVALID, "bad-cert-inputvalues-outofrange");

    // check all of the outputs because change is computed subtracting bwd transfers from them
    if (!CheckAmounts(state))
        return false;

    if (totalVinAmount < GetValueOfChange() )
        return state.DoS(100, error("CheckInputs(): %s value in (%s) < value out (%s)",
                                    GetHash().ToString(),
                                    FormatMoney(totalVinAmount), FormatMoney(GetValueOfChange()) ),
                         REJECT_INVALID, "bad-cert-in-belowout");

    CAmount nCertFee = totalVinAmount - GetValueOfChange();
    if (nCertFee < 0)
        return state.DoS(100, error("CheckFeeAmount(): %s nCertFee < 0", GetHash().ToString()),
                         REJECT_INVALID, "bad-cert-fee-negative");

    if (!MoneyRange(nCertFee))
        return state.DoS(100, error("CheckFeeAmount(): nCertFee out of range"),
                         REJECT_INVALID, "bad-cert-fee-outofrange");

    return true;
}

bool CScCertificate::CheckInputsInteraction(CValidationState &state) const
{
    for(const CTxIn& txin: vin)
        if (txin.prevout.IsNull())
            return state.DoS(10, error("CheckInputsInteraction(): prevout is null"),
                             REJECT_INVALID, "bad-txns-prevout-null");

    return true;
}

CAmount CScCertificate::GetFeeAmount(const CAmount& valueIn) const
{
    return (valueIn - GetValueOfChange());
}

std::string CScCertificate::EncodeHex() const
{
    return EncodeHexCert(*this);
}

std::string CScCertificate::ToString() const
{
    CAmount total = GetValueOfBackwardTransfers();
    std::string str;
    str += strprintf("CScCertificate(hash=%s, ver=%d, vin.size()=%s, vout.size=%u, nFirstBwtPos=%d, totAmount=%d.%08d,\n)",
        GetHash().ToString().substr(0,10),
        nVersion,
        vin.size(),
        vout.size(),
        nFirstBwtPos,
        total / COIN, total % COIN);
    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].ToString() + "\n";
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";

    return str;
}

void CScCertificate::AddToBlock(CBlock* pblock) const
{
    LogPrint("cert", "%s():%d - adding to block cert %s\n", __func__, __LINE__, GetHash().ToString());
    pblock->vcert.push_back(*this);
}

void CScCertificate::AddToBlockTemplate(CBlockTemplate* pblocktemplate, CAmount fee, unsigned int sigops) const
{
    LogPrint("cert", "%s():%d - adding to block templ cert %s, fee=%s, sigops=%u\n", __func__, __LINE__,
        GetHash().ToString(), FormatMoney(fee), sigops);
    pblocktemplate->vCertFees.push_back(fee);
    pblocktemplate->vCertSigOps.push_back(sigops);
}

bool CScCertificate::ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const 
{
    bool areScSupported = zen::ForkManager::getInstance().areSidechainsSupported(nHeight);

    if (!areScSupported)
         return state.DoS(dosLevel, error("Sidechain are not supported"), REJECT_INVALID, "bad-cert-version");

    if (!CheckBlockAtHeight(state, nHeight, dosLevel))
        return false;

    return true;
}

//--------------------------------------------------------------------------------------------
// binaries other than zend that are produced in the build, do not call these members and therefore do not
// need linking all of the related symbols. We use this macro as it is already defined with a similar purpose
// in zen-tx binary build configuration
#ifdef BITCOIN_TX
std::shared_ptr<BaseSignatureChecker> CScCertificate::MakeSignatureChecker(unsigned int nIn, const CChain* chain, bool cacheStore) const
{
    return std::shared_ptr<BaseSignatureChecker>(NULL);
}

void CScCertificate::Relay() const {}
std::shared_ptr<const CTransactionBase> CScCertificate::MakeShared() const
{
    return std::shared_ptr<const CTransactionBase>();
}
#else

std::shared_ptr<BaseSignatureChecker> CScCertificate::MakeSignatureChecker(unsigned int nIn, const CChain* chain, bool cacheStore) const
{
    return std::shared_ptr<BaseSignatureChecker>(new CachingCertificateSignatureChecker(this, nIn, chain, cacheStore));
}

void CScCertificate::Relay() const { ::Relay(*this); }

std::shared_ptr<const CTransactionBase>
CScCertificate::MakeShared() const {
    return std::shared_ptr<const CTransactionBase>(new CScCertificate(*this));
}
#endif

CAmount CScCertificate::GetValueOfBackwardTransfers() const
{
    CAmount nValueOut = 0;
    for(int pos = nFirstBwtPos; pos < vout.size(); ++pos)
        nValueOut += vout[pos].nValue;

    return nValueOut;
}

CAmount CScCertificate::GetValueOfChange() const
{
    CAmount nValueOut = 0;
    for(unsigned int pos = 0; pos < nFirstBwtPos; ++pos)
        nValueOut += vout[pos].nValue;
    return nValueOut;
}

// Mutable Certificate
//-------------------------------------
CMutableScCertificate::CMutableScCertificate(): CMutableTransactionBase(),
    scId(), epochNumber(CScCertificate::EPOCH_NULL), quality(CScCertificate::QUALITY_NULL),
    endEpochBlockHash(), scProof(), nFirstBwtPos(0) { }

CMutableScCertificate::CMutableScCertificate(const CScCertificate& cert): CMutableTransactionBase(),
    scId(cert.GetScId()), epochNumber(cert.epochNumber), quality(cert.quality), 
    endEpochBlockHash(cert.endEpochBlockHash), scProof(cert.scProof), nFirstBwtPos(cert.nFirstBwtPos)
{
    nVersion = cert.nVersion;
    vin  = cert.GetVin();
    vout = cert.GetVout();
}

CMutableScCertificate& CMutableScCertificate::operator=(const CMutableScCertificate& rhs)
{
    nVersion          = rhs.nVersion;
    vin               = rhs.vin;
    vout              = rhs.vout;
    scId              = rhs.scId;
    epochNumber       = rhs.epochNumber;
    quality           = rhs.quality;
    endEpochBlockHash = rhs.endEpochBlockHash;
    scProof           = rhs.scProof;
    *const_cast<int*>(&nFirstBwtPos) = rhs.nFirstBwtPos;

    return *this;
}

uint256 CMutableScCertificate::GetHash() const
{
    return SerializeHash(*this);
}

void CMutableScCertificate::insertAtPos(unsigned int pos, const CTxOut& out) {
    if (pos < nFirstBwtPos)
        ++(*const_cast<int*>(&nFirstBwtPos));

    vout.insert(vout.begin() + pos, out);
}

void CMutableScCertificate::eraseAtPos(unsigned int pos) {
    if (pos < nFirstBwtPos)
        --(*const_cast<int*>(&nFirstBwtPos));

    vout.erase(vout.begin() + pos);
}

void CMutableScCertificate::resizeOut(unsigned int newSize) {
    if (newSize > nFirstBwtPos)
        vout.insert(vout.begin() + nFirstBwtPos, newSize - nFirstBwtPos, CTxOut());

    if (newSize < nFirstBwtPos)
        vout.erase(vout.begin() + newSize, vout.begin() + nFirstBwtPos);

    (*const_cast<int*>(&nFirstBwtPos)) = newSize;
}

void CMutableScCertificate::resizeBwt(unsigned int newSize) {
    vout.resize(nFirstBwtPos + newSize);
}

bool CMutableScCertificate::addOut(const CTxOut& out) {
    vout.insert(vout.begin() + nFirstBwtPos, out);
    ++(*const_cast<int*>(&nFirstBwtPos));

    return true;
}

bool CMutableScCertificate::addBwt(const CTxOut& out) {
    vout.push_back(out);
    return true;
}

bool CMutableScCertificate::add(const CTxScCreationOut& out) {return false;}
bool CMutableScCertificate::add(const CTxForwardTransferOut& out) {return false;}

std::string CMutableScCertificate::ToString() const
{
    std::string str;
    str += strprintf("CMutableScCertificate(hash=%s, ver=%d, vin.size()=%s, vout.size=%u\n)",
        GetHash().ToString().substr(0,10),
        nVersion,
        vin.size(),
        vout.size() );
    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].ToString() + "\n";
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";

    return str;
}


