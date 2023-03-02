// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include "primitives/transaction.h"
#include "primitives/certificate.h"
#include "sc/sidechaintypes.h"
#include "serialize.h"
#include "uint256.h"

class CCoinsViewCache;

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    static const size_t HEADER_SIZE=4+32+32+32+4+4+32; // excluding Equihash solution

    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint256 hashScTxsCommitment;
    uint32_t nTime;
    uint32_t nBits;
    uint256 nNonce;
    std::vector<unsigned char> nSolution;

    CBlockHeader()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(hashScTxsCommitment);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
        READWRITE(nSolution);
    }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        hashScTxsCommitment.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = uint256();
        nSolution.clear();
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const;

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }
};

class CBlockHeaderForNetwork : public CBlockHeader
{
    std::vector<CTransaction> vtx_dummy;
public:

    CBlockHeaderForNetwork()
    {
        SetNull();
    }

    explicit CBlockHeaderForNetwork(const CBlockHeader &header)
    {
        *static_cast<CBlockHeader*>(this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CBlockHeader*)this);
        READWRITE(vtx_dummy);
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx_dummy.clear();
    }
};

class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransaction> vtx;
    std::vector<CScCertificate> vcert;

    // memory only
    mutable std::vector<uint256> vMerkleTree;
    
    CBlock()
    {
        SetNull();
    }

    void SetBlockHeader(const CBlockHeader &header)
    {
        *static_cast<CBlockHeader*>(this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CBlockHeader*)this);
        READWRITE(vtx);
        if (this->nVersion == BLOCK_VERSION_SC_SUPPORT)
        {
            READWRITE(vcert);
        }
    }

    // compute the block size by summing up contributions:
    // 1. header
    // 2. number of transactions (compact size of vtx)
    // 3. transactions
    // and if block supports SC:
    // 4. number of certificates (compact size of vcert, 1 byte if no certs)
    // 5. certificates, if any
    // rturns as out params the size of header, total tx size and total cert size
    size_t GetSerializeComponentsSize(size_t& headerSize, size_t& totTxSize, size_t& totCertSize) const;

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        vcert.clear();
        vMerkleTree.clear();
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.hashScTxsCommitment = hashScTxsCommitment;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        block.nSolution      = nSolution;
        return block;
    }

    // Build the in-memory merkle tree for this block and return the merkle root.
    // If non-NULL, *mutated is set to whether mutation was detected in the merkle
    // tree (a duplication of transactions in the block leading to an identical
    // merkle root).
    uint256 BuildMerkleTree(bool* mutated = NULL) const;

    // Build the sc txs commitment tree as described in zendoo paper. It is based on contribution from
    // sidechains-related txes and certificates contained in this block. Returns the txs commitment.
    bool BuildScTxsCommitment(const CCoinsViewCache& view, uint256& scCommitment);
    bool BuildScTxsCommitmentGuard();
    
    std::vector<uint256> GetMerkleBranch(int nIndex) const;
    std::string ToString() const;

    // returns the vector of ptrs of tx and certs of the block (&tx1, .., &txn, &cert1, .., &certn).
    void GetTxAndCertsVector(std::vector<const CTransactionBase*>& vBase) const;

    // build the merkel tree storing it in the vMerkleTreeIn in/out vector and return the merkle root hash
    static uint256 BuildMerkleTree(std::vector<uint256>& vMerkleTreeIn, size_t vtxSize, bool* mutated = NULL);

    static uint256 CheckMerkleBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex);
};


/**
 * Custom serializer for CBlockHeader that omits the nonce and solution, for use
 * as input to Equihash.
 */
class CEquihashInput : private CBlockHeader
{
public:
    CEquihashInput(const CBlockHeader &header)
    {
        CBlockHeader::SetNull();
        *((CBlockHeader*)this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(hashScTxsCommitment);
        READWRITE(nTime);
        READWRITE(nBits);
    }
};


/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}

    CBlockLocator(const std::vector<uint256>& vHaveIn)
    {
        vHave = vHaveIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }

    friend bool operator==(const CBlockLocator& a, const CBlockLocator& b) {
        return (a.vHave == b.vHave);
    }
};

#endif // BITCOIN_PRIMITIVES_BLOCK_H
