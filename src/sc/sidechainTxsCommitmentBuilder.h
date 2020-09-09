#ifndef SIDECHAIN_TX_COMMITMENT_BUILDER
#define SIDECHAIN_TX_COMMITMENT_BUILDER

#include <map>
#include <vector>
#include <set>

#include <zendoo/zendoo_mc.h>
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

private:
    // the final model will have a fixed-height merkle tree for 'SCTxsCommitment'.
    // That will also imply having a limit per block to the number of crosschain outputs per SC
    // and also to the number of SC
    // -----------------------------

    // Key:   the side chain ID
    // Value: the array of fields into which forward transfers are mapped.
    //        Tx are ordered as they are included in the block
    std::map<uint256, std::vector<field_t*> > mScMerkleTreeLeavesFt;

    // The same for BTR (to be implemented)
    std::map<uint256, std::vector<field_t*> > mScMerkleTreeLeavesBtr;

    // Key:   the side chain ID
    // Value: the field into with the certificate related to this scid is mapped
    std::map<uint256, field_t* > mScCerts;

    // the catalog of scid, resulting after having collected all above contributions
    std::set<uint256> sScIds;

    class EmptyField
    {
    public:
        EmptyField();
        ~EmptyField();
        field_t* const zero;
    private:
        const unsigned char zerosHelperArray[SC_FIELD_SIZE];
    };
    static EmptyField emptyField;

    field_t* mapScTxToField(const uint256& ccoutHash, const uint256& txHash, unsigned int outPos);
    field_t* mapCertToField(const uint256& certHash);

    uint256 mapFieldToHash(const field_t* pField);

    inline unsigned int treeHeightForLeaves(unsigned int numberOfLeaves);
    field_t* merkleTreeRootOf(std::vector<field_t*>& leaves);
};

#endif
