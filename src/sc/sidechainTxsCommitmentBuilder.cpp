#include <sc/sidechainTxsCommitmentBuilder.h>
#include <primitives/transaction.h>
#include <primitives/certificate.h>

SidechainTxsCommitmentBuilder::EmptyField::EmptyField():
    zerosHelperArray{'\0'}, zero(zendoo_deserialize_field(zerosHelperArray))
{}

SidechainTxsCommitmentBuilder::EmptyField::~EmptyField()
{
    zendoo_field_free(const_cast<field_t*>(zero));
}

SidechainTxsCommitmentBuilder::EmptyField SidechainTxsCommitmentBuilder::emptyField;

#ifdef BITCOIN_TX
void SidechainTxsCommitmentBuilder::add(const CTransaction& tx) { return; }
void SidechainTxsCommitmentBuilder::add(const CScCertificate& cert) { return; }
uint256 SidechainTxsCommitmentBuilder::getCommitment() { return uint256(); }
#else
void SidechainTxsCommitmentBuilder::add(const CTransaction& tx)
{
    if (!tx.IsScVersion())
        return;

    unsigned int ccOutIdx = 0;
    LogPrint("sc", "%s():%d -getting leaves for vsc out\n", __func__, __LINE__);
    for(; ccOutIdx < tx.GetVscCcOut().size(); ++ccOutIdx)
    {
        uint256 scId = tx.GetVscCcOut().at(ccOutIdx).GetScId();
        sScIds.insert(scId);

        std::vector<field_t*>& vec = mScMerkleTreeLeavesFt[scId]; //create or pick entry for scId
        field_t* pField = mapScTxToField(tx.GetVscCcOut()[ccOutIdx].GetHash(), tx.GetHash(), ccOutIdx);
        vec.push_back(pField);
    }

    LogPrint("sc", "%s():%d -getting leaves for vft out\n", __func__, __LINE__);
    for(; ccOutIdx < tx.GetVftCcOut().size(); ++ccOutIdx)
    {
        uint256 scId = tx.GetVftCcOut().at(ccOutIdx).GetScId();
        sScIds.insert(scId);

        std::vector<field_t*>& vec = mScMerkleTreeLeavesFt[scId]; //create or pick entry for scId
        field_t* pField = mapScTxToField(tx.GetVftCcOut()[ccOutIdx].GetHash(), tx.GetHash(), ccOutIdx);
        vec.push_back(pField);
    }

    LogPrint("sc", "%s():%d - nIdx[%d]\n", __func__, __LINE__, ccOutIdx);
}

void SidechainTxsCommitmentBuilder::add(const CScCertificate& cert)
{
    uint256 scId = cert.GetScId();
    sScIds.insert(scId);
    mScCerts[scId] = mapCertToField(cert.GetHash());
}

uint256 SidechainTxsCommitmentBuilder::getCommitment()
{
    std::vector<field_t*> vSortedScLeaves;

    // set of scid is ordered
    for (const auto& scid : sScIds)
    {
        field_t* ftRootField = nullptr;
        auto itFt = mScMerkleTreeLeavesFt.find(scid);
        if (itFt != mScMerkleTreeLeavesFt.end() )
        {
            ftRootField = merkleTreeRootOf(itFt->second);
        }

        field_t* btrRootField = nullptr;
        auto itBtr = mScMerkleTreeLeavesBtr.find(scid);
        if (itBtr != mScMerkleTreeLeavesBtr.end() )
        {
            btrRootField = merkleTreeRootOf(itBtr->second);
        }

        field_t* certRootField = nullptr;
        auto itCert = mScCerts.find(scid);
        if (itCert != mScCerts.end() )
        {
            certRootField = itCert->second;
        }

        field_t* scIdField = mapCertToField(scid);

        std::vector<field_t*> singleSidechainComponents = {ftRootField, btrRootField, certRootField, scIdField};
        field_t * sidechainTreeRoot = merkleTreeRootOf(singleSidechainComponents);

        vSortedScLeaves.push_back(sidechainTreeRoot);
    }

    field_t * finalTreeRoot = emptyField.zero;
    if (vSortedScLeaves.size() != 0)
        finalTreeRoot = merkleTreeRootOf(vSortedScLeaves);

    uint256 res = mapFieldToHash(finalTreeRoot);
    if (finalTreeRoot != emptyField.zero) zendoo_field_free(finalTreeRoot);

    return res;
}
#endif

field_t* SidechainTxsCommitmentBuilder::mapScTxToField(const uint256& ccoutHash, const uint256& txHash, unsigned int outPos) const
{
    static_assert((sizeof(uint256) + sizeof(uint256) + sizeof(unsigned int)) < SC_FIELD_SIZE);

    std::string toHash = std::string(ccoutHash.begin(), ccoutHash.end()) +
            std::string(txHash.begin(), txHash.end()) +
            std::string(static_cast<char*>(static_cast<void*>(&outPos)));

    unsigned char hash[SC_FIELD_SIZE] = {};
    memcpy(hash, toHash.append(SC_FIELD_SIZE - toHash.size(), '\0').c_str(), SC_FIELD_SIZE);

    //generate field element. MISSING null-check
    field_t* field = zendoo_deserialize_field(hash);
    return field;
}

field_t* SidechainTxsCommitmentBuilder::mapCertToField(const uint256& certHash) const
{
    static_assert(sizeof(uint256) < SC_FIELD_SIZE);
    unsigned char hash[SC_FIELD_SIZE] = {};
    std::string resHex = std::string(certHash.begin(), certHash.end());
    memcpy(hash, resHex.append(SC_FIELD_SIZE - resHex.size(), '\0').c_str(), SC_FIELD_SIZE);

    //generate field element. MISSING null-check
    field_t* field = zendoo_deserialize_field(hash);
    return field;
}

uint256 SidechainTxsCommitmentBuilder::mapFieldToHash(const field_t* pField) const
{
    if (pField == nullptr)
        throw;

    unsigned char field_bytes[SC_FIELD_SIZE];
    zendoo_serialize_field(pField, field_bytes);

    uint256 res = Hash(BEGIN(field_bytes), END(field_bytes));
    return res;
}

unsigned int SidechainTxsCommitmentBuilder::treeHeightForLeaves(unsigned int numberOfLeaves) const
{
    if (numberOfLeaves == 1)
        return 2;

    unsigned int res = static_cast<unsigned int>(ceil(log2f(static_cast<float>(numberOfLeaves)))) + 1;
    return res;
}

field_t* SidechainTxsCommitmentBuilder::merkleTreeRootOf(std::vector<field_t*>& leaves) const
{
    //Notes: leaves are consumed, i.e. freed and nulled, if non-empty
    //       root is a deep-copy, hence it must be freed later on
    //       null leaves are mapped to emptyField
    //       it is guaranteed not to return nullptr
    field_t* res = nullptr;
    auto btrTree = ZendooGingerRandomAccessMerkleTree(treeHeightForLeaves(leaves.size()));
    for(field_t* & leaf: leaves)
    {
        //As of now we swallow possible errors in deserialization of field by replacing null with empty field
        btrTree.append((leaf != nullptr)? leaf : emptyField.zero);
        if (leaf != nullptr)
        {
            zendoo_field_free(leaf);
            leaf = nullptr;
        }
    }

    btrTree.finalize_in_place();
    res = btrTree.root();
    return (res == nullptr)? emptyField.zero : res;
}
