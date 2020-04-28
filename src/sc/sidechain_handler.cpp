#include <sc/sidechain_handler.h>
#include <sc/sidechain.h>
#include <main.h>
#include <coins.h>

CSidechainHandler::CSidechainHandler(const CCoinsViewCache& _view): view(_view) {};

bool CSidechainHandler::registerSidechain(const uint256& scId)
{
    if (registeredScIds.count(scId) != 0)
        return false; //already registered

    if (!view.HaveSidechain(scId))
       return false; //unknown sidechain

    sidechainState scState = isSidechainCeased(scId);
    if (scState != sidechainState::ALIVE)
        return false;


    registeredScIds.insert(scId);

    CSidechain scInfo;
    view.GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    int nextSafeguardHeight = scInfo.StartHeightForEpoch(currentEpoch + 1) + scInfo.SafeguardMargin();

    scByCeasingHeight[nextSafeguardHeight].insert(scId);
    return true;
}

bool CSidechainHandler::addCertificate(const CScCertificate & cert)
{
    if (registeredScIds.count(cert.GetScId()) == 0)
        return false;

    if (!view.HaveSidechain(cert.GetScId()))
        return false; //unknown sidechain

    sidechainState scState = isSidechainCeased(cert.GetScId());
    if (scState != sidechainState::ALIVE)
        return false;

    CSidechain scInfo;
    view.GetSidechain(cert.GetScId(), scInfo);

    //record certificates if they have at least a bwt
    for(const CTxOut& vout: cert.GetVout()) {
        if (vout.isFromBackwardTransfer)
            return lastEpochCertsBySc.insert(std::pair<uint256, uint256>(cert.GetScId(), cert.GetHash())).second;
    }

    //update termination height for sidechain
    int nextSafeguardHeight = scInfo.StartHeightForEpoch(cert.epochNumber+1) + scInfo.SafeguardMargin();
    scByCeasingHeight[nextSafeguardHeight].insert(cert.GetScId());
    scByCeasingHeight[nextSafeguardHeight - scInfo.creationData.withdrawalEpochLength].erase(cert.GetScId());

    return true;
}

void CSidechainHandler::removeCertificate(const CScCertificate & cert)
{
    lastEpochCertsBySc.erase(cert.GetScId());
    //TODO:scByCeasingHeight should prolly be updated as a consequence??
    return;
}

void CSidechainHandler::unregisterSidechain(const uint256& scId)
{
    if (registeredScIds.count(scId) == 0)
        return;

    lastEpochCertsBySc.erase(scId);

    if (!view.HaveSidechain(scId))
       return; //unknown sidechain

    CSidechain scInfo;
    view.GetSidechain(scId, scInfo);

    scMapIter it = scByCeasingHeight.begin();
    for(;it != scByCeasingHeight.end(); ++it )
    {
        if (it->second.count(scId) != 0) {
            it->second.erase(scId);
        }

        if(it->second.empty()) {
            scByCeasingHeight.erase(it->first);
            break;
        }


        if(std::distance(scByCeasingHeight.begin(),it) > scInfo.creationData.withdrawalEpochLength)
            break;

        ++it;
    }

    registeredScIds.erase(scId);
    return;
}

void CSidechainHandler::handleCeasingSidechains()
{
    if (scByCeasingHeight.count(chainActive.Height()) == 0)
        return; //no sidechains terminating at current height

    for (const uint256& ceasingScId : scByCeasingHeight.at(chainActive.Height())) {
        const uint256& certHash = lastEpochCertsBySc[ceasingScId];
        assert(view.HaveCoins(certHash)); //lastEpochCertsBySc have at least a bwt, hence they cannot be fully spent

        //create block and fully spent coins
    }

    return;
}

sidechainState CSidechainHandler::isSidechainCeased(const uint256& scId)
{
    if (!view.HaveSidechain(scId))
        return sidechainState::NOT_APPLICABLE;

    CSidechain scInfo;
    view.GetSidechain(scId, scInfo);

    if (chainActive.Height() < scInfo.creationBlockHeight)
        return sidechainState::NOT_APPLICABLE;

    int currentEpoch = scInfo.EpochFor(chainActive.Height());
    if (currentEpoch == 0)
        return sidechainState::ALIVE; //sidechain cannot cease in its first epoch

    if (currentEpoch > scInfo.lastEpochReferencedByCertificate + 1)
        return sidechainState::CEASED;

    if (currentEpoch == scInfo.lastEpochReferencedByCertificate + 1)
    {
        int targetEpochSafeguardHeight = scInfo.StartHeightForEpoch(currentEpoch+1) + scInfo.SafeguardMargin();
        if (chainActive.Height() > targetEpochSafeguardHeight)
            return sidechainState::CEASED;
    }

    return  sidechainState::ALIVE;
}
