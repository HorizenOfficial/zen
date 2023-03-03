#ifndef SIDECHAIN_TX_COMMITMENT_BUILDER
#define SIDECHAIN_TX_COMMITMENT_BUILDER

#include "coins.h"
#include <sc/sidechaintypes.h>

class CTransaction;
class CScCertificate;
class uint256;

class CTxScCreationOut;
class CTxForwardTransferOut;
class CBwtRequestOut;

class CTxCeasedSidechainWithdrawalInput;

class SidechainTxsCommitmentBuilder
{
public:
    SidechainTxsCommitmentBuilder();
    ~SidechainTxsCommitmentBuilder();

    SidechainTxsCommitmentBuilder(const SidechainTxsCommitmentBuilder&) = delete;
    SidechainTxsCommitmentBuilder& operator=(const SidechainTxsCommitmentBuilder&) = delete;

    bool add(const CTransaction& tx);
    bool add(const CScCertificate& cert, const CCoinsViewCache& view);
    uint256 getCommitment();

    static const uint256& getEmptyCommitment();

private:
    const commitment_tree_t* const _cmt;

    // private initializer for instantiating the const ptr in the ctor initializer lists
    const commitment_tree_t* const initPtr();

    bool add_scc(const CTxScCreationOut& ccout, const BufferWithSize& bws_tx_hash, uint32_t out_idx, CctpErrorCode& ret_code);
    bool add_fwt(const CTxForwardTransferOut& ccout, const BufferWithSize& bws_tx_hash, uint32_t out_idx, CctpErrorCode& ret_code);
    bool add_bwtr(const CBwtRequestOut& ccout, const BufferWithSize& bws_tx_hash, uint32_t out_idx, CctpErrorCode& ret_code);

    bool add_csw(const CTxCeasedSidechainWithdrawalInput& ccin, CctpErrorCode& ret_code);
    bool add_cert(const CScCertificate& cert, Sidechain::ScFixedParameters scFixedParams, CctpErrorCode& ret_code);
};

#endif