#ifndef SIDECHAIN_TX_COMMITMENT_BUILDER
#define SIDECHAIN_TX_COMMITMENT_BUILDER

#include <map>
#include <vector>
#include <set>
#include <string>

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
    void add(const CTransaction& tx);
    void add(const CScCertificate& cert);
    uint256 getCommitment();

private:
    // return the hash of a well known string (MAGIC_SC_STRING declared below) which can be used
    // as a null-semantic value
    static const uint256& getCrossChainNullHash();

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

    template <typename T>
    inline void addCrosschainOutput(const uint256& txHash, const T& txCcOut, unsigned int nCcOutPos, std::map<uint256, std::vector<uint256> >& map)
    {
        sScIds.insert(txCcOut.GetScId());

        // if the mapped value exists, vec is a reference to it. If it does not, vec is
        // a reference to the new element inserted in the map with the scid as a key
        std::vector<uint256>& vec = map[txCcOut.GetScId()];

        LogPrint("sc", "%s():%d - processing scId[%s], vec size = %d\n",
            __func__, __LINE__, txCcOut.GetScId().ToString(), vec.size());

        const uint256& ccOutHash = txCcOut.GetHash();

        LogPrint("sc", "%s():%d -Inputs: h1[%s], h2[%s], n[%d]\n",
            __func__, __LINE__, ccOutHash.ToString(), txHash.ToString(), nCcOutPos);

        const uint256& entry = HashCcOutput(ccOutHash, txHash, nCcOutPos);

        vec.push_back(entry);
        LogPrint("sc", "%s():%d -Output: entry[%s]\n", __func__, __LINE__, entry.ToString());

#ifdef DEBUG_SC_COMMITMENT_HASH
            CDataStream ss2(SER_NETWORK, PROTOCOL_VERSION);
            ss2 << ccOutHash;
            ss2 << txHash;
            ss2 << nCcOutPos;
            std::string ser2( HexStr(ss2.begin(), ss2.end()));
            const uint256& entry2 = Hash(ss2.begin(), ss2.begin() + (unsigned int)ss2.in_avail() );

            CHashWriter ss3(SER_GETHASH, PROTOCOL_VERSION);
            ss3 << ccOutHash;
            ss3 << txHash;
            ss3 << nCcOutPos;
            const uint256& entry3 = ss3.GetHash();

            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss << txCcOut;
            std::string ser( HexStr(ss.begin(), ss.end()));

            std::cout << __func__ << " -------------------------------------------" << std::endl;
            std::cout << "                       ccout: " << ser << std::endl;
            std::cout << "-------------------------------------------" << std::endl;
            std::cout << "                 Hash(ccout): " << ccOutHash.ToString() << std::endl;
            std::cout << "                        txid: " << txHash.ToString() << std::endl;
            std::cout << "                           n: " << std::hex << nCcOutPos << std::dec << std::endl;
            std::cout << "-------------------------------------------" << std::endl;
            std::cout << "    Hash(Hash(ccout)|txid|n): " << entry.ToString() << std::endl;
            std::cout << "-------------------------------------------" << std::endl;
            std::cout << "concat = Hash(ccout)|txid| n: " << ser2 << std::endl;
            std::cout << "                Hash(concat): " << entry2.ToString() << std::endl;
#endif
    }

    uint256 HashCcOutput(const uint256& ccoutHash, const uint256& txHash, unsigned int outPos);
};

#endif
