// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"
#include <boost/foreach.hpp>

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CBlock::BuildMerkleTree(bool* fMutated) const
{
    /* WARNING! If you're reading this because you're learning about crypto
       and/or designing a new system that will use merkle trees, keep in mind
       that the following merkle tree algorithm has a serious flaw related to
       duplicate txids, resulting in a vulnerability (CVE-2012-2459).

       The reason is that if the number of hashes in the list at a given time
       is odd, the last one is duplicated before computing the next level (which
       is unusual in Merkle trees). This results in certain sequences of
       transactions leading to the same merkle root. For example, these two
       trees:

                    A               A
                  /  \            /   \
                B     C         B       C
               / \    |        / \     / \
              D   E   F       D   E   F   F
             / \ / \ / \     / \ / \ / \ / \
             1 2 3 4 5 6     1 2 3 4 5 6 5 6

       for transaction lists [1,2,3,4,5,6] and [1,2,3,4,5,6,5,6] (where 5 and
       6 are repeated) result in the same root hash A (because the hash of both
       of (F) and (F,F) is C).

       The vulnerability results from being able to send a block with such a
       transaction list, with the same merkle root, and the same block hash as
       the original without duplication, resulting in failed validation. If the
       receiving node proceeds to mark that block as permanently invalid
       however, it will fail to accept further unmodified (and thus potentially
       valid) versions of the same block. We defend against this by detecting
       the case where we would hash two identical hashes at the end of the list
       together, and treating that identically to the block having an invalid
       merkle root. Assuming no double-SHA256 collisions, this will detect all
       known ways of changing the transactions without affecting the merkle
       root.
    */
    vMerkleTree.clear();
    vMerkleTree.reserve(vtx.size() * 2 + 16); // Safe upper bound for the number of total nodes.
    for (std::vector<CTransaction>::const_iterator it(vtx.begin()); it != vtx.end(); ++it)
        vMerkleTree.push_back(it->GetHash());

    return BuildMerkleTree(vMerkleTree, vtx.size(), fMutated);
}

uint256 CBlock::BuildMerkleTree(std::vector<uint256>& vMerkleTree, size_t vtxSize, bool* fMutated) const
{
    int j = 0;
    bool mutated = false;
    for (int nSize = vtxSize; nSize > 1; nSize = (nSize + 1) / 2)
    {
        for (int i = 0; i < nSize; i += 2)
        {
            int i2 = std::min(i+1, nSize-1);
            if (i2 == i + 1 && i2 + 1 == nSize && vMerkleTree[j+i] == vMerkleTree[j+i2]) {
                // Two identical hashes at the end of the list at a particular level.
                mutated = true;
            }
            vMerkleTree.push_back(Hash(BEGIN(vMerkleTree[j+i]),  END(vMerkleTree[j+i]),
                                       BEGIN(vMerkleTree[j+i2]), END(vMerkleTree[j+i2])));
#ifdef DEBUG_SC_HASH
            std::cout << " -------------------------------------------" << std::endl;
            std::cout << i << ") mkl hash: " << vMerkleTree.back().ToString() << std::endl;
#endif
        }
        j += nSize;
    }
    if (fMutated) {
        *fMutated = mutated;
    }
    return (vMerkleTree.empty() ? uint256() : vMerkleTree.back());
}

uint256 CBlock::BuildMerkleRootHash(const std::vector<uint256>& vInput) 
{
    std::vector<uint256> vTempMerkleTree = vInput;
    return BuildMerkleTree(vTempMerkleTree, vInput.size());
}

uint256 CBlock::BuildScMerkleRootsMap()
{
    // Key: the side chain ID
    // Value: the array of objs of type 'Hash( Hash(ccout) | txid | n)', where n is the index of the 
    // ccout in the tx pertaining to the current scid. Tx are ordered as they are included in the block  
    std::map<uint256, std::vector<uint256> > mScMerkleTreeLeaves; 

    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        tx.getCrosschainOutputs(mScMerkleTreeLeaves);
    }

    if (mScMerkleTreeLeaves.size() == 0)
    {
        return uint256();
    }

    // Note that by default the map is ordered by key value, therefore the entries in
    // the vector will be sorted in the same way
    std::vector<uint256> vSortedLeaves;

    BOOST_FOREACH(const auto& pair, mScMerkleTreeLeaves)
    {
        const uint256& scId = pair.first;
        const uint256& mklHash = BuildMerkleRootHash(pair.second);

        LogPrint("sc", "%s():%d built merkle root for sc[%s] with %d leaves: [%s]\n",
            __func__, __LINE__, scId.ToString(), pair.second.size(), mklHash.ToString() );

        const uint256& leaf = Hash( BEGIN(scId), END(scId), BEGIN(mklHash), END(mklHash) );

#ifdef DEBUG_SC_HASH
        std::cout << " -------------------------------------------" << std::endl;
        std::cout << "  sc mkl hash:  " << mklHash.ToString() << std::endl;
        std::cout << "  sc leaf hash: " << leaf.ToString() << std::endl;
#endif

        vSortedLeaves.push_back(leaf);
    }

    return BuildMerkleRootHash(vSortedLeaves);
}


std::vector<uint256> CBlock::GetMerkleBranch(int nIndex) const
{
    if (vMerkleTree.empty())
        BuildMerkleTree();
    std::vector<uint256> vMerkleBranch;
    int j = 0;
    for (int nSize = vtx.size(); nSize > 1; nSize = (nSize + 1) / 2)
    {
        int i = std::min(nIndex^1, nSize-1);
        vMerkleBranch.push_back(vMerkleTree[j+i]);
        nIndex >>= 1;
        j += nSize;
    }
    return vMerkleBranch;
}

uint256 CBlock::CheckMerkleBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex)
{
    if (nIndex == -1)
        return uint256();
    for (std::vector<uint256>::const_iterator it(vMerkleBranch.begin()); it != vMerkleBranch.end(); ++it)
    {
        if (nIndex & 1)
            hash = Hash(BEGIN(*it), END(*it), BEGIN(hash), END(hash));
        else
            hash = Hash(BEGIN(hash), END(hash), BEGIN(*it), END(*it));
        nIndex >>= 1;
    }
    return hash;
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, hashScMerkleRootsMap=%s, nTime=%u, nBits=%08x, nNonce=%s, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        hashScMerkleRootsMap.ToString(),
        nTime, nBits, nNonce.ToString(),
        vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vtx[i].ToString() << "\n";
    }
    s << "  vMerkleTree: ";
    for (unsigned int i = 0; i < vMerkleTree.size(); i++)
        s << " " << vMerkleTree[i].ToString();
    s << "\n";
    return s.str();
}
