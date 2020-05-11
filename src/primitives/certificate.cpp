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

CScCertificate::CScCertificate() : CTransactionBase(),
    scId(), epochNumber(EPOCH_NULL), endEpochBlockHash() { }

CScCertificate::CScCertificate(const CMutableScCertificate &cert) :
    scId(cert.scId), epochNumber(cert.epochNumber), endEpochBlockHash(cert.endEpochBlockHash)
{
    *const_cast<int*>(&nVersion) = cert.nVersion;
    *const_cast<std::vector<CTxIn>*>(&vin) = cert.vin;
    *const_cast<std::vector<CTxOut>*>(&vout) = cert.vout;
    UpdateHash();
}

CScCertificate& CScCertificate::operator=(const CScCertificate &cert) {
    CTransactionBase::operator=(cert);
    //---
    *const_cast<uint256*>(&scId) = cert.scId;
    *const_cast<int32_t*>(&epochNumber) = cert.epochNumber;
    *const_cast<uint256*>(&endEpochBlockHash) = cert.endEpochBlockHash;
    return *this;
}

CScCertificate::CScCertificate(const CScCertificate &cert) : epochNumber(0) {
    // call explicitly the copy of members of virtual base class
    *const_cast<int32_t*>(&nVersion) = cert.nVersion;
    *const_cast<std::vector<CTxIn>*>(&vin) = cert.vin;
    *const_cast<std::vector<CTxOut>*>(&vout) = cert.vout;
    *const_cast<uint256*>(&hash) = cert.hash;
    //---
    *const_cast<uint256*>(&scId) = cert.scId;
    *const_cast<int32_t*>(&epochNumber) = cert.epochNumber;
    *const_cast<uint256*>(&endEpochBlockHash) = cert.endEpochBlockHash;
}

void CScCertificate::UpdateHash() const
{
    *const_cast<uint256*>(&hash) = SerializeHash(*this);
}

bool CScCertificate::CheckVersionBasic(CValidationState &state) const
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

bool CScCertificate::CheckInputsAvailability(CValidationState &state) const
{
    // there might be no inputs if 0 fee, therefore this never fails
    return true;
}

bool CScCertificate::CheckOutputsAvailability(CValidationState &state) const
{
    // we allow empty certificate, that is with no backward transfers and no change
    return true;
}

bool CScCertificate::CheckSerializedSize(CValidationState &state) const
{
    BOOST_STATIC_ASSERT(MAX_BLOCK_SIZE > MAX_CERT_SIZE); // sanity
    if (CalculateSize() > MAX_CERT_SIZE)
    {
        return state.DoS(100, error("size limits failed"), REJECT_INVALID, "bad-cert-oversize");
    }

    return true;
}

bool CScCertificate::CheckFeeAmount(const CAmount& totalVinAmount, CValidationState& state) const
{
    if (!MoneyRange(totalVinAmount))
        return state.DoS(100, error("CheckFeeAmount(): total input amount out of range"),
                         REJECT_INVALID, "bad-cert-inputvalues-outofrange");

    // check all of the outputs because change is computed subtracting bwd transfers from them
    if (!CheckOutputsAmount(state))
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

CAmount CScCertificate::GetFeeAmount(const CAmount& valueIn) const
{
    return (valueIn - GetValueOfChange());
}

unsigned int CScCertificate::CalculateSize() const
{
    unsigned int sz = ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
//    LogPrint("cert", "%s():%d -sz=%u\n", __func__, __LINE__, sz);
    return sz;
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
bool CScCertificate::TryPushToMempool(bool fLimitFree, bool fRejectAbsurdFee) {return true;}
std::shared_ptr<BaseSignatureChecker> CScCertificate::MakeSignatureChecker(unsigned int nIn, const CChain* chain, bool cacheStore) const
{
    return std::shared_ptr<BaseSignatureChecker>(NULL);
}

bool CScCertificate::AcceptTxBaseToMemoryPool(CTxMemPool& pool, CValidationState &state, bool fLimitFree, 
    bool* pfMissingInputs, bool fRejectAbsurdFee) const { return true; }
void CScCertificate::Relay() const {}
unsigned int CScCertificate::GetSerializeSizeBase(int nType, int nVersion) const { return 0;}
std::shared_ptr<const CTransactionBase> CScCertificate::MakeShared() const
{
    return std::shared_ptr<const CTransactionBase>();
}
#else
bool CScCertificate::TryPushToMempool(bool fLimitFree, bool fRejectAbsurdFee)
{
    CValidationState state;
    return ::AcceptCertificateToMemoryPool(mempool, state, *this, fLimitFree, nullptr, fRejectAbsurdFee);
}

std::shared_ptr<BaseSignatureChecker> CScCertificate::MakeSignatureChecker(unsigned int nIn, const CChain* chain, bool cacheStore) const
{
    return std::shared_ptr<BaseSignatureChecker>(new CachingCertificateSignatureChecker(this, nIn, chain, cacheStore));
}

bool CScCertificate::AcceptTxBaseToMemoryPool(CTxMemPool& pool, CValidationState &state, bool fLimitFree, 
    bool* pfMissingInputs, bool fRejectAbsurdFee) const
{
    return ::AcceptCertificateToMemoryPool(pool, state, *this, fLimitFree, pfMissingInputs, fRejectAbsurdFee);
}

void CScCertificate::Relay() const { ::Relay(*this); }

unsigned int CScCertificate::GetSerializeSizeBase(int nType, int nVersion) const { return this->GetSerializeSize(nType, nVersion);}

std::shared_ptr<const CTransactionBase>
CScCertificate::MakeShared() const {
    return std::shared_ptr<const CTransactionBase>(new CScCertificate(*this));
}
#endif

void CScCertificate::addToScCommitment(std::map<uint256, uint256>& map, std::set<uint256>& sScIds) const
{
    sScIds.insert(scId);
    map[scId] = GetHash();
}

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


