#ifndef SIDECHAIN_TX_COMMITMENT_BUILDER
#define SIDECHAIN_TX_COMMITMENT_BUILDER

#include <sc/sidechaintypes.h>

class CTransaction;
class CScCertificate;
class uint256;

class SidechainTxsCommitmentBuilder
{
public:
    SidechainTxsCommitmentBuilder();
    ~SidechainTxsCommitmentBuilder();

    SidechainTxsCommitmentBuilder(const SidechainTxsCommitmentBuilder&) = delete;
    SidechainTxsCommitmentBuilder& operator=(const SidechainTxsCommitmentBuilder&) = delete;

    void add(const CTransaction& tx);
    void add(const CScCertificate& cert);
    uint256 getCommitment();

private:
    const commitment_tree_t* const _cmt;

    // private initializer for instantiating the const ptr in the ctor initializer lists
    const commitment_tree_t* const initPtr();
};

#endif
