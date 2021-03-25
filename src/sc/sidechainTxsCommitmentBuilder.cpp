#include <sc/sidechainTxsCommitmentBuilder.h>
#include <primitives/transaction.h>
#include <primitives/certificate.h>
#include <uint256.h>
#include <algorithm>
#include <iostream>
#include <zendoo/zendoo_mc.h>

// TODO remove when not needed anymore
#include <gtest/libzendoo_test_files.h>

#ifdef BITCOIN_TX
void SidechainTxsCommitmentBuilder::add(const CTransaction& tx) { return; }
void SidechainTxsCommitmentBuilder::add(const CScCertificate& cert) { return; }
uint256 SidechainTxsCommitmentBuilder::getCommitment() { return uint256(); }
SidechainTxsCommitmentBuilder::SidechainTxsCommitmentBuilder(): _cmt(nullptr) {}
SidechainTxsCommitmentBuilder::~SidechainTxsCommitmentBuilder(){}
#else
SidechainTxsCommitmentBuilder::SidechainTxsCommitmentBuilder(): _cmt(initPtr())
{
    assert(_cmt != nullptr);
}

const commitment_tree_t* const SidechainTxsCommitmentBuilder::initPtr()
{
    return zendoo_commitment_tree_create();
}

SidechainTxsCommitmentBuilder::~SidechainTxsCommitmentBuilder()
{
    assert(_cmt != nullptr);
    zendoo_commitment_tree_delete(const_cast<commitment_tree_t*>(_cmt));
}


void SidechainTxsCommitmentBuilder::add(const CTransaction& tx)
{
    assert(_cmt != nullptr);

    if (!tx.IsScVersion())
        return;

    CctpErrorCode ret_code = CctpErrorCode::OK;

    const uint256& tx_hash = tx.GetHash();
    BufferWithSize bws_tx_hash(tx_hash.begin(), tx_hash.size());


    uint32_t out_idx = 0;

    for (unsigned int scIdx = 0; scIdx < tx.GetVscCcOut().size(); ++scIdx)
    {
        const CTxScCreationOut& ccout = tx.GetVscCcOut().at(scIdx);

        const uint256& scId = ccout.GetScId();
        BufferWithSize bws_scid(scId.begin(), scId.size());

        const uint256& pub_key = ccout.address;
        BufferWithSize bws_pk(pub_key.begin(), pub_key.size());

        BufferWithSize bws_custom_data(nullptr, 0);
        if (!ccout.customData.empty())
        {
            bws_custom_data.data = (unsigned char*)(&ccout.customData[0]);
            bws_custom_data.len = ccout.customData.size();
        }
 
        BufferWithSize bws_constant(nullptr, 0);
        if(ccout.constant.is_initialized())
        {
            bws_constant.data = ccout.constant->GetDataBuffer();
            bws_constant.len = ccout.constant->GetDataSize();
        }
            
        BufferWithSize bws_cert_vk(ccout.wCertVk.GetDataBuffer(), ccout.wCertVk.GetDataSize());
 
        BufferWithSize bws_mbtr_vk(nullptr, 0);
        if(ccout.wMbtrVk.is_initialized())
        {
            bws_mbtr_vk.data = ccout.wMbtrVk->GetDataBuffer();
            bws_mbtr_vk.len = ccout.wMbtrVk->GetDataSize();
        }
            
        BufferWithSize bws_csw_vk(nullptr, 0);
        if(ccout.wCeasedVk.is_initialized())
        {
            bws_csw_vk.data = ccout.wCeasedVk->GetDataBuffer();
            bws_csw_vk.len = ccout.wCeasedVk->GetDataSize();
        }

        bool ret = zendoo_commitment_tree_add_scc(const_cast<commitment_tree_t*>(_cmt),
             &bws_scid,
             ccout.nValue,
             &bws_pk,
             ccout.withdrawalEpochLength,
             &bws_custom_data,
             &bws_constant,
             &bws_cert_vk,
             &bws_mbtr_vk,
             &bws_csw_vk,
             &bws_tx_hash,
             out_idx,
             &ret_code
        );

        if (!ret)
        {
            LogPrintf("%s():%d Error adding sc creation: tx[%s], pos[%d], ret_code[%d]\n", __func__, __LINE__,
                tx_hash.ToString(), scIdx, ret_code);
            return;
        }
 
        out_idx++;
    }

    for (unsigned int fwtIdx = 0; fwtIdx < tx.GetVftCcOut().size(); ++fwtIdx)
    {
        const CTxForwardTransferOut& ccout = tx.GetVftCcOut().at(fwtIdx);

        const uint256& fwtScId = ccout.GetScId();
        BufferWithSize bws_fwt_scid((unsigned char*)fwtScId.begin(), fwtScId.size());

        const uint256& fwt_pub_key = ccout.address;
        BufferWithSize bws_fwt_pk((unsigned char*)fwt_pub_key.begin(), fwt_pub_key.size());

        bool ret = zendoo_commitment_tree_add_fwt(const_cast<commitment_tree_t*>(_cmt),
             &bws_fwt_scid,
             ccout.nValue,
             &bws_fwt_pk,
             &bws_tx_hash,
             out_idx,
             &ret_code
        );
        if (!ret)
        {
            LogPrintf("%s():%d Error adding fwt: tx[%s], pos[%d], ret_code[%d]\n", __func__, __LINE__,
                tx_hash.ToString(), fwtIdx, ret_code);
            return;
        }
 
        out_idx++;
    }

    for (unsigned int bwtrIdx = 0; bwtrIdx < tx.GetVBwtRequestOut().size(); ++bwtrIdx)
    {
        const CBwtRequestOut& ccout = tx.GetVBwtRequestOut().at(bwtrIdx);

        const uint256& bwtrScId = ccout.GetScId();
        BufferWithSize bws_bwtr_scid(bwtrScId.begin(), bwtrScId.size());

        const uint160& bwtr_pk_hash = ccout.mcDestinationAddress;
        BufferWithSize bws_bwtr_pk_hash(bwtr_pk_hash.begin(), bwtr_pk_hash.size());

        BufferWithSize bws_req_data(ccout.scRequestData.GetDataBuffer(), ccout.scRequestData.GetDataSize());
            
        bool ret = zendoo_commitment_tree_add_bwtr(const_cast<commitment_tree_t*>(_cmt),
             &bws_bwtr_scid,
             ccout.scFee,
             &bws_req_data,
             &bws_bwtr_pk_hash,
             &bws_tx_hash,
             out_idx,
             &ret_code
        );
        if (!ret)
        {
            LogPrintf("%s():%d Error adding bwtr: tx[%s], pos[%d], ret_code[%d]\n", __func__, __LINE__,
                tx_hash.ToString(), bwtrIdx, ret_code);
            return;
        }
 
        out_idx++;
    }

    for (unsigned int cswIdx = 0; cswIdx < tx.GetVcswCcIn().size(); ++cswIdx)
    {
        const CTxCeasedSidechainWithdrawalInput& ccin = tx.GetVcswCcIn().at(cswIdx);

        const uint256& cswScId = ccin.scId;
        BufferWithSize bws_csw_scid(cswScId.begin(), cswScId.size());

        const uint160& csw_pk_hash = ccin.pubKeyHash;
        BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());

        BufferWithSize bws_nullifier(ccin.nullifier.GetDataBuffer(), ccin.nullifier.GetDataSize());
            
        // TODO - they are not optional; for the time being set to a non empty field element
        const CFieldElement& dumFe = CFieldElement{SAMPLE_FIELD}; // libzendoo_test_files.h 
        BufferWithSize bws_active_cert_data_hash( dumFe.GetDataBuffer(), dumFe.GetDataSize());

        bool ret = zendoo_commitment_tree_add_csw(const_cast<commitment_tree_t*>(_cmt),
             &bws_csw_scid,
             ccin.nValue,
             &bws_nullifier,
             &bws_csw_pk_hash,
             &bws_active_cert_data_hash,
             &ret_code
        );
        if (!ret)
        {
            LogPrintf("%s():%d Error adding csw: tx[%s], pos[%d], ret_code[%d]\n", __func__, __LINE__,
                tx_hash.ToString(), cswIdx, ret_code);
            return;
        }
    }
}

void SidechainTxsCommitmentBuilder::add(const CScCertificate& cert)
{
    assert(_cmt != nullptr);

    CctpErrorCode ret_code = CctpErrorCode::OK;

    const uint256& certScId = cert.GetScId();
    BufferWithSize bws_cert_scid(certScId.begin(), certScId.size());

    const CFieldElement& cdh = cert.GetDataHash(); 
    BufferWithSize bws_cert_data_hash(cdh.GetDataBuffer(), cdh.GetDataSize());

    const backward_transfer_t* bt_list =  nullptr;
    std::vector<backward_transfer_t> vbt_list;
    for(int pos = cert.nFirstBwtPos; pos < cert.GetVout().size(); ++pos)
    {
        const CTxOut& out = cert.GetVout()[pos];
        const auto& bto = CBackwardTransferOut(out);
        backward_transfer_t x;
        x.amount = bto.nValue;
        memcpy(x.pk_dest, bto.pubKeyHash.begin(), sizeof(x.pk_dest));
        vbt_list.push_back(x);
    }

    if (!vbt_list.empty())
        bt_list = (const backward_transfer_t*)&vbt_list[0];

    size_t bt_list_len = vbt_list.size();

    // TODO - they are not optional; for the time being set to a non empty field element
    const CFieldElement& dumFe = CFieldElement{SAMPLE_FIELD}; // libzendoo_test_files.h 
    BufferWithSize bws_custom_fields_merkle_root( dumFe.GetDataBuffer(), dumFe.GetDataSize());
    BufferWithSize bws_end_cum_comm_tree_root( dumFe.GetDataBuffer(), dumFe.GetDataSize());
            
    bool ret = zendoo_commitment_tree_add_cert(const_cast<commitment_tree_t*>(_cmt),
         &bws_cert_scid,
         cert.epochNumber,
         cert.quality,
         &bws_cert_data_hash,
         bt_list,
         bt_list_len,
         &bws_custom_fields_merkle_root,
         &bws_end_cum_comm_tree_root,
         &ret_code
    );
    if (!ret)
    {
        LogPrintf("%s():%d Error adding cert[%s], ret_code[%d]\n", __func__, __LINE__,
            cert.GetHash().ToString(), ret_code);
    }
}

uint256 SidechainTxsCommitmentBuilder::getCommitment()
{
    assert(_cmt != nullptr);
    field_t* fe = zendoo_commitment_tree_get_commitment(const_cast<commitment_tree_t*>(_cmt));
    assert(fe != nullptr);

    wrappedFieldPtr res = {fe, CFieldPtrDeleter{}};
    CFieldElement finalTreeRoot{res};

    return finalTreeRoot.GetLegacyHashTO_BE_REMOVED();
}
#endif
