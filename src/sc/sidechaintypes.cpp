#include "sc/sidechaintypes.h"
#include "util.h"

////////////////////////////// Custom Config types //////////////////////////////
void FieldElementCertificateFieldConfig::checkValid()
{
    //TENTATIVE IMPLEMENTATION, BEFORE ACTUAL ONE
	if(nBits <=0 || nBits > SC_FIELD_SIZE) {
		throw std::invalid_argument("FieldElementCertificateFieldConfig size must be strictly positive");
	}
}

FieldElementCertificateFieldConfig::FieldElementCertificateFieldConfig(int32_t nBitsIn): CustomCertificateFieldConfig(), nBits(nBitsIn)
{
    checkValid();
}

int32_t FieldElementCertificateFieldConfig::getBitSize() const
{
    //TENTATIVE IMPLEMENTATION, BEFORE ACTUAL ONE
    return nBits;
}

//----------------------------------------------------------------------------------
void BitVectorCertificateFieldConfig::checkValid() {
    //TENTATIVE IMPLEMENTATION, BEFORE ACTUAL ONE
    bool isValid = ((bitVectorSizeBits >= 0) && (bitVectorSizeBits <= MAX_BIT_VECTOR_SIZE_BITS)) &&
    		((maxCompressedSizeBytes >= 0) && (maxCompressedSizeBytes <= MAX_COMPRESSED_SIZE_BYTES));
    if(!isValid) {
		throw std::invalid_argument(
					std::string("BitVectorCertificateFieldConfig bitVectorSizeBits=" + std::to_string(bitVectorSizeBits) +
					", must be in the range [0, ") + std::to_string(MAX_BIT_VECTOR_SIZE_BITS) + std::string(")"));
    }
}

BitVectorCertificateFieldConfig::BitVectorCertificateFieldConfig(int32_t bitVectorSizeBits, int32_t maxCompressedSizeBytes): CustomCertificateFieldConfig(), bitVectorSizeBits(bitVectorSizeBits), maxCompressedSizeBytes(maxCompressedSizeBytes)
{
	checkValid();
}


////////////////////////////// Custom Field types //////////////////////////////


//----------------------------------------------------------------------------------------
FieldElementCertificateField::FieldElementCertificateField(const std::vector<unsigned char>& rawBytes)
    :CustomCertificateField(rawBytes) {}

FieldElementCertificateField& FieldElementCertificateField::operator=(const FieldElementCertificateField& rhs)
{
    *const_cast<std::vector<unsigned char>*>(&vRawData) = rhs.vRawData;
    return *this;
}


void FieldElementCertificateField::initialize(const FieldElementCertificateFieldConfig& cfg) const
{

	assert(state == VALIDATION_STATE::NOT_INITIALIZED);

	state = VALIDATION_STATE::INVALID;

	int rem = 0;

	assert(cfg.getBitSize() <= fieldElement.getBitSize());

	int bytes = getBytesFromBits(cfg.getBitSize(), rem);

	if (vRawData.size() != bytes )
	{
		LogPrint("sc", "%s():%d - ERROR: wrong size: data[%d] != cfg[%d]\n",
			__func__, __LINE__, vRawData.size(), cfg.getBitSize());
		return;
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
			return;
		}
	}

	try {
		*const_cast<libzendoomc::FieldElementWrapper*>(&fieldElement) = libzendoomc::FieldElementWrapper::getScFieldElement(vRawData); // TODO
		state = VALIDATION_STATE::VALID;
	} catch(...) {

	}
}

//----------------------------------------------------------------------------------
BitVectorCertificateField::BitVectorCertificateField(const std::vector<unsigned char>& rawBytes)
    :CustomCertificateField(rawBytes) {}

BitVectorCertificateField& BitVectorCertificateField::operator=(const BitVectorCertificateField& rhs)
{
    *const_cast<std::vector<unsigned char>*>(&vRawData) = rhs.vRawData;
    return *this;
}

void BitVectorCertificateField::initialize(const BitVectorCertificateFieldConfig& cfg) const
{
    /*
     *  TODO this is a dummy implementation, useful just for running preliminary tests
     *  In the final version using rust lib the steps to cover would be:
     *
     *   1. Reconstruct MerkleTree from the compressed raw data of vRawField
     *   2. Check for the MerkleTree validity
     *   3. Calculate and store the root hash.
     */

	assert(state == VALIDATION_STATE::NOT_INITIALIZED);
	state = VALIDATION_STATE::INVALID;

	if(vRawData.size() > cfg.getMaxCompressedSizeBytes()) {
		return;
	}

	/*

	TODO

	try {
			*const_cast<libzendoomc::FieldElementWrapper*>(&fieldElement) = RustImpl::getBitVectorMerkleRoot(vRawData, cfg.getBitVectorSizeBits());
			state = VALIDATION_STATE::VALID;
	} catch(...) {

	}
	*/

}

////////////////////////// End of Custom Field types ///////////////////////////
