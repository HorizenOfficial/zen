#ifndef SIDECHAIN_TX_COMMITMENT_BUILDER
#define SIDECHAIN_TX_COMMITMENT_BUILDER

#include <map>
#include <vector>
#include <set>
#include <string>

#include <uint256.h>

class CTransaction;
class CScCertificate;

// uncomment for debugging some sc commitment related hashing calculations
// #define DEBUG_SC_COMMITMENT_HASH 1

class SidechainTxsCommitmentBuilder
{
public:
    void add(const CTransaction& tx);
    void add(const CScCertificate& cert);
    uint256 getCommitment();

    // return the hash of a well known string (MAGIC_SC_STRING declared above) which can be used
    // as a null-semantic value
    static const uint256& getCrossChainNullHash();
private:
    // the final model will have a fixed-height merkle tree for 'SCTxsCommitment'.
    // That will also imply having a limit per block to the number of crosschain outputs per SC
    // and also to the number of SC
    // -----------------------------

    // Key:   the side chain ID
    // Value: the array of objs of type 'Hash( Hash(ccout) | txid | n)', where n is the index of the
    //        ccout in the tx pertaining to the key scid. Tx are ordered as they are included in the block
    std::map<uint256, std::vector<uint256> > mScMerkleTreeLeavesFt;

    // The same for BTR (to be implemented)
    std::map<uint256, std::vector<uint256> > mScMerkleTreeLeavesBtr;

    // Key:   the side chain ID
    // Value: the hash of the certificate related to this scid
    std::map<uint256, uint256 > mScCerts;

    // the catalog of scid, resulting after having collected all above contributions
    std::set<uint256> sScIds;

    static const std::string MAGIC_SC_STRING;

    // return the merkle root hash of the input leaves. The merkle tree is not saved.
    static uint256 getMerkleRootHash(const std::vector<uint256>& vInputLeaves);
};

#endif
