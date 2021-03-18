#include <sc/sidechainTxsCommitmentBuilder.h>
#include <primitives/transaction.h>
#include <primitives/certificate.h>

#ifdef BITCOIN_TX
void SidechainTxsCommitmentBuilder::add(const CTransaction& tx) { return; }
void SidechainTxsCommitmentBuilder::add(const CScCertificate& cert) { return; }
uint256 SidechainTxsCommitmentBuilder::getCommitment() { return uint256(); }
#else
void SidechainTxsCommitmentBuilder::add(const CTransaction& tx)
{
    if (!tx.IsScVersion())
        return;

    LogPrint("sc", "%s():%d -getting leaves for vsc out\n", __func__, __LINE__);
    for(unsigned int scIdx = 0; scIdx < tx.GetVscCcOut().size(); ++scIdx)
    {
        uint256 scId = tx.GetVscCcOut().at(scIdx).GetScId();
        sScIds.insert(scId);

        std::vector<field_t*>& vec = mScMerkleTreeLeavesFt[scId]; //create or pick entry for scId
        field_t* pField = mapScTxToField(tx.GetVscCcOut().at(scIdx).GetHash(), tx.GetHash(), scIdx);
        vec.push_back(pField);
    }

    unsigned int fwdBaseMapIdx = tx.GetVscCcOut().size();
    LogPrint("sc", "%s():%d -getting leaves for vft out\n", __func__, __LINE__);
    for(unsigned int fwdIdx = 0; fwdIdx < tx.GetVftCcOut().size(); ++fwdIdx)
    {
        uint256 scId = tx.GetVftCcOut().at(fwdIdx).GetScId();
        sScIds.insert(scId);

        std::vector<field_t*>& vec = mScMerkleTreeLeavesFt[scId]; //create or pick entry for scId
        field_t* pField = mapScTxToField(tx.GetVftCcOut().at(fwdIdx).GetHash(), tx.GetHash(), fwdBaseMapIdx + fwdIdx);
        vec.push_back(pField);
    }
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

    field_t * finalTreeRoot = merkleTreeRootOf(vSortedScLeaves);
    uint256 res = mapFieldToHash(finalTreeRoot);
    zendoo_field_free(finalTreeRoot);

    return res;
}
#endif

#include <algorithm>
#include <iostream>
#define IMPLEMENTATION 2
field_t* SidechainTxsCommitmentBuilder::mapScTxToField(const uint256& ccoutHash, const uint256& txHash, unsigned int outPos)
{
    static_assert((sizeof(uint256) + sizeof(uint256) + sizeof(unsigned int)) <= SC_FIELD_SAFE_SIZE,
            "ScTx data to field point mapping not working with current configuration");

#if IMPLEMENTATION == 1
    std::vector<unsigned char> mediumTerm;
    mediumTerm.insert(mediumTerm.end(), ccoutHash.begin(), ccoutHash.end());
    mediumTerm.insert(mediumTerm.end(), txHash.begin(), txHash.end());

    unsigned char* outPosPtr = static_cast<unsigned char*>(static_cast<void*>(&outPos));
    std::vector<unsigned char> tmp(outPosPtr, outPosPtr + sizeof(unsigned int));
    std::move(std::begin(tmp), std::end(tmp), std::back_inserter(mediumTerm));
    mediumTerm.resize(SC_FIELD_SIZE, '\0');

    LogPrintf("%s():%d - Data about to be mapped to field point [%s]\n",
        __func__, __LINE__, HexStr(mediumTerm));

    //generate field element. MISSING null-check
    field_t* field = zendoo_deserialize_field(mediumTerm.data());
    return field;
#endif
#if IMPLEMENTATION == 2
    std::vector<unsigned char> mediumTerm(SC_FIELD_SIZE, '\0');
    mediumTerm.insert(mediumTerm.begin(), ccoutHash.begin(), ccoutHash.end());
    mediumTerm.insert(mediumTerm.begin()+ccoutHash.size()/sizeof(unsigned char), txHash.begin(), txHash.end());

    unsigned char* outPosPtr = static_cast<unsigned char*>(static_cast<void*>(&outPos));
    std::vector<unsigned char> tmp(outPosPtr, outPosPtr + sizeof(unsigned int));
    std::move(std::begin(tmp), std::end(tmp), mediumTerm.begin()+(ccoutHash.size()+txHash.size())/sizeof(unsigned char));
    mediumTerm.resize(SC_FIELD_SIZE, '\0');

    LogPrintf("%s():%d - Data about to be mapped to field point [%s]\n",
        __func__, __LINE__, HexStr(mediumTerm));

    //generate field element. MISSING null-check
    field_t* field = zendoo_deserialize_field(mediumTerm.data());
    return field;
#endif
#if IMPLEMENTATION == 3
    std::string toHash = std::string(ccoutHash.begin(), ccoutHash.end()) +
            std::string(txHash.begin(), txHash.end()) +
            std::string(static_cast<char*>(static_cast<void*>(&outPos)), sizeof(unsigned int));

    unsigned char mediumTerm[SC_FIELD_SIZE] = {};
    memcpy(mediumTerm, toHash.append(SC_FIELD_SIZE - toHash.size(), '\0').c_str(), SC_FIELD_SIZE);

    LogPrintf("%s():%d - Data about to be mapped to field point [%s]\n",
        __func__, __LINE__, HexStr(mediumTerm));

    //generate field element. MISSING null-check
    field_t* field = zendoo_deserialize_field(mediumTerm);
    return field;
#endif
}

field_t* SidechainTxsCommitmentBuilder::mapCertToField(const uint256& certHash)
{
    static_assert(sizeof(uint256) <= SC_FIELD_SAFE_SIZE,
            "Certificate data to field point mapping not working with current configuration");
    unsigned char hash[SC_FIELD_SIZE] = {};
    std::string resHex = std::string(certHash.begin(), certHash.end());
    memcpy(hash, resHex.append(SC_FIELD_SIZE - resHex.size(), '\0').c_str(), SC_FIELD_SIZE);

    //generate field element. MISSING null-check
    field_t* field = zendoo_deserialize_field(hash);
    return field;
}

uint256 SidechainTxsCommitmentBuilder::mapFieldToHash(const field_t* pField)
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
    assert(numberOfLeaves > 0);

    return static_cast<unsigned int>(ceil(log2f(static_cast<float>(numberOfLeaves))));
}

field_t* SidechainTxsCommitmentBuilder::merkleTreeRootOf(std::vector<field_t*>& leaves) const
{
    //Notes: Leaves are consumed, i.e. freed and nulled;
    //       It is guaranteed not to return nullptr.

    unsigned int numberOfLeaves = leaves.size();
    if (numberOfLeaves == 0) 
        numberOfLeaves = 1;
    
    auto btrTree = ZendooGingerMerkleTree(treeHeightForLeaves(numberOfLeaves), numberOfLeaves);
    for(field_t* & leaf: leaves)
    {
        if (leaf != nullptr) {
            btrTree.append(leaf);
            zendoo_field_free(leaf);
            leaf = nullptr;
        }
    }

    btrTree.finalize_in_place();
    field_t* root = btrTree.root();
    return root;
}
