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

CBackwardTransferOut::CBackwardTransferOut(const CTxOut& txout): nValue(txout.nValue)
{
    auto it = std::find(txout.scriptPubKey.begin(), txout.scriptPubKey.end(), OP_HASH160);
    assert(it != txout.scriptPubKey.end());
    ++it;
    assert(*it == sizeof(uint160));
    ++it;
    std::vector<unsigned char>  pubKeyV(it, (it + sizeof(uint160)));
    pubKeyHash = uint160(pubKeyV);
}

CScCertificate::CScCertificate(int versionIn): CTransactionBase(versionIn),
    scId(), epochNumber(EPOCH_NOT_INITIALIZED), endEpochBlockHash(), nFirstBwtPos(-1) {}

CScCertificate::CScCertificate(const CScCertificate &cert): CTransactionBase(cert),
    scId(cert.scId), epochNumber(cert.epochNumber),
    endEpochBlockHash(cert.endEpochBlockHash), nFirstBwtPos(cert.nFirstBwtPos) {}

CScCertificate& CScCertificate::operator=(const CScCertificate &cert)
{
    CTransactionBase::operator=(cert);
    *const_cast<uint256*>(&scId) = cert.scId;
    *const_cast<int32_t*>(&epochNumber) = cert.epochNumber;
    *const_cast<uint256*>(&endEpochBlockHash) = cert.endEpochBlockHash;
    *const_cast<int*>(&nFirstBwtPos) = cert.nFirstBwtPos;
    return *this;
}

CScCertificate::CScCertificate(const CMutableScCertificate &cert): CTransactionBase(cert),
    scId(cert.scId), epochNumber(cert.epochNumber), endEpochBlockHash(cert.endEpochBlockHash), nFirstBwtPos(0)
{
    for(const CTxOut& out: cert.vout)
    {
        if (!out.isFromBackwardTransfer)
            ++(*const_cast<int*>(&nFirstBwtPos));
        else
            break;
    }

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
    return true;
}

bool CScCertificate::CheckVersionIsStandard(std::string& reason, int nHeight) const {
    if (!zen::ForkManager::getInstance().areSidechainsSupported(nHeight))
    {
        reason = "version";
        return false;
    }

    return true;
}

bool CScCertificate::CheckAmounts(CValidationState &state) const
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

        if (txout.isFromBackwardTransfer && txout.nValue == 0)
            return state.DoS(100, error("CheckAmounts(): backward transfer has zero amount"),
                             REJECT_INVALID, "bad-txns-bwd-vout-zero");
        nCumulatedValueOut += txout.nValue;
        if (!MoneyRange(nCumulatedValueOut))
            return state.DoS(100, error("CheckAmounts(): txout total out of range"),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
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
    str += strprintf("CScCertificate(hash=%s, ver=%d, vin.size()=%s, vout.size=%u, totAmount=%d.%08d\n)",
        GetHash().ToString().substr(0,10),
        nVersion,
        vin.size(),
        vout.size(),
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

bool CScCertificate::CheckFinal(int flags) const
{
    // as of now certificate finality has yet to be defined (see tx.nLockTime)
    return true;
}

//--------------------------------------------------------------------------------------------
// binaries other than zend that are produced in the build, do not call these members and therefore do not
// need linking all of the related symbols. We use this macro as it is already defined with a similar purpose
// in zen-tx binary build configuration
#ifdef BITCOIN_TX
bool CScCertificate::TryPushToMempool(bool fLimitFree, bool fRejectAbsurdFee) const {return true;}
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
bool CScCertificate::TryPushToMempool(bool fLimitFree, bool fRejectAbsurdFee) const
{
    CValidationState state;
    return ::AcceptCertificateToMemoryPool(mempool, state, *this, fLimitFree, nullptr, fRejectAbsurdFee);
}

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
    for (auto out : vout)
        if (out.isFromBackwardTransfer)
            nValueOut += out.nValue;
    return nValueOut;
}

int CScCertificate::GetNumbOfBackwardTransfers() const
{
    int size = 0;
    for (auto out : vout)
        if (out.isFromBackwardTransfer)
            size += 1;
    return size;
}


// Mutable Certificate
//-------------------------------------
CMutableScCertificate::CMutableScCertificate() :
        scId(), epochNumber(CScCertificate::EPOCH_NULL), endEpochBlockHash() {}

CMutableScCertificate::CMutableScCertificate(const CScCertificate& cert) :
    scId(cert.GetScId()), epochNumber(cert.epochNumber), endEpochBlockHash(cert.endEpochBlockHash)
{
    nVersion = cert.nVersion;
    vin  = cert.GetVin();
    vout = cert.GetVout();
}

uint256 CMutableScCertificate::GetHash() const
{
    return SerializeHash(*this);
}

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


