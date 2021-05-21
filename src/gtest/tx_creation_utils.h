#ifndef TX_CREATION_UTILS_H
#define TX_CREATION_UTILS_H

#include <boost/thread.hpp>

#include "amount.h"
#include "chainparams.h"
#include "coins.h"
#include "keystore.h"
#include "sc/asyncproofverifier.h"

class CTransaction;
class CMutableTransaction;
class CScCertificate;
class CMutableScCertificate;
class uint256;
class CBlock;

namespace txCreationUtils
{
class CNakedCCoinsViewCache : public CCoinsViewCache
{
public:
    CNakedCCoinsViewCache(CCoinsView* pWrappedView) : CCoinsViewCache(pWrappedView)
    {
        uint256 dummyAnchor = uint256S("59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7"); //anchor for empty block!?
        this->hashAnchor = dummyAnchor;

        CAnchorsCacheEntry dummyAnchorsEntry;
        dummyAnchorsEntry.entered = true;
        dummyAnchorsEntry.flags = CAnchorsCacheEntry::DIRTY;
        this->cacheAnchors[dummyAnchor] = dummyAnchorsEntry;
    };
    CSidechainsMap& getSidechainMap() { return this->cacheSidechains; };
    CSidechainEventsMap& getScEventsMap() { return this->cacheSidechainEvents; };
};

CMutableTransaction populateTx(int txVersion,
                               const CAmount& creationTxAmount = CAmount(0),
                               int epochLength = 5,
                               const CAmount& ftScFee = CAmount(0),
                               const CAmount& mbtrScFee = CAmount(0),
                               int mbtrDataLength = 0);
void signTx(CMutableTransaction& mtx);
void signTx(CMutableScCertificate& mcert);

CTransaction createNewSidechainTxWith(const CAmount & creationTxAmount, int epochLength = 15);
CTransaction createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount);
CTxCeasedSidechainWithdrawalInput CreateCSWInput(
    const uint256& scId, const std::string& nullifierHex, const std::string& actCertDataHex,
    const std::string& ceasingCumScTxCommTreeHex, CAmount amount);
CTransaction createCSWTxWith(const CTxCeasedSidechainWithdrawalInput& csw);

CTransaction createCoinBase(const CAmount& amount);
CTransaction createTransparentTx(bool ccIsNull = true); //ccIsNull = false allows generation of faulty tx with non-empty cross chain output
CTransaction createSproutTx(bool ccIsNull = true);      //ccIsNull = false allows generation of faulty tx with non-empty cross chain output

void addNewScCreationToTx(CTransaction& tx, const CAmount& scAmount);

CScCertificate createCertificate(const uint256 & scId, int epochNum, const uint256 & endEpochBlockHash,
                                 const CFieldElement& endEpochCumScTxCommTreeRoot, CAmount changeTotalAmount/* = 0*/, unsigned int numChangeOut/* = 0*/,
                                 CAmount bwtTotalAmount/* = 1*/, unsigned int numBwt/* = 1*/, CAmount ftScFee/* = 0*/, CAmount mbtrScFee/* = 0*/, const int quality = 3);

uint256 CreateSpendableCoinAtHeight(CCoinsViewCache& targetView, unsigned int coinHeight);

void storeSidechain(CSidechainsMap& mapToWriteInto, const uint256& scId, const CSidechain& sidechain);
void storeSidechainEvent(CSidechainEventsMap& mapToWriteInto, int eventHeight, const CSidechainEvents& scEvent);
} // namespace txCreationUtils

namespace chainSettingUtils
{
void ExtendChainActiveToHeight(int targetHeight);
void ExtendChainActiveWithBlock(const CBlock& block);
} // namespace chainSettingUtils

namespace blockchain_test_utils
{

using namespace txCreationUtils;

/**
 * @brief A struct containing all the parameters needed to create a Transcation object. 
 */
struct CTransactionCreationArguments
{
    int32_t nVersion;   /**< The version of the transaction. */

    std::vector<CTxCeasedSidechainWithdrawalInput> vcsw_ccin;     /**< The list of CSW inputs */
    std::vector<CTxScCreationOut>                  vsc_ccout;     /**< The list of sidechain creation outputs */
    std::vector<CTxForwardTransferOut>             vft_ccout;     /**< The list of sidechain forward transfer outputs */
    std::vector<CBwtRequestOut>                    vmbtr_out;     /**< The list of sidechain backward transfer request outputs */
};

class CInMemorySidechainDb final: public CCoinsView {
public:
    CInMemorySidechainDb()  = default;
    virtual ~CInMemorySidechainDb() = default;

    bool HaveSidechain(const uint256& scId) const override {
        return sidechainsInMemoryMap.count(scId) && sidechainsInMemoryMap.at(scId).flag != CSidechainsCacheEntry::Flags::ERASED;
    }
    bool GetSidechain(const uint256& scId, CSidechain& info) const override {
        if(!HaveSidechain(scId))
            return false;
        info = sidechainsInMemoryMap.at(scId).sidechain;
        return true;
    }

    bool HaveSidechainEvents(int height)  const override {
        return eventsInMemoryMap.count(height) && eventsInMemoryMap.at(height).flag != CSidechainEventsCacheEntry::Flags::ERASED;
    }
    bool GetSidechainEvents(int height, CSidechainEvents& scEvents) const override {
        if(!HaveSidechainEvents(height))
            return false;
        scEvents = eventsInMemoryMap.at(height).scEvents;
        return true;
    }

    virtual void GetScIds(std::set<uint256>& scIdsList) const override {
        for (auto& entry : sidechainsInMemoryMap)
            scIdsList.insert(entry.first);
        return;
    }

    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap,
                    CSidechainEventsMap& mapSidechainEvents, CCswNullifiersMap& cswNullifiers) override
    {
        for (auto& entryToWrite : sidechainMap)
            WriteMutableEntry(entryToWrite.first, entryToWrite.second, sidechainsInMemoryMap);

        for (auto& entryToWrite : mapSidechainEvents)
            WriteMutableEntry(entryToWrite.first, entryToWrite.second, eventsInMemoryMap);

        sidechainMap.clear();
        mapSidechainEvents.clear();
        return true;
    }

private:
    mutable boost::unordered_map<uint256, CSidechainsCacheEntry, CCoinsKeyHasher> sidechainsInMemoryMap;
    mutable boost::unordered_map<int, CSidechainEventsCacheEntry> eventsInMemoryMap;
};

/**
 * @brief An helper class with utility functions to be used for managing the blockchain
 * (for instance to create a sidechain, to generate blocks, etc.).
 * 
 */
class BlockchainTestManager
{
public:
    static BlockchainTestManager& GetInstance()
    {
        assert(Params().NetworkIDString() == "regtest");

        static BlockchainTestManager instance;
        return instance;
    }

    BlockchainTestManager(const BlockchainTestManager&) = delete;
    BlockchainTestManager& operator=(const BlockchainTestManager&) = delete;

    // GETTERS
    std::shared_ptr<CInMemorySidechainDb> CoinsView();
    std::shared_ptr<CNakedCCoinsViewCache> CoinsViewCache();

    // BLOCKCHAIN HELPERS
    void ExtendChainActiveToHeight(int targetHeight);
    void ExtendChainActiveWithBlock(const CBlock& block);
    void Reset();

    // TRANSACTION HELPERS
    CTxCeasedSidechainWithdrawalInput CreateCswInput(uint256 scId, CAmount nValue);
    CMutableTransaction CreateTransaction(const CTransactionCreationArguments& args);

    // SIDECHAIN HELPERS
    CScCertificate GenerateCertificate(uint256 scId, int epochNumber, int64_t quality, CTransactionBase* inputTxBase = nullptr);
    void StoreSidechainWithCurrentHeight(const uint256& scId, const CSidechain& sidechain, int chainActiveHeight);

    // ASYNC PROOF VERIFIER HELPERS
    size_t PendingAsyncCertProves();
    size_t PendingAsyncCswProves();
    AsyncProofVerifierStatistics GetAsyncProofVerifierStatistics();
    uint32_t GetAsyncProofVerifierMaxBatchVerifyDelay();
    void ResetAsyncProofVerifier();

private:
    BlockchainTestManager();

    void InitCoinGeneration();
    std::pair<uint256, CCoinsCacheEntry> GenerateCoinsAmount(const CAmount & amountToGenerate);
    bool StoreCoins(std::pair<uint256, CCoinsCacheEntry> entryToStore);

    boost::thread_group threadGroup;
    std::shared_ptr<CInMemorySidechainDb> view;
    std::shared_ptr<CNakedCCoinsViewCache> viewCache;

    CKey                     coinsKey;
    CBasicKeyStore           keystore;
    CScript                  coinsScript;

};

} // namespace blockchain_test_utils

#endif
