#include <sc/sidechainTxsCommitmentBuilder.h>
#include <primitives/transaction.h>
#include <primitives/certificate.h>
#include <uint256.h>
#include <algorithm>
#include <iostream>

const CFieldElement SidechainTxsCommitmentBuilder::defaultLeaf {std::vector<unsigned char>(CFieldElement::ByteSize(), 0x0)};

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

        std::vector<CFieldElement>& vec = mScMerkleTreeLeavesFt[scId]; //create or pick entry for scId
        CFieldElement field = mapScTxToField(tx.GetVscCcOut().at(scIdx).GetHash(), tx.GetHash(), scIdx);
        vec.push_back(field);
    }

    unsigned int fwdBaseMapIdx = tx.GetVscCcOut().size();
    LogPrint("sc", "%s():%d -getting leaves for vft out\n", __func__, __LINE__);
    for(unsigned int fwdIdx = 0; fwdIdx < tx.GetVftCcOut().size(); ++fwdIdx)
    {
        uint256 scId = tx.GetVftCcOut().at(fwdIdx).GetScId();
        sScIds.insert(scId);

        std::vector<CFieldElement>& vec = mScMerkleTreeLeavesFt[scId]; //create or pick entry for scId
        CFieldElement field = mapScTxToField(tx.GetVftCcOut().at(fwdIdx).GetHash(), tx.GetHash(), fwdBaseMapIdx + fwdIdx);
        vec.push_back(field);
    }
}

void SidechainTxsCommitmentBuilder::add(const CScCertificate& cert)
{
    uint256 scId = cert.GetScId();
    sScIds.insert(scId);
    mScCerts[scId] = mapUint256ToField(cert.GetHash());
}

uint256 SidechainTxsCommitmentBuilder::getCommitment()
{
    std::vector<CFieldElement> vSortedScLeaves;

    // set of scid is ordered
    for (const auto& scid : sScIds)
    {
        CFieldElement ftRootField = defaultLeaf;
        auto itFt = mScMerkleTreeLeavesFt.find(scid);
        if (itFt != mScMerkleTreeLeavesFt.end() )
        {
            ftRootField = merkleTreeRootOf(itFt->second);
        }

        CFieldElement btrRootField = defaultLeaf;
        auto itBtr = mScMerkleTreeLeavesBtr.find(scid);
        if (itBtr != mScMerkleTreeLeavesBtr.end() )
        {
            btrRootField = merkleTreeRootOf(itBtr->second);
        }

        CFieldElement certRootField = defaultLeaf;
        auto itCert = mScCerts.find(scid);
        if (itCert != mScCerts.end() )
        {
            certRootField = itCert->second;
        }

        CFieldElement scIdField = mapUint256ToField(scid);

        std::vector<CFieldElement> singleSidechainComponents = {ftRootField, btrRootField, certRootField, scIdField};
        CFieldElement sidechainTreeRoot = merkleTreeRootOf(singleSidechainComponents);

        vSortedScLeaves.push_back(sidechainTreeRoot);
    }

    CFieldElement finalTreeRoot = merkleTreeRootOf(vSortedScLeaves);
    return finalTreeRoot.GetLegacyHashTO_BE_REMOVED();
}
#endif

CFieldElement SidechainTxsCommitmentBuilder::mapScTxToField(const uint256& ccoutHash, const uint256& txHash, unsigned int outPos)
{
    std::vector<unsigned char> mediumTerm;
    std::copy(ccoutHash.begin(), ccoutHash.end(), std::back_inserter(mediumTerm));
    std::copy(txHash.begin(), txHash.end(), std::back_inserter(mediumTerm));

    unsigned char* outPosPtr = static_cast<unsigned char*>(static_cast<void*>(&outPos));
    std::vector<unsigned char> tmp(outPosPtr, outPosPtr + sizeof(unsigned int));
    std::copy(tmp.begin(), tmp.end(), std::back_inserter(mediumTerm));
    mediumTerm.resize(CFieldElement::ByteSize(), '\0');

    LogPrintf("%s():%d - Data about to be mapped to field point [%s]\n",
        __func__, __LINE__, HexStr(mediumTerm));

    return CFieldElement{mediumTerm};
}

CFieldElement SidechainTxsCommitmentBuilder::mapUint256ToField(const uint256& hash)
{
    return CFieldElement{hash};
}

unsigned int SidechainTxsCommitmentBuilder::treeHeightForLeaves(unsigned int numberOfLeaves) const
{
    assert(numberOfLeaves > 0);
    return static_cast<unsigned int>(ceil(log2f(static_cast<float>(numberOfLeaves))));
}

CFieldElement SidechainTxsCommitmentBuilder::merkleTreeRootOf(std::vector<CFieldElement>& leaves) const
{
    unsigned int numberOfLeaves = leaves.size();
    if (numberOfLeaves == 0) 
        numberOfLeaves = 1;
    
    auto btrTree = ZendooGingerMerkleTree(treeHeightForLeaves(numberOfLeaves), numberOfLeaves);
    for(CFieldElement & leaf: leaves)
        btrTree.append(leaf.GetFieldElement().get());

    btrTree.finalize_in_place();
    wrappedFieldPtr wrappedRoot(btrTree.root(), CFieldPtrDeleter());
    return CFieldElement{wrappedRoot};
}
