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

enum class sidechainState {
    NOT_APPLICABLE = 0,
    ALIVE,
    CEASED
};

class CSidechainHandler
{
public:
    CSidechainHandler(const CCoinsViewCache& _view);
    ~CSidechainHandler() = default;

    bool registerSidechain(const uint256& scId);
    bool addCertificate(const CScCertificate & cert);

    void handleCeasingSidechains();

    void removeCertificate(const CScCertificate & cert);
    void unregisterSidechain(const uint256& scId);

    sidechainState isSidechainCeased(const uint256& scId);

private:
    const CCoinsViewCache & view;

    std::set<uint256> registeredScIds;
    std::map<int, std::set<uint256>> scByCeasingHeight;
    std::map<uint256, uint256> lastEpochCertsBySc;

    typedef std::map<uint256, std::set<uint256>>::const_iterator certMapIter;
    typedef std::map<int, std::set<uint256>>::iterator scMapIter;
};

#endif /* SC_SIDECHAIN_HANDLER_H_ */
