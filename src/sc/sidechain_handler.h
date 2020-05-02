/*
 * sidechainHandler.h
 *
 *  Created on: Apr 28, 2020
 *      Author: abenegia
 */

#ifndef SC_SIDECHAIN_HANDLER_H_
#define SC_SIDECHAIN_HANDLER_H_

#include <uint256.h>
#include <map>
#include <set>

class CCoinsViewCache;
class CSidechain;
class CScCertificate;
class CBlockUndo;

enum class sidechainState {
    NOT_APPLICABLE = 0,
    ALIVE,
    CEASED
};

class CSidechainHandler
{
public:
    CSidechainHandler();
    ~CSidechainHandler() = default;
    void setView(CCoinsViewCache& _view);

    bool registerSidechain(const uint256& scId, int height);
    bool addCertificate(const CScCertificate & cert, int height);

    void handleCeasingSidechains(CBlockUndo& blockundo, int height);
    bool restoreCeasedSidechains(const CBlockUndo& blockundo);

    void removeCertificate(const CScCertificate & cert);
    void unregisterSidechain(const uint256& scId);

    sidechainState isSidechainCeasedAtHeight(const uint256& scId, int height);

private:
    CCoinsViewCache * view;

    std::set<uint256> registeredScIds;
    std::map<int, std::set<uint256>> CeasingSidechains;
    std::map<uint256, uint256> lastEpochCerts;

    typedef std::map<uint256, std::set<uint256>>::const_iterator certMapIter;
    typedef std::map<int, std::set<uint256>>::iterator scMapIter;
};

#endif /* SC_SIDECHAIN_HANDLER_H_ */
