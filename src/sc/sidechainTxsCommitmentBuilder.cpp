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
bool SidechainTxsCommitmentBuilder::add(const CTransaction& tx) { return true; }
bool SidechainTxsCommitmentBuilder::add(const CScCertificate& cert) { return true; }
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

bool SidechainTxsCommitmentBuilder::add_scc(const CTxScCreationOut& ccout, const BufferWithSize& bws_tx_hash, uint32_t out_idx, CctpErrorCode& ret_code)
{
    field_t* scid_fe = (field_t*)ccout.GetScId().begin();

    const uint256& pub_key = ccout.address;
    BufferWithSize bws_pk(pub_key.begin(), pub_key.size());

    std::unique_ptr<BufferWithSize[]> bws_fe_cfg(nullptr);
    if (!ccout.vFieldElementCertificateFieldConfig.empty())
    {
        bws_fe_cfg.reset(new BufferWithSize(
            (const unsigned char*)&ccout.vFieldElementCertificateFieldConfig[0],
            (size_t)ccout.vFieldElementCertificateFieldConfig.size()
        ));
    }
 
    int bvcfg_size = ccout.vBitVectorCertificateFieldConfig.size(); 
    std::unique_ptr<BitVectorElementsConfig[]> bvcfg(new BitVectorElementsConfig[bvcfg_size]);
    int i = 0;
    for (auto entry: ccout.vBitVectorCertificateFieldConfig)
    {
        bvcfg[i].bit_vector_size_bits     = entry.getBitVectorSizeBits(); 
        bvcfg[i].max_compressed_byte_size = entry.getMaxCompressedSizeBytes(); 
        i++;
    }

    std::unique_ptr<BufferWithSize[]> bws_custom_data(nullptr);
    if (!ccout.customData.empty())
    {
        bws_custom_data.reset(new BufferWithSize(
            (unsigned char*)(&ccout.customData[0]),
            ccout.customData.size()
        ));
    }

    field_t* constant_fe = nullptr;
    if(ccout.constant.is_initialized())
    {
        constant_fe = ccout.constant->GetFieldElement().get();
    }
        
    BufferWithSize bws_cert_vk(ccout.wCertVk.GetDataBuffer(), ccout.wCertVk.GetDataSize());

    BufferWithSize bws_csw_vk(nullptr, 0);
    if(ccout.wCeasedVk.is_initialized())
    {
        bws_csw_vk.data = ccout.wCeasedVk->GetDataBuffer();
        bws_csw_vk.len = ccout.wCeasedVk->GetDataSize();
    }

    return zendoo_commitment_tree_add_scc(const_cast<commitment_tree_t*>(_cmt),
         scid_fe, 
         ccout.nValue,
         &bws_pk,
         &bws_tx_hash,
         out_idx,
         ccout.withdrawalEpochLength,
         ccout.mainchainBackwardTransferRequestDataLength,
         bws_fe_cfg.get(),
         bvcfg.get(),
         bvcfg_size,
         ccout.mainchainBackwardTransferRequestScFee, 
         ccout.forwardTransferScFee, 
         bws_custom_data.get(),
         constant_fe, 
         &bws_cert_vk,
         &bws_csw_vk,
         &ret_code
    );
}

bool SidechainTxsCommitmentBuilder::add_fwt(const CTxForwardTransferOut& ccout, const BufferWithSize& bws_tx_hash, uint32_t out_idx, CctpErrorCode& ret_code)
{
    field_t* scid_fe = (field_t*)ccout.GetScId().begin();

    const uint256& fwt_pub_key = ccout.address;
    BufferWithSize bws_fwt_pk((unsigned char*)fwt_pub_key.begin(), fwt_pub_key.size());

    return zendoo_commitment_tree_add_fwt(const_cast<commitment_tree_t*>(_cmt),
         scid_fe,
         ccout.nValue,
         &bws_fwt_pk,
         &bws_tx_hash,
         out_idx,
         &ret_code
    );
}

bool SidechainTxsCommitmentBuilder::add_bwtr(const CBwtRequestOut& ccout, const BufferWithSize& bws_tx_hash, uint32_t out_idx, CctpErrorCode& ret_code)
{
    field_t* scid_fe = (field_t*)ccout.GetScId().begin();

    int sc_req_data_len = ccout.vScRequestData.size(); 
    std::unique_ptr<const field_t*[]> sc_req_data(new const field_t*[sc_req_data_len]);
    int i = 0;
    for (auto entry: ccout.vScRequestData)
    {
        sc_req_data[i] = entry.GetFieldElement().get();
        i++;
    }

    const uint160& bwtr_pk_hash = ccout.mcDestinationAddress;
    BufferWithSize bws_bwtr_pk_hash(bwtr_pk_hash.begin(), bwtr_pk_hash.size());

    return zendoo_commitment_tree_add_bwtr(const_cast<commitment_tree_t*>(_cmt),
         scid_fe,
         ccout.scFee,
         sc_req_data.get(),
         sc_req_data_len,
         &bws_bwtr_pk_hash,
         &bws_tx_hash,
         out_idx,
         &ret_code
    );
}

bool SidechainTxsCommitmentBuilder::add_csw(const CTxCeasedSidechainWithdrawalInput& ccin, CctpErrorCode& ret_code)
{
    field_t* scid_fe = (field_t*)ccin.scId.begin();

    const uint160& csw_pk_hash = ccin.pubKeyHash;
    BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());

    return zendoo_commitment_tree_add_csw(const_cast<commitment_tree_t*>(_cmt),
         scid_fe,
         ccin.nValue,
         ccin.nullifier.GetFieldElement().get(),
         &bws_csw_pk_hash,
         &ret_code
    );
}

bool SidechainTxsCommitmentBuilder::add_cert(const CScCertificate& cert, CctpErrorCode& ret_code)
{
    field_t* scid_fe = (field_t*)cert.GetScId().begin();

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

    int custom_fields_len = cert.vFieldElementCertificateField.size(); 
    std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
    int i = 0;
    for (auto entry: cert.vFieldElementCertificateField)
    {
        CFieldElement fe{entry.getVRawData()};
        custom_fields[i] = fe.GetFieldElement().get();
        i++;
    }

    return zendoo_commitment_tree_add_cert(const_cast<commitment_tree_t*>(_cmt),
         scid_fe,
         cert.epochNumber,
         cert.quality,
         bt_list,
         bt_list_len,
         custom_fields.get(),
         custom_fields_len,
         cert.endEpochCumScTxCommTreeRoot.GetFieldElement().get(),
         cert.forwardTransferScFee,
         cert.mainchainBackwardTransferRequestScFee,
         &ret_code
    );
}

bool SidechainTxsCommitmentBuilder::add(const CTransaction& tx)
{
    assert(_cmt != nullptr);

    if (!tx.IsScVersion())
        return true;

    CctpErrorCode ret_code = CctpErrorCode::OK;

    const uint256& tx_hash = tx.GetHash();
    const BufferWithSize bws_tx_hash(tx_hash.begin(), tx_hash.size());

    uint32_t out_idx = 0;

    for (unsigned int scIdx = 0; scIdx < tx.GetVscCcOut().size(); ++scIdx)
    {
        const CTxScCreationOut& ccout = tx.GetVscCcOut().at(scIdx);

        if (!add_scc(ccout, bws_tx_hash, out_idx, ret_code))
        {
            LogPrintf("%s():%d Error adding sc creation: tx[%s], pos[%d], ret_code[%d]\n", __func__, __LINE__,
                tx_hash.ToString(), scIdx, ret_code);
            return false;
        }
        out_idx++;
    }

    for (unsigned int fwtIdx = 0; fwtIdx < tx.GetVftCcOut().size(); ++fwtIdx)
    {
        const CTxForwardTransferOut& ccout = tx.GetVftCcOut().at(fwtIdx);

        if (!add_fwt(ccout, bws_tx_hash, out_idx, ret_code))
        {
            LogPrintf("%s():%d Error adding fwt: tx[%s], pos[%d], ret_code[%d]\n", __func__, __LINE__,
                tx_hash.ToString(), fwtIdx, ret_code);
            return false;
        }
        out_idx++;
    }

    for (unsigned int bwtrIdx = 0; bwtrIdx < tx.GetVBwtRequestOut().size(); ++bwtrIdx)
    {
        const CBwtRequestOut& ccout = tx.GetVBwtRequestOut().at(bwtrIdx);

        if (!add_bwtr(ccout, bws_tx_hash, out_idx, ret_code))
        {
            LogPrintf("%s():%d Error adding bwtr: tx[%s], pos[%d], ret_code[%d]\n", __func__, __LINE__,
                tx_hash.ToString(), bwtrIdx, ret_code);
            return false;
        }
 
        out_idx++;
    }

    for (unsigned int cswIdx = 0; cswIdx < tx.GetVcswCcIn().size(); ++cswIdx)
    {
        const CTxCeasedSidechainWithdrawalInput& ccin = tx.GetVcswCcIn().at(cswIdx);

        if (!add_csw(ccin, ret_code))
        {
            LogPrintf("%s():%d Error adding csw: tx[%s], pos[%d], ret_code[%d]\n", __func__, __LINE__,
                tx_hash.ToString(), cswIdx, ret_code);
            return false;
        }
    }
    return true;
}

bool SidechainTxsCommitmentBuilder::add(const CScCertificate& cert)
{
    assert(_cmt != nullptr);

    CctpErrorCode ret_code = CctpErrorCode::OK;

    if (!add_cert(cert, ret_code))
    {
        LogPrintf("%s():%d Error adding cert[%s], ret_code[%d]\n", __func__, __LINE__,
            cert.GetHash().ToString(), ret_code);
        return false;
    }
    return true;
}

uint256 SidechainTxsCommitmentBuilder::getCommitment()
{
    assert(_cmt != nullptr);
    CctpErrorCode code;
    field_t* fe = zendoo_commitment_tree_get_commitment(const_cast<commitment_tree_t*>(_cmt), &code);
    assert(code == CctpErrorCode::OK);
    assert(fe != nullptr);

    wrappedFieldPtr res = {fe, CFieldPtrDeleter{}};
    CFieldElement finalTreeRoot{res};

    return finalTreeRoot.GetLegacyHashTO_BE_REMOVED();
}
#endif
