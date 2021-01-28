#include "sc/sidechaintypes.h"
#include "util.h"

////////////////////////////// Custom Config types //////////////////////////////
bool FieldElementConfig::isBitsLenghtValid() { return (nBits > 0); }

FieldElementConfig::FieldElementConfig(int32_t nBitsIn): CustomFieldConfig(), nBits(nBitsIn)
{
    if (!isBitsLenghtValid())
        throw std::invalid_argument("FieldElementConfig size must be strictly positive");
}

int32_t FieldElementConfig::getBitSize() const
{
    return nBits;
}

//----------------------------------------------------------------------------------
bool CompressedMerkleTreeConfig::isTreeHeightValid() {
    return ((treeHeight >= 0) && (treeHeight < MAX_TREE_HEIGHT));
}

CompressedMerkleTreeConfig::CompressedMerkleTreeConfig(int32_t treeHeightIn): CustomFieldConfig(), treeHeight(treeHeightIn)
{
	if (!isTreeHeightValid())
        throw std::invalid_argument(
            std::string("CompressedMerkleTreeConfig height=" + std::to_string(treeHeight) +
            ", must be in the range [0, ") + std::to_string(MAX_TREE_HEIGHT) + std::string(")"));
}

int32_t CompressedMerkleTreeConfig::getBitSize() const
{
    return (treeHeight == -1)? 0: 1 << treeHeight;
}


////////////////////////////// Custom Field types //////////////////////////////
FieldElement::FieldElement(const FieldElementConfig& cfg)
    :CustomField(cfg) {}

FieldElement::FieldElement(const std::vector<unsigned char>& rawBytes)
    :CustomField(rawBytes) {}

FieldElement& FieldElement::operator=(const FieldElement& rhs)
{
    *const_cast<std::vector<unsigned char>*>(&vRawField) = rhs.vRawField;
    return *this;
}

void FieldElement::InitFieldElement() const
{
    if (scFieldElement.IsNull())
    {
        if (vRawField.size() > scFieldElement.size())
        {
            LogPrint("sc", "%s():%d - Error: Wrong size: rawData[%d]>fieldElement[%d]\n", 
                __func__, __LINE__, vRawField.size(), scFieldElement.size());
            throw std::invalid_argument(
                std::string("Wrong size: rawData[" + std::to_string(vRawField.size()) + "] > fieldElement[" +
                std::to_string(scFieldElement.size()) + "]"));
        }
        // pad with zeroes, must have the same size for the internal repr
        std::vector<unsigned char> temp(vRawField);
        temp.resize(scFieldElement.size(), 0x0);
        *const_cast<libzendoomc::ScFieldElement*>(&scFieldElement) = libzendoomc::ScFieldElement(temp); // TODO
    }
}

const libzendoomc::ScFieldElement& FieldElement::GetFieldElement() const
{
    InitFieldElement();
    return scFieldElement;
}

#ifdef BITCOIN_TX
bool FieldElement::IsValid() const { return true; }
#else
bool FieldElement::IsValid() const
{
    InitFieldElement();
    if (scFieldElement.IsNull())
        return false;

    return libzendoomc::IsValidScFieldElement(scFieldElement);
};
#endif

bool FieldElement::checkCfg(const CustomFieldConfig& cfg) const
{
    if (vRawField.size() != cfg.getBitSize() )
    {
        LogPrint("sc", "%s():%d - ERROR: wrong size: data[%d] != cfg[%d]\n", 
            __func__, __LINE__, vRawField.size(), cfg.getBitSize());
        return false;
    }
    return true;
};

//----------------------------------------------------------------------------------
CompressedMerkleTree::CompressedMerkleTree(const CompressedMerkleTreeConfig& cfg)
    :CustomField(cfg) {}

CompressedMerkleTree::CompressedMerkleTree(const std::vector<unsigned char>& rawBytes)
    :CustomField(rawBytes) {}

CompressedMerkleTree& CompressedMerkleTree::operator=(const CompressedMerkleTree& rhs)
{
    *const_cast<std::vector<unsigned char>*>(&vRawField) = rhs.vRawField;
    return *this;
}

void CompressedMerkleTree::InitFieldElement() const
{
    if (merkleRoot.IsNull())
    {
        if (vRawField.size() > merkleRoot.size())
        {
            LogPrint("sc", "%s():%d - ERROR: wrong size: rawData[%d] > mklRoot[%d]\n", 
                __func__, __LINE__, vRawField.size(), merkleRoot.size());
            throw std::invalid_argument(
                std::string("Wrong size: rawData[" + std::to_string(vRawField.size()) + "] > mklRoot[" +
                std::to_string(merkleRoot.size()) + "]"));
        }
        // pad with zeroes, must have the same size for the internal fe repr
        std::vector<unsigned char> temp(vRawField);
        temp.resize(merkleRoot.size(), 0x0);
        *const_cast<libzendoomc::ScFieldElement*>(&merkleRoot) = libzendoomc::ScFieldElement(temp); // TODO
    }
}

const libzendoomc::ScFieldElement& CompressedMerkleTree::GetFieldElement() const
{
    InitFieldElement();
    return merkleRoot;
}

bool CompressedMerkleTree::IsValid() const
{
    InitFieldElement();
    if (merkleRoot.IsNull())
        return false;

    // TODO something like libzendoomc::IsValidScFieldElement() or exactly this?? In this case we can move this to base   
    return true;
}

bool CompressedMerkleTree::checkCfg(const CustomFieldConfig& cfg) const
{
    if (vRawField.size() > cfg.getBitSize() )
    {
        LogPrint("sc", "%s():%d - ERROR: mklTree wrong size: data[%d] > cfg[%d]\n", 
            __func__, __LINE__, vRawField.size(), cfg.getBitSize());
        return false;
    }
    return true;
}
////////////////////////// End of Custom Field types ///////////////////////////
