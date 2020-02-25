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

#include "main.h"

CScCertificate::CScCertificate() : CTransactionBase(),
    scId(), epochNumber(EPOCH_NULL), endEpochBlockHash(), totalAmount(), vbt_ccout(), nonce() { }

CScCertificate::CScCertificate(const CMutableScCertificate &cert) :
    scId(cert.scId), epochNumber(cert.epochNumber), endEpochBlockHash(cert.endEpochBlockHash),
    totalAmount(cert.totalAmount), vbt_ccout(cert.vbt_ccout), nonce(cert.nonce)
{
    *const_cast<int*>(&nVersion) = cert.nVersion;
    *const_cast<std::vector<CTxOut>*>(&vout) = cert.vout;
    UpdateHash();
}

CScCertificate& CScCertificate::operator=(const CScCertificate &cert) {
    CTransactionBase::operator=(cert);
    //---
    *const_cast<uint256*>(&scId) = cert.scId;
    *const_cast<int*>(&epochNumber) = cert.epochNumber;
    *const_cast<uint256*>(&endEpochBlockHash) = cert.endEpochBlockHash;
    *const_cast<CAmount*>(&totalAmount) = cert.totalAmount;
    *const_cast<std::vector<CTxBackwardTransferCrosschainOut>*>(&vbt_ccout) = cert.vbt_ccout;
    *const_cast<uint256*>(&nonce) = cert.nonce;
    return *this;
}

CScCertificate::CScCertificate(const CScCertificate &cert) : epochNumber(0), totalAmount(0) {
    // call explicitly the copy of members of virtual base class
    *const_cast<uint256*>(&hash) = cert.hash;
    *const_cast<int*>(&nVersion) = cert.nVersion;
    *const_cast<std::vector<CTxOut>*>(&vout) = cert.vout;
    //---
    *const_cast<uint256*>(&scId) = cert.scId;
    *const_cast<int*>(&epochNumber) = cert.epochNumber;
    *const_cast<uint256*>(&endEpochBlockHash) = cert.endEpochBlockHash;
    *const_cast<CAmount*>(&totalAmount) = cert.totalAmount;
    *const_cast<std::vector<CTxBackwardTransferCrosschainOut>*>(&vbt_ccout) = cert.vbt_ccout;
    *const_cast<uint256*>(&nonce) = cert.nonce;
}

void CScCertificate::UpdateHash() const
{
    *const_cast<uint256*>(&hash) = SerializeHash(*this);
}

CAmount CScCertificate::GetFeeAmount(CAmount /* unused */) const
{
    // this in principle might be a negative number, the caller must check if that is legal
    return (totalAmount - GetValueOut());
}

unsigned int CScCertificate::CalculateSize() const
{
    unsigned int sz = ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
//    LogPrint("cert", "%s():%d -sz=%u\n", __func__, __LINE__, sz);
    return sz;
}

unsigned int CScCertificate::CalculateModifiedSize(unsigned int /* unused nTxSize*/) const
{
    return CalculateSize();
}

std::string CScCertificate::EncodeHex() const
{
    return EncodeHexCert(*this);
}

std::string CScCertificate::ToString() const
{
    std::string str;
    str += strprintf("CScCertificate(hash=%s, ver=%d, vout.size=%u, totAmount=%d.%08d\n)",
        GetHash().ToString().substr(0,10),
        nVersion,
        vout.size(),
        totalAmount / COIN, totalAmount % COIN);
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";
// empty for the time being
//    for (unsigned int i = 0; i < vbt_ccout.size(); i++)
//        str += "    " + vbt_ccout[i].ToString() + "\n";

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

void CScCertificate::UpdateCoins(CValidationState &state, CCoinsViewCache& view, int nHeight) const
{
    // no inputs in cert, therefore no need to handle block undo
    CBlockUndo dum;
    UpdateCoins(state, view, dum, nHeight);
}

void CScCertificate::UpdateCoins(CValidationState &state, CCoinsViewCache& view, CBlockUndo& unused, int nHeight) const
{
    // no inputs in cert, therefore no need to handle block undo
    LogPrint("cert", "%s():%d - adding coins for cert [%s]\n", __func__, __LINE__, GetHash().ToString());
    view.ModifyCoins(GetHash())->FromTx(*this, nHeight);
}

bool CScCertificate::ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const 
{
    bool areScSupported = zen::ForkManager::getInstance().areSidechainsSupported(nHeight);
    if (!areScSupported)
         return state.DoS(dosLevel, error("Sidechain are not supported"), REJECT_INVALID, "bad-cert-version");
    return true;
}

bool CScCertificate::CheckFinal(int flags) const
{
    // as of now certificate finality has yet to be defined
    return true;
}


double CScCertificate::GetPriority(const CCoinsViewCache &view, int nHeight) const
{
    // TODO cert: for the time being return max prio, as shielded txes do
    return MAXIMUM_PRIORITY;
}

//--------------------------------------------------------------------------------------------
// binaries other than zend that are produced in the build, do not call these members and therefore do not
// need linking all of the related symbols. We use this macro as it is already defined with a similar purpose
// in zen-tx binary build configuration
#ifdef BITCOIN_TX

bool CScCertificate::TryPushToMempool(bool fLimitFree, bool fRejectAbsurdFee) {return true;}

bool CScCertificate::IsApplicableToState(CValidationState& state) const { return true; }
bool CScCertificate::IsStandard(std::string& reason, int nHeight) const { return true; }
unsigned int CScCertificate::GetLegacySigOpCount() const { return 0; }
#else
bool CScCertificate::TryPushToMempool(bool fLimitFree, bool fRejectAbsurdFee)
{
    CValidationState state;
    return ::AcceptCertificateToMemoryPool(mempool, state, *this, fLimitFree, nullptr, fRejectAbsurdFee);
}

bool CScCertificate::IsApplicableToState(CValidationState& state) const
{
    LogPrint("cert", "%s():%d - cert [%s]\n", __func__, __LINE__, GetHash().ToString());
    return Sidechain::ScMgr::instance().IsCertApplicableToState(*this, state);
}
    
bool CScCertificate::IsStandard(std::string& reason, int nHeight) const
{
    if (!zen::ForkManager::getInstance().areSidechainsSupported(nHeight))
    {
        reason = "version";
        return false;
    }

    return CheckOutputsAreStandard(nHeight, reason);
}

unsigned int CScCertificate::GetLegacySigOpCount() const 
{
    unsigned int nSigOps = 0;
    BOOST_FOREACH(const CTxOut& txout, vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}
#endif

void CScCertificate::getCrosschainOutputs(std::map<uint256, std::vector<uint256> >& map) const
{
    unsigned int nIdx = 0;
    LogPrint("sc", "%s():%d -getting leaves for cert vout\n", __func__, __LINE__);
    fillCrosschainOutput(scId, vout, nIdx, map);

    LogPrint("sc", "%s():%d -getting leaves for cert vbt out\n", __func__, __LINE__);
    fillCrosschainOutput(scId, vbt_ccout, nIdx, map);

    LogPrint("sc", "%s():%d - nIdx[%d]\n", __func__, __LINE__, nIdx);
}

// Mutable Certificate
//-------------------------------------
CMutableScCertificate::CMutableScCertificate() : totalAmount() {}

CMutableScCertificate::CMutableScCertificate(const CScCertificate& cert) :
    scId(cert.scId), epochNumber(cert.epochNumber), endEpochBlockHash(cert.endEpochBlockHash),
    totalAmount(cert.totalAmount), vbt_ccout(cert.vbt_ccout), nonce(cert.nonce)
{
    nVersion = cert.nVersion;
    vout = cert.vout;
}

uint256 CMutableScCertificate::GetHash() const
{
    return SerializeHash(*this);
}

// Crosschain out
//-------------------------------------
std::string CTxBackwardTransferCrosschainOut::ToString() const
{
    return strprintf("CTxBackwardTransferCrosschainOut()");
}

uint256 CTxBackwardTransferCrosschainOut::GetHash() const
{
    return SerializeHash(*this);
}

