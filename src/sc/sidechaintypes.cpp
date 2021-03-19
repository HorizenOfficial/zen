#include "sc/sidechaintypes.h"
#include "util.h"
#include <consensus/consensus.h>

///////////////////////////////// Field types //////////////////////////////////
#ifdef BITCOIN_TX
    CFieldElement::CFieldElement() {};
    CFieldElement::CFieldElement(const std::vector<unsigned char>& byteArrayIn) {};
    CFieldElement::CFieldElement(const uint256& value) {};
    CFieldElement::CFieldElement(const wrappedFieldPtr& wrappedField) {};
    void CFieldElement::SetByteArray(const std::vector<unsigned char>& byteArrayIn) {};
    void CFieldElement::SetNull() {};
    bool CFieldElement::IsNull() const {return false;};
    wrappedFieldPtr CFieldElement::GetFieldElement() const {return nullptr;};
    bool CFieldElement::IsValid() const {return false;};
    std::string CFieldElement::GetHexRepr() const {return std::string{};};
    CFieldElement CFieldElement::ComputeHash(const CFieldElement& lhs, const CFieldElement& rhs) { return CFieldElement{}; }
    const std::vector<unsigned char>&  CFieldElement::GetByteArray() const { static std::vector<unsigned char> dummy{}; return dummy;}
#else
CFieldElement::CFieldElement(): byteVector() { SetNull(); }

CFieldElement::CFieldElement(const std::vector<unsigned char>& byteArrayIn) : byteVector()
{
    this->SetByteArray(byteArrayIn);
}

void CFieldElement::SetByteArray(const std::vector<unsigned char>& byteArrayIn)
{
    assert(byteArrayIn.size() == CFieldElement::ByteSize());
    this->byteVector = byteArrayIn;
}

CFieldElement::CFieldElement(const uint256& value)
{
    this->byteVector.resize(CFieldElement::ByteSize(),0x0);
    std::copy(value.begin(), value.end(), this->byteVector.begin());
}

CFieldElement::CFieldElement(const wrappedFieldPtr& wrappedField)
{
    this->byteVector.resize(CFieldElement::ByteSize(),0x0);
    zendoo_serialize_field(wrappedField.get(), &byteVector[0]);
}

void CFieldElement::SetNull() { byteVector.resize(0); }
bool CFieldElement::IsNull() const { return byteVector.empty();}

const std::vector<unsigned char>&  CFieldElement::GetByteArray() const
{
    return byteVector;
}

wrappedFieldPtr CFieldElement::GetFieldElement() const
{
    if (this->byteVector.empty())
        return wrappedFieldPtr{nullptr};

    wrappedFieldPtr res = {zendoo_deserialize_field(&this->byteVector[0]), theFieldPtrDeleter};
    return res;
}

uint256 CFieldElement::GetLegacyHashTO_BE_REMOVED() const
{
    std::vector<unsigned char> tmp(this->byteVector.begin(), this->byteVector.begin()+32);
    return uint256(tmp);
}

std::string CFieldElement::GetHexRepr() const
{
    std::string res; //ADAPTED FROM UTILSTRENCONDING.CPP HEXSTR
    static const char hexmap[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                     '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
    res.reserve(this->byteVector.size()*2);
    for(const auto& byte: this->byteVector)
    {
        res.push_back(hexmap[byte>>4]);
        res.push_back(hexmap[byte&15]);
    }

    return res;
}

bool CFieldElement::IsValid() const
{
    if(this->GetFieldElement() == nullptr)
        return false;

    return true;
}

CFieldElement CFieldElement::ComputeHash(const CFieldElement& lhs, const CFieldElement& rhs)
{
    if(!lhs.IsValid() || !rhs.IsValid())
        throw std::runtime_error("Could not compute poseidon hash on null field elements");

    ZendooPoseidonHash digest{};

    digest.update(lhs.GetFieldElement().get());
    digest.update(rhs.GetFieldElement().get());

    wrappedFieldPtr res = {digest.finalize(), theFieldPtrDeleter};
    return CFieldElement(res);
}
#endif
////////////////////////////// End of Field types //////////////////////////////

CScProof::CScProof(const std::vector<unsigned char>& byteArrayIn) : byteVector()
{
    this->SetByteArray(byteArrayIn);
}

void CScProof::SetByteArray(const std::vector<unsigned char>& byteArrayIn)
{
    assert(byteArrayIn.size() == CScProof::ByteSize());
    this->byteVector = byteArrayIn;
}

void CScProof::SetNull() { byteVector.resize(0); }
bool CScProof::IsNull() const { return byteVector.empty();}

wrappedScProofPtr CScProof::GetProofPtr() const
{
    if (this->byteVector.empty())
        return wrappedScProofPtr{nullptr};

    wrappedScProofPtr res = {zendoo_deserialize_sc_proof(&this->byteVector[0]), theProofPtrDeleter};
    return res;
}

bool CScProof::IsValid() const
{
    if (this->GetProofPtr() == nullptr)
        return false;

    return true;
}

std::string CScProof::GetHexRepr() const
{
    std::string res; //ADAPTED FROM UTILSTRENCONDING.CPP HEXSTR
    static const char hexmap[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                     '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
    res.reserve(this->byteVector.size()*2);
    for(const auto& byte: this->byteVector)
    {
        res.push_back(hexmap[byte>>4]);
        res.push_back(hexmap[byte&15]);
    }

    return res;
}

/////////////////////// libzendoomc namespace definitions //////////////////////
namespace libzendoomc
{
    bool IsValidScVk(const ScVk& scVk)
    {
        auto scVkDeserialized = zendoo_deserialize_sc_vk(scVk.begin());
        if (scVkDeserialized == nullptr)
            return false;
        zendoo_sc_vk_free(scVkDeserialized);
        return true;
    }

    std::string ToString(Error err){
        return strprintf(
            "%s: [%d - %s]\n",
            err.msg,
            err.category,
            zendoo_get_category_name(err.category));
    }
}
/////////////////// end of libzendoomc namespace definitions ///////////////////

////////////////////////////// Custom Config types //////////////////////////////
bool FieldElementCertificateFieldConfig::IsValid() const
{
    if(nBits > 0 && nBits <= CFieldElement::ByteSize()*8)
        return true;
    else
        return false;
}

FieldElementCertificateFieldConfig::FieldElementCertificateFieldConfig(int32_t nBitsIn):
    CustomCertificateFieldConfig(), nBits(nBitsIn) {}

int32_t FieldElementCertificateFieldConfig::getBitSize() const
{
    return nBits;
}

//----------------------------------------------------------------------------------
bool BitVectorCertificateFieldConfig::IsValid() const
{
    bool isBitVectorSizeValid = (bitVectorSizeBits > 0) && (bitVectorSizeBits <= MAX_BIT_VECTOR_SIZE_BITS);
    if(!isBitVectorSizeValid)
        return false;

    if ((bitVectorSizeBits % 254 != 0) || (bitVectorSizeBits % 8 != 0))
        return false;

    bool isMaxCompressedSizeValid = (maxCompressedSizeBytes > 0) && (maxCompressedSizeBytes <= MAX_COMPRESSED_SIZE_BYTES);
    if(!isMaxCompressedSizeValid)
        return false;

    return true;
}

BitVectorCertificateFieldConfig::BitVectorCertificateFieldConfig(int32_t bitVectorSizeBits, int32_t maxCompressedSizeBytes):
    CustomCertificateFieldConfig(),
    bitVectorSizeBits(bitVectorSizeBits),
    maxCompressedSizeBytes(maxCompressedSizeBytes) {
    BOOST_STATIC_ASSERT(MAX_COMPRESSED_SIZE_BYTES <= MAX_CERT_SIZE); // sanity
}


////////////////////////////// Custom Field types //////////////////////////////


//----------------------------------------------------------------------------------------
FieldElementCertificateField::FieldElementCertificateField(const std::vector<unsigned char>& rawBytes)
    :CustomCertificateField(rawBytes), pReferenceCfg{nullptr} {}

FieldElementCertificateField::FieldElementCertificateField(const FieldElementCertificateField& rhs)
    :CustomCertificateField{}, pReferenceCfg{nullptr}
{
    *this = rhs;
}

FieldElementCertificateField& FieldElementCertificateField::operator=(const FieldElementCertificateField& rhs)
{
    *const_cast<std::vector<unsigned char>*>(&vRawData) = rhs.vRawData;
    if (rhs.pReferenceCfg != nullptr)
        this->pReferenceCfg = new FieldElementCertificateFieldConfig(*rhs.pReferenceCfg);
    else
        this->pReferenceCfg = nullptr;
    return *this;
}


bool FieldElementCertificateField::IsValid(const FieldElementCertificateFieldConfig& cfg) const
{
    return !this->GetFieldElement(cfg).IsNull();
}

const CFieldElement& FieldElementCertificateField::GetFieldElement(const FieldElementCertificateFieldConfig& cfg) const
{
    if (state != VALIDATION_STATE::NOT_INITIALIZED)
    {
        assert(pReferenceCfg != nullptr);
        if (*pReferenceCfg == cfg)
        {
            return fieldElement;
        }

        // revalidated with new cfg
        delete this->pReferenceCfg;
        this->pReferenceCfg = nullptr;
    }

    state = VALIDATION_STATE::INVALID;
    this->fieldElement = CFieldElement{};
    this->pReferenceCfg = new FieldElementCertificateFieldConfig(cfg);

    int rem = 0;

    assert(cfg.getBitSize() <= CFieldElement::BitSize());

    int bytes = getBytesFromBits(cfg.getBitSize(), rem);

    if (vRawData.size() != bytes )
    {
        LogPrint("sc", "%s():%d - ERROR: wrong size: data[%d] != cfg[%d]\n",
            __func__, __LINE__, vRawData.size(), cfg.getBitSize());
        return fieldElement;
    }

    if (rem)
    {
        // check null bits in the last byte are as expected
        unsigned char lastByte = vRawData.back();
        int numbOfZeroBits = getTrailingZeroBitsInByte(lastByte);
        if (numbOfZeroBits < (CHAR_BIT - rem))
        {
            LogPrint("sc", "%s():%d - ERROR: wrong number of null bits in last byte[0x%x]: %d vs %d\n",
                __func__, __LINE__, lastByte, numbOfZeroBits, (CHAR_BIT - rem));
            return fieldElement;
        }
    }

    std::vector<unsigned char> extendedRawData = vRawData;
    extendedRawData.insert(extendedRawData.begin(), CFieldElement::ByteSize()-vRawData.size(), 0x0);

    fieldElement.SetByteArray(extendedRawData);
    if (fieldElement.IsValid())
    {
        state = VALIDATION_STATE::VALID;
    } else
    {
        fieldElement = CFieldElement{};
    }

    return fieldElement;
}

//----------------------------------------------------------------------------------
BitVectorCertificateField::BitVectorCertificateField(const std::vector<unsigned char>& rawBytes)
    :CustomCertificateField(rawBytes), pReferenceCfg{nullptr} {}

BitVectorCertificateField::BitVectorCertificateField(const BitVectorCertificateField& rhs)
    :CustomCertificateField(), pReferenceCfg{nullptr}
{
    *this = rhs;
}

BitVectorCertificateField& BitVectorCertificateField::operator=(const BitVectorCertificateField& rhs)
{
    *const_cast<std::vector<unsigned char>*>(&vRawData) = rhs.vRawData;
    if (rhs.pReferenceCfg != nullptr)
        this->pReferenceCfg = new BitVectorCertificateFieldConfig(*rhs.pReferenceCfg);
    else
        this->pReferenceCfg = nullptr;
    return *this;
}

bool BitVectorCertificateField::IsValid(const BitVectorCertificateFieldConfig& cfg) const
{
    return !this->GetFieldElement(cfg).IsNull();
}

const CFieldElement& BitVectorCertificateField::GetFieldElement(const BitVectorCertificateFieldConfig& cfg) const
{
    if (state != VALIDATION_STATE::NOT_INITIALIZED)
    {
        assert(pReferenceCfg != nullptr);
        if (*pReferenceCfg == cfg)
        {
            return fieldElement;
        }

        // revalidated with new cfg
        delete this->pReferenceCfg;
        this->pReferenceCfg = nullptr;
    }

    state = VALIDATION_STATE::INVALID;
    this->fieldElement = CFieldElement{};
    this->pReferenceCfg = new BitVectorCertificateFieldConfig(cfg);

    if(vRawData.size() > cfg.getMaxCompressedSizeBytes()) {
        // this is invalid and fieldElement is Null 
        return fieldElement;
    }

    /*
     *  TODO this is a dummy implementation, useful just for running preliminary tests
     *  In the final version using rust lib the steps to cover would be:
     *
     *   1. Reconstruct MerkleTree from the compressed raw data of vRawField
     *   2. Check for the MerkleTree validity
     *   3. Calculate and store the root hash.
     */



    /*

    TODO

    try {
            fieldElement = RustImpl::getBitVectorMerkleRoot(vRawData, cfg.getBitVectorSizeBits());
            state = VALIDATION_STATE::VALID;
            return true;
    } catch(...) {
    }
    */
    // set a default impl for having a valid field returned here
    std::vector<unsigned char> extendedRawData = vRawData;
    // this is in order to have a valid field element with the final bytes set to 0
    extendedRawData.resize(CFieldElement::ByteSize() - 2, 0x0);
    extendedRawData.resize(CFieldElement::ByteSize(), 0x0);

    fieldElement.SetByteArray(extendedRawData);
    if (fieldElement.IsValid())
    {
        state = VALIDATION_STATE::VALID;
    } 

    return fieldElement;
}

////////////////////////// End of Custom Field types ///////////////////////////
