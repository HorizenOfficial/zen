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

    sidechainState scState = isSidechainCeasedAtHeight(scId, height);
    if (scState != sidechainState::ALIVE)
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

    sidechainState scState = isSidechainCeasedAtHeight(cert.GetScId(), height);
    if (scState != sidechainState::ALIVE)
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
    lastEpochCerts.erase(cert.GetScId());
    //TODO:scByCeasingHeight should prolly be updated as a consequence??
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
        }


        if(std::distance(CeasingSidechains.begin(),it) > scInfo.creationData.withdrawalEpochLength)
            break;

        ++it;
    }

    registeredScIds.erase(scId);
    return;
}

void CSidechainHandler::handleCeasingSidechains(CBlockUndo& blockUndo, int height)
{
    assert(height <= CeasingSidechains.begin()->first);

    if (CeasingSidechains.count(height) == 0)
        return; //no sidechains terminating at current height

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
            if (coins->vout.size() == 0) {
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

bool CSidechainHandler::restoreCeasedSidechains(const CBlockUndo& blockundo)
{
    bool fClean = true;
    if(blockundo.vtxundo.size() != 1)
        fClean = fClean && error("%s: malformed undo data", __func__);

    const uint256& coinHash = blockundo.vtxundo.at(0).refTx;
    if(coinHash.IsNull())
    {
        fClean = fClean && error("%s: malformed undo data, ", __func__);
        return fClean;
    }
    CCoinsModifier coins = view->ModifyCoins(coinHash);
    unsigned int firstBwtPos = blockundo.vtxundo.at(0).firstBwtPos;

    const std::vector<CTxInUndo>& outVec = blockundo.vtxundo.at(0).vprevout;

    for (size_t bwtOutPos = outVec.size(); bwtOutPos-- > 0;)
    {
        if (outVec.at(bwtOutPos).nHeight != 0)
        {
            if(!coins->IsPruned())
                fClean = fClean && error("%s: undo data overwriting existing transaction", __func__);
            coins->Clear();
            coins->fCoinBase  = outVec.at(bwtOutPos).fCoinBase;
            coins->nHeight    = outVec.at(bwtOutPos).nHeight;
            coins->nVersion   = outVec.at(bwtOutPos).nVersion;
            coins->originScId = outVec.at(bwtOutPos).originScId;
        } else
        {
            if(coins->IsPruned())
                fClean = fClean && error("%s: undo data adding output to missing transaction", __func__);
        }

        if(coins->IsAvailable(firstBwtPos + bwtOutPos))
            fClean = fClean && error("%s: undo data overwriting existing output", __func__);
        if (coins->vout.size() < (firstBwtPos + bwtOutPos+1))
            coins->vout.resize(firstBwtPos + bwtOutPos+1);
        coins->vout.at(firstBwtPos + bwtOutPos) = outVec.at(bwtOutPos).txout;
    }

    return fClean;
}

sidechainState CSidechainHandler::isSidechainCeasedAtHeight(const uint256& scId, int height)
{
    if (!view->HaveSidechain(scId))
        return sidechainState::NOT_APPLICABLE;

    if (height > chainActive.Height()+1)
        return sidechainState::NOT_APPLICABLE; //too much in the future, can't tell

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);

    if (height < scInfo.creationBlockHeight)
        return sidechainState::NOT_APPLICABLE;

    int currentEpoch = scInfo.EpochFor(height);

    if (currentEpoch > scInfo.lastEpochReferencedByCertificate + 2)
        return sidechainState::CEASED;

    if (currentEpoch == scInfo.lastEpochReferencedByCertificate + 2)
    {
        int targetEpochSafeguardHeight = scInfo.StartHeightForEpoch(currentEpoch) + scInfo.SafeguardMargin();
        if (height > targetEpochSafeguardHeight)
            return sidechainState::CEASED;
    }

    return  sidechainState::ALIVE;
}
