#include "sc/sidechaintypes.h"
#include "util.h"

////////////////////////////// Custom Config types //////////////////////////////
bool FieldElementCertificateFieldConfig::IsValid() const
{
	if(nBits <=0 || nBits > SC_FIELD_SIZE*8)
		return false;
	else
		return true;
}

FieldElementCertificateFieldConfig::FieldElementCertificateFieldConfig(int32_t nBitsIn): CustomCertificateFieldConfig(), nBits(nBitsIn) {}

int32_t FieldElementCertificateFieldConfig::getBitSize() const
{
    return nBits;
}

//----------------------------------------------------------------------------------
bool BitVectorCertificateFieldConfig::IsValid() const
{
    bool isBitVectorSizeValid = (bitVectorSizeBits >= 0) && (bitVectorSizeBits <= MAX_BIT_VECTOR_SIZE_BITS)
    if(!isBitVectorSizeValid)
    	return false;

    bool isMaxCompressedSizeValid = (maxCompressedSizeBytes >= 0) && (maxCompressedSizeBytes <= MAX_COMPRESSED_SIZE_BYTES)
    if(!isMaxCompressedSizeValid)
    	return false;

    return true;
}

BitVectorCertificateFieldConfig::BitVectorCertificateFieldConfig(int32_t bitVectorSizeBits, int32_t maxCompressedSizeBytes): CustomCertificateFieldConfig(), bitVectorSizeBits(bitVectorSizeBits), maxCompressedSizeBytes(maxCompressedSizeBytes)
{}


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
    	this->pReferenceCfg = new FieldElementCertificateFieldConfig{*rhs.pReferenceCfg};
    else
    	this->pReferenceCfg = nullptr;
    return *this;
}


bool FieldElementCertificateField::IsValid(const FieldElementCertificateFieldConfig& cfg) const
{
    if (state != VALIDATION_STATE::NOT_INITIALIZED)
    {
    	assert(pReferenceCfg != nullptr);
    	if (*pReferenceCfg == cfg)
    		return state == VALIDATION_STATE::VALID;

    	// revalidated with new cfg
    	delete this->pReferenceCfg;
    	this->pReferenceCfg = nullptr;
    }

	state = VALIDATION_STATE::INVALID;
	this->pReferenceCfg = new FieldElementCertificateFieldConfig(cfg);

	int rem = 0;

	assert(cfg.getBitSize() <= fieldElement.getBitSize());

	int bytes = getBytesFromBits(cfg.getBitSize(), rem);

	if (vRawData.size() != bytes )
	{
		LogPrint("sc", "%s():%d - ERROR: wrong size: data[%d] != cfg[%d]\n",
			__func__, __LINE__, vRawData.size(), cfg.getBitSize());
		return false;
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
			return false;
		}
	}

	try {
		*const_cast<libzendoomc::FieldElementWrapper*>(&fieldElement) = libzendoomc::FieldElementWrapper::getScFieldElement(vRawData); // TODO
		state = VALIDATION_STATE::VALID;
		return true;
	} catch(...) {}
	return false;
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
    if (state != VALIDATION_STATE::NOT_INITIALIZED)
    {
    	assert(pReferenceCfg != nullptr);
    	if (*pReferenceCfg == cfg)
    		return state == VALIDATION_STATE::VALID;

    	// revalidated with new cfg
    	delete this->pReferenceCfg;
    	this->pReferenceCfg = nullptr;
    }

	state = VALIDATION_STATE::INVALID;
	this->pReferenceCfg = new BitVectorCertificateFieldConfig(cfg);

	if(vRawData.size() > cfg.getMaxCompressedSizeBytes()) {
		return false;
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
			*const_cast<libzendoomc::FieldElementWrapper*>(&fieldElement) = RustImpl::getBitVectorMerkleRoot(vRawData, cfg.getBitVectorSizeBits());
			state = VALIDATION_STATE::VALID;
			return true;
	} catch(...) {
	}
	*/
	return false;
}

////////////////////////// End of Custom Field types ///////////////////////////
