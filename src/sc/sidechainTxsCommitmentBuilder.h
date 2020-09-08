#ifndef SIDECHAIN_TX_COMMITMENT_BUILDER
#define SIDECHAIN_TX_COMMITMENT_BUILDER

#include <map>
#include <vector>
#include <set>
#include <string>

#include <zendoo/zendoo_mc.h>
#include <uint256.h>
#include <hash.h>
#include <utilstrencodings.h>
#include <util.h>

class CTransaction;
class CScCertificate;

// uncomment for debugging some sc commitment related hashing calculations
// #define DEBUG_SC_COMMITMENT_HASH 1

class SidechainTxsCommitmentBuilder
{
public:
    SidechainTxsCommitmentBuilder();
    ~SidechainTxsCommitmentBuilder();
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

    template <typename T>
    inline void addCrosschainOutput(const uint256& txHash, const T& txCcOut, unsigned int nCcOutPos, std::map<uint256, std::vector<field_t*> >& map)
    {
        sScIds.insert(txCcOut.GetScId());

        // if the mapped value exists, vec is a reference to it. If it does not, vec is
        // a reference to the new element inserted in the map with the scid as a key
        std::vector<field_t*>& vec = map[txCcOut.GetScId()];

        LogPrint("sc", "%s():%d - processing scId[%s], vec size = %d\n",
            __func__, __LINE__, txCcOut.GetScId().ToString(), vec.size());

        const uint256& ccOutHash = txCcOut.GetHash();

        LogPrint("sc", "%s():%d -Inputs: h1[%s], h2[%s], n[%d]\n",
            __func__, __LINE__, ccOutHash.ToString(), txHash.ToString(), nCcOutPos);

        field_t* pField = mapScTxToField(ccOutHash, txHash, nCcOutPos);
        vec.push_back(pField);
    }

    field_t* mapScTxToField(const uint256& ccoutHash, const uint256& txHash, unsigned int outPos);
    field_t* mapCertToField(const uint256& certHash);

    static const unsigned char zeros[SC_FIELD_SIZE];
    field_t* const emptyField;

    uint256 mapFieldToHash(const field_t* pField);

    //helpers
    inline unsigned int treeHeightForLeaves(unsigned int numberOfLeaves);
    field_t* merkleTreeRootOf(std::vector<field_t*>& leaves);
};

#endif
