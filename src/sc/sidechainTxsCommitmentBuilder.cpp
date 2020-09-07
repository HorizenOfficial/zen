#include <sc/sidechainTxsCommitmentBuilder.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <primitives/certificate.h>

uint256 SidechainTxsCommitmentBuilder::getMerkleRootHash(const std::vector<uint256>& vInput)
{
    std::vector<uint256> vTempMerkleTree = vInput;
    return CBlock::BuildMerkleTree(vTempMerkleTree, vInput.size());
}

const std::string SidechainTxsCommitmentBuilder::MAGIC_SC_STRING = "Horizen ScTxsCommitment null hash string";

const uint256& SidechainTxsCommitmentBuilder::getCrossChainNullHash()
{
    static bool generated = false;
    static uint256 theHash;

    if (!generated)
    {
        CHashWriter ss(SER_GETHASH, 0);
        ss << MAGIC_SC_STRING;
        theHash = ss.GetHash();
        LogPrintf("%s():%d - Generated sc null hash [%s]\n", __func__, __LINE__, theHash.ToString());
        generated = true;
    }
    return theHash;
}
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
        addCrosschainOutput(tx.GetHash(), tx.GetVscCcOut()[ccOutIdx], ccOutIdx, mScMerkleTreeLeavesFt);

    LogPrint("sc", "%s():%d -getting leaves for vft out\n", __func__, __LINE__);
    for(; ccOutIdx < tx.GetVftCcOut().size(); ++ccOutIdx)
        addCrosschainOutput(tx.GetHash(), tx.GetVftCcOut()[ccOutIdx], ccOutIdx, mScMerkleTreeLeavesFt);

    LogPrint("sc", "%s():%d - nIdx[%d]\n", __func__, __LINE__, ccOutIdx);
}

void SidechainTxsCommitmentBuilder::add(const CScCertificate& cert)
{
    sScIds.insert(cert.GetScId());
    mScCerts[cert.GetScId()] = cert.GetHash();
}

uint256 SidechainTxsCommitmentBuilder::getCommitment()
{
    std::vector<uint256> vSortedScLeaves;

    // set of scid is ordered
    for (const auto& scid : sScIds)
    {
        uint256 ftHash(getCrossChainNullHash());
        uint256 btrHash(getCrossChainNullHash());
        uint256 wCertHash(getCrossChainNullHash());

        auto itFt = mScMerkleTreeLeavesFt.find(scid);
        if (itFt != mScMerkleTreeLeavesFt.end() )
        {
            ftHash = getMerkleRootHash(itFt->second);
        }

        auto itBtr = mScMerkleTreeLeavesBtr.find(scid);
        if (itBtr != mScMerkleTreeLeavesBtr.end() )
        {
            btrHash = getMerkleRootHash(itBtr->second);
        }

        auto itCert = mScCerts.find(scid);
        if (itCert != mScCerts.end() )
        {
            wCertHash = itCert->second;
        }

        const uint256& txsHash = Hash(
            BEGIN(ftHash),    END(ftHash),
            BEGIN(btrHash),   END(btrHash) );

        const uint256& scHash = Hash(
            BEGIN(txsHash),   END(txsHash),
            BEGIN(wCertHash), END(wCertHash),
            BEGIN(scid),      END(scid) );

#ifdef DEBUG_SC_COMMITMENT_HASH
        std::cout << " -------------------------------------------" << std::endl;
        std::cout << "  FtHash:  " << ftHash.ToString() << std::endl;
        std::cout << "  BtrHash: " << btrHash.ToString() << std::endl;
        std::cout << "  => TxsHash:   " << txsHash.ToString() << std::endl;
        std::cout << "     WCertHash: " << wCertHash.ToString() << std::endl;
        std::cout << "     scid:      " << scid.ToString() << std::endl;
        std::cout << "     => ScsHash:  " << scHash.ToString() << std::endl;
#endif
        vSortedScLeaves.push_back(scHash);
    }

    return getMerkleRootHash(vSortedScLeaves);
}
#endif

uint256 SidechainTxsCommitmentBuilder::HashCcOutput(const uint256& ccoutHash, const uint256& txHash, unsigned int outPos)
{
    uint256 res = Hash(BEGIN(ccoutHash), END(ccoutHash),
                       BEGIN(txHash),    END(txHash),
                       BEGIN(outPos),    END(outPos) );

    return res;
}
