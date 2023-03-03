#include <sc/sidechainTxsCommitmentGuard.h>
#include <primitives/transaction.h>
#include <primitives/certificate.h>
#include <uint256.h>
#include <algorithm>
#include <iostream>

#ifdef BITCOIN_TX
bool SidechainTxsCommitmentGuard::add(const CTransaction& tx, bool autoRewind) { return true; }
bool SidechainTxsCommitmentGuard::add(const CScCertificate& cert) { return true; }
#else

bool SidechainTxsCommitmentGuard::add_fwt(const CTxForwardTransferOut& ccout)
{
    LogPrint("sc", "%s():%d entering \n", __func__, __LINE__);

    // Check against the number of sidechains currently in the commitment tree
    // If we are adding a new sc and we already hit the limit, do not add anything
    if (!cbs.checkAvailableSpaceAliveSC(ccout.GetScId()))
        return false;

    // The sidechain id should not be already in the ceased tree
    if (cbs.checkExistenceInCeasedSCTree(ccout.GetScId())) {
        LogPrint("sc", "%s():%d - scTxsCommitment guard failed: adding fwt for scId[%s] on alive subtree, but already added on ceased subtree.\n",
            __func__, __LINE__, ccout.GetScId().ToString());
        return false;
    }

    CommitmentBuilderStatsAliveCounter& counterRef = cbs.cbsaMap[ccout.GetScId()];

    // Check against the number of FT already present in the sc subtree.
    // Only try to add if under the limit; increase the counter only if add was successful.
    if (counterRef.ft < cbs.FT_LIMIT) {
        ++counterRef.ft;
        return true;
    } else {
        LogPrint("sc", "%s():%d - scTxsCommitment guard failed: too many FT for sidechain scId[%s].\n",
            __func__, __LINE__, ccout.GetScId().ToString());
        return false;
    }
}

bool SidechainTxsCommitmentGuard::add_bwtr(const CBwtRequestOut& ccout)
{
    LogPrint("sc", "%s():%d entering \n", __func__, __LINE__);

    // Check against the number of sidechains currently in the commitment tree
    // If we are adding a new sc and we already hit the limit, do not add anything
    if (!cbs.checkAvailableSpaceAliveSC(ccout.GetScId()))
        return false;

    // The sidechain id should not be already in the ceased tree
    if (cbs.checkExistenceInCeasedSCTree(ccout.GetScId())) {
        LogPrint("sc", "%s():%d - scTxsCommitment guard failed: adding bwtr for scId[%s] on alive subtree, but already added on ceased subtree.\n",
            __func__, __LINE__, ccout.GetScId().ToString());
        return false;
    }

    CommitmentBuilderStatsAliveCounter& counterRef = cbs.cbsaMap[ccout.GetScId()];

    // Check against the number of BWT already present in the sc subtree.
    // Only try to add if under the limit; increase the counter only if add was successful.
    if (counterRef.bwtr < cbs.BWTR_LIMIT) {
        ++counterRef.bwtr;
        return true;
    } else {
        LogPrint("sc", "%s():%d - scTxsCommitment guard failed: too many BWTR for sidechain scId[%s].\n",
            __func__, __LINE__, ccout.GetScId().ToString());
        return false;
    }
}

bool SidechainTxsCommitmentGuard::add_csw(const CTxCeasedSidechainWithdrawalInput& ccin)
{
    LogPrint("sc", "%s():%d entering \n", __func__, __LINE__);

    // Check against the number of sidechains currently in the commitment tree
    // If we are adding a new sc and we already hit the limit, do not add anything
    if (!cbs.checkAvailableSpaceCeasedSC(ccin.scId))
        return false;

    // The sidechain id should not be already in the alive tree
    if (cbs.checkExistenceInAliveSCTree(ccin.scId)) {
        LogPrint("sc", "%s():%d - scTxsCommitment guard failed: adding csw for scId[%s] on ceased subtree, but already added on alive subtree.\n",
            __func__, __LINE__, ccin.scId.ToString());
        return false;
    }

    CommitmentBuilderStatsCeasedCounter& counterRef = cbs.cbscMap[ccin.scId];

    // Check against the number of CSW already present in the sc subtree.
    // Only try to add if under the limit; increase the counter only if add was successful.
    if (counterRef.csw < cbs.CSW_LIMIT) {
        ++counterRef.csw;
        return true;
    } else {
        LogPrint("sc", "%s():%d - scTxsCommitment guard failed: too many CSW for sidechain scId[%s].\n",
            __func__, __LINE__, ccin.scId.ToString());
        return false;
    }
}

bool SidechainTxsCommitmentGuard::add_cert(const CScCertificate& cert)
{
    LogPrint("sc", "%s():%d entering \n", __func__, __LINE__);

    size_t bt_list_len = cert.GetVout().size() - cert.nFirstBwtPos;

    // Check against the number of sidechains currently in the commitment tree
    // If we are adding a new sc and we already hit the limit, do not add anything
    if (!cbs.checkAvailableSpaceAliveSC(cert.GetScId()))
        return false;

    // The sidechain id should not be already in the ceased tree
    if (cbs.checkExistenceInCeasedSCTree(cert.GetScId())) {
        LogPrint("sc", "%s():%d - scTxsCommitment guard failed: adding cert for scId[%s] on alive subtree, but already added on ceased subtree.\n",
            __func__, __LINE__, cert.GetScId().ToString());
        return false;
    }

    // We have an hard limit for the total number of backward transfers inside all the certificates per sidechain
    if (cbs.cbsaMap[cert.GetScId()].bwt + bt_list_len > cbs.BWT_LIMIT) {
        LogPrint("sc", "%s():%d - scTxsCommitment guard failed: too many total BWT when adding cert for scId[%s]\n",
            __func__, __LINE__, cert.GetScId().ToString());
        return false;
    }

    CommitmentBuilderStatsAliveCounter& counterRef = cbs.cbsaMap[cert.GetScId()];

    // Check against the number of CERT already present in the sc subtree.
    // Only try to add if under the limit; increase the counter only if add was successful.
    if (counterRef.cert < cbs.CERT_LIMIT) {
        ++counterRef.cert;
        counterRef.bwt += bt_list_len;
        return true;
    } else {
        LogPrint("sc", "%s():%d - scTxsCommitment guard failed: too many CERT for sidechain scId[%s].\n",
            __func__, __LINE__, cert.GetScId().ToString());
        return false;
    }
}

bool SidechainTxsCommitmentGuard::add(const CTransaction& tx, bool autoRewind)
{
    if (!tx.IsScVersion())
        return true;

    LogPrint("sc", "%s():%d adding tx[%s] to ScTxsCommitmentGuard\n", __func__, __LINE__, tx.GetHash().ToString());

    const uint256& tx_hash = tx.GetHash();

    bool addOK = true;
    int addedFt = 0, addedBwtr = 0, addedCsw = 0;

    for (unsigned int fwtIdx = 0; fwtIdx < tx.GetVftCcOut().size(); ++fwtIdx)
    {
        const CTxForwardTransferOut& ccout = tx.GetVftCcOut().at(fwtIdx);

        if (!add_fwt(ccout))
        {
            LogPrintf("%s():%d Error adding fwt: tx[%s], pos[%d]\n", __func__, __LINE__,
                tx_hash.ToString(), fwtIdx);
            addOK = false;
            break;
        } else {
            addedFt++;
        }
    }

    if (addOK) {
        for (unsigned int bwtrIdx = 0; bwtrIdx < tx.GetVBwtRequestOut().size(); ++bwtrIdx)
        {
            const CBwtRequestOut& ccout = tx.GetVBwtRequestOut().at(bwtrIdx);

            if (!add_bwtr(ccout))
            {
                LogPrintf("%s():%d Error adding bwtr: tx[%s], pos[%d]\n", __func__, __LINE__,
                    tx_hash.ToString(), bwtrIdx);
                addOK = false;
                break;
            } else {
                addedBwtr++;
            }
        }
    }

    if (addOK) {
        for (unsigned int cswIdx = 0; cswIdx < tx.GetVcswCcIn().size(); ++cswIdx)
        {
            const CTxCeasedSidechainWithdrawalInput& ccin = tx.GetVcswCcIn().at(cswIdx);

            if (!add_csw(ccin))
            {
                LogPrintf("%s():%d Error adding csw: tx[%s], pos[%d]\n", __func__, __LINE__,
                    tx_hash.ToString(), cswIdx);
                addOK = false;
                break;
            } else {
                addedCsw++;
            }
        }
    }

    // Restore CBS to a valid state if we were not able to add any of the FT / BWTR / CSW
    if (!addOK && autoRewind) {
        rewind(tx, addedFt, addedBwtr, addedCsw);
        keepMapsClean();
    }
    
    return addOK;
}

bool SidechainTxsCommitmentGuard::add(const CScCertificate& cert)
{
    LogPrint("sc", "%s():%d adding cert[%s] to ScTxsCommitmentGuard\n", __func__, __LINE__, cert.GetHash().ToString());

    if (!add_cert(cert))
    {
        LogPrintf("%s():%d Error adding cert[%s]\n", __func__, __LINE__,
            cert.GetHash().ToString());
        return false;
    }
    return true;
}

// Remove from the maps all the sidechains without entities
void SidechainTxsCommitmentGuard::keepMapsClean() {
    auto itA = cbs.cbsaMap.begin();
    while (itA != cbs.cbsaMap.end()) {
        if ((itA->second.bwt == 0) && (itA->second.bwtr == 0) && (itA->second.cert == 0) && (itA->second.ft == 0))
            cbs.cbsaMap.erase(itA++);
        else
            ++itA;
    }

    auto itC = cbs.cbscMap.begin();
    while (itC != cbs.cbscMap.end()) {
        if (itC->second.csw == 0)
            cbs.cbscMap.erase(itC++);
        else
            ++itC;
    }
}

void SidechainTxsCommitmentGuard::rewind(const CTransaction& tx, const int addedFt, const int addedBwtr, const int addedCsw) {
    LogPrint("sc", "%s():%d Rewind scCommitmentGuard after failure %d FT, %d BWTR, %d CSW \n",
            __func__, __LINE__, addedFt, addedBwtr, addedCsw);

    for (unsigned int cswIdx = 0; cswIdx < addedCsw; ++cswIdx)
    {
        const CTxCeasedSidechainWithdrawalInput& ccin = tx.GetVcswCcIn().at(cswIdx);
        CommitmentBuilderStatsCeasedCounter& counterRef = cbs.cbscMap[ccin.scId];
        counterRef.csw--;
    }

    for (unsigned int bwtrIdx = 0; bwtrIdx < addedBwtr; ++bwtrIdx)
    {
        const CBwtRequestOut& ccout = tx.GetVBwtRequestOut().at(bwtrIdx);
        CommitmentBuilderStatsAliveCounter& counterRef = cbs.cbsaMap[ccout.GetScId()];
        counterRef.bwtr--;
    }

    for (unsigned int fwtIdx = 0; fwtIdx < addedFt; ++fwtIdx)
    {
        const CTxForwardTransferOut& ccout = tx.GetVftCcOut().at(fwtIdx);
        CommitmentBuilderStatsAliveCounter& counterRef = cbs.cbsaMap[ccout.GetScId()];
        counterRef.ft--;
    }
}

void SidechainTxsCommitmentGuard::rewind(const CTransaction& tx) {
    LogPrint("sc", "%s():%d Rewind scCommitmentGuard after tx failure \n", __func__, __LINE__);

    rewind(tx, tx.GetVftCcOut().size(), tx.GetVBwtRequestOut().size(), tx.GetVcswCcIn().size());
    keepMapsClean();
}

void SidechainTxsCommitmentGuard::rewind(const CScCertificate& cert) {
    LogPrint("sc", "%s():%d Rewind scCommitmentGuard after cert failure \n", __func__, __LINE__);

    size_t bt_list_len = cert.GetVout().size() - cert.nFirstBwtPos;
    CommitmentBuilderStatsAliveCounter& counterRef = cbs.cbsaMap[cert.GetScId()];
    --counterRef.cert;
    counterRef.bwt -= bt_list_len;
    keepMapsClean();
}



#endif
