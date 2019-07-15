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
    uint256 address;
    CAmount lockedAmount;
};


struct ScTransactionBase
{
    uint256 scId;
    virtual ~ScTransactionBase() {}
};


struct ScTransactionWithAmount : public ScTransactionBase
{
    CAmount nValue;
    uint256 address;

    ScTransactionWithAmount() :nValue(0) {}
};


//--------------------------------
struct CertifierLock : public ScTransactionWithAmount
{
    int64_t activeFromWithdrawalEpoch; 

    CertifierLock() : activeFromWithdrawalEpoch(-1) {}
};

//--------------------------------
class CTxForwardTransferCrosschainOut;

struct ForwardTransfer : public ScTransactionWithAmount
{
    ForwardTransfer(const CTxForwardTransferCrosschainOut&);
};

//--------------------------------
struct ScCreation : public ScTransactionBase
{
    int startBlockHeight; 

    ScCreation() :startBlockHeight(-1) {}
    
    // all creation data follows...
    // TODO
};

/*
class ScTransaction
{
    std::vector<ScCreation> creations;
    std::vector<CertifierLock> certifierLocks;
    std::vector<ForwardTransfer> forwardTransfers;
};
*/

//--------------------------------
class ScInfo
{
public:
    ScInfo() : creationBlockIndex(NULL), creationTxIndex(-1), ownerTxHash(), balance(0) {}
    
    // reference to the block containing the tx that created the side chain 
    const CBlockIndex* creationBlockIndex;;
    // index of the creating tx in the block
    int creationTxIndex;
    // hash of the tx who created it
    uint256 ownerTxHash;

    // total amount given by sum(fw transfer)
    CAmount balance;

    // list of certifiers
    std::vector<ScCertifier> certifiers;

    // creation data
    ScCreation creationData;

    std::string ToString() const;
};

//--------------------------------
typedef boost::unordered_map<uint256, ScInfo, ObjectHasher> ScInfoMap;

class CTxMemPool;

class ScMgr
{
  private:
    ScMgr(); // Disallow instantiation outside of the class.
    ScInfoMap mScInfo;

    typedef boost::unordered_map<uint256, std::vector<ForwardTransfer>, ObjectHasher> ScFwdTransfers;
    ScFwdTransfers _cachedFwTransfers;

  public:

    static ScMgr& instance();

    ScMgr(const ScMgr&) = delete;
    ScMgr& operator=(const ScMgr &) = delete;
    ScMgr(ScMgr &&) = delete;
    ScMgr & operator=(ScMgr &&) = delete;

    void addSidechain(const uint256& id, ScInfo& info);
    void removeSidechain(const uint256& id);

    bool sidechainExists(const uint256& id);

    bool addBlockScTransactions(const CBlock& block, const CBlockIndex* pindex);
    bool removeBlockScTransactions(const CBlock& block);

    bool updateSidechainBalance(const uint256& id, const CAmount& amount);
    CAmount getSidechainBalance(const uint256& id);

    bool getScInfo(const uint256& scId, ScInfo& info);
    bool checkSidechainTxCreation(const CTransaction& tx);
    bool checkCreationInMemPool(CTxMemPool& pool, const CTransaction& tx);

    // used at node startupwhen verifying blocks
    void addSidechainsAndCacheAmounts(const CBlock& block, const CBlockIndex* pindex);
    bool updateAmountsFromCache();

    // dbg functions
    void dump_info();
    void dump_info(const uint256& scId);
}; 

#endif // _SIDECHAIN_H
