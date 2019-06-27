#ifndef _SIDECHAIN_H
#define _SIDECHAIN_H

#include <vector>

#include "amount.h"
#include "chain.h"
#include "hash.h"
#include <boost/unordered_map.hpp>

//--------------------------------
class ScCertifier
{
public:
    CAmount lockedAmount;
};


class ScTransactionBase
{
    const uint256 scId;
};


class ScTransactionWithAmount : public ScTransactionBase
{
    CAmount nValue;
    uint256 address;
};


//--------------------------------
class CertifierLock : public ScTransactionWithAmount
{
    int64_t activeFromWithdrawalEpoch; 
};

//--------------------------------
class ForwardTransfer : public ScTransactionBase
{
};

//--------------------------------
class ScCreation : public ScTransactionBase
{
    // all creation data follows...
    // TODO
};

class ScTransaction
{
    std::vector<ScCreation> creations;
    std::vector<CertifierLock> certifierLocks;
    std::vector<ForwardTransfer> forwardTransfers;
};

//--------------------------------
class ScInfo
{
public:
    // reference to the block containing the tx that created the side chain 
    const CBlockIndex* creationBlockIndex;;

    // total amount given by sum(fw transfer)
    CAmount balance;

    // list of certifiers
    std::vector<ScCertifier> certifiers;

    // creation data
    const ScCreation creationData;

    std::string ToString() const;
};

//--------------------------------
typedef boost::unordered_map<uint256, ScInfo, ObjectHasher> ScInfoMap;

#endif // _SIDECHAIN_H
