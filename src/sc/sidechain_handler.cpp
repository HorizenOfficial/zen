#include <sc/sidechain_handler.h>
#include <sc/sidechain.h>
#include <main.h>
#include <coins.h>
#include <undo.h>

CSidechainHandler::CSidechainHandler(): view(nullptr) {
    static CCoinsView dummyBack;
    static CCoinsViewCache dummyView(&dummyBack);
    view = &dummyView;
}

void CSidechainHandler::setView(CCoinsViewCache& _view) { view=&_view;}

bool CSidechainHandler::registerSidechain(const uint256& scId, int height)
{
    if (registeredScIds.count(scId) != 0)
        return true; //already registered

    if (!view->HaveSidechain(scId))
       return false; //unknown sidechain

    Sidechain::state scState = Sidechain::isCeasedAtHeight(*view, scId, height);
    if (scState != Sidechain::state::ALIVE)
        return false;

    registeredScIds.insert(scId);

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(height);
    int nextCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch + 1) + scInfo.SafeguardMargin() +1;

    CeasingSidechains[nextCeasingHeight].insert(scId);
    return true;
}

bool CSidechainHandler::addCertificate(const CScCertificate & cert, int height)
{
    if (registeredScIds.count(cert.GetScId()) == 0)
        return false;

    Sidechain::state scState = Sidechain::isCeasedAtHeight(*view, cert.GetScId(), height);
    if (scState != Sidechain::state::ALIVE)
        return false;

    CSidechain scInfo;
    view->GetSidechain(cert.GetScId(), scInfo);

    //record certificates if they have at least a bwt
    for(const CTxOut& vout: cert.GetVout()) {
        if (vout.isFromBackwardTransfer)
            lastEpochCerts.insert(std::pair<uint256, uint256>(cert.GetScId(), cert.GetHash())).second;
    }

    //update termination height for sidechain
    int nextCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2) + scInfo.SafeguardMargin()+1;
    CeasingSidechains[nextCeasingHeight].insert(cert.GetScId());

    int prevCeasingHeight = nextCeasingHeight - scInfo.creationData.withdrawalEpochLength;
    CeasingSidechains[nextCeasingHeight - scInfo.creationData.withdrawalEpochLength].erase(cert.GetScId());
    if (CeasingSidechains[prevCeasingHeight].size() == 0)
        CeasingSidechains.erase(prevCeasingHeight);

    return true;
}

void CSidechainHandler::removeCertificate(const CScCertificate & cert)
{
    if (registeredScIds.count(cert.GetScId()) == 0)
        return;

    CSidechain scInfo;
    assert(view->GetSidechain(cert.GetScId(), scInfo));

    lastEpochCerts.erase(cert.GetScId());

    int prevCeasingHeight = 0;
    for(scMapIter it = CeasingSidechains.begin(); it != CeasingSidechains.end(); ++it ) {
        if (it->second.count(cert.GetScId()) != 0) {
            prevCeasingHeight = it->first;
            break;
        }
    }

    CeasingSidechains[prevCeasingHeight].erase(cert.GetScId());
    if (CeasingSidechains[prevCeasingHeight].size() == 0)
        CeasingSidechains.erase(prevCeasingHeight);

    CeasingSidechains[prevCeasingHeight- scInfo.creationData.withdrawalEpochLength].insert(cert.GetScId());
    return;
}

void CSidechainHandler::unregisterSidechain(const uint256& scId)
{
    if (registeredScIds.count(scId) == 0)
        return;

    lastEpochCerts.erase(scId);

    if (!view->HaveSidechain(scId))
       return; //unknown sidechain

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);

    scMapIter it = CeasingSidechains.begin();
    for(;it != CeasingSidechains.end(); ++it )
    {
        if (it->second.count(scId) != 0) {
            it->second.erase(scId);
        }

        if(it->second.empty()) {
            CeasingSidechains.erase(it->first);
            break;
        } else {
            ++it;
        }
    }

    registeredScIds.erase(scId);
    return;
}

void CSidechainHandler::handleCeasingSidechains(CBlockUndo& blockUndo, int height)
{
    if (CeasingSidechains.count(height) == 0)
        return; //no sidechains terminating at current height

    assert(height <= CeasingSidechains.begin()->first);

    for (const uint256& ceasingScId : CeasingSidechains.at(height))
    {
        const uint256& certHash = lastEpochCerts[ceasingScId];

        //lastEpochCertsBySc have at least a bwt, hence they cannot be fully spent
        assert(view->HaveCoins(certHash));
        CCoinsModifier coins = view->ModifyCoins(certHash);

        //null all bwt outputs and add related txundo in block
        bool foundFirstBwt = false;
        for(unsigned int pos = 0; pos < coins->vout.size(); ++pos)
        {
            if (!coins->IsAvailable(pos))
                continue;
            if (!coins->vout[pos].isFromBackwardTransfer)
                continue;

            if (!foundFirstBwt) {
                blockUndo.vtxundo.push_back(CTxUndo());
                blockUndo.vtxundo.back().refTx = certHash;
                blockUndo.vtxundo.back().firstBwtPos = pos;
                foundFirstBwt = true;
            }

            blockUndo.vtxundo.back().vprevout.push_back(CTxInUndo(coins->vout[pos]));
            coins->Spend(pos);
            if (coins->vout.size() == 0 || coins->vout[pos].isFromBackwardTransfer) {
                CTxInUndo& undo = blockUndo.vtxundo.back().vprevout.back();
                undo.nHeight    = coins->nHeight;
                undo.fCoinBase  = coins->fCoinBase;
                undo.nVersion   = coins->nVersion;
                undo.originScId = coins->originScId;
            }
        }
    }

    return;
}
