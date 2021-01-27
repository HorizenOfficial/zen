#include "sc/sidechaintypes.h"

////////////////////////////// Custom Config types //////////////////////////////
FieldElementConfig::FieldElementConfig(int32_t nBitsIn): CustomFieldConfig(), nBits(nBitsIn)
{
    if (nBits <= 0)
        throw std::invalid_argument("FieldElementConfig size must be strictly positive");
}

int32_t FieldElementConfig::getBitSize() const
{
    return nBits;
}

//----------------------------------------------------------------------------------
CompressedMerkleTreeConfig::CompressedMerkleTreeConfig(int32_t treeHeightIn): CustomFieldConfig(), treeHeight(treeHeightIn)
{
    if (treeHeight <= 0)
        throw std::invalid_argument(
            std::string("CompressedMerkleTreeConfig height must be strictly positive: ") + std::to_string(treeHeight));

    if (treeHeight >= MAX_TREE_HEIGHT)
        throw std::invalid_argument(
            std::string("CompressedMerkleTreeConfig height too large: ") + std::to_string(treeHeight) +
            std::string(", Max: ") + std::to_string(MAX_TREE_HEIGHT));
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

const libzendoomc::ScFieldElement& FieldElement::GetFieldElement()
{
    if (scFieldElement.IsNull())
        scFieldElement = libzendoomc::ScFieldElement(vRawField);

    return scFieldElement;
}

// TODO
bool FieldElement::IsValid() const { return true; };
bool FieldElement::checkCfg(const CustomFieldConfig& cfg) const { return true; };

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

const libzendoomc::ScFieldElement& CompressedMerkleTree::GetFieldElement()
{
    if (merkleRoot.IsNull())
        merkleRoot = libzendoomc::ScFieldElement(vRawField);

    return merkleRoot;
}

// TODO
bool CompressedMerkleTree::IsValid() const { return true; };
bool CompressedMerkleTree::checkCfg(const CustomFieldConfig& cfg) const { return true; };
////////////////////////// End of Custom Field types ///////////////////////////
