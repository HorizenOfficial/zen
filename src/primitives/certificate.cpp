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
#include "script/sigcache.h"
#include "main.h"
#include "sc/proofverifier.h"
#include <univalue.h>

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
    endEpochCumScTxCommTreeRoot(), scProof(), vFieldElementCertificateField(),
    vBitVectorCertificateField(), nFirstBwtPos(0),
    forwardTransferScFee(INT_NULL), mainchainBackwardTransferRequestScFee(INT_NULL) {}

CScCertificate::CScCertificate(const CScCertificate &cert): CTransactionBase(cert),
    scId(cert.scId), epochNumber(cert.epochNumber), quality(cert.quality),
    endEpochCumScTxCommTreeRoot(cert.endEpochCumScTxCommTreeRoot),
    scProof(cert.scProof), vFieldElementCertificateField(cert.vFieldElementCertificateField),
    vBitVectorCertificateField(cert.vBitVectorCertificateField),
    nFirstBwtPos(cert.nFirstBwtPos), forwardTransferScFee(cert.forwardTransferScFee),
    mainchainBackwardTransferRequestScFee(cert.mainchainBackwardTransferRequestScFee) {}

CScCertificate& CScCertificate::operator=(const CScCertificate &cert)
{
    CTransactionBase::operator=(cert);
    *const_cast<uint256*>(&scId) = cert.scId;
    *const_cast<int32_t*>(&epochNumber) = cert.epochNumber;
    *const_cast<int64_t*>(&quality) = cert.quality;
    *const_cast<CFieldElement*>(&endEpochCumScTxCommTreeRoot) = cert.endEpochCumScTxCommTreeRoot;
    *const_cast<CScProof*>(&scProof) = cert.scProof;
    *const_cast<std::vector<FieldElementCertificateField>*>(&vFieldElementCertificateField) = cert.vFieldElementCertificateField;
    *const_cast<std::vector<BitVectorCertificateField>*>(&vBitVectorCertificateField) = cert.vBitVectorCertificateField;
    *const_cast<int*>(&nFirstBwtPos) = cert.nFirstBwtPos;
    *const_cast<CAmount*>(&forwardTransferScFee) = cert.forwardTransferScFee;
    *const_cast<CAmount*>(&mainchainBackwardTransferRequestScFee) = cert.mainchainBackwardTransferRequestScFee;
    return *this;
}

CScCertificate::CScCertificate(const CMutableScCertificate &cert): CTransactionBase(cert),
    scId(cert.scId), epochNumber(cert.epochNumber), quality(cert.quality),
    endEpochCumScTxCommTreeRoot(cert.endEpochCumScTxCommTreeRoot),
    scProof(cert.scProof), vFieldElementCertificateField(cert.vFieldElementCertificateField),
    vBitVectorCertificateField(cert.vBitVectorCertificateField),
    nFirstBwtPos(cert.nFirstBwtPos), forwardTransferScFee(cert.forwardTransferScFee),
    mainchainBackwardTransferRequestScFee(cert.mainchainBackwardTransferRequestScFee)
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
        return state.DoS(100, error("cert version"), CValidationState::Code::INVALID, "bad-cert-version");
    }

    return true;
}

bool CScCertificate::IsVersionStandard(int nHeight) const
{
    return true;
}

bool CScCertificate::CheckInputsOutputsNonEmpty(CValidationState &state) const
{
    // Certificates can not contain empty `vin` 
    if (GetVin().empty())
    {
        LogPrint("sc", "%s():%d - Error: cert[%s]\n", __func__, __LINE__, GetHash().ToString() );
        return state.DoS(10, error("%s(): vin empty", __func__),
                         CValidationState::Code::INVALID, "bad-cert-vin-empty");
    }

    return true;
}

bool CScCertificate::CheckSerializedSize(CValidationState &state) const
{
    uint32_t size = GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
    if (size > MAX_CERT_SIZE) {
        LogPrintf("%s():%d - Cert id = %s, size = %d, limit = %d, cert = %s\n",
            __func__, __LINE__, GetHash().ToString(), size, MAX_CERT_SIZE, ToString());
        return state.DoS(100, error("checkSerializedSizeLimits(): size limits failed"),
                         CValidationState::Code::INVALID, "bad-cert-oversize");
    }

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
                             CValidationState::Code::INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, error("CheckAmounts(): txout.nValue too high"),
                             CValidationState::Code::INVALID, "bad-txns-vout-toolarge");

        if (pos >= nFirstBwtPos && txout.nValue == 0)
            return state.DoS(100, error("CheckAmounts(): backward transfer has zero amount"),
                             CValidationState::Code::INVALID, "bad-txns-bwd-vout-zero");
        nCumulatedValueOut += txout.nValue;
        if (!MoneyRange(nCumulatedValueOut))
            return state.DoS(100, error("CheckAmounts(): txout total out of range"),
                             CValidationState::Code::INVALID, "bad-txns-txouttotal-toolarge");
    }

    if (!MoneyRange(GetValueOfBackwardTransfers()))
    {
        LogPrint("sc", "%s():%d - Invalid cert[%s] : certificate amount is outside range\n",
            __func__, __LINE__, GetHash().ToString() );
        return state.DoS(100, error("%s: certificate amount is outside range",
            __func__), CValidationState::Code::INVALID, "bwd-transfer-amount-outside-range");
    }

    return true;
}

bool CScCertificate::CheckFeeAmount(const CAmount& totalVinAmount, CValidationState& state) const
{
    if (!MoneyRange(totalVinAmount))
        return state.DoS(100, error("CheckFeeAmount(): total input amount out of range"),
                         CValidationState::Code::INVALID, "bad-cert-inputvalues-outofrange");

    // check all of the outputs because change is computed subtracting bwd transfers from them
    if (!CheckAmounts(state))
        return false;

    if (totalVinAmount < GetValueOfChange() )
        return state.DoS(100, error("CheckInputs(): %s value in (%s) < value out (%s)",
                                    GetHash().ToString(),
                                    FormatMoney(totalVinAmount), FormatMoney(GetValueOfChange()) ),
                         CValidationState::Code::INVALID, "bad-cert-in-belowout");

    CAmount nCertFee = totalVinAmount - GetValueOfChange();
    if (nCertFee < 0)
        return state.DoS(100, error("CheckFeeAmount(): %s nCertFee < 0", GetHash().ToString()),
                         CValidationState::Code::INVALID, "bad-cert-fee-negative");

    if (!MoneyRange(nCertFee))
        return state.DoS(100, error("CheckFeeAmount(): nCertFee out of range"),
                         CValidationState::Code::INVALID, "bad-cert-fee-outofrange");

    return true;
}

bool CScCertificate::CheckInputsInteraction(CValidationState &state) const
{
    for(const CTxIn& txin: vin)
        if (txin.prevout.IsNull())
            return state.DoS(10, error("CheckInputsInteraction(): prevout is null"),
                             CValidationState::Code::INVALID, "bad-txns-prevout-null");

    return true;
}

CAmount CScCertificate::GetFeeAmount(const CAmount& valueIn) const
{
    return (valueIn - GetValueOfChange());
}
#ifdef BITCOIN_TX
std::string CScCertificate::EncodeHex() const {return std::string{};}
#else
std::string CScCertificate::EncodeHex() const
{
    return EncodeHexCert(*this);
}
#endif

std::string CScCertificate::ToString() const
{
    CAmount total = GetValueOfBackwardTransfers();
    std::string str;
    str += strprintf("CScCertificate(hash=%s, ver=%d, vin.size()=%s, vout.size=%u, nFirstBwtPos=%d, ftFee=%d, mbtrFee=%d totAmount=%d.%08d\n)",
        GetHash().ToString().substr(0,10),
        nVersion,
        vin.size(),
        vout.size(),
        nFirstBwtPos,
        forwardTransferScFee,
        mainchainBackwardTransferRequestScFee,
        total / COIN, total % COIN);

    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].ToString() + "\n";
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";

    return str;
}

bool CScCertificate::CheckInputsLimit() const {
    // Node operator can choose to reject tx by number of transparent inputs
    static_assert(std::numeric_limits<size_t>::max() >= std::numeric_limits<int64_t>::max(), "size_t too small");
    size_t limit = (size_t) GetArg("-mempooltxinputlimit", 0);
    if (limit > 0) {
        size_t n = GetVin().size();
        if (n > limit) {
            LogPrint("mempool", "%s():%d - Dropping cert %s : too many transparent inputs %zu > limit %zu\n",
                __func__, __LINE__, GetHash().ToString(), n, limit);
            return false;
        }
    }
    return true;
}

//--------------------------------------------------------------------------------------------
// binaries other than zend that are produced in the build, do not call these members and therefore do not
// need linking all of the related symbols. We use this macro as it is already defined with a similar purpose
// in zen-tx binary build configuration
#ifdef BITCOIN_TX
bool CScCertificate::ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const { return false;}
bool CScCertificate::VerifyScript(
        const CScript& scriptPubKey, unsigned int nFlags, unsigned int nIn, const CChain* chain,
        bool cacheStore, ScriptError* serror) const { return true; }
void CScCertificate::AddJoinSplitToJSON(UniValue& entry) const { return; }
void CScCertificate::Relay() const {}
std::shared_ptr<const CTransactionBase> CScCertificate::MakeShared() const
{
    return std::shared_ptr<const CTransactionBase>();
}
CFieldElement CScCertificate::GetDataHash(const Sidechain::ScFixedParameters& scFixedParams) const
{
     static const CFieldElement dummy; return dummy;
}
#else
bool CScCertificate::ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const
{
    bool areScSupported = zen::ForkManager::getInstance().areSidechainsSupported(nHeight);

    if (!areScSupported)
         return state.DoS(dosLevel, error("Sidechain are not supported"), CValidationState::Code::INVALID, "bad-cert-version");

    if (!CheckBlockAtHeight(state, nHeight, dosLevel))
        return false;

    return true;
}

bool CScCertificate::VerifyScript(
        const CScript& scriptPubKey, unsigned int nFlags, unsigned int nIn, const CChain* chain,
        bool cacheStore, ScriptError* serror) const
{
    if (nIn >= GetVin().size() )
        return ::error("%s:%d can not verify Signature: nIn too large for vin size %d",
                                       GetHash().ToString(), nIn, GetVin().size());

    const CScript &scriptSig = GetVin()[nIn].scriptSig;

    if (!::VerifyScript(scriptSig, scriptPubKey, nFlags,
                      CachingCertificateSignatureChecker(this, nIn, chain, cacheStore),
                      serror))
    {
        return ::error("%s:%d VerifySignature failed: %s", GetHash().ToString(), nIn, ScriptErrorString(*serror));
    }

    return true;
}

void CScCertificate::AddJoinSplitToJSON(UniValue& entry) const
{
    entry.pushKV("vjoinsplit", UniValue{UniValue::VARR});
}

void CScCertificate::Relay() const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(10000);
    ss << *this;
    ::Relay(*this, ss);
}

std::shared_ptr<const CTransactionBase>
CScCertificate::MakeShared() const {
    return std::shared_ptr<const CTransactionBase>(new CScCertificate(*this));
}

CFieldElement CScCertificate::GetDataHash(const Sidechain::ScFixedParameters& scFixedParams) const
{
    CCertProofVerifierInput input = CScProofVerifier::CertificateToVerifierItem(*this, scFixedParams, nullptr, nullptr);

    int custom_fields_len = input.vCustomFields.size(); 
    std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
    int i = 0;
    std::vector<wrappedFieldPtr> vSptr;
    for (auto entry: input.vCustomFields)
    {
        wrappedFieldPtr sptrFe = entry.GetFieldElement();
        custom_fields[i] = sptrFe.get();
        vSptr.push_back(sptrFe);
        i++;
    }

    const backward_transfer_t* bt_list_ptr = input.bt_list.data();
    int bt_list_len = input.bt_list.size();

    // mc crypto lib wants a null ptr if we have no fields
    if (custom_fields_len == 0)
        custom_fields.reset();
    if (bt_list_len == 0)
        bt_list_ptr = nullptr;

    wrappedFieldPtr sptrScId = CFieldElement(input. scId).GetFieldElement();
    field_t* scidFe = sptrScId.get();

    CctpErrorCode errorCode;

    field_t* certDataHash = zendoo_get_cert_data_hash(scidFe,
                                                      input.epochNumber,
                                                      input.quality,
                                                      bt_list_ptr,
                                                      bt_list_len,
                                                      custom_fields.get(),
                                                      custom_fields_len,
                                                      input.endEpochCumScTxCommTreeRoot.GetFieldElement().get(),
                                                      input.mainchainBackwardTransferRequestScFee,
                                                      input.forwardTransferScFee,
                                                      &errorCode
                                                      );
    if (errorCode != CctpErrorCode::OK)
    {
        LogPrintf("%s():%d - could not get cert data hash: error code[0x%x]\n", __func__, __LINE__, errorCode);
        assert(certDataHash == nullptr);
    }

    return CFieldElement{wrappedFieldPtr{certDataHash, CFieldPtrDeleter{}}};
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
    endEpochCumScTxCommTreeRoot(), scProof(), vFieldElementCertificateField(),
    vBitVectorCertificateField(), nFirstBwtPos(0),
    forwardTransferScFee(CScCertificate::INT_NULL), mainchainBackwardTransferRequestScFee(CScCertificate::INT_NULL) {}

CMutableScCertificate::CMutableScCertificate(const CScCertificate& cert): CMutableTransactionBase(),
    scId(cert.GetScId()), epochNumber(cert.epochNumber), quality(cert.quality), 
    endEpochCumScTxCommTreeRoot(cert.endEpochCumScTxCommTreeRoot),
    scProof(cert.scProof), vFieldElementCertificateField(cert.vFieldElementCertificateField),
    vBitVectorCertificateField(cert.vBitVectorCertificateField), nFirstBwtPos(cert.nFirstBwtPos),
    forwardTransferScFee(cert.forwardTransferScFee), mainchainBackwardTransferRequestScFee(cert.mainchainBackwardTransferRequestScFee)
{
    nVersion = cert.nVersion;
    vin  = cert.GetVin();
    vout = cert.GetVout();
}

CMutableScCertificate& CMutableScCertificate::operator=(const CMutableScCertificate& rhs)
{
    nVersion                              = rhs.nVersion;
    vin                                   = rhs.vin;
    vout                                  = rhs.vout;
    scId                                  = rhs.scId;
    epochNumber                           = rhs.epochNumber;
    quality                               = rhs.quality;
    endEpochCumScTxCommTreeRoot           = rhs.endEpochCumScTxCommTreeRoot;
    scProof                               = rhs.scProof;
    vFieldElementCertificateField         = rhs.vFieldElementCertificateField;
    vBitVectorCertificateField            = rhs.vBitVectorCertificateField;
    *const_cast<int*>(&nFirstBwtPos)      = rhs.nFirstBwtPos;
    forwardTransferScFee                  = rhs.forwardTransferScFee;
    mainchainBackwardTransferRequestScFee = rhs.mainchainBackwardTransferRequestScFee;

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

std::string CMutableScCertificate::ToString() const
{
    std::string str;
    str += strprintf("CMutableScCertificate(hash=%s, ver=%d, ftFee=%d, mbtrFee=%d, vin.size()=%s, vout.size=%u\n)",
        GetHash().ToString().substr(0,10),
        nVersion,
        forwardTransferScFee,
        mainchainBackwardTransferRequestScFee,
        vin.size(),
        vout.size() );

    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].ToString() + "\n";
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";

    return str;
}


